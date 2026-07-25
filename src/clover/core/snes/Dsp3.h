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
    // Command-level model of the DSP-3 cartridge processor.
    class dsp3_t
    {
    public:
        void power_on() noexcept;
        [[nodiscard]] uint8_t read_data() noexcept;
        [[nodiscard]] uint8_t read_status() const noexcept;
        void write_data(uint8_t value) noexcept;
        [[nodiscard]] bool capture_causal_state(
            std::vector<std::byte>& state
        ) const noexcept;
        [[nodiscard]] bool restore_causal_state(
            std::span<const std::byte> state
        ) noexcept;

    private:
        enum class operation_t : uint8_t
        {
            command,
            coordinate,
            cell_offset,
            set_dimensions,
            adjacent_direction,
            adjacent_coordinate,
            adjacent_offset,
            absorb_until_ffff,
            absorb_zero,
            convert_count,
            convert_stream,
            absorb_one,
            zero_result_one,
            zero_result_two,
            reset_pending,
            memory_dump,
            rom_version,
            decode_count,
            decode_output_count,
            decode_symbols,
            decode_tree,
            decode_data,
            set_start,
            path_radius,
            path_terrain,
            path_cost,
            path_advance,
            path_prepare_results,
            path_result_radius,
            path_results,
            unsupported
        };

        void access_data(bool read, uint8_t& value) noexcept;
        void execute_access() noexcept;
        void finish_command() noexcept;
        void begin_command(uint8_t command) noexcept;
        void compute_cell_offset() noexcept;
        void begin_adjacent_cell() noexcept;
        void convert_bitmap_access() noexcept;
        void decode_symbols_access() noexcept;
        void decode_tree_access() noexcept;
        void decode_data_access() noexcept;
        void begin_path_search() noexcept;
        void finish_path_inputs() noexcept;
        void begin_path_results() noexcept;
        void present_path_cell() noexcept;
        void build_path_cells(uint8_t minimum_radius, uint8_t maximum_radius) noexcept;
        void move_cell(int16_t& column, int16_t& row, size_t direction, bool wrap) const noexcept;
        [[nodiscard]] uint16_t cell_offset(int16_t column, int16_t row) const noexcept;
        [[nodiscard]] bool read_bits(uint8_t count) noexcept;
        [[nodiscard]] static int16_t neighbor_delta(size_t index) noexcept;

        uint16_t _data_register{ 0x0080u };
        uint8_t _status{ 0x84u };
        operation_t _operation{ operation_t::command };
        uint16_t _index{};

        int16_t _columns{};
        int16_t _rows{};
        int16_t _add_column{};
        int16_t _add_row{};
        uint16_t _x{};
        uint16_t _y{};

        std::array<uint8_t, 8> _bitmap{};
        std::array<uint8_t, 8> _bitplanes{};
        uint16_t _bitmap_index{};
        uint16_t _bitplane_index{};
        uint16_t _conversion_count{};

        uint16_t _codeword_count{};
        uint16_t _output_word_count{};
        uint16_t _symbol{};
        uint16_t _bit_count{};
        uint16_t _bits_left{};
        uint16_t _requested_bits{};
        uint16_t _requested_data{};
        uint16_t _bit_command{ 0xffffu };
        uint8_t _base_length{};
        uint16_t _base_codes{};
        uint16_t _base_code{ 0xffffu };
        std::array<uint16_t, 512> _codes{};
        std::array<uint8_t, 8> _code_lengths{};
        std::array<uint16_t, 8> _code_offsets{};
        uint16_t _lz_stage{};
        uint8_t _lz_length{};

        uint8_t _start_column{};
        uint8_t _start_row{};
        uint8_t _maximum_search_radius{};
        uint8_t _maximum_path_radius{};
        std::vector<uint8_t> _terrain = std::vector<uint8_t>(0x2000);
        std::vector<uint8_t> _cost = std::vector<uint8_t>(0x2000);
        std::vector<uint8_t> _weight = std::vector<uint8_t>(0x2000);
        std::vector<uint16_t> _path_coordinates = std::vector<uint16_t>(0x2000);
        std::vector<uint16_t> _path_cells = std::vector<uint16_t>(0x2000);
        size_t _path_count{};
        size_t _path_index{};
    };
}
