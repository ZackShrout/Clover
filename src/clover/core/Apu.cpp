//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Apu.h"

namespace clover::core
{
    void apu_t::power_on() noexcept
    {
        reset();
    }

    void apu_t::reset() noexcept
    {
        _master_clock = 0;
    }

    void apu_t::step(master_clock_delta_t master_clocks) noexcept
    {
        _master_clock += master_clocks;
    }

    master_clock_count_t apu_t::master_clock() const noexcept
    {
        return _master_clock;
    }
}
