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
#include <filesystem>
#include <optional>
#include <string>
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
        void present_test_pattern() noexcept;
        void queue_audio(const frontend::audio_frame_view_t& audio) noexcept;
        void reset_audio() noexcept;
        [[nodiscard]] const frontend::gamepad_state_t& gamepad_state() const noexcept;
        [[nodiscard]] bool consume_capture_marker() noexcept;
        [[nodiscard]] bool consume_reset_request() noexcept;
        [[nodiscard]] bool consume_import_rom_request() noexcept;
        [[nodiscard]] bool consume_open_library_request() noexcept;
        [[nodiscard]] bool consume_open_temporary_rom_request() noexcept;
        [[nodiscard]] bool consume_quit_request() noexcept;
        [[nodiscard]] bool consume_pause_request() noexcept;
        [[nodiscard]] bool consume_frame_advance_request() noexcept;
        [[nodiscard]] std::optional<size_t> consume_speed_selection() noexcept;
        [[nodiscard]] std::optional<size_t> consume_video_plane_selection() noexcept;
        void set_paused(bool paused) noexcept;
        void set_speed_selection(size_t index) noexcept;
        void set_video_planes(std::span<const frontend::video_plane_descriptor_t> planes);
        void show_rom_library(std::vector<std::string> display_names) noexcept;
        [[nodiscard]] std::optional<size_t> consume_library_selection() noexcept;
        [[nodiscard]] bool rom_library_visible() const noexcept;
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
        void render_menu() noexcept;
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
        std::vector<uint32_t> _test_pattern{};
        frontend::gamepad_state_t _input{};
        std::array<bool, SDL_SCANCODE_COUNT> _key_states{};
        bool _audio_started{ false };
        bool _capture_marker_requested{ false };
        bool _reset_requested{ false };
        bool _import_rom_requested{ false };
        bool _open_library_requested{ false };
        bool _open_temporary_rom_requested{ false };
        bool _quit_requested{ false };
        bool _pause_requested{ false };
        bool _frame_advance_requested{ false };
        bool _paused{ false };
        size_t _speed_selection{ 1u };
        std::optional<size_t> _requested_speed_selection{};
        std::vector<std::string> _video_plane_names{};
        std::vector<bool> _video_plane_enabled{};
        std::optional<size_t> _video_plane_selection{};
        bool _rom_library_visible{ false };
        std::vector<std::string> _rom_library_names{};
        size_t _rom_library_selected{ 0u };
        size_t _rom_library_scroll{ 0u };
        std::optional<size_t> _rom_library_selection{};
        uint8_t _open_menu{ 0 };
        uint8_t _hovered_menu_item{ 0 };
        uint8_t _pressed_menu_item{ 0 };
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
        [[nodiscard]] static bool load_persistent_memory(frontend::emulator_core_t& core,
                                                         const std::filesystem::path& save_path) noexcept;
        [[nodiscard]] static bool flush_persistent_memory(frontend::emulator_core_t& core,
                                                          const std::filesystem::path& save_path) noexcept;
    };
}
