//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesEmulatorCore.h"

namespace clover::frontend
{
    system_id_t snes_emulator_core_t::system() const noexcept
    {
        return system_id_t::snes;
    }

    void snes_emulator_core_t::power_on() noexcept
    {
        _console.power_on();
    }

    void snes_emulator_core_t::run_frame() noexcept
    {
        _console.run_frame();
    }

    const core::framebuffer_t& snes_emulator_core_t::framebuffer() const noexcept
    {
        return _console.framebuffer();
    }
}
