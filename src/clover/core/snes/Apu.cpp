//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"

#include "clover/core/snes/Bus.h"

#include <algorithm>
#include <array>

namespace
{
    constexpr uint8_t k_ram_disabled_read_value{ 0x5au };
    constexpr uint16_t k_trace_pc_min{ 0xffdau };
    constexpr uint16_t k_trace_pc_max{ 0xffe8u };
    constexpr uint16_t k_trace_pc2_min{ 0x0800u };
    constexpr uint16_t k_trace_pc2_max{ 0x08ffu };

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

    void copy_dsp_state_bytes(unsigned char** output, void* input, size_t size)
    {
        std::copy_n(static_cast<const uint8_t*>(input), size, *output);
        *output += size;
    }

} // anonymous namespace

namespace clover::core
{
    void apu_t::power_on() noexcept
    {
        initialize(false);
    }

    void apu_t::reset() noexcept
    {
        initialize(true);
    }

    void apu_t::initialize(bool warm_reset) noexcept
    {
        _master_clock = 0;
        _smp_clock_credit = 0;
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
        _waiting = false;
        _stopped = false;
        _last_opcode = 0;
        _io = {};
        _timer0 = {};
        _timer1 = {};
        _timer2 = {};
        _apu_to_cpu_ports = { 0x00u, 0x00u, 0x00u, 0x00u };
        _cpu_to_apu_ports = { 0x00u, 0x00u, 0x00u, 0x00u };
        _instruction_context = {};
        _smp_suspended_for_cpu = false;
        _instruction_trace_count = 0;
        _io_trace_count = 0;
        _dsp_clock_remainder = 0;

        if (!warm_reset)
        {
            _ram.fill(0);
            _dsp.init(_ram.data(), _ram.data());
            _dsp_initialized = true;
        }
        else if (_dsp_initialized)
        {
            _dsp.soft_reset();
        }
    }

    void apu_t::step(master_clock_delta_t master_clocks) noexcept
    {
        _master_clock += master_clocks;
        _smp_clock_credit += static_cast<int64_t>(master_clocks) * k_smp_clock_frequency_hz;

        if (_smp_suspended_for_cpu)
            return;

        while (!_halted && _smp_clock_credit >= k_master_clock_frequency_hz)
        {
            if (!execute_instruction())
                return;
        }
    }

    void apu_t::synchronize_cpu_thread() noexcept
    {
        if (!_smp_suspended_for_cpu
            || _smp_clock_credit <= k_scheduler_zero_credit
            || _halted)
            return;

        _smp_suspended_for_cpu = false;
        while (!_halted)
        {
            if (!execute_instruction())
                return;

            if (_smp_clock_credit < -k_force_cpu_sync_credit)
            {
                _smp_suspended_for_cpu = true;
                return;
            }
        }
    }

    void apu_t::begin_cpu_io_window(bus_t& bus, master_clock_delta_t target_clocks) noexcept
    {
        _cpu_io_window_bus = &bus;
        _cpu_io_window_target_clocks = target_clocks;
        _cpu_io_window_consumed_master_numerator = 0;
    }

    void apu_t::end_cpu_io_window() noexcept
    {
        _cpu_io_window_bus = nullptr;
        _cpu_io_window_target_clocks = 0;
        _cpu_io_window_consumed_master_numerator = 0;
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

    void apu_t::begin_audio_frame() noexcept
    {
        _dsp.set_output(_audio_samples.data(), static_cast<int>(_audio_samples.size()));
    }

    std::span<const int16_t> apu_t::audio_samples() const noexcept
    {
        if (_dsp.output_overflowed())
            return { _audio_samples.data(), _audio_samples.size() };

        const size_t sample_count{
            std::min(static_cast<size_t>(_dsp.sample_count()), _audio_samples.size())
        };
        return {
            _audio_samples.data(),
            sample_count
        };
    }

    bool apu_t::audio_output_overflowed() const noexcept
    {
        return _dsp.output_overflowed();
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
            .waiting = _waiting,
            .stopped = _stopped,
            .last_opcode = _last_opcode,
            .smp_clock_credit = _smp_clock_credit,
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

    uint8_t apu_t::peek_dsp_register(uint8_t address) const noexcept
    {
        return static_cast<uint8_t>(_dsp.read(address & 0x7fu));
    }

    std::array<uint8_t, SPC_DSP::state_size> apu_t::dsp_state() noexcept
    {
        std::array<uint8_t, SPC_DSP::state_size> state{};
        unsigned char* output{ state.data() };
        _dsp.copy_state(&output, copy_dsp_state_bytes);
        return state;
    }

    uint16_t apu_t::instruction_trace_count() const noexcept
    {
        return _instruction_trace_count;
    }

    const std::array<apu_state_t::trace_entry_t, k_apu_trace_capacity>& apu_t::instruction_trace() const noexcept
    {
        return _instruction_trace;
    }

    uint16_t apu_t::io_trace_count() const noexcept
    {
        return _io_trace_count;
    }

    const std::array<apu_state_t::io_trace_entry_t, k_apu_trace_capacity>& apu_t::io_trace() const noexcept
    {
        return _io_trace;
    }

    bool apu_t::execute_instruction() noexcept
    {
        const bool replaying{ _instruction_context.active };
        if (!replaying)
        {
            _instruction_context = {
                .active = true,
                .abort_requested = false,
                .start_registers = _registers,
                .start_current_opcode_pc = _current_opcode_pc,
                .start_last_opcode = _last_opcode,
                .accesses = {},
                .access_count = 0,
                .replay_cursor = 0
            };
        }
        else
        {
            _instruction_context.abort_requested = false;
            _instruction_context.replay_cursor = 0;
            _registers = _instruction_context.start_registers;
            _current_opcode_pc = _instruction_context.start_current_opcode_pc;
            _last_opcode = _instruction_context.start_last_opcode;
        }

        if (_waiting || _stopped)
        {
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            if (_instruction_context.abort_requested)
            {
                _registers = _instruction_context.start_registers;
                _current_opcode_pc = _instruction_context.start_current_opcode_pc;
                _last_opcode = _instruction_context.start_last_opcode;
                _smp_suspended_for_cpu = true;
                return false;
            }

            _instruction_context = {};
            return true;
        }

        const uint16_t pc{ _registers.pc };
        _current_opcode_pc = pc;
        const uint8_t opcode{ spc_fetch_u8() };
        _last_opcode = opcode;
        if (!replaying)
            trace_instruction(pc, opcode);
        if (execute_load_store_opcode(opcode)
            || execute_alu_opcode(opcode)
            || execute_branch_bit_opcode(opcode)
            || execute_control_opcode(opcode))
        {
            if (_instruction_context.abort_requested)
            {
                _registers = _instruction_context.start_registers;
                _current_opcode_pc = _instruction_context.start_current_opcode_pc;
                _last_opcode = _instruction_context.start_last_opcode;
                _smp_suspended_for_cpu = true;
                return false;
            }

            _instruction_context = {};
            return true;
        }

        halt_on_unimplemented_opcode(opcode);
        _instruction_context = {};
        return true;
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

    uint8_t apu_t::spc_fetch_u8() noexcept
    {
        return spc_read_u8(_registers.pc++);
    }

    uint16_t apu_t::spc_fetch_u16() noexcept
    {
        const uint8_t low{ spc_fetch_u8() };
        const uint8_t high{ spc_fetch_u8() };
        return static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u));
    }

    void apu_t::spc_consume_opcode_fetch() noexcept
    {
        // Opcode fetch timing is consumed up-front in execute_instruction(),
        // matching the bsnes SPC700 fetch/read model.
    }

    void apu_t::spc_idle() noexcept
    {
        if (_instruction_context.abort_requested)
            return;

        if (replay_access(access_kind_t::idle, 0) != nullptr)
            return;

        wait_for_access(std::nullopt, false);
        (void)append_access(access_kind_t::idle, 0, 0, false);
    }

    uint8_t apu_t::spc_read_u8(uint16_t address) noexcept
    {
        if (_instruction_context.abort_requested)
            return 0;

        if (access_journal_entry_t* replay{
                replay_access(access_kind_t::read, address)
            })
        {
            if (!replay->awaiting_cpu_sync)
                return replay->value;

            const uint8_t value{ read_u8(address) };
            wait_for_access(address, true);
            replay->value = value;
            replay->awaiting_cpu_sync = false;
            return value;
        }

        if (is_cpu_port_address(address))
        {
            wait_for_access(address, true);
            if (_smp_clock_credit <= k_scheduler_zero_credit)
            {
                (void)append_access(access_kind_t::read, address, 0, true);
                request_cpu_sync();
                return 0;
            }

            const uint8_t value{ read_u8(address) };
            wait_for_access(address, true);
            (void)append_access(access_kind_t::read, address, value, false);
            return value;
        }

        wait_for_access(address, false);
        const uint8_t value{ read_u8(address) };
        (void)append_access(access_kind_t::read, address, value, false);
        return value;
    }

    void apu_t::spc_write_u8(uint16_t address, uint8_t value) noexcept
    {
        if (_instruction_context.abort_requested)
            return;

        if (access_journal_entry_t* replay{
                replay_access(access_kind_t::write, address)
            })
        {
            if (replay->awaiting_cpu_sync)
            {
                write_u8(address, replay->value);
                replay->awaiting_cpu_sync = false;
            }
            return;
        }

        wait_for_access(address, false);
        const bool synchronizes_cpu{
            is_cpu_port_address(address)
            || (address == 0x00f1u && (value & 0x30u) != 0)
        };
        if (synchronizes_cpu
            && _smp_clock_credit <= k_scheduler_zero_credit)
        {
            (void)append_access(access_kind_t::write, address, value, true);
            request_cpu_sync();
            return;
        }

        write_u8(address, value);
        (void)append_access(access_kind_t::write, address, value, false);
    }

    uint8_t apu_t::spc_load_direct(uint8_t address) noexcept
    {
        return spc_read_u8(direct_page_address(address));
    }

    void apu_t::spc_store_direct(uint8_t address, uint8_t value) noexcept
    {
        spc_write_u8(direct_page_address(address), value);
    }

    uint8_t apu_t::spc_load_direct_indexed(uint8_t address, uint8_t index) noexcept
    {
        spc_idle();
        return spc_load_direct(static_cast<uint8_t>(address + index));
    }

    uint8_t apu_t::spc_read_abs_indexed(uint16_t address, uint8_t index) noexcept
    {
        spc_idle();
        return spc_read_u8(static_cast<uint16_t>(address + index));
    }

    uint8_t apu_t::spc_read_indexed_indirect(uint8_t address, uint8_t index) noexcept
    {
        spc_idle();
        const uint8_t indexed_address{ static_cast<uint8_t>(address + index) };
        const uint8_t low{ spc_load_direct(indexed_address) };
        const uint8_t high{ spc_load_direct(static_cast<uint8_t>(indexed_address + 1u)) };
        const uint16_t effective_address{
            static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u))
        };
        return spc_read_u8(effective_address);
    }

    uint8_t apu_t::spc_read_indirect_indexed(uint8_t address, uint8_t index) noexcept
    {
        const uint8_t low{ spc_load_direct(address) };
        const uint8_t high{ spc_load_direct(static_cast<uint8_t>(address + 1u)) };
        const uint16_t effective_address{
            static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u))
        };
        spc_idle();
        return spc_read_u8(static_cast<uint16_t>(effective_address + index));
    }

    void apu_t::spc_write_abs(uint16_t address, uint8_t value) noexcept
    {
        (void)spc_read_u8(address);
        spc_write_u8(address, value);
    }

    void apu_t::spc_write_abs_indexed(uint16_t address, uint8_t index, uint8_t value) noexcept
    {
        spc_idle();
        const uint16_t effective_address{ static_cast<uint16_t>(address + index) };
        (void)spc_read_u8(effective_address);
        spc_write_u8(effective_address, value);
    }

    void apu_t::spc_write_indexed_indirect(uint8_t address, uint8_t index, uint8_t value) noexcept
    {
        spc_idle();
        const uint8_t indexed_address{ static_cast<uint8_t>(address + index) };
        const uint8_t low{ spc_load_direct(indexed_address) };
        const uint8_t high{ spc_load_direct(static_cast<uint8_t>(indexed_address + 1u)) };
        const uint16_t effective_address{
            static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u))
        };
        (void)spc_read_u8(effective_address);
        spc_write_u8(effective_address, value);
    }

    void apu_t::spc_write_indirect_indexed(uint8_t address, uint8_t index, uint8_t value) noexcept
    {
        const uint8_t low{ spc_load_direct(address) };
        const uint8_t high{ spc_load_direct(static_cast<uint8_t>(address + 1u)) };
        const uint16_t effective_address{
            static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u))
        };
        spc_idle();
        (void)spc_read_u8(static_cast<uint16_t>(effective_address + index));
        spc_write_u8(static_cast<uint16_t>(effective_address + index), value);
    }

    void apu_t::spc_push_stack(uint8_t value) noexcept
    {
        spc_write_u8(static_cast<uint16_t>(0x0100u | _registers.sp), value);
        --_registers.sp;
    }

    uint8_t apu_t::spc_pull_stack() noexcept
    {
        ++_registers.sp;
        return spc_read_u8(static_cast<uint16_t>(0x0100u | _registers.sp));
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
        uint8_t value{ spc_read_u8(address) };
        if ((value & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(value << 1u);
        spc_write_u8(address, value);
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
        uint8_t value{ spc_read_u8(address) };
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 1u : 0u) };
        if ((value & 0x80u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>((value << 1u) | carry_in);
        spc_write_u8(address, value);
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
        uint8_t value{ spc_read_u8(address) };
        if ((value & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(value >> 1u);
        spc_write_u8(address, value);
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
        uint8_t value{ spc_read_u8(address) };
        const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 0x80u : 0u) };
        if ((value & 0x01u) != 0)
            _registers.psw |= k_psw_carry;
        else
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

        value = static_cast<uint8_t>(carry_in | (value >> 1u));
        spc_write_u8(address, value);
        set_nz_flags(value);
    }

    void apu_t::or_accumulator(uint8_t value) noexcept
    {
        _registers.a = static_cast<uint8_t>(_registers.a | value);
        set_nz_flags(_registers.a);
    }

    void apu_t::or_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) | immediate) };
        spc_store_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::xor_accumulator(uint8_t value) noexcept
    {
        _registers.a = static_cast<uint8_t>(_registers.a ^ value);
        set_nz_flags(_registers.a);
    }

    void apu_t::and_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) & immediate) };
        spc_store_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::xor_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept
    {
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) ^ immediate) };
        spc_store_direct(direct_address, value);
        set_nz_flags(value);
    }

    void apu_t::or_indirect_x_with_indirect_y() noexcept
    {
        (void)spc_read_u8(_registers.pc);
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(_registers.x) | spc_load_direct(_registers.y)) };
        spc_store_direct(_registers.x, value);
        set_nz_flags(value);
    }

    void apu_t::and_indirect_x_with_indirect_y() noexcept
    {
        (void)spc_read_u8(_registers.pc);
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(_registers.x) & spc_load_direct(_registers.y)) };
        spc_store_direct(_registers.x, value);
        set_nz_flags(value);
    }

    void apu_t::xor_indirect_x_with_indirect_y() noexcept
    {
        (void)spc_read_u8(_registers.pc);
        const uint8_t value{ static_cast<uint8_t>(spc_load_direct(_registers.x) ^ spc_load_direct(_registers.y)) };
        spc_store_direct(_registers.x, value);
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
        const uint8_t data{ spc_load_direct(direct_address) };
        spc_idle();
        const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
        if (((data & bit_mask) != 0) != branch_on_set)
            return;

        spc_idle();
        spc_idle();
        _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
    }

    void apu_t::branch_relative_if_accumulator_not_equal_direct(uint8_t direct_address) noexcept
    {
        const uint8_t data{ spc_load_direct(direct_address) };
        spc_idle();
        const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
        if (_registers.a == data)
            return;

        spc_idle();
        spc_idle();
        _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
    }

    void apu_t::branch_relative_if_accumulator_not_equal_direct_indexed(uint8_t direct_address, uint8_t index) noexcept
    {
        spc_idle();
        const uint8_t data{ spc_load_direct(static_cast<uint8_t>(direct_address + index)) };
        spc_idle();
        const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
        if (_registers.a == data)
            return;

        spc_idle();
        spc_idle();
        _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
    }

    void apu_t::decrement_direct_and_branch_if_not_zero(uint8_t direct_address) noexcept
    {
        uint8_t value{ spc_load_direct(direct_address) };
        value = static_cast<uint8_t>(value - 1u);
        spc_store_direct(direct_address, value);
        const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
        if (value == 0)
            return;

        spc_idle();
        spc_idle();
        _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
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

        // The S-SMP compares Y with the full nine-bit X * 2 value here.
        // Narrowing the product to eight bits selects the overflow algorithm
        // incorrectly for every divisor with bit 7 set.
        if (static_cast<uint16_t>(_registers.y)
            < (static_cast<uint16_t>(_registers.x) << 1u))
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
        const uint8_t data{ spc_read_u8(address) };
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
        (void)spc_read_u8(address);
        spc_write_u8(address, updated);
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
        const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
        if (!condition)
            return;

        spc_idle();
        spc_idle();
        _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
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
            value = static_cast<uint8_t>(_dsp.read(_io.dsp_address & 0x7fu));
            break;

        case 0x00f4u:
        case 0x00f5u:
        case 0x00f6u:
        case 0x00f7u:
            synchronize_cpu_io_visibility();
            value = _cpu_to_apu_ports[address & 0x03u];
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
                synchronize_cpu_io_visibility();
                _cpu_to_apu_ports[0] = 0x00u;
                _cpu_to_apu_ports[1] = 0x00u;
            }

            if ((value & 0x20u) != 0)
            {
                synchronize_cpu_io_visibility();
                _cpu_to_apu_ports[2] = 0x00u;
                _cpu_to_apu_ports[3] = 0x00u;
            }

            _ipl_rom_enabled = (value & 0x80u) != 0;
            return;

        case 0x00f2u:
            _io.dsp_address = value;
            return;

        case 0x00f3u:
            if ((_io.dsp_address & 0x80u) == 0)
                _dsp.write(_io.dsp_address & 0x7fu, value);
            return;

        case 0x00f4u:
        case 0x00f5u:
        case 0x00f6u:
        case 0x00f7u:
            synchronize_cpu_io_visibility();
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

    void apu_t::step_access_cycles(master_clock_delta_t cycle_clocks,
                                   master_clock_delta_t timer_clocks) noexcept
    {
        step_dsp(cycle_clocks);
        step_timer(_timer0, timer_clocks);
        step_timer(_timer1, timer_clocks);
        step_timer(_timer2, timer_clocks);

        const int64_t master_clocks{
            static_cast<int64_t>(cycle_clocks) * k_master_clock_frequency_hz
        };
        _cpu_io_window_consumed_master_numerator += master_clocks;
        _smp_clock_credit -= master_clocks;
    }

    void apu_t::step_dsp(master_clock_delta_t smp_clocks) noexcept
    {
        const master_clock_delta_t total_clocks{ _dsp_clock_remainder + smp_clocks };
        const master_clock_delta_t dsp_clocks{ total_clocks / 2u };
        _dsp_clock_remainder = total_clocks & 1u;
        if (dsp_clocks != 0)
            _dsp.run(static_cast<int>(dsp_clocks));
    }

    void apu_t::wait_for_access(std::optional<uint16_t> address, bool half) noexcept
    {
        static constexpr std::array<master_clock_delta_t, 4> k_cycle_wait_states{ 2u, 4u, 10u, 20u };
        static constexpr std::array<master_clock_delta_t, 4> k_timer_wait_states{ 2u, 4u, 8u, 16u };

        uint8_t wait_state_index{ _io.external_wait_states };
        if (!address.has_value())
            wait_state_index = _io.internal_wait_states;
        else if ((address.value() & 0xfff0u) == 0x00f0u)
            wait_state_index = _io.internal_wait_states;
        else if (address.value() >= 0xffc0u && _ipl_rom_enabled)
            wait_state_index = _io.internal_wait_states;

        master_clock_delta_t cycle_clocks{ k_cycle_wait_states[wait_state_index] };
        master_clock_delta_t timer_clocks{ k_timer_wait_states[wait_state_index] };
        if (half)
        {
            cycle_clocks >>= 1u;
            timer_clocks >>= 1u;
        }

        step_access_cycles(cycle_clocks, timer_clocks);
    }

    void apu_t::step_spc_cycles(master_clock_delta_t spc_cycles) noexcept
    {
        step_dsp(spc_cycles);
        step_timer(_timer0, spc_cycles);
        step_timer(_timer1, spc_cycles);
        step_timer(_timer2, spc_cycles);

        const int64_t master_clocks{
            static_cast<int64_t>(spc_cycles) * k_master_clock_frequency_hz
        };
        _cpu_io_window_consumed_master_numerator += master_clocks;
        _smp_clock_credit -= master_clocks;
    }

    void apu_t::synchronize_cpu_io_visibility() noexcept
    {
        if (_cpu_io_window_bus == nullptr)
            return;

        master_clock_delta_t visible_clocks{
            static_cast<master_clock_delta_t>(
                _cpu_io_window_consumed_master_numerator / k_smp_clock_frequency_hz
            )
        };
        if (visible_clocks > _cpu_io_window_target_clocks)
            visible_clocks = _cpu_io_window_target_clocks;

        _cpu_io_window_bus->synchronize_apu_io_access(visible_clocks);
    }

    apu_t::access_journal_entry_t* apu_t::replay_access(access_kind_t kind,
                                                        uint16_t address) noexcept
    {
        if (_instruction_context.replay_cursor
            >= _instruction_context.access_count)
        {
            return nullptr;
        }

        access_journal_entry_t& entry{
            _instruction_context.accesses[_instruction_context.replay_cursor++]
        };
        if (entry.kind != kind || entry.address != address)
        {
            _halted = true;
            _last_opcode = 0xffu;
            return nullptr;
        }

        return &entry;
    }

    apu_t::access_journal_entry_t* apu_t::append_access(
        access_kind_t kind,
        uint16_t address,
        uint8_t value,
        bool awaiting_cpu_sync) noexcept
    {
        if (_instruction_context.access_count
            >= _instruction_context.accesses.size())
        {
            _halted = true;
            _last_opcode = 0xffu;
            return nullptr;
        }

        access_journal_entry_t& entry{
            _instruction_context.accesses[_instruction_context.access_count++]
        };
        entry = {
            .kind = kind,
            .address = address,
            .value = value,
            .awaiting_cpu_sync = awaiting_cpu_sync
        };
        ++_instruction_context.replay_cursor;
        return &entry;
    }

    void apu_t::request_cpu_sync() noexcept
    {
        _instruction_context.abort_requested = true;
    }

    void apu_t::halt_on_unimplemented_opcode(uint8_t opcode) noexcept
    {
        _last_opcode = opcode;
        _halted = true;
        _smp_clock_credit = 0;
    }

    void apu_t::trace_instruction(uint16_t pc, uint8_t opcode) noexcept
    {
        const bool in_primary_trace_range{ pc >= k_trace_pc_min && pc <= k_trace_pc_max };
        const bool in_secondary_trace_range{ pc >= k_trace_pc2_min && pc <= k_trace_pc2_max };
        if (!in_primary_trace_range && !in_secondary_trace_range)
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
        const bool in_primary_trace_range{ _registers.pc >= k_trace_pc_min && _registers.pc <= k_trace_pc_max };
        const bool in_secondary_trace_range{ _registers.pc >= k_trace_pc2_min && _registers.pc <= k_trace_pc2_max };
        if (!((address >= 0x00f4u && address <= 0x00ffu)
              || in_primary_trace_range
              || in_secondary_trace_range))
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
            .psw = _registers.psw,
            .smp_clock_credit = _smp_clock_credit
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
