//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Console.h"
#include "clover/frontend/EmulatorCore.h"

namespace clover::frontend
{
    struct snes_emulator_core_t final : emulator_core_t
    {
    public:
        [[nodiscard]] system_id_t system() const noexcept override;
        void power_on() noexcept override;
        void run_frame() noexcept override;
        [[nodiscard]] const core::framebuffer_t& framebuffer() const noexcept override;

    private:
        core::console_t _console{};
    };
}
