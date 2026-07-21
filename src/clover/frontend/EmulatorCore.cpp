//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/EmulatorCore.h"

#include "clover/frontend/SnesEmulatorCore.h"

namespace clover::frontend
{
    std::unique_ptr<emulator_core_t> create_emulator_core(system_id_t system) noexcept
    {
        switch (system)
        {
        case system_id_t::snes:
            return std::make_unique<snes_emulator_core_t>();
        }

        return nullptr;
    }
}
