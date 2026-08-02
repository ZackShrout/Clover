//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/DebugTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace clover::frontend
{
    enum class system_id_t
    {
        snes
    };

    enum class pixel_format_t : uint8_t
    {
        argb8888
    };

    enum class gamepad_button_t : uint8_t
    {
        dpad_up,
        dpad_down,
        dpad_left,
        dpad_right,
        face_south,
        face_east,
        face_west,
        face_north,
        left_shoulder,
        right_shoulder,
        back,
        start
    };

    struct gamepad_state_t
    {
        uint32_t buttons{ 0 };

        [[nodiscard]] bool pressed(gamepad_button_t button) const noexcept
        {
            return (buttons & (1u << static_cast<uint8_t>(button))) != 0u;
        }

        void set(gamepad_button_t button, bool pressed) noexcept
        {
            const uint32_t mask{ 1u << static_cast<uint8_t>(button) };
            buttons = pressed ? buttons | mask : buttons & ~mask;
        }
    };

    struct display_info_t
    {
        uint32_t framebuffer_width{ 0 };
        uint32_t framebuffer_height{ 0 };
        float pixel_aspect_ratio{ 1.f };
        double nominal_refresh_hz{ 60.0 };
    };

    struct video_frame_view_t
    {
        const void* pixels{ nullptr };
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        size_t pitch_bytes{ 0 };
        pixel_format_t format{ pixel_format_t::argb8888 };
    };

    struct audio_frame_view_t
    {
        std::span<const int16_t> interleaved_samples{};
        uint32_t sample_rate_hz{ 0 };
        uint8_t channels{ 0 };
        bool discontinuity{ false };
    };

    using video_plane_id_t = uint32_t;

    struct video_plane_descriptor_t
    {
        video_plane_id_t id{ 0u };
        std::string_view label{};
        bool enabled{ true };
    };

    struct video_plane_frame_view_t
    {
        const void* pixels{ nullptr };
        uint32_t width{ 0u };
        uint32_t height{ 0u };
        size_t pitch_bytes{ 0u };
        pixel_format_t format{ pixel_format_t::argb8888 };
        uint64_t frame_index{ 0u };
    };

    struct video_plane_control_t
    {
    public:
        virtual ~video_plane_control_t() = default;
        [[nodiscard]] virtual std::span<const video_plane_descriptor_t> video_planes() const noexcept = 0;
        [[nodiscard]] virtual bool set_video_plane_enabled(video_plane_id_t id,
                                                           bool enabled) noexcept = 0;
        [[nodiscard]] virtual bool inspect_video_plane_frame(
            video_plane_id_t,
            video_plane_frame_view_t&
        ) const noexcept
        {
            return false;
        }
    };

    struct emulator_core_t
    {
    public:
        virtual ~emulator_core_t() = default;

        [[nodiscard]] virtual system_id_t system() const noexcept = 0;
        [[nodiscard]] virtual bool load_media(std::span<const std::byte> media) noexcept = 0;
        virtual void power_on() noexcept = 0;
        virtual void reset() noexcept = 0;
        virtual void set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept = 0;
        virtual void run_frame() noexcept = 0;
        // Debugger-driven execution can cross video-frame boundaries without
        // entering run_frame().  Frontends that expose such execution may use
        // this hook to publish the most recently completed display frame.
        virtual void refresh_video_frame() noexcept {}
        [[nodiscard]] virtual display_info_t display_info() const noexcept = 0;
        [[nodiscard]] virtual video_frame_view_t video_frame() const noexcept = 0;
        [[nodiscard]] virtual audio_frame_view_t audio_frame() const noexcept = 0;
        [[nodiscard]] virtual std::span<const std::byte> persistent_memory() const noexcept = 0;
        [[nodiscard]] virtual bool load_persistent_memory(std::span<const std::byte> data) noexcept = 0;
        [[nodiscard]] virtual bool persistent_memory_dirty() const noexcept = 0;
        virtual void mark_persistent_memory_clean() noexcept = 0;
        [[nodiscard]] virtual video_plane_control_t* video_plane_control() noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual debug_target_t* debug_target() noexcept
        {
            return nullptr;
        }
    };

    [[nodiscard]] std::unique_ptr<emulator_core_t> create_emulator_core(system_id_t system) noexcept;
}
