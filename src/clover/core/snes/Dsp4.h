//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>

namespace clover::core
{
    // Cartridge-facing byte-stream interface for the DSP-4 state machine.
    class dsp4_t
    {
    public:
        void power_on() noexcept;
        [[nodiscard]] uint8_t read_data() noexcept;
        [[nodiscard]] static uint8_t read_status() noexcept;
        void write_data(uint8_t value) noexcept;
    };
}
