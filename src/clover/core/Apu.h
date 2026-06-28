//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/Timing.h"

namespace clover::core
{
    struct apu_t
    {
    public:
        void power_on() noexcept;
        void reset() noexcept;
        void step(master_clock_delta_t master_clocks) noexcept;
        [[nodiscard]] master_clock_count_t master_clock() const noexcept;

    private:
        master_clock_count_t _master_clock{ 0 };
    };
}
