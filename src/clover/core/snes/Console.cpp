//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

namespace clover::core
{
    void console_t::power_on() noexcept
    {
        _bus.connect_apu(_apu);
        _bus.connect_cartridge(_cartridge);
        _bus.connect_cpu(_cpu);
        _bus.connect_ppu(_ppu);
        _bus.connect_dma(_dma);
        _cpu.attach_bus(_bus);
        _cpu.attach_interrupt_controller(_interrupts);
        _cpu.attach_ppu(_ppu);
        _scheduler.reset();
        _cartridge.reset();
        _bus.power_on();
        _cpu.power_on();
        _cpu.load_reset_vector(_bus);
        _dma.reset();
        _interrupts.reset();
        _ppu.power_on();
        _apu.power_on();
        _ppu.present(_framebuffer);
        _powered_on = true;
    }

    void console_t::reset() noexcept
    {
        _scheduler.reset();
        _bus.reset();
        _cpu.reset();
        _cpu.load_reset_vector(_bus);
        _dma.reset();
        _interrupts.reset();
        _ppu.reset();
        _apu.reset();
        _ppu.present(_framebuffer);
    }

    void console_t::set_startup_entropy_mode(startup_entropy_mode_t mode) noexcept
    {
        _bus.set_entropy_mode(mode);
        _ppu.set_entropy_mode(mode);
    }

    startup_entropy_mode_t console_t::startup_entropy_mode() const noexcept
    {
        return _ppu.entropy_mode();
    }

    void console_t::set_startup_entropy_seed(uint32_t seed, uint32_t sequence) noexcept
    {
        _bus.set_entropy_seed(seed, sequence);
        _ppu.set_entropy_seed(seed, sequence);
    }

    void console_t::clear_startup_entropy_seed() noexcept
    {
        _bus.clear_entropy_seed();
        _ppu.clear_entropy_seed();
    }

    bool console_t::load_cartridge(std::span<const std::byte> rom_data) noexcept
    {
        const bool loaded{ _cartridge.load(rom_data) };
        if (!loaded)
            return false;

        if (_powered_on)
            reset();

        return true;
    }

    hardware_step_result_t console_t::step_hardware() noexcept
    {
        if (!_powered_on)
            return {};

        const hardware_step_result_t step{
            _scheduler.step_hardware(_cpu, _bus, _ppu, _apu, _dma, _interrupts)
        };
        if (step.ppu.frame_complete)
            _ppu.present(_framebuffer);

        return step;
    }

    void console_t::run_scanline() noexcept
    {
        if (!_powered_on)
            return;

        _scheduler.run_scanline(_cpu, _bus, _ppu, _apu, _dma, _interrupts);
    }

    void console_t::run_frame() noexcept
    {
        if (!_powered_on)
            return;

        _ppu.set_frame_capture_enabled(true);
        _scheduler.run_frame(_cpu, _bus, _ppu, _apu, _dma, _interrupts);
        _ppu.set_frame_capture_enabled(false);
        _ppu.present(_framebuffer);
    }

    uint8_t console_t::read_u8(uint32_t address) noexcept
    {
        return _bus.read_u8(address);
    }

    void console_t::write_u8(uint32_t address, uint8_t value) noexcept
    {
        _bus.write_u8(address, value);
    }

    const framebuffer_t& console_t::framebuffer() const noexcept
    {
        return _framebuffer;
    }

    master_clock_count_t console_t::master_clock() const noexcept
    {
        return _scheduler.master_clock();
    }

    uint64_t console_t::frame_index() const noexcept
    {
        return _scheduler.frame_index();
    }

    video_standard_t console_t::video_standard() const noexcept
    {
        return _ppu.video_standard();
    }

    const video_timing_t& console_t::video_timing() const noexcept
    {
        return _ppu.video_timing();
    }

    timing_snapshot_t console_t::timing() const noexcept
    {
        return _ppu.timing();
    }

    timing_snapshot_t console_t::cpu_timing() const noexcept
    {
        return _cpu.timing(_ppu.video_timing());
    }

    const cpu_state_t& console_t::cpu_state() const noexcept
    {
        return _cpu.state();
    }

    uint32_t console_t::cpu_wram_address() const noexcept
    {
        return _cpu.wram_address();
    }

    uint64_t console_t::cpu_placeholder_opcode_count() const noexcept
    {
        return _cpu.placeholder_opcode_count();
    }

    void console_t::set_cpu_interrupt_poll_phase_for_testing(master_clock_delta_t phase) noexcept
    {
        _cpu.set_interrupt_poll_phase_for_testing(phase);
    }

    master_clock_delta_t console_t::cpu_interrupt_poll_phase_for_testing() const noexcept
    {
        return _cpu.interrupt_poll_phase_for_testing();
    }

    master_clock_delta_t console_t::current_scanline_clocks() const noexcept
    {
        return _ppu.current_scanline_clocks();
    }

    void console_t::refresh_framebuffer(const ppu_presentation_options_t& options) noexcept
    {
        _ppu.present(_framebuffer, options);
    }

    hardware_timing_snapshot_t console_t::capture_timing_snapshot() const noexcept
    {
        return {
            .cpu_timing = _cpu.timing(_ppu.video_timing()),
            .cpu_timing_nmi_delay = _cpu.delayed_timing(_ppu.video_timing(), 2),
            .cpu_timing_irq_delay = _cpu.delayed_timing(_ppu.video_timing(), 10),
            .ppu_timing = _ppu.timing(),
            .interrupts = _interrupts.sample(),
            .dma_activity = _dma.activity(),
            .hdma_pending = _dma.hdma_pending(),
            .general_dma_pending = _dma.general_dma_pending()
        };
    }

    ppu_render_state_snapshot_t console_t::ppu_render_state() const noexcept
    {
        return _ppu.render_state_snapshot();
    }

    ppu_compositor_snapshot_t console_t::ppu_compositor_state() const noexcept
    {
        return _ppu.compositor_snapshot();
    }

    std::size_t console_t::ppu_cgram_write_trace_count() const noexcept
    {
        return _ppu.cgram_write_trace_count();
    }

    const std::array<ppu_cgram_write_trace_t, ppu_cgram_write_trace_capacity>&
    console_t::ppu_cgram_write_trace() const noexcept
    {
        return _ppu.cgram_write_trace();
    }

    std::size_t console_t::ppu_oam_write_trace_count() const noexcept
    {
        return _ppu.oam_write_trace_count();
    }

    const std::array<ppu_oam_write_trace_t, ppu_oam_write_trace_capacity>&
    console_t::ppu_oam_write_trace() const noexcept
    {
        return _ppu.oam_write_trace();
    }

    const std::array<uint16_t, 32 * 1024>& console_t::ppu_vram() const noexcept
    {
        return _ppu.vram();
    }

    const std::array<uint8_t, 544>& console_t::ppu_oam() const noexcept
    {
        return _ppu.oam();
    }

    const std::array<uint16_t, 256>& console_t::ppu_cgram() const noexcept
    {
        return _ppu.cgram();
    }

    void console_t::set_frame_capture_enabled(bool enabled) noexcept
    {
        _ppu.set_frame_capture_enabled(enabled);
    }

    void console_t::set_ppu_entropy_mode(ppu_entropy_mode_t mode) noexcept
    {
        _bus.set_entropy_mode(mode);
        _ppu.set_entropy_mode(mode);
    }

    ppu_entropy_mode_t console_t::ppu_entropy_mode() const noexcept
    {
        return _ppu.entropy_mode();
    }

    void console_t::set_ppu_entropy_seed(uint32_t seed, uint32_t sequence) noexcept
    {
        _bus.set_entropy_seed(seed, sequence);
        _ppu.set_entropy_seed(seed, sequence);
    }

    void console_t::clear_ppu_entropy_seed() noexcept
    {
        _bus.clear_entropy_seed();
        _ppu.clear_entropy_seed();
    }

    void console_t::set_ppu_cgram_write_trace_start_frame(uint64_t frame_index) noexcept
    {
        _ppu.set_cgram_write_trace_start_frame(frame_index);
    }

    void console_t::set_ppu_oam_write_trace_start_frame(uint64_t frame_index) noexcept
    {
        _ppu.set_oam_write_trace_start_frame(frame_index);
    }

    uint8_t console_t::open_bus() const noexcept
    {
        return _bus.open_bus();
    }

    interrupt_state_t console_t::interrupts() const noexcept
    {
        return _interrupts.sample();
    }

    bool console_t::hdma_pending() const noexcept
    {
        return _dma.hdma_pending();
    }

    bool console_t::general_dma_pending() const noexcept
    {
        return _dma.general_dma_pending();
    }

    dma_activity_t console_t::dma_activity() const noexcept
    {
        return _dma.activity();
    }

    apu_state_t console_t::apu_state() const noexcept
    {
        return _apu.state();
    }

    uint8_t console_t::apu_peek_ram(uint16_t address) const noexcept
    {
        return _apu.peek_ram(address);
    }

    uint8_t console_t::apu_peek_dsp_register(uint8_t address) const noexcept
    {
        return _apu.peek_dsp_register(address);
    }

    uint16_t console_t::apu_instruction_trace_count() const noexcept
    {
        return _apu.instruction_trace_count();
    }

    const std::array<apu_state_t::trace_entry_t, k_apu_trace_capacity>& console_t::apu_instruction_trace() const noexcept
    {
        return _apu.instruction_trace();
    }

    uint16_t console_t::apu_io_trace_count() const noexcept
    {
        return _apu.io_trace_count();
    }

    const std::array<apu_state_t::io_trace_entry_t, k_apu_trace_capacity>& console_t::apu_io_trace() const noexcept
    {
        return _apu.io_trace();
    }

    uint8_t console_t::ppu_register_write_trace_count() const noexcept
    {
        return _bus.ppu_register_write_trace_count();
    }

    const std::array<bus_t::ppu_register_write_trace_t, bus_t::k_ppu_register_write_trace_capacity>& console_t::ppu_register_write_trace() const noexcept
    {
        return _bus.ppu_register_write_trace();
    }

    uint8_t console_t::system_register_write_trace_count() const noexcept
    {
        return _bus.system_register_write_trace_count();
    }

    const std::array<bus_t::system_register_write_trace_t, bus_t::k_system_register_write_trace_capacity>& console_t::system_register_write_trace() const noexcept
    {
        return _bus.system_register_write_trace();
    }

    uint8_t console_t::watched_write_trace_count() const noexcept
    {
        return _bus.watched_write_trace_count();
    }

    const std::array<bus_t::watched_write_trace_t, bus_t::k_watched_write_trace_capacity>& console_t::watched_write_trace() const noexcept
    {
        return _bus.watched_write_trace();
    }

    std::span<const uint8_t> console_t::wram_span(uint32_t offset, uint32_t length) const noexcept
    {
        return _bus.wram_span(offset, length);
    }

    uint16_t console_t::apu_port_trace_count() const noexcept
    {
        return _bus.apu_port_trace_count();
    }

    const std::array<bus_t::apu_port_trace_t, bus_t::k_apu_port_trace_capacity>& console_t::apu_port_trace() const noexcept
    {
        return _bus.apu_port_trace();
    }
}
