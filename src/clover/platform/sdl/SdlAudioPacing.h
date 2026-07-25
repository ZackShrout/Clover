//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <algorithm>
#include <cstdint>

namespace clover::platform::sdl_audio_pacing
{
    constexpr int64_t target_queue_ns{ 75'000'000 };
    constexpr int64_t maximum_adjustment_ns{ 750'000 };
    constexpr int64_t proportional_divisor{ 64 };

    [[nodiscard]] constexpr int64_t adjustment_ns(int queued_bytes,
                                                   uint32_t sample_rate_hz,
                                                   uint8_t channels) noexcept
    {
        if (queued_bytes < 0 || sample_rate_hz == 0u || channels == 0u)
            return 0;

        const int64_t bytes_per_second{
            static_cast<int64_t>(sample_rate_hz)
                * static_cast<int64_t>(channels)
                * static_cast<int64_t>(sizeof(int16_t))
        };
        const int64_t queued_ns{
            static_cast<int64_t>(queued_bytes) * 1'000'000'000ll / bytes_per_second
        };
        return std::clamp((queued_ns - target_queue_ns) / proportional_divisor,
                          -maximum_adjustment_ns,
                          maximum_adjustment_ns);
    }

    [[nodiscard]] constexpr int initial_queue_bytes(uint32_t sample_rate_hz,
                                                    uint8_t channels) noexcept
    {
        if (sample_rate_hz == 0u || channels == 0u)
            return 0;
        return static_cast<int>(
            static_cast<int64_t>(sample_rate_hz)
                * static_cast<int64_t>(channels)
                * static_cast<int64_t>(sizeof(int16_t))
                * target_queue_ns
                / 1'000'000'000ll
        );
    }
}
