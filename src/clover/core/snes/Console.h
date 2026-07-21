//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Apu.h"
#include "clover/core/snes/Bus.h"
#include "clover/core/snes/Cartridge.h"
#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/Dma.h"
#include "clover/core/FrameBuffer.h"
#include "clover/core/snes/Interrupts.h"
#include "clover/core/snes/Ppu.h"
#include "clover/core/snes/Scheduler.h"

#include <cstddef>
#include <span>

namespace clover::core
{
    struct hardware_timing_snapshot_t
    {
        timing_snapshot_t cpu_timing{};
        timing_snapshot_t cpu_timing_nmi_delay{};
        timing_snapshot_t cpu_timing_irq_delay{};
        timing_snapshot_t ppu_timing{};
        interrupt_state_t interrupts{};
        dma_activity_t dma_activity{ dma_activity_t::idle };
        bool hdma_pending{ false };
        bool general_dma_pending{ false };
    };

    struct console_t
    {
    public:
        void power_on() noexcept;
        void reset() noexcept;
        void set_startup_entropy_mode(startup_entropy_mode_t mode) noexcept;
        [[nodiscard]] startup_entropy_mode_t startup_entropy_mode() const noexcept;
        void set_startup_entropy_seed(uint32_t seed, uint32_t sequence = 0u) noexcept;
        void clear_startup_entropy_seed() noexcept;
        [[nodiscard]] bool load_cartridge(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] hardware_step_result_t step_hardware() noexcept;
        void run_scanline() noexcept;
        void run_frame() noexcept;
        void set_controller_state(uint8_t port, uint16_t state) noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address) noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] const framebuffer_t& framebuffer() const noexcept;
        [[nodiscard]] std::span<const int16_t> audio_samples() const noexcept;
        [[nodiscard]] bool audio_output_overflowed() const noexcept;
        [[nodiscard]] std::span<const std::byte> persistent_memory() const noexcept;
        [[nodiscard]] bool load_persistent_memory(std::span<const std::byte> data) noexcept;
        [[nodiscard]] bool persistent_memory_dirty() const noexcept;
        void mark_persistent_memory_clean() noexcept;
        [[nodiscard]] master_clock_count_t master_clock() const noexcept;
        [[nodiscard]] uint64_t frame_index() const noexcept;
        [[nodiscard]] video_standard_t video_standard() const noexcept;
        [[nodiscard]] const video_timing_t& video_timing() const noexcept;
        [[nodiscard]] timing_snapshot_t timing() const noexcept;
        [[nodiscard]] timing_snapshot_t cpu_timing() const noexcept;
        [[nodiscard]] const cpu_state_t& cpu_state() const noexcept;
        [[nodiscard]] uint32_t cpu_wram_address() const noexcept;
        [[nodiscard]] uint64_t cpu_placeholder_opcode_count() const noexcept;
        void set_cpu_interrupt_poll_phase_for_testing(master_clock_delta_t phase) noexcept;
        [[nodiscard]] master_clock_delta_t cpu_interrupt_poll_phase_for_testing() const noexcept;
        [[nodiscard]] master_clock_delta_t current_scanline_clocks() const noexcept;
        void refresh_framebuffer(const ppu_presentation_options_t& options = {}) noexcept;
        void set_presentation_layer_mask(uint8_t visible_layer_mask) noexcept;
        [[nodiscard]] hardware_timing_snapshot_t capture_timing_snapshot() const noexcept;
        [[nodiscard]] ppu_render_state_snapshot_t ppu_render_state() const noexcept;
        [[nodiscard]] ppu_compositor_snapshot_t ppu_compositor_state() const noexcept;
        [[nodiscard]] std::size_t ppu_cgram_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<ppu_cgram_write_trace_t, ppu_cgram_write_trace_capacity>&
            ppu_cgram_write_trace() const noexcept;
        [[nodiscard]] std::size_t ppu_oam_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<ppu_oam_write_trace_t, ppu_oam_write_trace_capacity>&
            ppu_oam_write_trace() const noexcept;
        [[nodiscard]] const std::array<uint16_t, 32 * 1024>& ppu_vram() const noexcept;
        [[nodiscard]] const std::array<uint8_t, 544>& ppu_oam() const noexcept;
        [[nodiscard]] const std::array<uint16_t, 256>& ppu_cgram() const noexcept;
        void set_frame_capture_enabled(bool enabled) noexcept;
        void set_ppu_entropy_mode(ppu_entropy_mode_t mode) noexcept;
        [[nodiscard]] ppu_entropy_mode_t ppu_entropy_mode() const noexcept;
        void set_ppu_entropy_seed(uint32_t seed, uint32_t sequence = 0u) noexcept;
        void clear_ppu_entropy_seed() noexcept;
        void set_ppu_cgram_write_trace_start_frame(uint64_t frame_index) noexcept;
        void set_ppu_oam_write_trace_start_frame(uint64_t frame_index) noexcept;
        [[nodiscard]] uint8_t open_bus() const noexcept;
        [[nodiscard]] interrupt_state_t interrupts() const noexcept;
        [[nodiscard]] bool hdma_pending() const noexcept;
        [[nodiscard]] bool general_dma_pending() const noexcept;
        [[nodiscard]] dma_activity_t dma_activity() const noexcept;
        [[nodiscard]] apu_state_t apu_state() const noexcept;
        [[nodiscard]] uint8_t apu_peek_ram(uint16_t address) const noexcept;
        [[nodiscard]] uint8_t apu_peek_dsp_register(uint8_t address) const noexcept;
        [[nodiscard]] std::array<uint8_t, SPC_DSP::state_size> apu_dsp_state() noexcept;
        [[nodiscard]] uint16_t apu_instruction_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::trace_entry_t, k_apu_trace_capacity>& apu_instruction_trace() const noexcept;
        [[nodiscard]] uint16_t apu_io_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_state_t::io_trace_entry_t, k_apu_trace_capacity>& apu_io_trace() const noexcept;
        [[nodiscard]] uint8_t ppu_register_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<bus_t::ppu_register_write_trace_t, bus_t::k_ppu_register_write_trace_capacity>& ppu_register_write_trace() const noexcept;
        [[nodiscard]] uint8_t system_register_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<bus_t::system_register_write_trace_t, bus_t::k_system_register_write_trace_capacity>& system_register_write_trace() const noexcept;
        [[nodiscard]] uint8_t watched_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<bus_t::watched_write_trace_t, bus_t::k_watched_write_trace_capacity>& watched_write_trace() const noexcept;
        [[nodiscard]] std::span<const uint8_t> wram_span(uint32_t offset, uint32_t length) const noexcept;
        void set_apu_port_trace_enabled(bool enabled) noexcept;
        [[nodiscard]] uint16_t apu_port_trace_count() const noexcept;
        [[nodiscard]] const std::array<bus_t::apu_port_trace_t, bus_t::k_apu_port_trace_capacity>& apu_port_trace() const noexcept;

    private:
        scheduler_t _scheduler{};
        bus_t _bus{};
        cartridge_t _cartridge{};
        cpu_t _cpu{};
        dma_t _dma{};
        interrupt_controller_t _interrupts{};
        ppu_t _ppu{};
        apu_t _apu{};
        framebuffer_t _framebuffer{};
        bool _powered_on{ false };
    };
}
