//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Console.h"

namespace clover::core
{
    void console_t::power_on() noexcept
    {
        _framebuffer.clear();
        _powered_on = true;
    }

    void console_t::reset() noexcept
    {
        _framebuffer.clear();
    }

    void console_t::run_frame() noexcept
    {
        if (!_powered_on)
            return;

        // Placeholder output so the scaffold has a visible runtime contract.
        _framebuffer.clear(0xff101820u);
    }

    const framebuffer_t& console_t::framebuffer() const noexcept
    {
        return _framebuffer;
    }
}
