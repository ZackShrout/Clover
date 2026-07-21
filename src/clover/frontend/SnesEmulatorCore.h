//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Console.h"
#include "clover/frontend/EmulatorCore.h"

namespace clover::frontend
{
    [[nodiscard]] uint16_t snes_joypad_state(const gamepad_state_t& state) noexcept;

    struct snes_emulator_core_t final : emulator_core_t
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

    private:
        core::console_t _console{};
    };
}
