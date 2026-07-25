//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

#include <memory>
#include <utility>

namespace
{
    [[nodiscard]] clover::core::video_standard_t resolved_video_standard(
        const clover::core::console_causal_state_t& state
    ) noexcept
    {
        using namespace clover::core;
        switch (state.hardware_configuration.region)
        {
        case snes_region_selection_t::ntsc:
            return video_standard_t::ntsc;
        case snes_region_selection_t::pal:
            return video_standard_t::pal;
        case snes_region_selection_t::automatic:
        {
            if (!state.cartridge.loaded)
                return video_standard_t::ntsc;
            const uint8_t destination{ state.cartridge.header.destination_code };
            return (destination >= 0x02u && destination <= 0x0cu)
                    || destination == 0x11u
                ? video_standard_t::pal
                : video_standard_t::ntsc;
        }
        }
        return static_cast<video_standard_t>(0xffu);
    }

    [[nodiscard]] bool valid_cross_subsystem_state(
        const clover::core::console_causal_state_t& state,
        const clover::core::snes_hardware_profile_t& profile
    ) noexcept
    {
        using namespace clover::core;
        const video_standard_t standard{ resolved_video_standard(state) };
        const video_timing_t& timing{ video_timing_for(standard) };
        const master_clock_count_t clock{ state.scheduler.master_clock };

        return state.powered_on
            && state.resolved_video_standard == standard
            && state.cpu.video_timing == timing
            && state.ppu.video_timing == timing
            && state.cpu.cpu_version == profile.cpu_version
            && state.ppu.ppu1_version == profile.ppu1_version
            && state.ppu.ppu2_version == profile.ppu2_version
            && state.apu.master_clock_frequency_hz
                == static_cast<int64_t>(master_clock_frequency_hz(standard))
            && state.cpu.master_clock == clock
            && state.cpu.counter.master_clock == clock
            && state.ppu.counter.master_clock == clock
            && state.apu.master_clock == clock
            && state.cpu.counter == state.ppu.counter
            && state.scheduler.frame_index == state.ppu.frame_counter
            && state.cpu.visible_scanlines
                == timing.active_visible_scanlines(state.ppu.display_overscan)
            && state.cpu.interlace == state.ppu.timing_interlace
            && state.bus.entropy_mode == state.ppu.entropy_mode
            && state.bus.entropy_seed_override_enabled
                == state.ppu.entropy_seed_override_enabled
            && state.bus.entropy_seed == state.ppu.entropy_seed
            && state.bus.entropy_sequence == state.ppu.entropy_sequence
            && state.interrupts.nmi_transition_clock <= clock
            && state.interrupts.irq_transition_clock <= clock;
    }
}

namespace clover::core
{
    void console_t::apply_hardware_configuration() noexcept
    {
        const snes_hardware_profile_t* profile{
            snes_hardware_profile(_hardware_configuration.model)
        };
        if (profile == nullptr || !profile->implemented)
            profile = &k_default_snes_hardware_profile;

        video_standard_t standard{ video_standard_t::ntsc };
        switch (_hardware_configuration.region)
        {
        case snes_region_selection_t::automatic:
            standard = _cartridge.loaded()
                ? _cartridge.declared_video_standard()
                : video_standard_t::ntsc;
            break;
        case snes_region_selection_t::ntsc:
            standard = video_standard_t::ntsc;
            break;
        case snes_region_selection_t::pal:
            standard = video_standard_t::pal;
            break;
        }

        _hardware_identity = { .profile = profile, .video_standard = standard };
        const video_timing_t& video_timing{ video_timing_for(standard) };
        _cpu.configure_hardware(video_timing, profile->cpu_version);
        _ppu.configure_hardware(video_timing, profile->ppu1_version, profile->ppu2_version);
        _apu.configure_master_clock(master_clock_frequency_hz(standard));
    }

    void console_t::power_on() noexcept
    {
        apply_hardware_configuration();
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
        _dma.reset();
        _interrupts.reset();
        _ppu.power_on();
        _apu.power_on();
        _powered_on = true;
        static_cast<void>(_scheduler.step_hardware(_cpu, _bus, _ppu, _apu, _dma, _interrupts));
        _ppu.present(_framebuffer);
    }

    void console_t::reset() noexcept
    {
        _scheduler.reset();
        _cartridge.reset();
        _bus.reset();
        _cpu.reset();
        _dma.reset();
        _interrupts.reset();
        _ppu.reset();
        _apu.reset();
        static_cast<void>(_scheduler.step_hardware(_cpu, _bus, _ppu, _apu, _dma, _interrupts));
        _ppu.present(_framebuffer);
    }

    bool console_t::set_hardware_configuration(snes_hardware_configuration_t configuration) noexcept
    {
        const snes_hardware_profile_t* profile{ snes_hardware_profile(configuration.model) };
        if (profile == nullptr || !profile->implemented)
            return false;

        _hardware_configuration = configuration;
        apply_hardware_configuration();
        if (_powered_on)
            reset();
        return true;
    }

    snes_hardware_configuration_t console_t::hardware_configuration() const noexcept
    {
        return _hardware_configuration;
    }

    snes_hardware_identity_t console_t::hardware_identity() const noexcept
    {
        return _hardware_identity;
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

        apply_hardware_configuration();

        if (_powered_on)
            reset();

        return true;
    }

    std::span<const std::byte> console_t::canonical_media() const noexcept
    {
        return _cartridge.canonical_media();
    }

    cartridge_address_mapping_t console_t::translate_cartridge_address(
        uint32_t address
    ) const noexcept
    {
        return _cartridge.translate_address(address);
    }

    bool console_t::inspect_u8(uint32_t address, uint8_t& value) const noexcept
    {
        if (_bus.inspect_u8(address, value))
            return true;

        // Static analysis is valid after media load and before power-on, when
        // the runtime bus has not yet been connected to the cartridge.
        return !_powered_on && _cartridge.inspect_u8(address, value);
    }

    void console_t::set_observation_sink(snes_observation_sink_t* sink) noexcept
    {
        _scheduler.set_observation_sink(sink);
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

    cpu_boundary_step_result_t console_t::step_cpu_boundary() noexcept
    {
        if (!_powered_on)
            return {};

        const master_clock_count_t starting_clock{ _scheduler.master_clock() };
        while (true)
        {
            const hardware_step_result_t step{ step_hardware() };
            if (step.cpu_boundary == cpu_step_boundary_t::none)
                continue;

            return {
                .status = cpu_boundary_step_status_t::complete,
                .boundary = step.cpu_boundary,
                .elapsed_master_clocks = _scheduler.master_clock() - starting_clock
            };
        }
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

        begin_audio_frame();
        _ppu.set_frame_capture_enabled(true);
        _scheduler.run_frame(_cpu, _bus, _ppu, _apu, _dma, _interrupts);
        _ppu.set_frame_capture_enabled(false);
        _ppu.present(_framebuffer);
    }

    void console_t::begin_audio_frame() noexcept
    {
        _apu.begin_audio_frame();
    }

    void console_t::set_controller_state(uint8_t port, uint16_t state) noexcept
    {
        _cpu.set_controller_state(port, state);
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

    std::span<const int16_t> console_t::audio_samples() const noexcept
    {
        return _apu.audio_samples();
    }

    bool console_t::audio_output_overflowed() const noexcept
    {
        return _apu.audio_output_overflowed();
    }

    std::span<const uint8_t> console_t::cartridge_expansion_memory() const noexcept
    {
        return _cartridge.expansion_memory();
    }

    std::span<const std::byte> console_t::persistent_memory() const noexcept
    {
        return _cartridge.persistent_memory();
    }

    bool console_t::load_persistent_memory(std::span<const std::byte> data) noexcept
    {
        return _cartridge.load_persistent_memory(data);
    }

    bool console_t::persistent_memory_dirty() const noexcept
    {
        return _cartridge.persistent_memory_dirty();
    }

    void console_t::mark_persistent_memory_clean() noexcept
    {
        _cartridge.mark_persistent_memory_clean();
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

    void console_t::set_completed_frame_queue_enabled(bool enabled) noexcept
    {
        _ppu.set_completed_frame_queue_enabled(enabled);
    }

    bool console_t::pop_completed_frame(framebuffer_t& framebuffer) noexcept
    {
        return _ppu.pop_completed_frame(framebuffer);
    }

    void console_t::set_presentation_layer_mask(uint8_t visible_layer_mask) noexcept
    {
        _ppu.set_presentation_layer_mask(visible_layer_mask);
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

    std::array<uint8_t, SPC_DSP::state_size> console_t::apu_dsp_state() noexcept
    {
        return _apu.dsp_state();
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

    void console_t::set_apu_port_trace_enabled(bool enabled) noexcept
    {
        _bus.set_apu_port_trace_enabled(enabled);
    }

    void console_t::set_legacy_trace_enabled(bool enabled) noexcept
    {
        _bus.set_legacy_trace_enabled(enabled);
        _apu.set_legacy_trace_enabled(enabled);
    }

    uint16_t console_t::apu_port_trace_count() const noexcept
    {
        return _bus.apu_port_trace_count();
    }

    const std::array<bus_t::apu_port_trace_t, bus_t::k_apu_port_trace_capacity>& console_t::apu_port_trace() const noexcept
    {
        return _bus.apu_port_trace();
    }

    console_checkpoint_result_t console_t::capture_causal_state(
        console_causal_state_t& state
    ) noexcept
    {
        if (!_powered_on)
            return console_checkpoint_result_t::not_powered_on;
        if (_apu.cpu_io_window_active())
            return console_checkpoint_result_t::active_cpu_io_window;
        const snes_hardware_profile_t* profile{
            snes_hardware_profile(_hardware_configuration.model)
        };
        if (profile == nullptr || !profile->implemented)
            return console_checkpoint_result_t::invalid_hardware_configuration;

        try
        {
            auto captured{ std::make_unique<console_causal_state_t>() };
            captured->powered_on = true;
            captured->hardware_configuration = _hardware_configuration;
            captured->resolved_video_standard = _hardware_identity.video_standard;
            captured->scheduler = _scheduler.capture_causal_state();
            captured->bus = _bus.capture_causal_state();
            captured->cpu = _cpu.capture_causal_state();
            captured->dma = _dma.capture_causal_state();
            captured->interrupts = _interrupts.capture_causal_state();
            _ppu.capture_causal_state(captured->ppu);

            const cartridge_state_result_t cartridge_result{
                _cartridge.capture_causal_state(captured->cartridge)
            };
            if (cartridge_result == cartridge_state_result_t::unsupported_hardware)
                return console_checkpoint_result_t::unsupported_hardware;
            if (cartridge_result == cartridge_state_result_t::allocation_failed)
                return console_checkpoint_result_t::allocation_failed;
            if (cartridge_result != cartridge_state_result_t::success)
                return console_checkpoint_result_t::invalid_subsystem_state;

            const apu_causal_state_result_t apu_result{
                _apu.capture_causal_state(captured->apu)
            };
            if (apu_result == apu_causal_state_result_t::active_cpu_io_window)
                return console_checkpoint_result_t::active_cpu_io_window;
            if (apu_result != apu_causal_state_result_t::success)
                return console_checkpoint_result_t::invalid_subsystem_state;

            if (!valid_cross_subsystem_state(*captured, *profile))
                return console_checkpoint_result_t::cross_subsystem_mismatch;

            state = std::move(*captured);
            return console_checkpoint_result_t::success;
        }
        catch (...)
        {
            return console_checkpoint_result_t::allocation_failed;
        }
    }

    console_checkpoint_result_t console_t::restore_causal_state(
        const console_causal_state_t& state
    ) noexcept
    {
        if (!_powered_on || !state.powered_on)
            return console_checkpoint_result_t::not_powered_on;
        if (_apu.cpu_io_window_active())
            return console_checkpoint_result_t::active_cpu_io_window;
        if (_cartridge.loaded() != state.cartridge.loaded)
            return console_checkpoint_result_t::media_mismatch;

        const snes_hardware_profile_t* profile{
            snes_hardware_profile(state.hardware_configuration.model)
        };
        if (profile == nullptr || !profile->implemented
            || state.hardware_configuration.region
                > snes_region_selection_t::pal)
        {
            return console_checkpoint_result_t::invalid_hardware_configuration;
        }
        if (!valid_cross_subsystem_state(state, *profile))
            return console_checkpoint_result_t::cross_subsystem_mismatch;

        try
        {
            auto candidate{ std::make_unique<console_t>() };
            if (!candidate->set_hardware_configuration(
                    state.hardware_configuration))
            {
                return console_checkpoint_result_t::invalid_hardware_configuration;
            }
            if (state.cartridge.loaded
                && !candidate->load_cartridge(_cartridge.canonical_media()))
            {
                // The source is the already-loaded canonical media of the live
                // console, so structural parsing cannot newly fail here.
                return console_checkpoint_result_t::allocation_failed;
            }
            candidate->power_on();

            const cartridge_state_result_t cartridge_result{
                candidate->_cartridge.restore_causal_state(state.cartridge)
            };
            if (cartridge_result == cartridge_state_result_t::unsupported_hardware)
                return console_checkpoint_result_t::unsupported_hardware;
            if (cartridge_result == cartridge_state_result_t::topology_mismatch)
                return console_checkpoint_result_t::media_mismatch;
            if (cartridge_result != cartridge_state_result_t::success)
                return console_checkpoint_result_t::invalid_subsystem_state;

            const bool ppu_restored{
                candidate->_ppu.restore_causal_state(state.ppu)
            };
            const bool bus_restored{
                candidate->_bus.restore_causal_state(state.bus)
            };
            const bool cpu_restored{
                candidate->_cpu.restore_causal_state(state.cpu)
            };
            const bool dma_restored{
                candidate->_dma.restore_causal_state(state.dma)
            };
            const bool interrupts_restored{
                candidate->_interrupts.restore_causal_state(state.interrupts)
            };
            if (!ppu_restored
                || !bus_restored
                || !cpu_restored
                || !dma_restored
                || !interrupts_restored)
            {
                return console_checkpoint_result_t::invalid_subsystem_state;
            }
            const apu_causal_state_result_t apu_result{
                candidate->_apu.restore_causal_state(state.apu)
            };
            if (apu_result != apu_causal_state_result_t::success)
                return console_checkpoint_result_t::invalid_subsystem_state;
            candidate->_scheduler.restore_causal_state(state.scheduler);

            if (candidate->_scheduler.master_clock() != state.scheduler.master_clock
                || candidate->_cpu.timing(candidate->_ppu.video_timing()).master_clock
                    != state.scheduler.master_clock
                || candidate->_ppu.timing().master_clock
                    != state.scheduler.master_clock
                || candidate->_apu.master_clock() != state.scheduler.master_clock
                || candidate->_scheduler.frame_index() != state.scheduler.frame_index
                || candidate->_ppu.frame_index() != state.scheduler.frame_index)
            {
                return console_checkpoint_result_t::cross_subsystem_mismatch;
            }

            // Every fallible restore has succeeded against an independently
            // wired candidate. The same payload and current cartridge topology
            // now make the live commit a no-fail operation.
            const cartridge_state_result_t live_cartridge_result{
                _cartridge.restore_causal_state(state.cartridge)
            };
            if (live_cartridge_result != cartridge_state_result_t::success)
                return console_checkpoint_result_t::media_mismatch;

            _hardware_configuration = state.hardware_configuration;
            _hardware_identity = candidate->_hardware_identity;
            _scheduler.restore_causal_state(state.scheduler);
            static_cast<void>(_bus.restore_causal_state(state.bus));
            static_cast<void>(_cpu.restore_causal_state(state.cpu));
            static_cast<void>(_dma.restore_causal_state(state.dma));
            static_cast<void>(_interrupts.restore_causal_state(state.interrupts));
            static_cast<void>(_ppu.restore_causal_state(state.ppu));
            static_cast<void>(_apu.restore_causal_state(state.apu));
            _powered_on = true;
            _ppu.present(_framebuffer);
            return console_checkpoint_result_t::success;
        }
        catch (...)
        {
            return console_checkpoint_result_t::allocation_failed;
        }
    }
}
