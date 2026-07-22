//
// Created by Zack Shrout on 7/22/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace clover::core
{
    // Command-level model of the NEC uPD77C25-based DSP-1B cartridge processor.
    class dsp1_t
    {
    public:
        void power_on() noexcept;
        [[nodiscard]] uint8_t read_data() noexcept;
        [[nodiscard]] uint8_t read_status() noexcept;
        void write_data(uint8_t value) noexcept;

    private:
        enum class phase_t : uint8_t { command, parameters, results };

        [[nodiscard]] static uint8_t parameter_count(uint8_t command) noexcept;
        void access_data(bool read, uint8_t& value) noexcept;
        [[nodiscard]] uint16_t current_result() const noexcept;
        [[nodiscard]] static uint16_t result_count(uint8_t command) noexcept;
        void execute() noexcept;
        void set_matrix(size_t index, int16_t scale, int16_t az, int16_t ay, int16_t ax) noexcept;
        void objective(size_t index) noexcept;
        void subjective(size_t index) noexcept;
        void scalar(size_t index) noexcept;
        void project() noexcept;
        void finish_result_word() noexcept;

        std::array<int16_t, 7> _parameters{};
        std::array<int16_t, 4> _results{};
        std::array<std::array<std::array<int16_t, 3>, 3>, 3> _matrices{};
        phase_t _phase{ phase_t::command };
        uint8_t _command{};
        uint16_t _word_index{};
        uint8_t _byte_index{};
        uint16_t _data_register{ 0x0080u };
        bool _status_high_byte{};
        bool _frozen{};
        bool _raster_output_written{};
    };
}
