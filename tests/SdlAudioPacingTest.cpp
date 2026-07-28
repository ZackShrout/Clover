//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/SdlAudioPacing.h"

int main()
{
    using namespace clover::platform::sdl_audio_pacing;

    constexpr uint32_t sample_rate{ 32'040u };
    constexpr uint8_t channels{ 2u };
    constexpr int target_bytes{ 9'612 };

    static_assert(initial_queue_bytes(sample_rate, channels) == target_bytes);
    static_assert(adjustment_ns(target_bytes, sample_rate, channels) == 0);
    static_assert(adjustment_ns(0, sample_rate, channels) == -maximum_adjustment_ns);
    static_assert(adjustment_ns(target_bytes * 2, sample_rate, channels)
                  == maximum_adjustment_ns);
    static_assert(adjustment_ns(-1, sample_rate, channels) == 0);
    static_assert(adjustment_ns(target_bytes, 0u, channels) == 0);
    static_assert(adjustment_ns(target_bytes, sample_rate, 0u) == 0);
    static_assert(queue_is_empty(0));
    static_assert(!queue_is_empty(-1));
    static_assert(!queue_is_empty(target_bytes));

    // A modest queue error produces a proportional correction instead of
    // abruptly changing the emulation cadence.
    constexpr int ten_milliseconds_of_audio{ 1'281 };
    const int64_t low_adjustment{
        adjustment_ns(target_bytes - ten_milliseconds_of_audio,
                      sample_rate,
                      channels)
    };
    const int64_t high_adjustment{
        adjustment_ns(target_bytes + ten_milliseconds_of_audio,
                      sample_rate,
                      channels)
    };
    if (low_adjustment >= 0 || high_adjustment <= 0)
        return 1;
    if (low_adjustment < -170'000 || low_adjustment > -140'000)
        return 2;
    if (high_adjustment < 140'000 || high_adjustment > 170'000)
        return 3;
    return 0;
}
