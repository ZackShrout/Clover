#pragma once

#include "clover/core/FrameBuffer.h"

namespace clover::core
{
    struct console_t
    {
    public:
        void power_on() noexcept;
        void reset() noexcept;
        void run_frame() noexcept;
        [[nodiscard]] const framebuffer_t& framebuffer() const noexcept;

    private:
        framebuffer_t _framebuffer{};
        bool _powered_on{ false };
    };
}
