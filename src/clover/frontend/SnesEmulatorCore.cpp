//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesEmulatorCore.h"

namespace clover::frontend
{
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
        return _console.load_cartridge(media);
    }

    void snes_emulator_core_t::power_on() noexcept
    {
        _console.power_on();
        _console.set_presentation_layer_mask(_visible_layer_mask);
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
}
