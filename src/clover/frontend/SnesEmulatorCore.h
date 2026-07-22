//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Console.h"
#include "clover/frontend/EmulatorCore.h"

#include <array>

namespace clover::frontend
{
    [[nodiscard]] uint16_t snes_joypad_state(const gamepad_state_t& state) noexcept;

    struct snes_emulator_core_t final : emulator_core_t, video_plane_control_t
    {
    public:
        [[nodiscard]] system_id_t system() const noexcept override;
        [[nodiscard]] bool load_media(std::span<const std::byte> media) noexcept override;
        void power_on() noexcept override;
        void reset() noexcept override;
        void set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept override;
        void run_frame() noexcept override;
        [[nodiscard]] display_info_t display_info() const noexcept override;
        [[nodiscard]] video_frame_view_t video_frame() const noexcept override;
        [[nodiscard]] audio_frame_view_t audio_frame() const noexcept override;
        [[nodiscard]] std::span<const std::byte> persistent_memory() const noexcept override;
        [[nodiscard]] bool load_persistent_memory(std::span<const std::byte> data) noexcept override;
        [[nodiscard]] bool persistent_memory_dirty() const noexcept override;
        void mark_persistent_memory_clean() noexcept override;
        [[nodiscard]] video_plane_control_t* video_plane_control() noexcept override;
        [[nodiscard]] std::span<const video_plane_descriptor_t> video_planes() const noexcept override;
        [[nodiscard]] bool set_video_plane_enabled(video_plane_id_t id,
                                                   bool enabled) noexcept override;
        [[nodiscard]] bool set_hardware_configuration(
            core::snes_hardware_configuration_t configuration
        ) noexcept;
        [[nodiscard]] core::snes_hardware_identity_t hardware_identity() const noexcept;

    private:
        core::console_t _console{};
        uint8_t _visible_layer_mask{ core::ppu_presentation_options_t::k_all_layers_visible };
        std::array<video_plane_descriptor_t, 5> _video_planes{
            video_plane_descriptor_t{ 0u, "BG1", true },
            video_plane_descriptor_t{ 1u, "BG2", true },
            video_plane_descriptor_t{ 2u, "BG3", true },
            video_plane_descriptor_t{ 3u, "BG4", true },
            video_plane_descriptor_t{ 4u, "Objects", true }
        };
    };
}
