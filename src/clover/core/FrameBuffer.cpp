//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/FrameBuffer.h"

#include <algorithm>

namespace clover::core
{
    const uint32_t* framebuffer_t::data() const noexcept
    {
        return _pixels.data();
    }

    uint32_t* framebuffer_t::data() noexcept
    {
        return _pixels.data();
    }

    void framebuffer_t::clear(uint32_t rgba8) noexcept
    {
        std::fill(_pixels.begin(), _pixels.end(), rgba8);
    }
}
