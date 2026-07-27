//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

namespace clover::platform
{
    struct workbench_app_shell_t
    {
        [[nodiscard]] int run(int argc, char** argv);
    };
}
