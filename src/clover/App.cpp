//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/App.h"

#include "clover/frontend/EmulatorCore.h"

namespace clover
{
    int app_t::run() const noexcept
    {
        auto emulator_core{ frontend::create_emulator_core(frontend::system_id_t::snes) };
        if (!emulator_core)
            return 1;
        emulator_core->power_on();
        emulator_core->run_frame();
        return 0;
    }
}
