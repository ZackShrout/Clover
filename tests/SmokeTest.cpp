//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Console.h"

int main()
{
    clover::core::console_t console{};
    console.power_on();
    console.run_frame();

    const clover::core::framebuffer_t& framebuffer{ console.framebuffer() };
    return framebuffer.data()[0] == 0xff101820u ? 0 : 1;
}
