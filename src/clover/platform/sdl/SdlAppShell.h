//
// Created by Zack Shrout on 7/9/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/EmulatorCore.h"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace clover::platform
{
    // SDL owns presentation and physical-device policy only. It communicates
    // with every emulator through frontend's system-neutral frame and input
    // contracts.
    struct sdl_presentation_t
    {
    public:
        [[nodiscard]] bool initialize(SDL_Window* window,
                                      const frontend::display_info_t& display,
                                      const frontend::audio_frame_view_t& audio_format) noexcept;
        void shutdown() noexcept;
        void handle_event(const SDL_Event& event) noexcept;
        void present(const frontend::video_frame_view_t& frame) noexcept;
        void queue_audio(const frontend::audio_frame_view_t& audio) noexcept;
        [[nodiscard]] const frontend::gamepad_state_t& gamepad_state() const noexcept;
        [[nodiscard]] bool consume_capture_marker() noexcept;
        [[nodiscard]] int audio_queued_bytes_before_put() const noexcept;
        [[nodiscard]] int audio_queued_bytes_after_put() const noexcept;
        [[nodiscard]] bool audio_started() const noexcept;
        [[nodiscard]] uint64_t audio_empty_queue_observations() const noexcept;

    private:
        [[nodiscard]] bool key_pressed(SDL_Scancode scancode) const noexcept;
        [[nodiscard]] bool physical_control_pressed(frontend::gamepad_button_t button) const noexcept;
        [[nodiscard]] bool gamepad_button_pressed(SDL_GamepadButton button) const noexcept;
        [[nodiscard]] bool gamepad_axis_pressed(SDL_GamepadAxis axis, bool positive) const noexcept;
        [[nodiscard]] SDL_FRect presentation_rect() const noexcept;
        [[nodiscard]] bool open_gamepad(SDL_JoystickID joystick_id) noexcept;
        void open_first_available_gamepad() noexcept;
        void close_gamepad() noexcept;
        void refresh_gamepad_state() noexcept;

        SDL_Window* _window{ nullptr };
        SDL_Renderer* _renderer{ nullptr };
        SDL_Texture* _texture{ nullptr };
        SDL_AudioStream* _audio_stream{ nullptr };
        SDL_Gamepad* _gamepad{ nullptr };
        SDL_JoystickID _gamepad_id{ 0 };
        frontend::display_info_t _display{};
        frontend::gamepad_state_t _input{};
        std::array<bool, SDL_SCANCODE_COUNT> _key_states{};
        bool _audio_started{ false };
        bool _capture_marker_requested{ false };
        int _audio_queued_bytes_before_put{ -1 };
        int _audio_queued_bytes_after_put{ -1 };
        uint64_t _audio_empty_queue_observations{ 0 };
    };

    struct sdl_app_shell_t
    {
    public:
        [[nodiscard]] int run(int argc, char** argv) noexcept;

    private:
        [[nodiscard]] static std::vector<std::byte> load_file_bytes(const char* path) noexcept;
    };
}
