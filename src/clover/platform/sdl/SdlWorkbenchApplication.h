//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/EmulatorCore.h"

namespace clover::platform::sdl
{
    [[nodiscard]] int run_registered_workbench_application(
        frontend::system_id_t system,
        int argc,
        char** argv
    );
}
