#pragma once

#include <array>
#include <cstdint>

namespace clover::core
{
    struct framebuffer_t
    {
        static constexpr int k_width{ 256 };
        static constexpr int k_height{ 240 };
        static constexpr int k_pixel_count{ k_width * k_height };

        [[nodiscard]] const uint32_t* data() const noexcept;
        [[nodiscard]] uint32_t* data() noexcept;
        void clear(uint32_t rgba8 = 0xff000000u) noexcept;

    private:
        std::array<uint32_t, k_pixel_count> _pixels{};
    };
}
