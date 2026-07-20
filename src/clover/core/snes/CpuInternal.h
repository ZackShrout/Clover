//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Bus.h"
#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/Interrupts.h"

#include <cstdio>

namespace clover::core
{
    inline constexpr master_clock_delta_t k_cpu_bus_cycle_clocks{ 6 };
    // S-CPU reads expose bus data four master clocks before the access
    // retires; writes remain visible at the end of their full access.
    inline constexpr master_clock_delta_t k_cpu_read_tail_clocks{ 4 };
    inline constexpr uint8_t k_status_negative{ 0x80u };
    inline constexpr uint8_t k_status_overflow{ 0x40u };
    inline constexpr uint8_t k_status_accumulator_width{ 0x20u };
    inline constexpr uint8_t k_status_index_width{ 0x10u };
    inline constexpr uint8_t k_status_decimal{ 0x08u };
    inline constexpr uint8_t k_status_irq_disable{ 0x04u };
    inline constexpr uint8_t k_status_zero{ 0x02u };
    inline constexpr uint8_t k_status_carry{ 0x01u };

    [[nodiscard]] inline master_clock_delta_t cpu_access_clocks(uint32_t address,
                                                                bool fast_rom_enabled) noexcept
    {
        // Match bsnes' CPU bus timing categories:
        // - FastROM banks in $80-$FF, $8000-$FFFF: 6 clocks when MEMSEL is set
        // - Most ROM/WRAM accesses in mapped regions: 8 clocks
        // - CPU internal / low WRAM window: 6 clocks
        // - Slow MMIO/CPU register window: 12 clocks
        if ((address & 0x408000u) != 0u)
        {
            if ((address & 0x800000u) != 0u && fast_rom_enabled)
                return 6;

            return 8;
        }

        if (((address + 0x6000u) & 0x4000u) != 0u)
            return 8;

        if (((address - 0x4000u) & 0x7e00u) != 0u)
            return 6;

        return 12;
    }

    [[nodiscard]] inline uint32_t program_address(const cpu_state_t& state) noexcept
    {
        return (static_cast<uint32_t>(state.pb) << 16u) | state.pc;
    }

    [[nodiscard]] inline uint32_t stack_address(const cpu_state_t& state) noexcept
    {
        if (state.emulation_mode)
            return 0x000100u | (state.sp & 0x00ffu);

        return state.sp;
    }

    [[nodiscard]] inline uint32_t data_address(const cpu_state_t& state, uint16_t address) noexcept
    {
        return (static_cast<uint32_t>(state.db) << 16u) | address;
    }

    [[nodiscard]] inline bool accumulator_is_8bit(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode || (state.p & k_status_accumulator_width) != 0;
    }

    [[nodiscard]] inline bool index_is_8bit(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode || (state.p & k_status_index_width) != 0;
    }

    inline void set_zero_negative_flags(cpu_state_t& state,
                                        uint16_t value,
                                        bool is_8bit) noexcept
    {
        const uint16_t mask{ static_cast<uint16_t>(is_8bit ? 0x00ffu : 0xffffu) };
        const uint16_t negative_mask{ static_cast<uint16_t>(is_8bit ? 0x0080u : 0x8000u) };
        const uint16_t masked_value{ static_cast<uint16_t>(value & mask) };

        if (masked_value == 0)
            state.p |= k_status_zero;
        else
            state.p &= static_cast<uint8_t>(~k_status_zero);

        if ((masked_value & negative_mask) != 0)
            state.p |= k_status_negative;
        else
            state.p &= static_cast<uint8_t>(~k_status_negative);
    }

    inline void apply_index_width_to_state(cpu_state_t& state) noexcept
    {
        if (!index_is_8bit(state))
            return;

        state.x &= 0x00ffu;
        state.y &= 0x00ffu;
    }

    inline void normalize_status_for_mode(cpu_state_t& state) noexcept
    {
        if (state.emulation_mode)
            state.p |= static_cast<uint8_t>(k_status_accumulator_width | k_status_index_width);

        apply_index_width_to_state(state);
    }

    inline void enter_emulation_mode(cpu_state_t& state) noexcept
    {
        state.emulation_mode = true;
        normalize_status_for_mode(state);
        state.sp = static_cast<uint16_t>(0x0100u | (state.sp & 0x00ffu));
    }

    [[nodiscard]] inline uint16_t effective_direct_address(const cpu_state_t& state, uint8_t offset) noexcept
    {
        return static_cast<uint16_t>(state.d + offset);
    }

    [[nodiscard]] inline uint16_t effective_direct_indexed_address(const cpu_state_t& state,
                                                                   uint8_t offset,
                                                                   uint16_t index) noexcept
    {
        return static_cast<uint16_t>(effective_direct_address(state, offset) + index);
    }

    [[nodiscard]] inline uint16_t direct_indexed_pointer_address(const cpu_state_t& state,
                                                                 uint8_t offset,
                                                                 uint16_t index,
                                                                 uint8_t byte_offset) noexcept
    {
        const uint16_t indexed_address{ static_cast<uint16_t>(state.d + offset + index) };
        if (state.emulation_mode && (state.d & 0x00ffu) != 0)
        {
            return static_cast<uint16_t>((indexed_address & 0xff00u)
                | static_cast<uint16_t>((indexed_address + byte_offset) & 0x00ffu));
        }

        return static_cast<uint16_t>(indexed_address + byte_offset);
    }

    [[nodiscard]] inline uint16_t effective_absolute_address(uint16_t address) noexcept
    {
        return address;
    }

    [[nodiscard]] inline bool indexed_read_requires_idle(const cpu_state_t& state,
                                                         uint16_t address,
                                                         uint16_t index) noexcept
    {
        // Indexed read timing follows the index-register width and page-cross
        // shape that bsnes models via idle4(), not the accumulator width.
        if (!index_is_8bit(state))
            return true;

        const uint16_t indexed_address{ static_cast<uint16_t>(address + index) };
        return (address & 0xff00u) != (indexed_address & 0xff00u);
    }

    [[nodiscard]] inline uint16_t mask_for_width(bool is_8bit) noexcept
    {
        return static_cast<uint16_t>(is_8bit ? 0x00ffu : 0xffffu);
    }

    [[nodiscard]] inline uint16_t hardware_nmi_vector(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode ? 0xfffau : 0xffeau;
    }

    [[nodiscard]] inline uint16_t hardware_irq_vector(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode ? 0xfffeu : 0xffeeu;
    }

    [[nodiscard]] inline uint16_t brk_vector(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode ? 0xfffeu : 0xffe6u;
    }

    [[nodiscard]] inline uint16_t cop_vector(const cpu_state_t& state) noexcept
    {
        return state.emulation_mode ? 0xfff4u : 0xffe4u;
    }

    struct cpu_step_executor_t
    {
    public:
        explicit cpu_step_executor_t(bus_t& bus,
                                     cpu_t& cpu,
                                     dma_t& dma,
                                     interrupt_controller_t& interrupts,
                                     bool fast_rom_enabled) noexcept
            : _bus(bus)
            , _cpu(cpu)
            , _dma(dma)
            , _interrupts(interrupts)
            , _fast_rom_enabled(fast_rom_enabled)
        {
        }

        [[nodiscard]] uint8_t read_u8(uint32_t address) noexcept
        {
            begin_cpu_cycle();
            const master_clock_delta_t access_clocks{ cpu_access_clocks(address, _fast_rom_enabled) };
            const master_clock_delta_t leading_clocks{
                static_cast<master_clock_delta_t>(access_clocks - k_cpu_read_tail_clocks)
            };
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.service_dma_edge(
                    _bus, _dma, _interrupts, _ppu_step_result, access_clocks
                )
            );
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.advance_execution(leading_clocks, _dma, _interrupts, _ppu_step_result)
            );
            const uint8_t value{ _bus.read_u8(address) };
            _bus.trace_cpu_apu_port_access(address, value, false, _master_clocks);
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.advance_execution(
                    k_cpu_read_tail_clocks, _dma, _interrupts, _ppu_step_result
                )
            );
            _cpu.alu_edge();
            return value;
        }

        void write_u8(uint32_t address, uint8_t value) noexcept
        {
            begin_cpu_cycle();
            _cpu.alu_edge();
            const master_clock_delta_t access_clocks{ cpu_access_clocks(address, _fast_rom_enabled) };
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.service_dma_edge(
                    _bus, _dma, _interrupts, _ppu_step_result, access_clocks
                )
            );
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.advance_execution(access_clocks, _dma, _interrupts, _ppu_step_result)
            );
            _bus.trace_cpu_apu_port_access(address, value, true, _master_clocks);
            _bus.write_u8(address, value);
        }

        [[nodiscard]] uint8_t fetch_opcode(cpu_state_t& state) noexcept
        {
            const uint8_t opcode{ read_u8(program_address(state)) };
            ++state.pc;
            return opcode;
        }

        [[nodiscard]] uint8_t fetch_operand_u8(cpu_state_t& state) noexcept
        {
            const uint8_t value{ read_u8(program_address(state)) };
            ++state.pc;
            return value;
        }

        [[nodiscard]] uint16_t fetch_operand_u16(cpu_state_t& state) noexcept
        {
            const uint8_t low{ fetch_operand_u8(state) };
            const uint8_t high{ fetch_operand_u8(state) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint32_t fetch_operand_u24(cpu_state_t& state) noexcept
        {
            const uint16_t low_word{ fetch_operand_u16(state) };
            const uint8_t bank{ fetch_operand_u8(state) };
            return (static_cast<uint32_t>(bank) << 16u) | low_word;
        }

        void push_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            write_u8(stack_address(state), value);
            --state.sp;
            if (state.emulation_mode)
                state.sp = static_cast<uint16_t>(0x0100u | (state.sp & 0x00ffu));
        }

        [[nodiscard]] uint8_t pull_u8(cpu_state_t& state) noexcept
        {
            ++state.sp;
            if (state.emulation_mode)
                state.sp = static_cast<uint16_t>(0x0100u | (state.sp & 0x00ffu));
            return read_u8(stack_address(state));
        }

        void push_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            push_u8(state, static_cast<uint8_t>(value >> 8u));
            push_u8(state, static_cast<uint8_t>(value & 0x00ffu));
        }

        [[nodiscard]] uint16_t pull_u16(cpu_state_t& state) noexcept
        {
            const uint8_t low{ pull_u8(state) };
            const uint8_t high{ pull_u8(state) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint16_t stack_relative_address(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            idle();
            return static_cast<uint16_t>(state.sp + offset);
        }

        [[nodiscard]] uint8_t read_direct_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            return read_u8(effective_direct_address(state, offset));
        }

        [[nodiscard]] uint16_t read_direct_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(address + 1u)) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indexed_u8(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            return read_u8(effective_direct_indexed_address(state, offset, index));
        }

        [[nodiscard]] uint16_t read_direct_indexed_u16(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint16_t address{ effective_direct_indexed_address(state, offset, index) };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(address + 1u)) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_absolute_u8(cpu_state_t& state) noexcept
        {
            return read_u8(data_address(state, effective_absolute_address(fetch_operand_u16(state))));
        }

        [[nodiscard]] uint16_t read_absolute_u16(cpu_state_t& state) noexcept
        {
            const uint16_t address{ effective_absolute_address(fetch_operand_u16(state)) };
            const uint32_t data_base{ data_address(state, address) };
            const uint8_t low{ read_u8(data_base) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_long_u8(cpu_state_t& state) noexcept
        {
            return read_u8(fetch_operand_u24(state));
        }

        [[nodiscard]] uint16_t read_long_u16(cpu_state_t& state) noexcept
        {
            const uint32_t address{ fetch_operand_u24(state) };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8((address + 1u) & 0x00ffffffu) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_absolute_indexed_u8(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint16_t base_address{ effective_absolute_address(fetch_operand_u16(state)) };
            if (indexed_read_requires_idle(state, base_address, index))
                idle();

            const uint16_t address{ static_cast<uint16_t>(base_address + index) };
            return read_u8(data_address(state, address));
        }

        [[nodiscard]] uint16_t read_absolute_indexed_u16(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint16_t base_address{ effective_absolute_address(fetch_operand_u16(state)) };
            if (indexed_read_requires_idle(state, base_address, index))
                idle();

            const uint16_t address{ static_cast<uint16_t>(base_address + index) };
            const uint8_t low{ read_u8(data_address(state, address)) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_long_indexed_u8(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint32_t address{ (fetch_operand_u24(state) + index) & 0x00ffffffu };
            return read_u8(address);
        }

        [[nodiscard]] uint16_t read_long_indexed_u16(cpu_state_t& state, uint16_t index) noexcept
        {
            const uint32_t address{ (fetch_operand_u24(state) + index) & 0x00ffffffu };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8((address + 1u) & 0x00ffffffu) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indirect_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low | (high << 8u)) };
            return read_u8(data_address(state, address));
        }

        [[nodiscard]] uint16_t read_direct_indirect_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            const uint8_t low{ read_u8(data_address(state, address)) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indexed_indirect_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint8_t low{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 0)) };
            const uint8_t high{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 1)) };
            const uint16_t address{ static_cast<uint16_t>(low | (high << 8u)) };
            return read_u8(data_address(state, address));
        }

        [[nodiscard]] uint16_t read_direct_indexed_indirect_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint8_t low_pointer{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 0)) };
            const uint8_t high_pointer{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 1)) };
            const uint16_t address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            const uint8_t low{ read_u8(data_address(state, address)) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indirect_indexed_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t base_address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            if (indexed_read_requires_idle(state, base_address, state.y))
                idle();
            return read_u8(data_address(state, static_cast<uint16_t>(base_address + state.y)));
        }

        [[nodiscard]] uint16_t read_direct_indirect_indexed_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t base_address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            if (indexed_read_requires_idle(state, base_address, state.y))
                idle();
            const uint16_t address{ static_cast<uint16_t>(base_address + state.y) };
            const uint8_t low{ read_u8(data_address(state, address)) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indirect_long_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{ (static_cast<uint32_t>(bank) << 16u) | low | (high << 8u) };
            return read_u8(address);
        }

        [[nodiscard]] uint16_t read_direct_indirect_long_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (static_cast<uint32_t>(bank) << 16u) | low_pointer | (high_pointer << 8u)
            };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8((address + 1u) & 0x00ffffffu) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_direct_indirect_long_indexed_u8(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (((static_cast<uint32_t>(bank) << 16u) | low | (high << 8u)) + state.y) & 0x00ffffffu
            };
            return read_u8(address);
        }

        [[nodiscard]] uint16_t read_direct_indirect_long_indexed_u16(cpu_state_t& state) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (((static_cast<uint32_t>(bank) << 16u) | low_pointer | (high_pointer << 8u)) + state.y) & 0x00ffffffu
            };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8((address + 1u) & 0x00ffffffu) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_stack_relative_u8(cpu_state_t& state) noexcept
        {
            return read_u8(stack_relative_address(state));
        }

        [[nodiscard]] uint16_t read_stack_relative_u16(cpu_state_t& state) noexcept
        {
            const uint16_t address{ stack_relative_address(state) };
            const uint8_t low{ read_u8(address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(address + 1u)) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint8_t read_stack_relative_indirect_indexed_u8(cpu_state_t& state) noexcept
        {
            const uint16_t pointer_address{ stack_relative_address(state) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            idle();
            const uint16_t address{ static_cast<uint16_t>((low | (high << 8u)) + state.y) };
            return read_u8(data_address(state, address));
        }

        [[nodiscard]] uint16_t read_stack_relative_indirect_indexed_u16(cpu_state_t& state) noexcept
        {
            const uint16_t pointer_address{ stack_relative_address(state) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            idle();
            const uint16_t address{
                static_cast<uint16_t>((low_pointer | (high_pointer << 8u)) + state.y)
            };
            const uint8_t low{ read_u8(data_address(state, address)) };
            const uint8_t high{ read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        void write_direct_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            write_u8(effective_direct_address(state, offset), value);
        }

        void write_direct_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t address{ effective_direct_address(state, offset) };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indexed_u8(cpu_state_t& state, uint16_t index, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            write_u8(effective_direct_indexed_address(state, offset, index), value);
        }

        void write_direct_indexed_u16(cpu_state_t& state, uint16_t index, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint16_t address{ effective_direct_indexed_address(state, offset, index) };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(value >> 8u));
        }

        void write_absolute_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            write_u8(data_address(state, effective_absolute_address(fetch_operand_u16(state))), value);
        }

        void write_absolute_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint16_t address{ effective_absolute_address(fetch_operand_u16(state)) };
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void write_long_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            write_u8(fetch_operand_u24(state), value);
        }

        void write_long_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint32_t address{ fetch_operand_u24(state) };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8((address + 1u) & 0x00ffffffu, static_cast<uint8_t>(value >> 8u));
        }

        void write_absolute_indexed_u8(cpu_state_t& state, uint16_t index, uint8_t value) noexcept
        {
            const uint16_t base_address{ effective_absolute_address(fetch_operand_u16(state)) };
            idle();
            const uint16_t address{ static_cast<uint16_t>(base_address + index) };
            if (state.pb == 0x09u
                && address >= 0xee80u
                && address <= 0xeea0u)
            {
                std::printf("CPU abs-indexed write8: PB:%02x PC:%04x base=%04x index=%04x addr=%04x value=%02x DB:%02x P:%02x\n",
                            state.pb,
                            state.pc,
                            base_address,
                            index,
                            address,
                            value,
                            state.db,
                            state.p);
            }
            write_u8(data_address(state, address), value);
        }

        void write_absolute_indexed_u16(cpu_state_t& state, uint16_t index, uint16_t value) noexcept
        {
            const uint16_t base_address{ effective_absolute_address(fetch_operand_u16(state)) };
            idle();
            const uint16_t address{ static_cast<uint16_t>(base_address + index) };
            if (state.pb == 0x09u
                && address >= 0xee80u
                && address <= 0xeea0u)
            {
                std::printf("CPU abs-indexed write16: PB:%02x PC:%04x base=%04x index=%04x addr=%04x value=%04x DB:%02x P:%02x\n",
                            state.pb,
                            state.pc,
                            base_address,
                            index,
                            address,
                            value,
                            state.db,
                            state.p);
            }
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void write_long_indexed_u8(cpu_state_t& state, uint16_t index, uint8_t value) noexcept
        {
            write_u8((fetch_operand_u24(state) + index) & 0x00ffffffu, value);
        }

        void write_long_indexed_u16(cpu_state_t& state, uint16_t index, uint16_t value) noexcept
        {
            const uint32_t address{ (fetch_operand_u24(state) + index) & 0x00ffffffu };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8((address + 1u) & 0x00ffffffu, static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indirect_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low | (high << 8u)) };
            write_u8(data_address(state, address), value);
        }

        void write_direct_indirect_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indexed_indirect_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint8_t low{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 0)) };
            const uint8_t high{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 1)) };
            const uint16_t address{ static_cast<uint16_t>(low | (high << 8u)) };
            write_u8(data_address(state, address), value);
        }

        void write_direct_indexed_indirect_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            idle();
            const uint8_t low_pointer{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 0)) };
            const uint8_t high_pointer{ read_u8(direct_indexed_pointer_address(state, offset, state.x, 1)) };
            const uint16_t address{ static_cast<uint16_t>(low_pointer | (high_pointer << 8u)) };
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indirect_indexed_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>((low | (high << 8u)) + state.y) };
            idle();
            write_u8(data_address(state, address), value);
        }

        void write_direct_indirect_indexed_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>((low_pointer | (high_pointer << 8u)) + state.y) };
            idle();
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indirect_long_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{ (static_cast<uint32_t>(bank) << 16u) | low | (high << 8u) };
            write_u8(address, value);
        }

        void write_direct_indirect_long_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (static_cast<uint32_t>(bank) << 16u) | low_pointer | (high_pointer << 8u)
            };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8((address + 1u) & 0x00ffffffu, static_cast<uint8_t>(value >> 8u));
        }

        void write_direct_indirect_long_indexed_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (((static_cast<uint32_t>(bank) << 16u) | low | (high << 8u)) + state.y) & 0x00ffffffu
            };
            write_u8(address, value);
        }

        void write_direct_indirect_long_indexed_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint8_t offset{ fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                idle();
            const uint16_t pointer_address{ effective_direct_address(state, offset) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            const uint8_t bank{ read_u8(static_cast<uint16_t>(pointer_address + 2u)) };
            const uint32_t address{
                (((static_cast<uint32_t>(bank) << 16u) | low_pointer | (high_pointer << 8u)) + state.y) & 0x00ffffffu
            };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8((address + 1u) & 0x00ffffffu, static_cast<uint8_t>(value >> 8u));
        }

        void write_stack_relative_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            write_u8(stack_relative_address(state), value);
        }

        void write_stack_relative_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint16_t address{ stack_relative_address(state) };
            write_u8(address, static_cast<uint8_t>(value & 0x00ffu));
            write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(value >> 8u));
        }

        void write_stack_relative_indirect_indexed_u8(cpu_state_t& state, uint8_t value) noexcept
        {
            const uint16_t pointer_address{ stack_relative_address(state) };
            const uint8_t low{ read_u8(pointer_address) };
            const uint8_t high{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            idle();
            const uint16_t address{ static_cast<uint16_t>((low | (high << 8u)) + state.y) };
            write_u8(data_address(state, address), value);
        }

        void write_stack_relative_indirect_indexed_u16(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint16_t pointer_address{ stack_relative_address(state) };
            const uint8_t low_pointer{ read_u8(pointer_address) };
            const uint8_t high_pointer{ read_u8(static_cast<uint16_t>(pointer_address + 1u)) };
            idle();
            const uint16_t address{
                static_cast<uint16_t>((low_pointer | (high_pointer << 8u)) + state.y)
            };
            write_u8(data_address(state, address), static_cast<uint8_t>(value & 0x00ffu));
            write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                     static_cast<uint8_t>(value >> 8u));
        }

        void idle() noexcept
        {
            begin_cpu_cycle();
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.service_dma_edge(
                    _bus,
                    _dma,
                    _interrupts,
                    _ppu_step_result,
                    k_cpu_bus_cycle_clocks
                )
            );
            _master_clocks = static_cast<master_clock_delta_t>(
                _master_clocks + _cpu.advance_execution(k_cpu_bus_cycle_clocks, _dma, _interrupts, _ppu_step_result)
            );
            _cpu.alu_edge();
        }

        void idle_irq(cpu_state_t& state) noexcept
        {
            const interrupt_state_t interrupt_state{ _interrupts.sample() };
            if (interrupt_state.nmi_pending || interrupt_state.irq_pending)
            {
                static_cast<void>(read_u8(program_address(state)));
                return;
            }

            idle();
        }

        void observe_opcode_edge(const cpu_state_t& state) noexcept
        {
            _interrupts.observe_opcode_edge((state.p & k_status_irq_disable) != 0);
            _observed_opcode_edge = true;
        }

        void set_irq_lock() noexcept
        {
            _interrupts.set_irq_lock();
        }

        void retire_instruction() noexcept
        {
            _interrupts.observe_opcode_edge(
                _last_cycle_start_clock,
                (_cpu.state().p & k_status_irq_disable) != 0
            );
            _observed_opcode_edge = true;
            _retired_instruction = true;
        }

        void retire_internal_operation() noexcept
        {
            idle();
            retire_instruction();
        }

        void retire_irq_sensitive_internal_operation(cpu_state_t& state) noexcept
        {
            observe_opcode_edge(state);
            idle_irq(state);
            _retired_instruction = true;
        }

        [[nodiscard]] bool observed_opcode_edge() const noexcept
        {
            return _observed_opcode_edge;
        }

        [[nodiscard]] bool retired_instruction() const noexcept
        {
            return _retired_instruction;
        }

        [[nodiscard]] cpu_step_result_t finish() const noexcept
        {
            return {
                .master_clocks = _master_clocks,
                .ppu = _ppu_step_result,
                .stepped_hardware = true
            };
        }

    private:
        void begin_cpu_cycle() noexcept
        {
            _last_cycle_start_clock = _cpu._dma_counter;
        }

        bus_t& _bus;
        cpu_t& _cpu;
        dma_t& _dma;
        interrupt_controller_t& _interrupts;
        bool _fast_rom_enabled{ false };
        master_clock_delta_t _master_clocks{ 0 };
        bool _observed_opcode_edge{ false };
        bool _retired_instruction{ false };
        master_clock_count_t _last_cycle_start_clock{ 0 };
        ppu_step_result_t _ppu_step_result{};
    };

    inline void enter_interrupt_handler(cpu_state_t& state,
                                        cpu_step_executor_t& executor,
                                        uint16_t vector,
                                        bool fetch_signature_byte,
                                        bool clear_emulation_break_flag) noexcept
    {
        if (fetch_signature_byte)
            static_cast<void>(executor.fetch_operand_u8(state));
        else
            static_cast<void>(executor.read_u8(program_address(state)));

        if (!state.emulation_mode)
            executor.push_u8(state, state.pb);

        executor.push_u16(state, state.pc);
        uint8_t stacked_status{ state.p };
        if (clear_emulation_break_flag && state.emulation_mode)
            stacked_status &= static_cast<uint8_t>(~k_status_index_width);

        executor.push_u8(state, stacked_status);
        state.p |= k_status_irq_disable;
        state.p &= static_cast<uint8_t>(~k_status_decimal);

        const uint8_t vector_low{ executor.read_u8(vector) };
        const uint8_t vector_high{ executor.read_u8(static_cast<uint16_t>(vector + 1u)) };
        state.pb = 0;
        state.pc = static_cast<uint16_t>(vector_low | (vector_high << 8u));
        executor.retire_instruction();
    }

    inline void return_from_interrupt(cpu_state_t& state,
                                      cpu_step_executor_t& executor) noexcept
    {
        executor.idle();
        executor.idle();
        state.p = executor.pull_u8(state);
        normalize_status_for_mode(state);
        state.pc = executor.pull_u16(state);
        if (!state.emulation_mode)
            state.pb = executor.pull_u8(state);
        executor.retire_instruction();
    }

    [[nodiscard]] inline uint16_t adc_binary(uint16_t lhs,
                                             uint16_t rhs,
                                             bool carry_in,
                                             bool is_8bit,
                                             uint8_t& status) noexcept
    {
        const uint32_t mask{ is_8bit ? 0x00ffu : 0xffffu };
        const uint32_t sign_mask{ is_8bit ? 0x0080u : 0x8000u };
        const uint32_t result{
            (lhs & mask) + (rhs & mask) + static_cast<uint32_t>(carry_in ? 1u : 0u)
        };
        const uint16_t masked_result{ static_cast<uint16_t>(result & mask) };

        if (result > mask)
            status |= k_status_carry;
        else
            status &= static_cast<uint8_t>(~k_status_carry);

        const bool overflow{
            ((~(lhs ^ rhs) & (lhs ^ masked_result)) & sign_mask) != 0
        };
        if (overflow)
            status |= k_status_overflow;
        else
            status &= static_cast<uint8_t>(~k_status_overflow);

        return masked_result;
    }

    [[nodiscard]] inline uint16_t adc_decimal(uint16_t lhs,
                                              uint16_t rhs,
                                              bool carry_in,
                                              bool is_8bit,
                                              uint8_t& status) noexcept
    {
        const uint8_t digits{ static_cast<uint8_t>(is_8bit ? 2u : 4u) };
        uint16_t result{ 0 };
        uint8_t carry{ static_cast<uint8_t>(carry_in ? 1u : 0u) };

        for (uint8_t digit_index{ 0 }; digit_index < digits; ++digit_index)
        {
            const uint8_t shift{ static_cast<uint8_t>(digit_index << 2u) };
            uint8_t nibble{
                static_cast<uint8_t>(((lhs >> shift) & 0x0fu) + ((rhs >> shift) & 0x0fu) + carry)
            };
            if (nibble > 9u)
            {
                nibble = static_cast<uint8_t>(nibble + 6u);
                carry = 1u;
            }
            else
            {
                carry = 0u;
            }

            result = static_cast<uint16_t>(result | ((nibble & 0x0fu) << shift));
        }

        if (carry != 0)
            status |= k_status_carry;
        else
            status &= static_cast<uint8_t>(~k_status_carry);

        const uint8_t preserved_status{ status };
        static_cast<void>(adc_binary(lhs, rhs, carry_in, is_8bit, status));
        status = static_cast<uint8_t>((status & static_cast<uint8_t>(~k_status_carry))
            | (preserved_status & k_status_carry));

        return result;
    }

    [[nodiscard]] inline uint16_t adc_value(cpu_state_t& state,
                                            uint16_t lhs,
                                            uint16_t rhs) noexcept
    {
        const bool is_8bit{ accumulator_is_8bit(state) };
        const bool carry_in{ (state.p & k_status_carry) != 0 };
        const uint16_t result{
            (state.p & k_status_decimal) != 0
                ? adc_decimal(lhs, rhs, carry_in, is_8bit, state.p)
                : adc_binary(lhs, rhs, carry_in, is_8bit, state.p)
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t sbc_value(cpu_state_t& state,
                                            uint16_t lhs,
                                            uint16_t rhs) noexcept
    {
        const bool is_8bit{ accumulator_is_8bit(state) };
        const uint16_t width_mask{ mask_for_width(is_8bit) };
        const uint16_t inverted_rhs{ static_cast<uint16_t>((~rhs) & width_mask) };
        return static_cast<uint16_t>(adc_value(state, lhs, inverted_rhs) & width_mask);
    }

    inline void compare_value(cpu_state_t& state,
                              uint16_t lhs,
                              uint16_t rhs,
                              bool is_8bit) noexcept
    {
        const uint16_t width_mask{ mask_for_width(is_8bit) };
        const uint16_t masked_lhs{ static_cast<uint16_t>(lhs & width_mask) };
        const uint16_t masked_rhs{ static_cast<uint16_t>(rhs & width_mask) };

        if (masked_lhs >= masked_rhs)
            state.p |= k_status_carry;
        else
            state.p &= static_cast<uint8_t>(~k_status_carry);

        set_zero_negative_flags(state,
                                static_cast<uint16_t>(masked_lhs - masked_rhs),
                                is_8bit);
    }

    [[nodiscard]] inline uint16_t logic_value(cpu_state_t& state,
                                              uint16_t value,
                                              bool is_8bit) noexcept
    {
        const uint16_t masked{ static_cast<uint16_t>(value & mask_for_width(is_8bit)) };
        set_zero_negative_flags(state, masked, is_8bit);
        return masked;
    }

    [[nodiscard]] inline uint16_t asl_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        const uint16_t carry_mask{ static_cast<uint16_t>(is_8bit ? 0x0080u : 0x8000u) };
        const uint16_t result{
            static_cast<uint16_t>((value << 1u) & mask_for_width(is_8bit))
        };
        if ((value & carry_mask) != 0)
            state.p |= k_status_carry;
        else
            state.p &= static_cast<uint8_t>(~k_status_carry);
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t lsr_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        if ((value & 0x0001u) != 0)
            state.p |= k_status_carry;
        else
            state.p &= static_cast<uint8_t>(~k_status_carry);

        const uint16_t result{
            static_cast<uint16_t>((value & mask_for_width(is_8bit)) >> 1u)
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t rol_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        const uint16_t carry_mask{ static_cast<uint16_t>(is_8bit ? 0x0080u : 0x8000u) };
        const uint16_t carry_in{ static_cast<uint16_t>((state.p & k_status_carry) != 0 ? 1u : 0u) };
        if ((value & carry_mask) != 0)
            state.p |= k_status_carry;
        else
            state.p &= static_cast<uint8_t>(~k_status_carry);

        const uint16_t result{
            static_cast<uint16_t>(((value << 1u) | carry_in) & mask_for_width(is_8bit))
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t ror_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        const uint16_t carry_in{
            static_cast<uint16_t>((state.p & k_status_carry) != 0 ? (is_8bit ? 0x0080u : 0x8000u) : 0u)
        };
        if ((value & 0x0001u) != 0)
            state.p |= k_status_carry;
        else
            state.p &= static_cast<uint8_t>(~k_status_carry);

        const uint16_t result{
            static_cast<uint16_t>(((value & mask_for_width(is_8bit)) >> 1u) | carry_in)
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t inc_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        const uint16_t result{
            static_cast<uint16_t>((value + 1u) & mask_for_width(is_8bit))
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    [[nodiscard]] inline uint16_t dec_value(cpu_state_t& state,
                                            uint16_t value,
                                            bool is_8bit) noexcept
    {
        const uint16_t result{
            static_cast<uint16_t>((value - 1u) & mask_for_width(is_8bit))
        };
        set_zero_negative_flags(state, result, is_8bit);
        return result;
    }

    inline void bit_test(cpu_state_t& state,
                         uint16_t lhs,
                         uint16_t rhs,
                         bool is_8bit,
                         bool update_high_flags) noexcept
    {
        if (((lhs & rhs) & mask_for_width(is_8bit)) == 0)
            state.p |= k_status_zero;
        else
            state.p &= static_cast<uint8_t>(~k_status_zero);

        if (!update_high_flags)
            return;

        const uint16_t negative_mask{ static_cast<uint16_t>(is_8bit ? 0x0080u : 0x8000u) };
        const uint16_t overflow_mask{ static_cast<uint16_t>(is_8bit ? 0x0040u : 0x4000u) };
        if ((rhs & negative_mask) != 0)
            state.p |= k_status_negative;
        else
            state.p &= static_cast<uint8_t>(~k_status_negative);

        if ((rhs & overflow_mask) != 0)
            state.p |= k_status_overflow;
        else
            state.p &= static_cast<uint8_t>(~k_status_overflow);
    }

    [[nodiscard]] bool execute_load_opcode(uint8_t opcode,
                                           cpu_state_t& state,
                                           cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_jump_opcode(uint8_t opcode,
                                           cpu_state_t& state,
                                           cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_memory_opcode(uint8_t opcode,
                                             cpu_state_t& state,
                                             cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_transfer_opcode(uint8_t opcode,
                                               cpu_state_t& state,
                                               cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_stack_opcode(uint8_t opcode,
                                            cpu_state_t& state,
                                            cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_alu_opcode(uint8_t opcode,
                                          cpu_state_t& state,
                                          cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_modify_opcode(uint8_t opcode,
                                             cpu_state_t& state,
                                             cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_system_opcode(uint8_t opcode,
                                             cpu_t& cpu,
                                             cpu_state_t& state,
                                             cpu_step_executor_t& executor) noexcept;
    [[nodiscard]] bool execute_branch_opcode(uint8_t opcode,
                                             cpu_state_t& state,
                                             cpu_step_executor_t& executor) noexcept;
} // namespace clover::core
