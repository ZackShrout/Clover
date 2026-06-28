//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/App.h"

#include "clover/core/Console.h"

namespace clover
{
    int app_t::run() const noexcept
    {
        core::console_t console{};
        console.power_on();
        console.run_frame();
        return 0;
    }
}
