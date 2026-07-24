//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>
#include <memory>

namespace clover::core
{
    // Cartridge-facing byte-stream interface for the DSP-4 state machine.
    class dsp4_t
    {
    public:
        dsp4_t();
        ~dsp4_t();
        dsp4_t(dsp4_t&&) noexcept;
        dsp4_t& operator=(dsp4_t&&) noexcept;
        dsp4_t(const dsp4_t&) = delete;
        dsp4_t& operator=(const dsp4_t&) = delete;

        void power_on() noexcept;
        [[nodiscard]] uint8_t read_data() noexcept;
        [[nodiscard]] static uint8_t read_status() noexcept;
        void write_data(uint8_t value) noexcept;

    private:
        struct state_t;
        std::unique_ptr<state_t> _state;
    };
}
