//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace clover::core
{
    struct framebuffer_t
    {
        static constexpr int k_width{ 256 };
        static constexpr int k_height{ 240 };
        static constexpr int k_pixel_count{ k_width * k_height };
        static constexpr int k_max_width{ 512 };
        static constexpr int k_max_height{ 480 };
        static constexpr int k_max_pixel_count{ k_max_width * k_max_height };

        [[nodiscard]] const uint32_t* data() const noexcept;
        [[nodiscard]] uint32_t* data() noexcept;
        [[nodiscard]] uint32_t width() const noexcept;
        [[nodiscard]] uint32_t height() const noexcept;
        [[nodiscard]] uint32_t pitch_pixels() const noexcept;
        [[nodiscard]] size_t pixel_count() const noexcept;
        void set_geometry(uint32_t width, uint32_t height, uint32_t pitch_pixels = 0u) noexcept;
        void clear(uint32_t rgba8 = 0xff000000u) noexcept;
        [[nodiscard]] bool operator==(const framebuffer_t&) const noexcept = default;

    private:
        std::array<uint32_t, k_max_pixel_count> _pixels{};
        uint32_t _width{ k_width };
        uint32_t _height{ k_height };
        uint32_t _pitch_pixels{ k_width };
    };
}
