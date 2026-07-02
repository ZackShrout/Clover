//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Apu.h"

#include <algorithm>
#include <array>

namespace
{
    constexpr uint8_t k_ram_disabled_read_value{ 0x5au };

    constexpr std::array<uint8_t, 64> k_ipl_rom{
        0xcdu, 0xefu, 0xbdu, 0xe8u, 0x00u, 0xc6u, 0x1du, 0xd0u,
        0xfcu, 0x8fu, 0xaau, 0xf4u, 0x8fu, 0xbbu, 0xf5u, 0x78u,
        0xccu, 0xf4u, 0xd0u, 0xfbu, 0x2fu, 0x19u, 0xebu, 0xf4u,
        0xd0u, 0xfcu, 0x7eu, 0xf4u, 0xd0u, 0x0bu, 0xe4u, 0xf5u,
        0xcbu, 0xf4u, 0xd7u, 0x00u, 0xfcu, 0xd0u, 0xf3u, 0xabu,
        0x01u, 0x10u, 0xefu, 0x7eu, 0xf4u, 0x10u, 0xebu, 0xbau,
        0xf6u, 0xdau, 0x00u, 0xbau, 0xf4u, 0xc4u, 0xf4u, 0xddu,
        0x5du, 0xd0u, 0xdbu, 0x1fu, 0x00u, 0x00u, 0xc0u, 0xffu
    };
} // anonymous namespace

namespace clover::core
{
    void apu_t::power_on() noexcept
    {
        reset();
    }

    void apu_t::reset() noexcept
    {
        _master_clock = 0;
        _cycle_credit = 0;
        _registers = {
            .pc = static_cast<uint16_t>(k_ipl_rom[62] | (static_cast<uint16_t>(k_ipl_rom[63]) << 8u)),
            .a = 0,
            .x = 0,
            .y = 0,
            .sp = 0,
            .psw = 0
        };
        _ipl_rom_enabled = true;
        _halted = false;
        _last_opcode = 0;
        _io = {};
        _timer0 = {};
        _timer1 = {};
        _timer2 = {};
        _apu_to_cpu_ports = { 0x00u, 0x00u, 0x00u, 0x00u };
        _cpu_to_apu_ports = { 0x00u, 0x00u, 0x00u, 0x00u };
        _instruction_trace_count = 0;
        _io_trace_count = 0;
        _ram.fill(0);
    }

    void apu_t::step(master_clock_delta_t master_clocks) noexcept
    {
        _master_clock += master_clocks;
        _cycle_credit += static_cast<int64_t>(master_clocks);

        while (!_halted && _cycle_credit >= static_cast<int64_t>(k_master_clocks_per_spc_cycle))
        {
            execute_instruction();
        }
    }

    master_clock_count_t apu_t::master_clock() const noexcept
    {
        return _master_clock;
    }

    uint8_t apu_t::read_cpu_port(uint16_t address) const noexcept
    {
        return _apu_to_cpu_ports[address & 0x03u];
    }

    void apu_t::write_cpu_port(uint16_t address, uint8_t value) noexcept
    {
        _cpu_to_apu_ports[address & 0x03u] = value;
    }

    uint8_t apu_t::read_input_port(uint8_t port) const noexcept
    {
        return _cpu_to_apu_ports[port & 0x03u];
    }

    void apu_t::write_output_port(uint8_t port, uint8_t value) noexcept
    {
        _apu_to_cpu_ports[port & 0x03u] = value;
    }

    apu_state_t apu_t::state() const noexcept
    {
        return {
            .pc = _registers.pc,
            .a = _registers.a,
            .x = _registers.x,
            .y = _registers.y,
            .sp = _registers.sp,
            .psw = _registers.psw,
            .external_wait_states = _io.external_wait_states,
            .internal_wait_states = _io.internal_wait_states,
            .timers_disable = _io.timers_disable,
            .timers_enable = _io.timers_enable,
            .ipl_rom_enabled = _ipl_rom_enabled,
            .halted = _halted,
            .last_opcode = _last_opcode,
            .timer0 = {
                .stage0 = _timer0.stage0,
                .stage1 = _timer0.stage1,
                .stage2 = _timer0.stage2,
                .stage3 = _timer0.stage3,
                .line = _timer0.line,
                .enable = _timer0.enable,
                .target = _timer0.target
            },
            .timer1 = {
                .stage0 = _timer1.stage0,
                .stage1 = _timer1.stage1,
                .stage2 = _timer1.stage2,
                .stage3 = _timer1.stage3,
                .line = _timer1.line,
                .enable = _timer1.enable,
                .target = _timer1.target
            },
            .timer2 = {
                .stage0 = _timer2.stage0,
                .stage1 = _timer2.stage1,
                .stage2 = _timer2.stage2,
                .stage3 = _timer2.stage3,
                .line = _timer2.line,
                .enable = _timer2.enable,
                .target = _timer2.target
            },
            .instruction_trace_count = _instruction_trace_count,
            .io_trace_count = _io_trace_count
        };
    }

    uint8_t apu_t::peek_ram(uint16_t address) const noexcept
    {
        return _ram[address];
    }

    uint8_t apu_t::instruction_trace_count() const noexcept
    {
        return _instruction_trace_count;
    }

    const std::array<apu_state_t::trace_entry_t, 128>& apu_t::instruction_trace() const noexcept
    {
        return _instruction_trace;
    }

    uint8_t apu_t::io_trace_count() const noexcept
    {
        return _io_trace_count;
    }

    const std::array<apu_state_t::io_trace_entry_t, 128>& apu_t::io_trace() const noexcept
    {
        return _io_trace;
    }

    void apu_t::execute_instruction() noexcept
    {
        const uint16_t pc{ _registers.pc };
        const uint8_t opcode{ fetch_u8() };
        _last_opcode = opcode;
        trace_instruction(pc, opcode);
        if (execute_load_store_opcode(opcode)
            || execute_alu_opcode(opcode)
            || execute_branch_bit_opcode(opcode)
            || execute_control_opcode(opcode))
        {
            return;
        }

        halt_on_unimplemented_opcode(opcode);
    }

    uint8_t apu_t::fetch_u8() noexcept
    {
        return read_u8(_registers.pc++);
    }

    uint16_t apu_t::fetch_u16() noexcept
    {
        const uint8_t low{ fetch_u8() };
        const uint8_t high{ fetch_u8() };
        return static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u));
    }

    uint8_t apu_t::fetch_u8_phased(master_clock_delta_t before_cycles,
                                   master_clock_delta_t after_cycles) noexcept
    {
        if (before_cycles != 0)
            step_spc_cycles(before_cycles);

        const uint8_t value{ fetch_u8() };

        if (after_cycles != 0)
            step_spc_cycles(after_cycles);

        return value;
    }

    uint8_t apu_t::read_u8(uint16_t address) const noexcept
    {
        if (_ipl_rom_enabled && address >= 0xffc0u)
            return k_ipl_rom[address & 0x003fu];

        if (is_io_address(address))
            return const_cast<apu_t*>(this)->read_io(address);

        if (_io.ram_disable)
            return k_ram_disabled_read_value;

        return _ram[address];
    }

    bool apu_t::is_io_address(uint16_t address) noexcept
    {
        return (address & 0xfff0u) == 0x00f0u;
    }

    bool apu_t::is_cpu_port_address(uint16_t address) noexcept
    {
        return (address & 0xfffcu) == 0x00f4u;
    }

    uint8_t apu_t::read_u8_phased(uint16_t address,
                                  master_clock_delta_t before_cycles,
                                  master_clock_delta_t after_cycles) noexcept
    {
        if (before_cycles != 0)
            step_spc_cycles(before_cycles);

        const uint8_t value{ read_u8(address) };

        if (after_cycles != 0)
            step_spc_cycles(after_cycles);

        return value;
    }

    void apu_t::write_u8(uint16_t address, uint8_t value) noexcept
    {
        if (_io.ram_writable && !_io.ram_disable)
            _ram[address] = value;

        if ((address & 0xfff0u) == 0x00f0u)
            write_io(address, value);
    }

    void apu_t::write_u8_phased(uint16_t address,
                                uint8_t value,
                                master_clock_delta_t before_cycles,
                                master_clock_delta_t after_cycles) noexcept
    {
        if (before_cycles != 0)
            step_spc_cycles(before_cycles);

        write_u8(address, value);

        if (after_cycles != 0)
            step_spc_cycles(after_cycles);
    }

    uint16_t apu_t::read_u16(uint16_t address) const noexcept
    {
        return static_cast<uint16_t>(read_u8(address) | (static_cast<uint16_t>(read_u8(static_cast<uint16_t>(address + 1u))) << 8u));
    }

    void apu_t::write_u16(uint16_t address, uint16_t value) noexcept
    {
        write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
        write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(value >> 8u));
    }

    uint16_t apu_t::direct_page_address(uint8_t address) const noexcept
    {
        const uint16_t page_base{ static_cast<uint16_t>((_registers.psw & k_psw_direct_page) != 0 ? 0x0100u : 0x0000u) };
        return static_cast<uint16_t>(page_base | address);
    }

    uint8_t apu_t::read_direct(uint8_t address) const noexcept
    {
        return read_u8(direct_page_address(address));
    }

    uint8_t apu_t::read_direct_phased(uint8_t address,
                                      master_clock_delta_t before_cycles,
                                      master_clock_delta_t after_cycles) noexcept
    {
        return read_u8_phased(direct_page_address(address), before_cycles, after_cycles);
    }

    uint8_t apu_t::read_direct_indexed(uint8_t address, uint8_t index) const noexcept
    {
        return read_u8(direct_page_address(static_cast<uint8_t>(address + index)));
    }

    uint8_t apu_t::read_direct_indexed_phased(uint8_t address,
                                              uint8_t index,
                                              master_clock_delta_t before_cycles,
                                              master_clock_delta_t after_cycles) noexcept
    {
        return read_u8_phased(direct_page_address(static_cast<uint8_t>(address + index)),
                              before_cycles,
                              after_cycles);
    }

    void apu_t::write_direct(uint8_t address, uint8_t value) noexcept
    {
        write_u8(direct_page_address(address), value);
    }

    uint16_t apu_t::read_direct_u16(uint8_t address) const noexcept
    {
        return static_cast<uint16_t>(read_direct(address)
            | (static_cast<uint16_t>(read_direct(static_cast<uint8_t>(address + 1u))) << 8u));
    }

    uint16_t apu_t::read_direct_indexed_u16(uint8_t address, uint8_t index) const noexcept
    {
        const uint8_t indexed_address{ static_cast<uint8_t>(address + index) };
        return static_cast<uint16_t>(read_direct(indexed_address)
            | (static_cast<uint16_t>(read_direct(static_cast<uint8_t>(indexed_address + 1u))) << 8u));
    }

    void apu_t::write_direct_u16(uint8_t address, uint16_t value) noexcept
    {
        write_direct(address, static_cast<uint8_t>(value & 0x00ffu));
        write_direct(static_cast<uint8_t>(address + 1u), static_cast<uint8_t>(value >> 8u));
    }

    void apu_t::clear_direct_bit(uint8_t address, uint8_t bit_mask) noexcept
    {
        write_direct(address, static_cast<uint8_t>(read_direct(address) & static_cast<uint8_t>(~bit_mask)));
    }

    void apu_t::set_direct_bit(uint8_t address, uint8_t bit_mask) noexcept
    {
        write_direct(address, static_cast<uint8_t>(read_direct(address) | bit_mask));
    }

    bool apu_t::direct_bit_is_set(uint8_t address, uint8_t bit_mask) const noexcept
    {
        return (read_direct(address) & bit_mask) != 0;
    }

    uint8_t apu_t::read_x_indirect() const noexcept
    {
        return read_direct(_registers.x);
    }

    uint8_t apu_t::read_indexed_indirect(uint8_t zero_page_address) const noexcept
    {
        const uint16_t base{ read_direct_indexed_u16(zero_page_address, _registers.x) };
        return read_u8(base);
    }

    uint8_t apu_t::read_x_indirect_increment() noexcept
    {
        const uint8_t value{ read_direct(_registers.x) };
        ++_registers.x;
        set_nz_flags(value);
        return value;
    }

    void apu_t::write_x_indirect(uint8_t value) noexcept
    {
        write_direct(_registers.x, value);
    }

    void apu_t::write_indexed_indirect(uint8_t zero_page_address, uint8_t value) noexcept
    {
        const uint16_t base{ read_direct_indexed_u16(zero_page_address, _registers.x) };
        write_u8(base, value);
    }

    uint8_t apu_t::read_indirect_y(uint8_t zero_page_address) const noexcept
    {
        const uint16_t base{ read_direct_u16(zero_page_address) };
        return read_u8(static_cast<uint16_t>(base + _registers.y));
    }

    void apu_t::write_indirect_y(uint8_t zero_page_address, uint8_t value) noexcept
    {
        const uint16_t base{ read_direct_u16(zero_page_address) };
        write_u8(static_cast<uint16_t>(base + _registers.y), value);
    }

    void apu_t::push_stack(uint8_t value) noexcept
    {
        write_u8(static_cast<uint16_t>(0x0100u | _registers.sp), value);
        --_registers.sp;
    }

    uint8_t apu_t::pull_stack() noexcept
    {
        ++_registers.sp;
        return read_u8(static_cast<uint16_t>(0x0100u | _registers.sp));
    }

    void apu_t::set_nz_flags(uint8_t value) noexcept
    {
        if (value == 0)
            _registers.psw |= k_psw_zero;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

        if ((value & 0x80u) != 0)
            _registers.psw |= k_psw_negative;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_negative);
    }

    void apu_t::add_with_carry(uint8_t value) noexcept
    {
        const uint16_t carry_in{ static_cast<uint16_t>((_registers.psw & k_psw_carry) != 0 ? 1u : 0u) };
        const uint16_t lhs{ _registers.a };
        const uint16_t rhs{ value };
        const uint16_t result{ static_cast<uint16_t>(lhs + rhs + carry_in) };
        const uint8_t result8{ static_cast<uint8_t>(result & 0x00ffu) };

        if (result > 0x00ffu)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        if (((lhs & 0x0fu) + (rhs & 0x0fu) + carry_in) > 0x0fu)
            _registers.psw |= k_psw_half_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_half_carry);

        if (((~(lhs ^ rhs)) & (lhs ^ result8) & 0x80u) != 0)
            _registers.psw |= k_psw_overflow;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_overflow);

        _registers.a = result8;
        set_nz_flags(_registers.a);
    }

    void apu_t::subtract_with_carry(uint8_t value) noexcept
    {
        add_with_carry(static_cast<uint8_t>(~value));
    }

    void apu_t::and_accumulator(uint8_t value) noexcept
    {
        _registers.a = static_cast<uint8_t>(_registers.a & value);
        set_nz_flags(_registers.a);
    }

    void apu_t::arithmetic_shift_left_accumulator() noexcept
    {
        if ((_registers.a & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        _registers.a = static_cast<uint8_t>(_registers.a << 1u);
        set_nz_flags(_registers.a);
    }

    void apu_t::arithmetic_shift_left_memory(uint16_t address) noexcept
    {
        uint8_t value{ read_u8(address) };
        if ((value & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(value << 1u);
        write_u8(address, value);
        set_nz_flags(value);
    }

    void apu_t::rotate_left_accumulator() noexcept
    {
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 1u : 0u) };
        if ((_registers.a & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        _registers.a = static_cast<uint8_t>((_registers.a << 1u) | carry_in);
        set_nz_flags(_registers.a);
    }

    void apu_t::rotate_left_memory(uint16_t address) noexcept
    {
        uint8_t value{ read_u8(address) };
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 1u : 0u) };
        if ((value & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>((value << 1u) | carry_in);
        write_u8(address, value);
        set_nz_flags(value);
    }

    void apu_t::logical_shift_right_accumulator() noexcept
    {
        if ((_registers.a & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        _registers.a = static_cast<uint8_t>(_registers.a >> 1u);
        set_nz_flags(_registers.a);
    }

    void apu_t::logical_shift_right_memory(uint16_t address) noexcept
    {
        uint8_t value{ read_u8(address) };
        if ((value & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(value >> 1u);
        write_u8(address, value);
        set_nz_flags(value);
    }

    void apu_t::rotate_right_accumulator() noexcept
    {
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 0x80u : 0u) };
        if ((_registers.a & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        _registers.a = static_cast<uint8_t>(carry_in | (_registers.a >> 1u));
        set_nz_flags(_registers.a);
    }

    void apu_t::rotate_right_memory(uint16_t address) noexcept
    {
        uint8_t value{ read_u8(address) };
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 0x80u : 0u) };
        if ((value & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(carry_in | (value >> 1u));
        write_u8(address, value);
        set_nz_flags(value);
    }

    void apu_t::or_accumulator(uint8_t value) noexcept
    {
        _registers.a = static_cast<uint8_t>(_registers.a | value);
        set_nz_flags(_registers.a);
    }

    void apu_t::or_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(direct_address) | immediate) };
        write_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::xor_accumulator(uint8_t value) noexcept
    {
        _registers.a = static_cast<uint8_t>(_registers.a ^ value);
        set_nz_flags(_registers.a);
    }

    void apu_t::and_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(direct_address) & immediate) };
        write_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::xor_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(direct_address) ^ immediate) };
        write_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::or_indirect_x_with_indirect_y() noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(_registers.x) | read_direct(_registers.y)) };
        write_direct(_registers.x, value);
        set_nz_flags(value);
    }

    void apu_t::and_indirect_x_with_indirect_y() noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(_registers.x) & read_direct(_registers.y)) };
        write_direct(_registers.x, value);
        set_nz_flags(value);
    }

    void apu_t::xor_indirect_x_with_indirect_y() noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(read_direct(_registers.x) ^ read_direct(_registers.y)) };
        write_direct(_registers.x, value);
        set_nz_flags(value);
    }

    void apu_t::branch_relative_if_direct_bit_clear(uint8_t direct_address, uint8_t bit_mask) noexcept
    {
        branch_relative_if_direct_bit(direct_address, bit_mask, false);
    }

    void apu_t::branch_relative_if_direct_bit_set(uint8_t direct_address, uint8_t bit_mask) noexcept
    {
        branch_relative_if_direct_bit(direct_address, bit_mask, true);
    }

    void apu_t::branch_relative_if_direct_bit(uint8_t direct_address, uint8_t bit_mask, bool branch_on_set) noexcept
    {
        const int8_t displacement{ static_cast<int8_t>(fetch_u8()) };
        if (direct_bit_is_set(direct_address, bit_mask) == branch_on_set)
        {
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            step_spc_cycles(7);
            return;
        }

        step_spc_cycles(5);
    }

    void apu_t::branch_relative_if_accumulator_not_equal_direct(uint8_t direct_address) noexcept
    {
        const int8_t displacement{ static_cast<int8_t>(fetch_u8()) };
        if (_registers.a != read_direct(direct_address))
        {
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            step_spc_cycles(7);
            return;
        }

        step_spc_cycles(5);
    }

    void apu_t::branch_relative_if_accumulator_not_equal_direct_indexed(uint8_t direct_address, uint8_t index) noexcept
    {
        const int8_t displacement{ static_cast<int8_t>(fetch_u8()) };
        if (_registers.a != read_direct_indexed(direct_address, index))
        {
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            step_spc_cycles(8);
            return;
        }

        step_spc_cycles(6);
    }

    void apu_t::decrement_direct_and_branch_if_not_zero(uint8_t direct_address) noexcept
    {
        const int8_t displacement{ static_cast<int8_t>(fetch_u8()) };
        const uint8_t value{ static_cast<uint8_t>(read_direct(direct_address) - 1u) };
        write_direct(direct_address, value);
        if (value != 0)
        {
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            step_spc_cycles(7);
            return;
        }

        step_spc_cycles(5);
    }

    void apu_t::multiply_ya() noexcept
    {
        const uint16_t result{ static_cast<uint16_t>(static_cast<uint16_t>(_registers.y) * static_cast<uint16_t>(_registers.a)) };
        _registers.a = static_cast<uint8_t>(result & 0x00ffu);
        _registers.y = static_cast<uint8_t>(result >> 8u);

        if (_registers.y == 0)
            _registers.psw |= k_psw_zero;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

        if ((_registers.y & 0x80u) != 0)
            _registers.psw |= k_psw_negative;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_negative);
    }

    void apu_t::exchange_accumulator_nibbles() noexcept
    {
        _registers.a = static_cast<uint8_t>((_registers.a << 4u) | (_registers.a >> 4u));
        set_nz_flags(_registers.a);
    }

    uint16_t apu_t::add_word(uint16_t lhs, uint16_t rhs) noexcept
    {
        _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        _registers.a = static_cast<uint8_t>(lhs & 0x00ffu);
        add_with_carry(static_cast<uint8_t>(rhs & 0x00ffu));
        const uint8_t low{ _registers.a };

        _registers.a = static_cast<uint8_t>(lhs >> 8u);
        add_with_carry(static_cast<uint8_t>(rhs >> 8u));
        const uint8_t high{ _registers.a };

        const uint16_t result{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
        if (result == 0)
            _registers.psw |= k_psw_zero;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

        return result;
    }

    uint16_t apu_t::subtract_word(uint16_t lhs, uint16_t rhs) noexcept
    {
        _registers.psw |= k_psw_carry;

        _registers.a = static_cast<uint8_t>(lhs & 0x00ffu);
        subtract_with_carry(static_cast<uint8_t>(rhs & 0x00ffu));
        const uint8_t low{ _registers.a };

        _registers.a = static_cast<uint8_t>(lhs >> 8u);
        subtract_with_carry(static_cast<uint8_t>(rhs >> 8u));
        const uint8_t high{ _registers.a };

        const uint16_t result{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
        if (result == 0)
            _registers.psw |= k_psw_zero;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

        return result;
    }

    void apu_t::divide_ya_by_x() noexcept
    {
        const uint16_t ya{ static_cast<uint16_t>(_registers.a | (static_cast<uint16_t>(_registers.y) << 8u)) };

        if ((_registers.y & 0x0fu) >= (_registers.x & 0x0fu))
            _registers.psw |= k_psw_half_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_half_carry);

        if (_registers.y >= _registers.x)
            _registers.psw |= k_psw_overflow;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_overflow);

        if (_registers.y < static_cast<uint8_t>(_registers.x << 1u))
        {
            _registers.a = static_cast<uint8_t>(ya / _registers.x);
            _registers.y = static_cast<uint8_t>(ya % _registers.x);
        }
        else
        {
            const uint16_t numerator{ static_cast<uint16_t>(ya - (static_cast<uint16_t>(_registers.x) << 9u)) };
            const uint16_t denominator{ static_cast<uint16_t>(256u - _registers.x) };
            _registers.a = static_cast<uint8_t>(255u - (numerator / denominator));
            _registers.y = static_cast<uint8_t>(_registers.x + (numerator % denominator));
        }

        set_nz_flags(_registers.a);
    }

    void apu_t::test_and_modify_bits_absolute(uint16_t address, bool set_bits) noexcept
    {
        const uint8_t data{ read_u8(address) };
        const uint8_t result{ static_cast<uint8_t>(_registers.a - data) };

        if (result == 0)
            _registers.psw |= k_psw_zero;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

        if ((result & 0x80u) != 0)
            _registers.psw |= k_psw_negative;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_negative);

        const uint8_t updated{ set_bits ? static_cast<uint8_t>(data | _registers.a)
                                        : static_cast<uint8_t>(data & static_cast<uint8_t>(~_registers.a)) };
        write_u8(address, updated);
    }

    void apu_t::set_compare_flags(uint8_t lhs, uint8_t rhs) noexcept
    {
        const uint16_t result{ static_cast<uint16_t>(static_cast<uint16_t>(lhs) - static_cast<uint16_t>(rhs)) };
        if (lhs >= rhs)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        set_nz_flags(static_cast<uint8_t>(result & 0x00ffu));
    }

    void apu_t::branch_relative_if(bool condition) noexcept
    {
        const int8_t displacement{ static_cast<int8_t>(fetch_u8()) };
        if (condition)
        {
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            step_spc_cycles(4);
            return;
        }

        step_spc_cycles(2);
    }

    uint8_t apu_t::read_io(uint16_t address) noexcept
    {
        uint8_t value{ 0 };
        switch (address)
        {
        case 0x00f0u:
        case 0x00f1u:
            value = 0x00u;
            break;

        case 0x00f2u:
            value = _io.dsp_address;
            break;

        case 0x00f3u:
            value = 0x00u;
            break;

        case 0x00f4u:
        case 0x00f5u:
        case 0x00f6u:
        case 0x00f7u:
            // bsnes models an extra bus-hold cycle on both sides of CPUIO reads.
            step_spc_cycles(1);
            value = _cpu_to_apu_ports[address & 0x03u];
            step_spc_cycles(1);
            break;

        case 0x00f8u:
            value = _io.aux4;
            break;

        case 0x00f9u:
            value = _io.aux5;
            break;

        case 0x00fau:
        case 0x00fbu:
        case 0x00fcu:
            value = 0x00u;
            break;

        case 0x00fdu:
            value = static_cast<uint8_t>(_timer0.stage3 & 0x0fu);
            _timer0.stage3 = 0;
            break;

        case 0x00feu:
            value = static_cast<uint8_t>(_timer1.stage3 & 0x0fu);
            _timer1.stage3 = 0;
            break;

        case 0x00ffu:
            value = static_cast<uint8_t>(_timer2.stage3 & 0x0fu);
            _timer2.stage3 = 0;
            break;

        default:
            value = _ram[address];
            break;
        }

        trace_io_access(address, value, false);
        return value;
    }

    void apu_t::write_io(uint16_t address, uint8_t value) noexcept
    {
        trace_io_access(address, value, true);
        switch (address)
        {
        case 0x00f0u:
            // TEST writes are only honored when the direct-page flag is clear.
            if ((_registers.psw & k_psw_direct_page) != 0)
                return;

            _io.timers_disable = (value & 0x01u) != 0;
            _io.ram_writable = (value & 0x02u) != 0;
            _io.ram_disable = (value & 0x04u) != 0;
            _io.timers_enable = (value & 0x08u) != 0;
            _io.external_wait_states = static_cast<uint8_t>((value >> 4u) & 0x03u);
            _io.internal_wait_states = static_cast<uint8_t>((value >> 6u) & 0x03u);
            synchronize_timer_stage1(_timer0);
            synchronize_timer_stage1(_timer1);
            synchronize_timer_stage1(_timer2);
            return;

        case 0x00f1u:
            reset_timer(_timer0, (value & 0x01u) != 0);
            reset_timer(_timer1, (value & 0x02u) != 0);
            reset_timer(_timer2, (value & 0x04u) != 0);

            if ((value & 0x10u) != 0)
            {
                _cpu_to_apu_ports[0] = 0x00u;
                _cpu_to_apu_ports[1] = 0x00u;
            }

            if ((value & 0x20u) != 0)
            {
                _cpu_to_apu_ports[2] = 0x00u;
                _cpu_to_apu_ports[3] = 0x00u;
            }

            _ipl_rom_enabled = (value & 0x80u) != 0;
            return;

        case 0x00f2u:
            _io.dsp_address = value;
            return;

        case 0x00f3u:
            return;

        case 0x00f4u:
        case 0x00f5u:
        case 0x00f6u:
        case 0x00f7u:
            _apu_to_cpu_ports[address & 0x03u] = value;
            return;

        case 0x00f8u:
            _io.aux4 = value;
            return;

        case 0x00f9u:
            _io.aux5 = value;
            return;

        case 0x00fau:
            _timer0.target = value;
            return;

        case 0x00fbu:
            _timer1.target = value;
            return;

        case 0x00fcu:
            _timer2.target = value;
            return;

        default:
            return;
        }
    }

    void apu_t::reset_timer(timer_t<128>& timer, bool enabled) noexcept
    {
        if (!timer.enable && enabled)
        {
            timer.stage2 = 0;
            timer.stage3 = 0;
        }

        timer.enable = enabled;
    }

    void apu_t::reset_timer(timer_t<16>& timer, bool enabled) noexcept
    {
        if (!timer.enable && enabled)
        {
            timer.stage2 = 0;
            timer.stage3 = 0;
        }

        timer.enable = enabled;
    }

    template <uint8_t Frequency>
    void apu_t::synchronize_timer_stage1(timer_t<Frequency>& timer) noexcept
    {
        bool level{ timer.stage1 != 0 };
        if (!_io.timers_enable || _io.timers_disable)
            level = false;

        const bool falling_edge{ timer.line && !level };
        timer.line = level;
        if (!falling_edge || !timer.enable)
            return;

        ++timer.stage2;
        if (timer.stage2 != timer.target)
            return;

        timer.stage2 = 0;
        timer.stage3 = static_cast<uint8_t>((timer.stage3 + 1u) & 0x0fu);
    }

    template <uint8_t Frequency>
    void apu_t::step_timer(timer_t<Frequency>& timer, master_clock_delta_t spc_cycles) noexcept
    {
        for (master_clock_delta_t cycle{ 0 }; cycle < spc_cycles; ++cycle)
        {
            ++timer.stage0;
            if (timer.stage0 < Frequency)
                continue;

            timer.stage0 = 0;
            timer.stage1 ^= 0x01u;
            synchronize_timer_stage1(timer);
        }
    }

    void apu_t::step_spc_cycles(master_clock_delta_t spc_cycles) noexcept
    {
        step_timer(_timer0, spc_cycles);
        step_timer(_timer1, spc_cycles);
        step_timer(_timer2, spc_cycles);

        const int64_t master_clocks{
            static_cast<int64_t>(spc_cycles) * static_cast<int64_t>(k_master_clocks_per_spc_cycle)
        };
        _cycle_credit -= master_clocks;
    }

    void apu_t::halt_on_unimplemented_opcode(uint8_t opcode) noexcept
    {
        _last_opcode = opcode;
        _halted = true;
        _cycle_credit = 0;
    }

    void apu_t::trace_instruction(uint16_t pc, uint8_t opcode) noexcept
    {
        if (pc < 0x0540u || pc > 0x0554u)
            return;

        const apu_state_t::trace_entry_t entry{
            .master_clock = static_cast<uint64_t>(_master_clock),
            .pc = pc,
            .opcode = opcode,
            .a = _registers.a,
            .x = _registers.x,
            .y = _registers.y,
            .sp = _registers.sp,
            .psw = _registers.psw,
            .timer0_stage2 = _timer0.stage2,
            .timer0_stage3 = _timer0.stage3,
            .port0 = _cpu_to_apu_ports[0],
            .port1 = _cpu_to_apu_ports[1],
            .port2 = _cpu_to_apu_ports[2],
            .port3 = _cpu_to_apu_ports[3]
        };

        if (_instruction_trace_count < _instruction_trace.size())
        {
            _instruction_trace[_instruction_trace_count++] = entry;
            return;
        }

        std::move(std::begin(_instruction_trace) + 1u,
                  std::end(_instruction_trace),
                  std::begin(_instruction_trace));
        _instruction_trace[_instruction_trace.size() - 1u] = entry;
    }

    void apu_t::trace_io_access(uint16_t address, uint8_t value, bool is_write) noexcept
    {
        if (!((address >= 0x00f4u && address <= 0x00ffu)
              || (_registers.pc >= 0x0540u && _registers.pc <= 0x0554u)))
        {
            return;
        }

        const apu_state_t::io_trace_entry_t entry{
            .master_clock = static_cast<uint64_t>(_master_clock),
            .pc = _registers.pc,
            .opcode = _last_opcode,
            .address = address,
            .value = value,
            .is_write = is_write,
            .a = _registers.a,
            .x = _registers.x,
            .y = _registers.y,
            .psw = _registers.psw
        };

        if (_io_trace_count < _io_trace.size())
        {
            _io_trace[_io_trace_count++] = entry;
            return;
        }

        std::move(std::begin(_io_trace) + 1u,
                  std::end(_io_trace),
                  std::begin(_io_trace));
        _io_trace[_io_trace.size() - 1u] = entry;
    }
}
