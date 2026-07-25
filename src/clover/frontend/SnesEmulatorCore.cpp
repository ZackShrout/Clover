//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesEmulatorCore.h"

#include <algorithm>
#include <new>

namespace clover::frontend
{
    namespace
    {
        [[nodiscard]] execution_boundary_t execution_boundary(
            core::cpu_step_boundary_t boundary
        ) noexcept
        {
            switch (boundary)
            {
            case core::cpu_step_boundary_t::instruction_retired:
                return execution_boundary_t::instruction;
            case core::cpu_step_boundary_t::reset_completed:
                return execution_boundary_t::reset;
            case core::cpu_step_boundary_t::interrupt_entered:
                return execution_boundary_t::interrupt;
            case core::cpu_step_boundary_t::waiting:
                return execution_boundary_t::waiting;
            case core::cpu_step_boundary_t::stopped:
                return execution_boundary_t::stopped;
            case core::cpu_step_boundary_t::none:
                return execution_boundary_t::none;
            }
            return execution_boundary_t::none;
        }
    }

    uint16_t snes_joypad_state(const gamepad_state_t& state) noexcept
    {
        uint16_t result{ 0 };
        const auto map = [&result, &state](gamepad_button_t button, uint8_t bit) noexcept
        {
            if (state.pressed(button))
                result |= static_cast<uint16_t>(1u << bit);
        };

        map(gamepad_button_t::face_south, 15u); // B
        map(gamepad_button_t::face_west, 14u);  // Y
        map(gamepad_button_t::back, 13u);
        map(gamepad_button_t::start, 12u);
        map(gamepad_button_t::dpad_up, 11u);
        map(gamepad_button_t::dpad_down, 10u);
        map(gamepad_button_t::dpad_left, 9u);
        map(gamepad_button_t::dpad_right, 8u);
        map(gamepad_button_t::face_east, 7u);   // A
        map(gamepad_button_t::face_north, 6u);  // X
        map(gamepad_button_t::left_shoulder, 5u);
        map(gamepad_button_t::right_shoulder, 4u);
        return result;
    }

    system_id_t snes_emulator_core_t::system() const noexcept
    {
        return system_id_t::snes;
    }

    bool snes_emulator_core_t::load_media(std::span<const std::byte> media) noexcept
    {
        if (!_console.load_cartridge(media))
            return false;

        _address_spaces[2].size_bytes = _console.canonical_media().size();
        return true;
    }

    void snes_emulator_core_t::power_on() noexcept
    {
        _console.power_on();
        _console.set_presentation_layer_mask(_visible_layer_mask);
        _machine_running = true;
        _debug_paused = false;
    }

    void snes_emulator_core_t::reset() noexcept
    {
        _console.reset();
        _console.set_presentation_layer_mask(_visible_layer_mask);
    }

    void snes_emulator_core_t::set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept
    {
        if (port < 2u)
            _console.set_controller_state(static_cast<uint8_t>(port), snes_joypad_state(state));
    }

    void snes_emulator_core_t::run_frame() noexcept
    {
        if (_debug_paused)
            return;

        _console.run_frame();
        if (_visible_layer_mask != core::ppu_presentation_options_t::k_all_layers_visible)
        {
            _console.refresh_framebuffer({
                .visible_layer_mask = _visible_layer_mask
            });
        }
    }

    display_info_t snes_emulator_core_t::display_info() const noexcept
    {
        const core::video_timing_t& timing{ _console.video_timing() };
        const double refresh_hz{
            static_cast<double>(core::master_clock_frequency_hz(timing.standard))
                / static_cast<double>(timing.master_clocks_per_frame())
        };
        return {
            .framebuffer_width = core::framebuffer_t::k_max_width,
            .framebuffer_height = core::framebuffer_t::k_max_height,
            .pixel_aspect_ratio = timing.standard == core::video_standard_t::pal
                ? 55.f / 43.f
                : 8.f / 7.f,
            .nominal_refresh_hz = refresh_hz
        };
    }

    bool snes_emulator_core_t::set_hardware_configuration(
        core::snes_hardware_configuration_t configuration
    ) noexcept
    {
        return _console.set_hardware_configuration(configuration);
    }

    core::snes_hardware_identity_t snes_emulator_core_t::hardware_identity() const noexcept
    {
        return _console.hardware_identity();
    }

    video_frame_view_t snes_emulator_core_t::video_frame() const noexcept
    {
        return {
            .pixels = _console.framebuffer().data(),
            .width = _console.framebuffer().width(),
            .height = _console.framebuffer().height(),
            .pitch_bytes = _console.framebuffer().pitch_pixels() * sizeof(uint32_t),
            .format = pixel_format_t::argb8888
        };
    }

    audio_frame_view_t snes_emulator_core_t::audio_frame() const noexcept
    {
        return {
            .interleaved_samples = _console.audio_samples(),
            .sample_rate_hz = core::apu_t::k_audio_sample_rate_hz,
            .channels = 2u,
            .discontinuity = _console.audio_output_overflowed()
        };
    }

    std::span<const std::byte> snes_emulator_core_t::persistent_memory() const noexcept
    {
        return _console.persistent_memory();
    }

    bool snes_emulator_core_t::load_persistent_memory(std::span<const std::byte> data) noexcept
    {
        return _console.load_persistent_memory(data);
    }

    bool snes_emulator_core_t::persistent_memory_dirty() const noexcept
    {
        return _console.persistent_memory_dirty();
    }

    void snes_emulator_core_t::mark_persistent_memory_clean() noexcept
    {
        _console.mark_persistent_memory_clean();
    }

    video_plane_control_t* snes_emulator_core_t::video_plane_control() noexcept
    {
        return this;
    }

    debug_target_t* snes_emulator_core_t::debug_target() noexcept
    {
        return this;
    }

    std::span<const video_plane_descriptor_t> snes_emulator_core_t::video_planes() const noexcept
    {
        return _video_planes;
    }

    bool snes_emulator_core_t::set_video_plane_enabled(video_plane_id_t id, bool enabled) noexcept
    {
        if (id >= _video_planes.size())
            return false;
        const uint8_t bit{ static_cast<uint8_t>(1u << id) };
        _visible_layer_mask = enabled
            ? static_cast<uint8_t>(_visible_layer_mask | bit)
            : static_cast<uint8_t>(_visible_layer_mask & ~bit);
        _video_planes[id].enabled = enabled;
        _console.set_presentation_layer_mask(_visible_layer_mask);
        return true;
    }

    std::span<const execution_domain_descriptor_t>
        snes_emulator_core_t::execution_domains() const noexcept
    {
        return _execution_domains;
    }

    std::span<const address_space_descriptor_t>
        snes_emulator_core_t::address_spaces() const noexcept
    {
        return _address_spaces;
    }

    memory_inspection_result_t snes_emulator_core_t::inspect_memory(
        debug_address_t address,
        std::span<std::byte> destination
    ) const noexcept
    {
        if (destination.empty())
        {
            return {
                .status = memory_inspection_status_t::complete
            };
        }

        const auto in_range = [address, destination](uint64_t size) noexcept
        {
            return address.value < size
                && destination.size() <= size - address.value;
        };

        if (address.space == snes_debug::k_wram_space)
        {
            if (!in_range(core::bus_t::k_wram_size))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            const std::span<const uint8_t> bytes{
                _console.wram_span(
                    static_cast<uint32_t>(address.value),
                    static_cast<uint32_t>(destination.size())
                )
            };
            for (size_t index{ 0 }; index < bytes.size(); ++index)
                destination[index] = static_cast<std::byte>(bytes[index]);
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_canonical_media_space)
        {
            const std::span<const std::byte> media{ _console.canonical_media() };
            if (!in_range(media.size()))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
                destination[index] = media[static_cast<size_t>(address.value) + index];
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_apu_ram_space)
        {
            if (!in_range(0x00010000u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
            {
                destination[index] = static_cast<std::byte>(
                    _console.apu_peek_ram(
                        static_cast<uint16_t>(address.value + index)
                    )
                );
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_cpu_bus_space)
        {
            if (!in_range(0x01000000u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
            {
                uint8_t value{};
                if (!_console.inspect_u8(
                    static_cast<uint32_t>(address.value + index),
                    value
                ))
                {
                    return {
                        .status = memory_inspection_status_t::unavailable,
                        .bytes_read = index
                    };
                }
                destination[index] = static_cast<std::byte>(value);
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        return {
            .status = memory_inspection_status_t::invalid_address_space
        };
    }

    address_translation_result_t snes_emulator_core_t::translate_address(
        debug_address_t source,
        address_space_id_t destination_space
    ) const noexcept
    {
        const auto valid_space = [this](address_space_id_t id) noexcept
        {
            for (const address_space_descriptor_t& descriptor : _address_spaces)
            {
                if (descriptor.id == id)
                    return true;
            }
            return false;
        };
        if (!valid_space(source.space) || !valid_space(destination_space))
        {
            return {
                .status = address_translation_status_t::invalid_address_space
            };
        }

        if (source.space != snes_debug::k_cpu_bus_space
            || destination_space != snes_debug::k_canonical_media_space)
        {
            return {
                .status = address_translation_status_t::unsupported
            };
        }
        if (source.value >= 0x01000000u)
        {
            return {
                .status = address_translation_status_t::unmapped
            };
        }

        const core::cartridge_address_mapping_t mapping{
            _console.translate_cartridge_address(static_cast<uint32_t>(source.value))
        };
        if (mapping.kind != core::cartridge_address_kind_t::program_rom)
        {
            return {
                .status = address_translation_status_t::unmapped
            };
        }

        return {
            .status = address_translation_status_t::complete,
            .address = {
                .space = snes_debug::k_canonical_media_space,
                .value = mapping.storage_offset
            }
        };
    }

    execution_control_t* snes_emulator_core_t::execution_control() noexcept
    {
        return this;
    }

    execution_step_result_t snes_emulator_core_t::step_execution_domain(
        execution_domain_id_t domain
    ) noexcept
    {
        if (domain != snes_debug::k_main_cpu_domain)
        {
            const execution_step_status_t status{
                domain == snes_debug::k_audio_cpu_domain
                    ? execution_step_status_t::unsupported
                    : execution_step_status_t::invalid_domain
            };
            return {
                .status = status,
                .domain = domain
            };
        }

        if (!_machine_running)
        {
            return {
                .status = execution_step_status_t::not_running,
                .domain = domain
            };
        }
        if (!_debug_paused)
        {
            return {
                .status = execution_step_status_t::not_paused,
                .domain = domain
            };
        }

        const core::cpu_boundary_step_result_t result{ _console.step_cpu_boundary() };
        if (result.status != core::cpu_boundary_step_status_t::complete)
        {
            return {
                .status = execution_step_status_t::not_running,
                .domain = domain
            };
        }

        return {
            .status = execution_step_status_t::complete,
            .domain = domain,
            .boundary = execution_boundary(result.boundary),
            .machine_clocks_elapsed = result.elapsed_master_clocks
        };
    }

    observation_control_t* snes_emulator_core_t::observation_control() noexcept
    {
        return this;
    }

    observation_mask_t snes_emulator_core_t::available_observations() const noexcept
    {
        return k_observe_execution_boundary;
    }

    observation_mask_t snes_emulator_core_t::observation_mask() const noexcept
    {
        return _observation_mask;
    }

    bool snes_emulator_core_t::set_observation_mask(observation_mask_t mask) noexcept
    {
        if ((mask & ~available_observations()) != 0u)
            return false;
        if (mask == _observation_mask)
            return true;

        if (mask == 0u)
        {
            _console.set_observation_sink(nullptr);
            _observation_sink.disable();
            _observation_storage.reset();
            _observation_mask = 0u;
            return true;
        }

        if (!_observation_storage)
        {
            _observation_storage.reset(
                new (std::nothrow) core::snes_observation_event_t[k_observation_capacity]
            );
            if (!_observation_storage)
                return false;
        }

        _observation_sink.configure(
            { _observation_storage.get(), k_observation_capacity },
            core::k_snes_observe_cpu_boundary
        );
        _console.set_observation_sink(&_observation_sink);
        _observation_mask = mask;
        return true;
    }

    observation_drain_result_t snes_emulator_core_t::drain_observations(
        std::span<observation_event_t> destination
    ) noexcept
    {
        const std::span<const core::snes_observation_event_t> source{
            _observation_sink.events()
        };
        const size_t count{ std::min(source.size(), destination.size()) };
        for (size_t index{ 0 }; index < count; ++index)
        {
            const core::snes_observation_event_t& event{ source[index] };
            destination[index] = {
                .kind = observation_kind_t::execution_boundary,
                .domain = snes_debug::k_main_cpu_domain,
                .machine_clock = event.master_clock,
                .frame_index = event.frame_index,
                .execution_boundary = {
                    .boundary = execution_boundary(event.cpu_boundary.boundary),
                    .address_before = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = (static_cast<uint32_t>(event.cpu_boundary.state_before.pb) << 16u)
                            | event.cpu_boundary.state_before.pc
                    },
                    .address_after = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = (static_cast<uint32_t>(event.cpu_boundary.state_after.pb) << 16u)
                            | event.cpu_boundary.state_after.pc
                    }
                }
            };
        }
        _observation_sink.discard(count);
        return {
            .events_written = count,
            .events_dropped = _observation_sink.take_dropped()
        };
    }

    void snes_emulator_core_t::clear_observations() noexcept
    {
        _observation_sink.clear();
    }

    debug_session_control_t* snes_emulator_core_t::debug_session_control() noexcept
    {
        return this;
    }

    debug_session_state_t snes_emulator_core_t::debug_session_state() const noexcept
    {
        if (!_machine_running)
            return debug_session_state_t::not_running;
        return _debug_paused
            ? debug_session_state_t::paused
            : debug_session_state_t::running;
    }

    debug_session_transition_result_t snes_emulator_core_t::pause_debug_session() noexcept
    {
        if (!_machine_running)
            return {};

        _debug_paused = true;
        return {
            .status = debug_session_transition_status_t::complete,
            .state = debug_session_state_t::paused
        };
    }

    debug_session_transition_result_t snes_emulator_core_t::resume_debug_session() noexcept
    {
        if (!_machine_running)
            return {};

        _debug_paused = false;
        return {
            .status = debug_session_transition_status_t::complete,
            .state = debug_session_state_t::running
        };
    }
}
