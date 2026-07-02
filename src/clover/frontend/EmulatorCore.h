//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/FrameBuffer.h"

#include <memory>

namespace clover::frontend
{
    enum class system_id_t
    {
        snes
    };

    struct emulator_core_t
    {
    public:
        virtual ~emulator_core_t() = default;

        [[nodiscard]] virtual system_id_t system() const noexcept = 0;
        virtual void power_on() noexcept = 0;
        virtual void run_frame() noexcept = 0;
        [[nodiscard]] virtual const core::framebuffer_t& framebuffer() const noexcept = 0;
    };

    // This seam intentionally looks a bit more abstract than Clover currently needs.
    // The app still hard-selects SNES today, but the frontend can now depend on a
    // stable emulator-core interface instead of the SNES console directly.
    [[nodiscard]] std::unique_ptr<emulator_core_t> create_default_emulator_core() noexcept;
}
