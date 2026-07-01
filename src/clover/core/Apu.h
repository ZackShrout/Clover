//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/Timing.h"

#include <array>
#include <cstdint>

namespace clover::core
{
    struct apu_state_t
    {
        struct trace_entry_t
        {
            uint64_t master_clock{ 0 };
            uint16_t pc{ 0 };
            uint8_t opcode{ 0 };
            uint8_t a{ 0 };
            uint8_t x{ 0 };
            uint8_t y{ 0 };
            uint8_t sp{ 0 };
            uint8_t psw{ 0 };
            uint8_t timer0_stage2{ 0 };
            uint8_t timer0_stage3{ 0 };
            uint8_t port0{ 0 };
            uint8_t port1{ 0 };
            uint8_t port2{ 0 };
            uint8_t port3{ 0 };
        };

        struct io_trace_entry_t
        {
            uint64_t master_clock{ 0 };
            uint16_t pc{ 0 };
            uint8_t opcode{ 0 };
            uint16_t address{ 0 };
            uint8_t value{ 0 };
            bool is_write{ false };
            uint8_t a{ 0 };
            uint8_t x{ 0 };
            uint8_t y{ 0 };
            uint8_t psw{ 0 };
        };

        struct timer_state_t
        {
            uint8_t stage0{ 0 };
            uint8_t stage1{ 0 };
            uint8_t stage2{ 0 };
            uint8_t stage3{ 0 };
            bool line{ false };
            bool enable{ false };
            uint8_t target{ 0 };
        };

        uint16_t pc{ 0 };
        uint8_t a{ 0 };
        uint8_t x{ 0 };
        uint8_t y{ 0 };
        uint8_t sp{ 0 };
        uint8_t psw{ 0 };
        uint8_t external_wait_states{ 0 };
        uint8_t internal_wait_states{ 0 };
        bool timers_disable{ false };
        bool timers_enable{ true };
        bool ipl_rom_enabled{ true };
        bool halted{ false };
        uint8_t last_opcode{ 0 };
        timer_state_t timer0{};
        timer_state_t timer1{};
        timer_state_t timer2{};
        uint8_t instruction_trace_count{ 0 };
        uint8_t io_trace_count{ 0 };
    };

    struct apu_t
    {
    public:
        void power_on() noexcept;
        void reset() noexcept;
        void step(master_clock_delta_t master_clocks) noexcept;
        [[nodiscard]] master_clock_count_t master_clock() const noexcept;
        [[nodiscard]] uint8_t read_cpu_port(uint16_t address) const noexcept;
        void write_cpu_port(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_input_port(uint8_t port) const noexcept;
        void write_output_port(uint8_t port, uint8_t value) noexcept;
        [[nodiscard]] apu_state_t state() const noexcept;
        [[nodiscard]] uint8_t peek_ram(uint16_t address) const noexcept;
        [[nodiscard]] uint8_t instruction_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::trace_entry_t, 128>& instruction_trace() const noexcept;
        [[nodiscard]] uint8_t io_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::io_trace_entry_t, 128>& io_trace() const noexcept;

    private:
        template <uint8_t Frequency>
        struct timer_t
        {
            uint8_t stage0{ 0 };
            uint8_t stage1{ 0 };
            uint8_t stage2{ 0 };
            uint8_t stage3{ 0 };
            bool line{ false };
            bool enable{ false };
            uint8_t target{ 0 };
        };

        struct io_state_t
        {
            bool timers_disable{ false };
            bool ram_writable{ true };
            bool ram_disable{ false };
            bool timers_enable{ true };
            uint8_t external_wait_states{ 0 };
            uint8_t internal_wait_states{ 0 };
            uint8_t dsp_address{ 0 };
            uint8_t aux4{ 0 };
            uint8_t aux5{ 0 };
        };

        struct spc700_registers_t
        {
            uint16_t pc{ 0 };
            uint8_t a{ 0 };
            uint8_t x{ 0 };
            uint8_t y{ 0 };
            uint8_t sp{ 0 };
            uint8_t psw{ 0 };
        };

        static constexpr uint8_t k_psw_carry{ 0x01u };
        static constexpr uint8_t k_psw_zero{ 0x02u };
        static constexpr uint8_t k_psw_half_carry{ 0x08u };
        static constexpr uint8_t k_psw_break{ 0x10u };
        static constexpr uint8_t k_psw_direct_page{ 0x20u };
        static constexpr uint8_t k_psw_overflow{ 0x40u };
        static constexpr uint8_t k_psw_negative{ 0x80u };
        static constexpr master_clock_delta_t k_master_clocks_per_spc_cycle{ 21u };

        void execute_instruction() noexcept;
        [[nodiscard]] bool execute_load_store_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_alu_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_branch_bit_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_control_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] uint8_t fetch_u8() noexcept;
        [[nodiscard]] uint16_t fetch_u16() noexcept;
        [[nodiscard]] uint8_t fetch_u8_phased(master_clock_delta_t before_cycles,
                                              master_clock_delta_t after_cycles) noexcept;
        [[nodiscard]] uint8_t read_u8(uint16_t address) const noexcept;
        [[nodiscard]] uint8_t read_u8_phased(uint16_t address,
                                             master_clock_delta_t before_cycles,
                                             master_clock_delta_t after_cycles) noexcept;
        void write_u8(uint16_t address, uint8_t value) noexcept;
        void write_u8_phased(uint16_t address,
                             uint8_t value,
                             master_clock_delta_t before_cycles,
                             master_clock_delta_t after_cycles) noexcept;
        [[nodiscard]] static bool is_io_address(uint16_t address) noexcept;
        [[nodiscard]] static bool is_cpu_port_address(uint16_t address) noexcept;
        [[nodiscard]] uint16_t read_u16(uint16_t address) const noexcept;
        void write_u16(uint16_t address, uint16_t value) noexcept;
        [[nodiscard]] uint16_t direct_page_address(uint8_t address) const noexcept;
        [[nodiscard]] uint8_t read_direct(uint8_t address) const noexcept;
        [[nodiscard]] uint8_t read_direct_phased(uint8_t address,
                                                master_clock_delta_t before_cycles,
                                                master_clock_delta_t after_cycles) noexcept;
        [[nodiscard]] uint8_t read_direct_indexed(uint8_t address, uint8_t index) const noexcept;
        [[nodiscard]] uint8_t read_direct_indexed_phased(uint8_t address,
                                                         uint8_t index,
                                                         master_clock_delta_t before_cycles,
                                                         master_clock_delta_t after_cycles) noexcept;
        void write_direct(uint8_t address, uint8_t value) noexcept;
        [[nodiscard]] uint16_t read_direct_u16(uint8_t address) const noexcept;
        [[nodiscard]] uint16_t read_direct_indexed_u16(uint8_t address, uint8_t index) const noexcept;
        void write_direct_u16(uint8_t address, uint16_t value) noexcept;
        void clear_direct_bit(uint8_t address, uint8_t bit_mask) noexcept;
        void set_direct_bit(uint8_t address, uint8_t bit_mask) noexcept;
        [[nodiscard]] bool direct_bit_is_set(uint8_t address, uint8_t bit_mask) const noexcept;
        [[nodiscard]] uint8_t read_x_indirect() const noexcept;
        [[nodiscard]] uint8_t read_indexed_indirect(uint8_t zero_page_address) const noexcept;
        [[nodiscard]] uint8_t read_x_indirect_increment() noexcept;
        void write_x_indirect(uint8_t value) noexcept;
        void write_indexed_indirect(uint8_t zero_page_address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_indirect_y(uint8_t zero_page_address) const noexcept;
        void write_indirect_y(uint8_t zero_page_address, uint8_t value) noexcept;
        void push_stack(uint8_t value) noexcept;
        [[nodiscard]] uint8_t pull_stack() noexcept;
        void set_nz_flags(uint8_t value) noexcept;
        void set_compare_flags(uint8_t lhs, uint8_t rhs) noexcept;
        void add_with_carry(uint8_t value) noexcept;
        void subtract_with_carry(uint8_t value) noexcept;
        void and_accumulator(uint8_t value) noexcept;
        void arithmetic_shift_left_accumulator() noexcept;
        void arithmetic_shift_left_memory(uint16_t address) noexcept;
        void rotate_left_accumulator() noexcept;
        void rotate_left_memory(uint16_t address) noexcept;
        void logical_shift_right_accumulator() noexcept;
        void logical_shift_right_memory(uint16_t address) noexcept;
        void rotate_right_accumulator() noexcept;
        void rotate_right_memory(uint16_t address) noexcept;
        void or_accumulator(uint8_t value) noexcept;
        void xor_accumulator(uint8_t value) noexcept;
        void or_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept;
        void and_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept;
        void xor_direct_immediate(uint8_t immediate, uint8_t direct_address) noexcept;
        void or_indirect_x_with_indirect_y() noexcept;
        void and_indirect_x_with_indirect_y() noexcept;
        void xor_indirect_x_with_indirect_y() noexcept;
        void branch_relative_if_direct_bit_clear(uint8_t direct_address, uint8_t bit_mask) noexcept;
        void branch_relative_if_direct_bit_set(uint8_t direct_address, uint8_t bit_mask) noexcept;
        void branch_relative_if_direct_bit(uint8_t direct_address, uint8_t bit_mask, bool branch_on_set) noexcept;
        void branch_relative_if_accumulator_not_equal_direct(uint8_t direct_address) noexcept;
        void branch_relative_if_accumulator_not_equal_direct_indexed(uint8_t direct_address, uint8_t index) noexcept;
        void decrement_direct_and_branch_if_not_zero(uint8_t direct_address) noexcept;
        void multiply_ya() noexcept;
        void exchange_accumulator_nibbles() noexcept;
        uint16_t add_word(uint16_t lhs, uint16_t rhs) noexcept;
        uint16_t subtract_word(uint16_t lhs, uint16_t rhs) noexcept;
        void divide_ya_by_x() noexcept;
        void test_and_modify_bits_absolute(uint16_t address, bool set_bits) noexcept;
        void branch_relative_if(bool condition) noexcept;
        [[nodiscard]] uint8_t read_io(uint16_t address) noexcept;
        void write_io(uint16_t address, uint8_t value) noexcept;
        void reset_timer(timer_t<128>& timer, bool enabled) noexcept;
        void reset_timer(timer_t<16>& timer, bool enabled) noexcept;
        template <uint8_t Frequency>
        void synchronize_timer_stage1(timer_t<Frequency>& timer) noexcept;
        template <uint8_t Frequency>
        void step_timer(timer_t<Frequency>& timer, master_clock_delta_t spc_cycles) noexcept;
        void step_spc_cycles(master_clock_delta_t spc_cycles) noexcept;
        void halt_on_unimplemented_opcode(uint8_t opcode) noexcept;
        void trace_instruction(uint16_t pc, uint8_t opcode) noexcept;
        void trace_io_access(uint16_t address, uint8_t value, bool is_write) noexcept;

        master_clock_count_t _master_clock{ 0 };
        int64_t _cycle_credit{ 0 };
        spc700_registers_t _registers{};
        bool _ipl_rom_enabled{ true };
        bool _halted{ false };
        uint8_t _last_opcode{ 0 };
        io_state_t _io{};
        timer_t<128> _timer0{};
        timer_t<128> _timer1{};
        timer_t<16> _timer2{};
        std::array<uint8_t, 4> _apu_to_cpu_ports{};
        std::array<uint8_t, 4> _cpu_to_apu_ports{};
        std::array<apu_state_t::trace_entry_t, 128> _instruction_trace{};
        uint8_t _instruction_trace_count{ 0 };
        std::array<apu_state_t::io_trace_entry_t, 128> _io_trace{};
        uint8_t _io_trace_count{ 0 };
        std::array<uint8_t, 64 * 1024> _ram{};
    };
}
