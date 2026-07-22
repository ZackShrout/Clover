//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"

#include <array>
#include <cstdint>

namespace clover::core
{
    struct bus_t;
    struct cpu_step_executor_t;
    struct dma_t;
    struct interrupt_controller_t;
    struct ppu_step_result_t;
    struct ppu_t;

    struct cpu_state_t
    {
        uint16_t pc{ 0 };
        uint16_t sp{ 0x01ffu };
        uint16_t a{ 0 };
        uint16_t x{ 0 };
        uint16_t y{ 0 };
        uint16_t d{ 0 };
        uint8_t p{ 0x34u };
        uint8_t db{ 0 };
        uint8_t pb{ 0 };
        bool emulation_mode{ true };
    };

    struct cpu_io_t
    {
        bool auto_joypad_poll{ false };
        bool hirq_enabled{ false };
        bool virq_enabled{ false };
        bool nmi_enabled{ false };
        bool irq_enabled{ false };
        bool nmi_flag{ false };
        bool irq_flag{ false };
        bool in_hblank{ false };
        bool in_vblank{ false };
        bool fast_rom_enabled{ false };
        bool controller_port_1_latch{ false };
        uint8_t controller_port_1_shift_count{ 0 };
        uint8_t controller_port_2_shift_count{ 0 };
        uint8_t nmi_hold_clocks{ 0 };
        uint8_t irq_hold_clocks{ 0 };
        uint8_t pio{ 0xffu };
        uint8_t multiply_a{ 0xffu };
        uint8_t multiply_b{ 0xffu };
        uint16_t dividend{ 0xffffu };
        uint8_t divisor{ 0xffu };
        uint16_t quotient{ 0 };
        uint16_t multiply_or_remainder{ 0 };
        uint16_t auto_joypad_busy_clocks{ 0 };
        uint16_t auto_joypad_latched_1{ 0 };
        uint16_t auto_joypad_latched_2{ 0 };
        uint16_t joy1{ 0 };
        uint16_t joy2{ 0 };
        uint16_t joy3{ 0 };
        uint16_t joy4{ 0 };
        uint16_t htime{ 0x01ffu };
        uint16_t vtime{ 0x01ffu };
        uint32_t wram_address{ 0 };
    };

    struct cpu_t
    {
    public:
        void attach_bus(bus_t& bus) noexcept;
        void attach_interrupt_controller(interrupt_controller_t& interrupts) noexcept;
        void attach_ppu(ppu_t& ppu) noexcept;
        void configure_hardware(const video_timing_t& video_timing, uint8_t cpu_version) noexcept;
        void power_on() noexcept;
        void reset() noexcept;
        [[nodiscard]] uint8_t dma_phase(master_clock_delta_t elapsed_master_clocks = 0) const noexcept;
        [[nodiscard]] timing_snapshot_t timing(const video_timing_t& video_timing) const noexcept;
        [[nodiscard]] timing_snapshot_t delayed_timing(const video_timing_t& video_timing,
                                                       master_clock_delta_t delay) const noexcept;
        [[nodiscard]] uint32_t wram_address() const noexcept;
        [[nodiscard]] uint64_t placeholder_opcode_count() const noexcept;
        [[nodiscard]] uint8_t read_register(uint16_t address,
                                           master_clock_delta_t elapsed_master_clocks = 0) noexcept;
        void write_register(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] hardware_slot_owner_t next_slot_owner(const dma_t& dma) const noexcept;
        [[nodiscard]] cpu_step_result_t step(bus_t& bus,
                                             dma_t& dma,
                                             interrupt_controller_t& interrupts) noexcept;
        [[nodiscard]] master_clock_delta_t apply_system_timing(master_clock_delta_t elapsed_master_clocks,
                                                               const video_timing_t& video_timing) noexcept;
        void account_external_cpu_clocks(master_clock_delta_t elapsed_master_clocks) noexcept;
        void on_dma_step(const dma_t& dma, interrupt_controller_t& interrupts) noexcept;
        void on_ppu_step(master_clock_delta_t elapsed_master_clocks,
                         const video_timing_t& video_timing,
                         const ppu_step_result_t& ppu_step,
                         dma_t& dma,
                         interrupt_controller_t& interrupts) noexcept;
        [[nodiscard]] const cpu_state_t& state() const noexcept;
        void set_controller_state(uint8_t port, uint16_t state) noexcept;
        void set_interrupt_poll_phase_for_testing(master_clock_delta_t phase) noexcept;
        [[nodiscard]] master_clock_delta_t interrupt_poll_phase_for_testing() const noexcept;

    private:
        [[nodiscard]] bool irq_condition(const timing_snapshot_t& irq_timing,
                                         const timing_snapshot_t& irq_gate_timing) const noexcept;
        void repoll_irq_on_register_write(interrupt_controller_t& interrupts) noexcept;
        [[nodiscard]] master_clock_delta_t advance_execution(master_clock_delta_t elapsed_master_clocks,
                                                             dma_t& dma,
                                                             interrupt_controller_t& interrupts,
                                                             ppu_step_result_t& aggregate) noexcept;
        [[nodiscard]] master_clock_delta_t service_dma_edge(bus_t& bus,
                                                            dma_t& dma,
                                                            interrupt_controller_t& interrupts,
                                                            ppu_step_result_t& aggregate,
                                                            master_clock_delta_t bus_cycle_clocks) noexcept;
        void set_waiting(bool waiting) noexcept;
        void set_stopped(bool stopped) noexcept;
        void alu_edge() noexcept;

        cpu_state_t _state{};
        cpu_io_t _io{};
        master_clock_count_t _master_clock{ 0 };
        // Free-running S-CPU divider source. Unlike instruction retirement
        // time, this includes scheduler-owned DMA and refresh stalls.
        master_clock_count_t _dma_counter{ 0 };
        raster_counter_t _counter{};
        master_clock_delta_t _interrupt_poll_phase{ 2 };
        timing_snapshot_t _last_timing{};
        timing_snapshot_t _last_irq_timing{};
        timing_snapshot_t _last_irq_gate_timing{};
        bus_t* _bus{ nullptr };
        interrupt_controller_t* _interrupts{ nullptr };
        ppu_t* _ppu{ nullptr };
        bool _irq_condition_valid{ false };
        bool _nmi_poll_valid{ false };
        bool _dma_active{ false };
        bool _reset_pending{ false };
        bool _waiting{ false };
        bool _wait_wake_idle_pending{ false };
        bool _stopped{ false };
        uint16_t _visible_scanlines{ k_ntsc_video_timing.visible_scanlines };
        video_timing_t _video_timing{ k_ntsc_video_timing };
        uint8_t _cpu_version{ 2 };
        bool _interlace{ false };
        uint16_t _dram_refresh_dot{ dram_refresh_dot_v2(0) };
        bool _dram_refresh_pending{ true };
        uint16_t _hdma_setup_dot{ hdma_setup_dot_v2(0) };
        bool _hdma_setup_pending{ true };
        uint8_t _multiply_counter{ 0 };
        uint8_t _divide_counter{ 0 };
        uint32_t _math_shift{ 0 };
        uint64_t _placeholder_opcode_count{ 0u };
        std::array<uint16_t, 2> _controller_state{};

        friend struct cpu_step_executor_t;
        friend bool execute_system_opcode(uint8_t opcode,
                                          cpu_t& cpu,
                                          cpu_state_t& state,
                                          cpu_step_executor_t& executor) noexcept;
    };
}
