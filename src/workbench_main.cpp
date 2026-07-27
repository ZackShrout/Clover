//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/WorkbenchAppShell.h"

#include <SDL3/SDL_main.h>

int main(int argc, char** argv)
{
    clover::platform::workbench_app_shell_t app{};
    return app.run(argc, argv);
}
