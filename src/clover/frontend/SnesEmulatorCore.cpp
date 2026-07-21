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
    }

    void snes_emulator_core_t::reset() noexcept
    {
        _console.reset();
    }

    void snes_emulator_core_t::set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept
    {
        if (port < 2u)
            _console.set_controller_state(static_cast<uint8_t>(port), snes_joypad_state(state));
    }

    void snes_emulator_core_t::run_frame() noexcept
    {
        _console.run_frame();
    }

    display_info_t snes_emulator_core_t::display_info() const noexcept
    {
        return {
            .framebuffer_width = core::framebuffer_t::k_width,
            .framebuffer_height = core::framebuffer_t::k_height,
            .pixel_aspect_ratio = 8.f / 7.f,
            .nominal_refresh_hz = 60.098812
        };
    }

    video_frame_view_t snes_emulator_core_t::video_frame() const noexcept
    {
        return {
            .pixels = _console.framebuffer().data(),
            .width = core::framebuffer_t::k_width,
            .height = core::framebuffer_t::k_height,
            .pitch_bytes = core::framebuffer_t::k_width * sizeof(uint32_t),
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
}
