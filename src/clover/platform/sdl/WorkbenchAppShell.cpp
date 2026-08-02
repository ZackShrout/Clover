//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/WorkbenchAppShell.h"

#include "clover/platform/sdl/SdlWorkbenchApplication.h"
#include "clover/utils/FileSystem.h"
#include "clover/workbench/WorkbenchTargetSupport.h"

#include <cstdio>
#include <memory>
#include <vector>

namespace clover::platform
{
    int workbench_app_shell_t::run(int argc, char** argv)
    {
        if (argc < 2)
        {
            std::fprintf(
                stderr,
                "Usage: clover_workbench <media> [system options]\n"
            );
            return 2;
        }
        const std::vector<std::byte> media{
            utils::read_binary_file(utils::path_from_utf8(argv[1]))
        };
        if (media.empty())
        {
            std::fprintf(stderr, "Unable to read the selected media.\n");
            return 1;
        }
        const std::unique_ptr<workbench::workbench_target_support_t> support{
            workbench::identify_workbench_target_support(media)
        };
        if (support == nullptr)
        {
            std::fprintf(
                stderr,
                "No Workbench support recognizes this media.\n"
            );
            return 1;
        }
        return sdl::run_registered_workbench_application(
            support->system(),
            argc,
            argv
        );
    }
}
