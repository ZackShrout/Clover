//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Console.h"

namespace clover::core
{
    void console_t::power_on() noexcept
    {
        _bus.connect_cartridge(_cartridge);
        _bus.connect_cpu(_cpu);
        _bus.connect_ppu(_ppu);
        _bus.connect_dma(_dma);
        _cpu.attach_bus(_bus);
        _cpu.attach_interrupt_controller(_interrupts);
        _cpu.attach_ppu(_ppu);
        _scheduler.reset();
        _cartridge.reset();
        _bus.reset();
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

        _scheduler.run_frame(_cpu, _bus, _ppu, _apu, _dma, _interrupts);
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

    master_clock_delta_t console_t::current_scanline_clocks() const noexcept
    {
        return _ppu.current_scanline_clocks();
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
}
