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

    uint32_t framebuffer_t::width() const noexcept { return _width; }
    uint32_t framebuffer_t::height() const noexcept { return _height; }
    uint32_t framebuffer_t::pitch_pixels() const noexcept { return _pitch_pixels; }
    size_t framebuffer_t::pixel_count() const noexcept
    {
        return static_cast<size_t>(_pitch_pixels) * _height;
    }

    void framebuffer_t::set_geometry(uint32_t width, uint32_t height, uint32_t pitch_pixels) noexcept
    {
        _width = std::clamp(width, 1u, static_cast<uint32_t>(k_max_width));
        _height = std::clamp(height, 1u, static_cast<uint32_t>(k_max_height));
        _pitch_pixels = pitch_pixels == 0u ? _width : std::clamp(
            pitch_pixels, _width, static_cast<uint32_t>(k_max_width)
        );
    }

    void framebuffer_t::clear(uint32_t rgba8) noexcept
    {
        std::fill(_pixels.begin(), _pixels.end(), rgba8);
    }
}
