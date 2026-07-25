//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clover::core
{
    // Byte-oriented command model of the DSP-2 cartridge processor.
    class dsp2_t
    {
    public:
        void power_on() noexcept;
        [[nodiscard]] uint8_t read_data() noexcept;
        void write_data(uint8_t value) noexcept;
        [[nodiscard]] bool capture_causal_state(
            std::vector<std::byte>& state
        ) const noexcept;
        [[nodiscard]] bool restore_causal_state(
            std::span<const std::byte> state
        ) noexcept;

    private:
        enum class input_stage_t : uint8_t
        {
            command,
            fixed_parameters,
            bitmap_data
        };

        void begin_command(uint8_t command) noexcept;
        void finish_input_stage() noexcept;
        void execute() noexcept;
        void convert_bitmap() noexcept;
        void overlay_bitmap() noexcept;
        void reverse_bitmap() noexcept;
        void multiply() noexcept;
        void scale_bitmap() noexcept;

        std::array<uint8_t, 512> _input{};
        std::array<uint8_t, 255> _output{};
        input_stage_t _input_stage{ input_stage_t::command };
        uint8_t _command{};
        uint8_t _transparent_color{};
        uint8_t _input_length{};
        uint8_t _output_length{};
        size_t _expected_input{};
        size_t _input_index{};
        size_t _output_count{};
        size_t _output_index{};
    };
}
