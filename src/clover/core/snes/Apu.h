//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"
#include "clover/core/snes/dsp/SPC_DSP.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace clover::core
{
    struct bus_t;
    inline constexpr std::size_t k_apu_trace_capacity{ 1024 };

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
            int64_t smp_clock_credit{ 0 };
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
        bool waiting{ false };
        bool stopped{ false };
        uint8_t last_opcode{ 0 };
        int64_t smp_clock_credit{ 0 };
        timer_state_t timer0{};
        timer_state_t timer1{};
        timer_state_t timer2{};
        uint16_t instruction_trace_count{ 0 };
        uint16_t io_trace_count{ 0 };
    };

    struct apu_t
    {
    public:
        static constexpr uint32_t k_audio_sample_rate_hz{ 32'040u };
        static constexpr size_t k_audio_buffer_sample_capacity{ 4096u };

        void power_on() noexcept;
        void reset() noexcept;
        void step(master_clock_delta_t master_clocks) noexcept;
        void synchronize_cpu_thread() noexcept;
        void begin_cpu_io_window(bus_t& bus, master_clock_delta_t target_clocks) noexcept;
        void end_cpu_io_window() noexcept;
        [[nodiscard]] master_clock_count_t master_clock() const noexcept;
        [[nodiscard]] uint8_t read_cpu_port(uint16_t address) const noexcept;
        void write_cpu_port(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_input_port(uint8_t port) const noexcept;
        void write_output_port(uint8_t port, uint8_t value) noexcept;
        void begin_audio_frame() noexcept;
        [[nodiscard]] std::span<const int16_t> audio_samples() const noexcept;
        [[nodiscard]] bool audio_output_overflowed() const noexcept;
        [[nodiscard]] apu_state_t state() const noexcept;
        [[nodiscard]] uint8_t peek_ram(uint16_t address) const noexcept;
        [[nodiscard]] uint8_t peek_dsp_register(uint8_t address) const noexcept;
        [[nodiscard]] std::array<uint8_t, SPC_DSP::state_size> dsp_state() noexcept;
        [[nodiscard]] uint16_t instruction_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::trace_entry_t, k_apu_trace_capacity>& instruction_trace() const noexcept;
        [[nodiscard]] uint16_t io_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::io_trace_entry_t, k_apu_trace_capacity>& io_trace() const noexcept;

    private:
        void initialize(bool warm_reset) noexcept;

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
        // bsnes models the SMP on the divided APU clock (~2.05056 MHz), not on
        // the post-wait-state effective ~1.024 MHz instruction cadence. The
        // wait-state values {2,4,10,20} are already expressed in that divided
        // SMP clock domain, so we convert master clocks to SMP clocks using the
        // same frequency ratio instead of multiplying each wait unit by 21.
        static constexpr int64_t k_master_clock_frequency_hz{ 21477272 };
        static constexpr int64_t k_smp_clock_frequency_hz{ k_audio_sample_rate_hz * 64 };
        static constexpr int64_t k_scheduler_zero_credit{ 0 };
        static constexpr int64_t k_force_cpu_sync_credit{
            768ll * 24ll * 24'000'000ll
        };

        [[nodiscard]] bool execute_instruction() noexcept;
        [[nodiscard]] bool execute_load_store_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_alu_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_branch_bit_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] bool execute_control_opcode(uint8_t opcode) noexcept;
        [[nodiscard]] uint8_t fetch_u8() noexcept;
        [[nodiscard]] uint16_t fetch_u16() noexcept;
        [[nodiscard]] uint8_t spc_fetch_u8() noexcept;
        [[nodiscard]] uint16_t spc_fetch_u16() noexcept;
        void spc_consume_opcode_fetch() noexcept;
        void spc_idle() noexcept;
        [[nodiscard]] uint8_t spc_read_u8(uint16_t address) noexcept;
        void spc_write_u8(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t spc_load_direct(uint8_t address) noexcept;
        void spc_store_direct(uint8_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t spc_load_direct_indexed(uint8_t address, uint8_t index) noexcept;
        [[nodiscard]] uint8_t spc_read_abs_indexed(uint16_t address, uint8_t index) noexcept;
        [[nodiscard]] uint8_t spc_read_indexed_indirect(uint8_t address, uint8_t index) noexcept;
        [[nodiscard]] uint8_t spc_read_indirect_indexed(uint8_t address, uint8_t index) noexcept;
        void spc_write_abs(uint16_t address, uint8_t value) noexcept;
        void spc_write_abs_indexed(uint16_t address, uint8_t index, uint8_t value) noexcept;
        void spc_write_indexed_indirect(uint8_t address, uint8_t index, uint8_t value) noexcept;
        void spc_write_indirect_indexed(uint8_t address, uint8_t index, uint8_t value) noexcept;
        void spc_push_stack(uint8_t value) noexcept;
        [[nodiscard]] uint8_t spc_pull_stack() noexcept;
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
        void step_access_cycles(master_clock_delta_t cycle_clocks,
                                master_clock_delta_t timer_clocks) noexcept;
        void step_dsp(master_clock_delta_t smp_clocks) noexcept;
        void wait_for_access(std::optional<uint16_t> address, bool half) noexcept;
        void step_spc_cycles(master_clock_delta_t spc_cycles) noexcept;
        void synchronize_cpu_io_visibility() noexcept;
        enum class access_kind_t : uint8_t
        {
            idle,
            read,
            write
        };
        struct access_journal_entry_t
        {
            access_kind_t kind{ access_kind_t::idle };
            uint16_t address{ 0 };
            uint8_t value{ 0 };
            bool awaiting_cpu_sync{ false };
        };
        struct instruction_context_t
        {
            bool active{ false };
            bool abort_requested{ false };
            spc700_registers_t start_registers{};
            uint16_t start_current_opcode_pc{ 0 };
            uint8_t start_last_opcode{ 0 };
            static constexpr uint8_t k_access_capacity{ 16 };
            std::array<access_journal_entry_t, k_access_capacity> accesses{};
            uint8_t access_count{ 0 };
            uint8_t replay_cursor{ 0 };
        };
        [[nodiscard]] access_journal_entry_t* replay_access(access_kind_t kind,
                                                            uint16_t address) noexcept;
        [[nodiscard]] access_journal_entry_t* append_access(access_kind_t kind,
                                                            uint16_t address,
                                                            uint8_t value,
                                                            bool awaiting_cpu_sync) noexcept;
        void request_cpu_sync() noexcept;
        void halt_on_unimplemented_opcode(uint8_t opcode) noexcept;
        void trace_instruction(uint16_t pc, uint8_t opcode) noexcept;
        void trace_io_access(uint16_t address, uint8_t value, bool is_write) noexcept;

        master_clock_count_t _master_clock{ 0 };
        int64_t _smp_clock_credit{ 0 };
        spc700_registers_t _registers{};
        bool _ipl_rom_enabled{ true };
        bool _halted{ false };
        bool _waiting{ false };
        bool _stopped{ false };
        uint16_t _current_opcode_pc{ 0 };
        uint8_t _last_opcode{ 0 };
        io_state_t _io{};
        SPC_DSP _dsp{};
        master_clock_delta_t _dsp_clock_remainder{ 0 };
        bool _dsp_initialized{ false };
        // 4096 interleaved values hold about 64 ms of stereo 32.04 kHz audio, covering
        // the longest PAL/interlaced video frame with headroom.
        std::array<int16_t, k_audio_buffer_sample_capacity> _audio_samples{};
        timer_t<128> _timer0{};
        timer_t<128> _timer1{};
        timer_t<16> _timer2{};
        std::array<uint8_t, 4> _apu_to_cpu_ports{};
        std::array<uint8_t, 4> _cpu_to_apu_ports{};
        instruction_context_t _instruction_context{};
        bool _smp_suspended_for_cpu{ false };
        bus_t* _cpu_io_window_bus{ nullptr };
        master_clock_delta_t _cpu_io_window_target_clocks{ 0 };
        int64_t _cpu_io_window_consumed_master_numerator{ 0 };
        std::array<apu_state_t::trace_entry_t, k_apu_trace_capacity> _instruction_trace{};
        uint16_t _instruction_trace_count{ 0 };
        std::array<apu_state_t::io_trace_entry_t, k_apu_trace_capacity> _io_trace{};
        uint16_t _io_trace_count{ 0 };
        std::array<uint8_t, 64 * 1024> _ram{};
    };
}
