//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/SdlWorkbenchApplication.h"

#include "clover/platform/sdl/snes/SnesWorkbenchApplication.h"

namespace clover::platform::sdl
{
    int run_registered_workbench_application(
        frontend::system_id_t system,
        int argc,
        char** argv
    )
    {
        switch (system)
        {
        case frontend::system_id_t::snes:
            return snes::run_snes_workbench_application(argc, argv);
        }
        return 1;
    }
}
