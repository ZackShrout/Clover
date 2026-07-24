//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dsp3.h"

#include <algorithm>
#include <string_view>

namespace
{
    // DSP-3's fixed 1024-word data ROM, stored as little-endian bytes. The
    // decoded values originate from the public DSP-3 research by Overload and
    // The Dumper, whose distribution terms explicitly permit reuse with credit.
    constexpr std::string_view dsp3_data_rom_base64{
        "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zACwAIgAsABEALAAAACwAP4AB/8CAP8A+AAHAP4A7gD/BwAC7wAA+AAH7gD///////8AAAAAAQABAAEAAQAAAAAA//////////8AAAAAAQABAAEAAQAAAAAA/////wAA//8BAAAAAQABAAAAAAD//////////wAA//8BAAAAAQABAAAAAAD///////8AAAAAAABEAIgAzAAQAVQB//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////8="
    };

    [[nodiscard]] constexpr uint8_t decode_base64(char value) noexcept
    {
        if (value >= 'A' && value <= 'Z')
            return static_cast<uint8_t>(value - 'A');
        if (value >= 'a' && value <= 'z')
            return static_cast<uint8_t>(value - 'a' + 26);
        if (value >= '0' && value <= '9')
            return static_cast<uint8_t>(value - '0' + 52);
        return value == '+' ? 62u : 63u;
    }

    [[nodiscard]] constexpr uint8_t dsp3_data_byte(size_t index) noexcept
    {
        const size_t group{ (index / 3u) * 4u };
        const size_t lane{ index % 3u };
        const uint32_t packed{
            static_cast<uint32_t>(decode_base64(dsp3_data_rom_base64[group])) << 18u
            | static_cast<uint32_t>(decode_base64(dsp3_data_rom_base64[group + 1u])) << 12u
            | static_cast<uint32_t>(decode_base64(dsp3_data_rom_base64[group + 2u])) << 6u
            | decode_base64(dsp3_data_rom_base64[group + 3u])
        };
        return static_cast<uint8_t>(packed >> (16u - lane * 8u));
    }

    [[nodiscard]] constexpr uint16_t dsp3_data_word(size_t index) noexcept
    {
        return static_cast<uint16_t>(
            dsp3_data_byte(index * 2u)
            | (static_cast<uint16_t>(dsp3_data_byte(index * 2u + 1u)) << 8u)
        );
    }

    static_assert(dsp3_data_word(0) == 0x8000u);
    static_assert(dsp3_data_word(1023) == 0xffffu);
}

namespace clover::core
{
    void dsp3_t::power_on() noexcept
    {
        _data_register = 0x0080u;
        _status = 0x84u;
        _operation = operation_t::command;
        _index = 0;
        _columns = 0;
        _rows = 0;
        _add_column = 0;
        _add_row = 0;
        _x = 0;
        _y = 0;
        _bitmap.fill(0);
        _bitplanes.fill(0);
        _bitmap_index = 0;
        _bitplane_index = 0;
        _conversion_count = 0;
        _codeword_count = 0;
        _output_word_count = 0;
        _symbol = 0;
        _bit_count = 0;
        _bits_left = 0;
        _requested_bits = 0;
        _requested_data = 0;
        _bit_command = 0xffffu;
        _base_length = 0;
        _base_codes = 0;
        _base_code = 0xffffu;
        _codes.fill(0);
        _code_lengths.fill(0);
        _code_offsets.fill(0);
        _lz_stage = 0;
        _lz_length = 0;
        _start_column = 0;
        _start_row = 0;
        _maximum_search_radius = 0;
        _maximum_path_radius = 0;
        std::fill(_terrain.begin(), _terrain.end(), 0);
        std::fill(_cost.begin(), _cost.end(), 0xffu);
        std::fill(_weight.begin(), _weight.end(), 0xffu);
        std::fill(_path_coordinates.begin(), _path_coordinates.end(), 0);
        std::fill(_path_cells.begin(), _path_cells.end(), 0);
        _path_count = 0;
        _path_index = 0;
    }

    uint8_t dsp3_t::read_data() noexcept
    {
        uint8_t value{};
        access_data(true, value);
        return value;
    }

    uint8_t dsp3_t::read_status() const noexcept
    {
        return _status;
    }

    void dsp3_t::write_data(uint8_t value) noexcept
    {
        access_data(false, value);
    }

    void dsp3_t::access_data(bool read, uint8_t& value) noexcept
    {
        if ((_status & 0x04u) != 0u)
        {
            if (read)
                value = static_cast<uint8_t>(_data_register);
            else
                _data_register = static_cast<uint16_t>(
                    (_data_register & 0xff00u) | value
                );
            execute_access();
            return;
        }

        _status ^= 0x10u;
        if ((_status & 0x10u) != 0u)
        {
            if (read)
                value = static_cast<uint8_t>(_data_register);
            else
                _data_register = static_cast<uint16_t>(
                    (_data_register & 0xff00u) | value
                );
            return;
        }

        if (read)
            value = static_cast<uint8_t>(_data_register >> 8u);
        else
            _data_register = static_cast<uint16_t>(
                (_data_register & 0x00ffu) | (static_cast<uint16_t>(value) << 8u)
            );
        execute_access();
    }

    void dsp3_t::finish_command() noexcept
    {
        _data_register = 0x0080u;
        _status = 0x84u;
        _operation = operation_t::command;
        _index = 0;
    }

    void dsp3_t::begin_command(uint8_t command) noexcept
    {
        _index = 0;
        _status = 0x80u;
        switch (command)
        {
        case 0x02u: _operation = operation_t::coordinate; break;
        case 0x03u: _operation = operation_t::cell_offset; break;
        case 0x06u: _operation = operation_t::set_dimensions; break;
        case 0x07u:
            _operation = operation_t::adjacent_direction;
            _status = 0x80u;
            return;
        case 0x0cu: _operation = operation_t::absorb_zero; break;
        case 0x0fu: _operation = operation_t::absorb_zero; break;
        case 0x10u: _operation = operation_t::absorb_until_ffff; break;
        case 0x18u: _operation = operation_t::convert_count; break;
        case 0x1cu: _operation = operation_t::absorb_one; break;
        case 0x1eu: _operation = operation_t::path_radius; break;
        case 0x1fu:
            _index = 0;
            _operation = operation_t::memory_dump;
            _data_register = dsp3_data_word(0);
            break;
        case 0x2fu: _operation = operation_t::rom_version; break;
        case 0x38u: _operation = operation_t::decode_count; break;
        case 0x3eu: _operation = operation_t::set_start; break;
        default:
            finish_command();
            break;
        }
    }

    void dsp3_t::compute_cell_offset() noexcept
    {
        const uint8_t column{ static_cast<uint8_t>(_data_register) };
        const uint8_t row{ static_cast<uint8_t>(_data_register >> 8u) };
        _data_register = static_cast<uint16_t>(_columns * row + column);
    }

    int16_t dsp3_t::neighbor_delta(size_t index) noexcept
    {
        static constexpr std::array<int16_t, 12> deltas{
            -1, 0, -1, 1, 0, 1, 1, 0, 0, -1, -1, -1
        };
        return deltas[index % deltas.size()];
    }

    void dsp3_t::begin_adjacent_cell() noexcept
    {
        const size_t direction{ static_cast<size_t>(_data_register & 0x07u) };
        _add_row = neighbor_delta(direction * 2u);
        _add_column = neighbor_delta(direction * 2u + 1u);
        _operation = operation_t::adjacent_coordinate;
        _status = 0x80u;
    }

    void dsp3_t::convert_bitmap_access() noexcept
    {
        if (_bitmap_index < 8u)
        {
            _bitmap[_bitmap_index++] = static_cast<uint8_t>(_data_register);
            _bitmap[_bitmap_index++] = static_cast<uint8_t>(_data_register >> 8u);
            if (_bitmap_index == 8u)
            {
                _bitplanes.fill(0);
                for (size_t source{}; source < 8u; ++source)
                {
                    for (size_t plane{}; plane < 8u; ++plane)
                    {
                        _bitplanes[plane] = static_cast<uint8_t>(
                            (_bitplanes[plane] << 1u)
                            | ((_bitmap[source] >> plane) & 1u)
                        );
                    }
                }
                _bitplane_index = 0;
                --_conversion_count;
            }
        }

        if (_bitmap_index != 8u)
            return;
        if (_bitplane_index == 8u)
        {
            if (_conversion_count == 0u)
            {
                finish_command();
                return;
            }
            _bitmap_index = 0;
            return;
        }
        _data_register = static_cast<uint16_t>(
            _bitplanes[_bitplane_index]
            | (static_cast<uint16_t>(_bitplanes[_bitplane_index + 1u]) << 8u)
        );
        _bitplane_index += 2u;
    }

    bool dsp3_t::read_bits(uint8_t count) noexcept
    {
        if (_bits_left == 0u)
        {
            _bits_left = count;
            _requested_bits = 0;
        }
        while (_bits_left != 0u)
        {
            if (_bit_count == 0u)
            {
                _status = 0xc0u;
                return false;
            }
            _requested_bits = static_cast<uint16_t>(_requested_bits << 1u);
            if ((_requested_data & 0x8000u) != 0u)
                ++_requested_bits;
            _requested_data = static_cast<uint16_t>(_requested_data << 1u);
            --_bit_count;
            --_bits_left;
        }
        return true;
    }

    void dsp3_t::decode_symbols_access() noexcept
    {
        _requested_data = _data_register;
        _bit_count = static_cast<uint16_t>(_bit_count + 16u);
        while (_codeword_count != 0u)
        {
            if (_bit_command == 0xffffu)
            {
                if (!read_bits(2))
                    return;
                _bit_command = _requested_bits;
            }

            switch (_bit_command)
            {
            case 0:
                if (!read_bits(9))
                    return;
                _symbol = _requested_bits;
                break;
            case 1:
                ++_symbol;
                break;
            case 2:
                if (!read_bits(1))
                    return;
                _symbol = static_cast<uint16_t>(_symbol + 2u + _requested_bits);
                break;
            case 3:
                if (!read_bits(4))
                    return;
                _symbol = static_cast<uint16_t>(_symbol + 4u + _requested_bits);
                break;
            default: break;
            }
            _bit_command = 0xffffu;
            _codes[_index++] = _symbol;
            --_codeword_count;
        }

        _index = 0;
        _symbol = 0;
        _base_codes = 0;
        _operation = operation_t::decode_tree;
        if (_bit_count != 0u)
            decode_tree_access();
    }

    void dsp3_t::decode_tree_access() noexcept
    {
        if (_bit_count == 0u)
        {
            _requested_data = _data_register;
            _bit_count = static_cast<uint16_t>(_bit_count + 16u);
        }
        if (_base_codes == 0u)
        {
            if (!read_bits(1))
                return;
            _base_length = _requested_bits != 0u ? 3u : 2u;
            _base_codes = static_cast<uint16_t>(1u << _base_length);
        }
        while (_base_codes != 0u)
        {
            if (!read_bits(3))
                return;
            ++_requested_bits;
            _code_lengths[_index] = static_cast<uint8_t>(_requested_bits);
            _code_offsets[_index] = _symbol;
            ++_index;
            _symbol = static_cast<uint16_t>(_symbol + (1u << _requested_bits));
            --_base_codes;
        }
        _base_code = 0xffffu;
        _lz_stage = 0;
        _operation = operation_t::decode_data;
        if (_bit_count != 0u)
            decode_data_access();
    }

    void dsp3_t::decode_data_access() noexcept
    {
        if (_bit_count == 0u)
        {
            if ((_status & 0x40u) != 0u)
            {
                _requested_data = _data_register;
                _bit_count = static_cast<uint16_t>(_bit_count + 16u);
            }
            else
            {
                _status = 0xc0u;
                return;
            }
        }

        if (_lz_stage == 1u)
        {
            if (!read_bits(1))
                return;
            _lz_length = _requested_bits != 0u ? 12u : 8u;
            ++_lz_stage;
        }
        if (_lz_stage == 2u)
        {
            if (!read_bits(_lz_length))
                return;
            _lz_stage = 0;
            if (--_output_word_count == 0u)
                _operation = operation_t::reset_pending;
            _status = 0x80u;
            _data_register = _requested_bits;
            return;
        }

        if (_base_code == 0xffffu)
        {
            if (!read_bits(_base_length))
                return;
            _base_code = _requested_bits;
        }
        if (!read_bits(_code_lengths[_base_code]))
            return;
        _symbol = _codes[_code_offsets[_base_code] + _requested_bits];
        _base_code = 0xffffu;
        if ((_symbol & 0xff00u) != 0u)
        {
            _symbol = static_cast<uint16_t>(_symbol + 0x7f02u);
            ++_lz_stage;
        }
        else if (--_output_word_count == 0u)
        {
            _operation = operation_t::reset_pending;
        }
        _status = 0x80u;
        _data_register = _symbol;
    }

    uint16_t dsp3_t::cell_offset(int16_t column, int16_t row) const noexcept
    {
        return static_cast<uint16_t>(_columns * row + column);
    }

    void dsp3_t::move_cell(
        int16_t& column,
        int16_t& row,
        size_t direction,
        bool wrap
    ) const noexcept
    {
        direction %= 6u;
        const int16_t column_delta{ neighbor_delta(direction * 2u + 1u) };
        int16_t row_delta{ neighbor_delta(direction * 2u) };
        if ((column & 1) != 0)
            row_delta = static_cast<int16_t>(row_delta + (column_delta & 1));
        column = static_cast<int16_t>(column + column_delta);
        row = static_cast<int16_t>(row + row_delta);
        if (!wrap)
            return;
        if (column < 0) column = static_cast<int16_t>(column + _columns);
        else if (column >= _columns) column = static_cast<int16_t>(column - _columns);
        if (row < 0) row = static_cast<int16_t>(row + _rows);
        else if (row >= _rows) row = static_cast<int16_t>(row - _rows);
    }

    void dsp3_t::build_path_cells(uint8_t minimum_radius, uint8_t maximum_radius) noexcept
    {
        _path_count = 0;
        for (uint16_t radius{ minimum_radius }; radius <= maximum_radius; ++radius)
        {
            for (size_t side{}; side < 6u; ++side)
            {
                int16_t column{ _start_column };
                int16_t row{ _start_row };
                for (uint16_t step{}; step < radius; ++step)
                    move_cell(column, row, side, true);
                for (uint16_t step{}; step < radius; ++step)
                {
                    if (_path_count < _path_cells.size())
                    {
                        _path_coordinates[_path_count] = static_cast<uint16_t>(
                            static_cast<uint8_t>(column)
                            | (static_cast<uint16_t>(static_cast<uint8_t>(row)) << 8u)
                        );
                        _path_cells[_path_count] = cell_offset(column, row);
                        ++_path_count;
                    }
                    move_cell(column, row, side + 2u, true);
                }
            }
        }
        _path_index = 0;
    }

    void dsp3_t::present_path_cell() noexcept
    {
        if (_path_index >= _path_count)
        {
            _data_register = 0xffffu;
            _status = 0x80u;
            _operation = operation_t::path_prepare_results;
            return;
        }
        _data_register = _path_coordinates[_path_index];
        _status = 0x80u;
        _operation = operation_t::path_terrain;
    }

    void dsp3_t::begin_path_search() noexcept
    {
        uint8_t minimum{ static_cast<uint8_t>(_data_register) };
        const uint8_t maximum{ static_cast<uint8_t>(_data_register >> 8u) };
        if (minimum == 0u)
            minimum = 1u;
        if (_maximum_search_radius >= minimum)
            minimum = static_cast<uint8_t>(_maximum_search_radius + 1u);
        _maximum_search_radius = std::max(_maximum_search_radius, maximum);
        build_path_cells(minimum, maximum);
        present_path_cell();
    }

    void dsp3_t::finish_path_inputs() noexcept
    {
        std::fill(_weight.begin(), _weight.end(), 0xffu);
        const uint16_t start{ cell_offset(_start_column, _start_row) };
        _terrain[start] = 0;
        _cost[start] = 0xffu;
        _weight[start] = 0;

        for (size_t index{}; index < _path_count; ++index)
        {
            const uint16_t cell{ _path_cells[index] };
            if (_cost[cell] >= 0x80u || _terrain[cell] >= 0x40u)
                continue;
            uint8_t best{ 0xffu };
            int16_t column{ static_cast<uint8_t>(_path_coordinates[index]) };
            int16_t row{ static_cast<uint8_t>(_path_coordinates[index] >> 8u) };
            for (size_t direction{ 1u }; direction <= 6u; ++direction)
            {
                int16_t adjacent_column{ column };
                int16_t adjacent_row{ row };
                move_cell(adjacent_column, adjacent_row, direction, false);
                if (adjacent_column < 0 || adjacent_column >= _columns
                    || adjacent_row < 0 || adjacent_row >= _rows)
                {
                    continue;
                }
                const uint16_t adjacent{ cell_offset(adjacent_column, adjacent_row) };
                if (_terrain[adjacent] < 0x80u || _weight[adjacent] == 0u)
                    best = std::min(best, _weight[adjacent]);
            }
            if (best != 0xffu)
                _weight[cell] = static_cast<uint8_t>(best + _cost[cell]);
        }
    }

    void dsp3_t::begin_path_results() noexcept
    {
        uint8_t minimum{ static_cast<uint8_t>(_data_register) };
        const uint8_t maximum{ static_cast<uint8_t>(_data_register >> 8u) };
        if (minimum == 0u)
            minimum = 1u;
        if (_maximum_path_radius >= minimum)
            minimum = static_cast<uint8_t>(_maximum_path_radius + 1u);
        _maximum_path_radius = std::max(_maximum_path_radius, maximum);
        build_path_cells(minimum, maximum);
        if (_path_count == 0u)
        {
            finish_command();
            return;
        }
        _data_register = _weight[_path_cells[0]];
        _status = 0x84u;
        _operation = operation_t::path_results;
    }

    void dsp3_t::execute_access() noexcept
    {
        switch (_operation)
        {
        case operation_t::command:
            if ((_data_register & 0xffu) < 0x40u)
                begin_command(static_cast<uint8_t>(_data_register));
            break;
        case operation_t::set_dimensions:
            _columns = static_cast<uint8_t>(_data_register);
            _rows = static_cast<uint8_t>(_data_register >> 8u);
            finish_command();
            break;
        case operation_t::cell_offset:
            compute_cell_offset();
            _operation = operation_t::zero_result_one;
            break;
        case operation_t::adjacent_direction:
            begin_adjacent_cell();
            break;
        case operation_t::adjacent_coordinate:
        {
            int16_t column{ static_cast<uint8_t>(_data_register) };
            int16_t row{ static_cast<uint8_t>(_data_register >> 8u) };
            if ((column & 1) != 0)
                row = static_cast<int16_t>(row + (_add_column & 1));
            column = static_cast<int16_t>(column + _add_column);
            row = static_cast<int16_t>(row + _add_row);
            if (column < 0) column = static_cast<int16_t>(column + _columns);
            else if (column >= _columns) column = static_cast<int16_t>(column - _columns);
            if (row < 0) row = static_cast<int16_t>(row + _rows);
            else if (row >= _rows) row = static_cast<int16_t>(row - _rows);
            _add_column = column;
            _add_row = row;
            _data_register = static_cast<uint16_t>(
                static_cast<uint8_t>(column) | (static_cast<uint16_t>(row) << 8u)
            );
            _operation = operation_t::adjacent_offset;
            break;
        }
        case operation_t::adjacent_offset:
            _data_register = static_cast<uint16_t>(_columns * _add_row + _add_column);
            _operation = operation_t::zero_result_one;
            break;
        case operation_t::absorb_until_ffff:
            if (_data_register == 0xffffu)
                finish_command();
            break;
        case operation_t::absorb_zero:
            _data_register = 0;
            _operation = operation_t::zero_result_one;
            break;
        case operation_t::convert_count:
            _conversion_count = _data_register;
            _bitmap_index = 0;
            _operation = operation_t::convert_stream;
            break;
        case operation_t::convert_stream:
            convert_bitmap_access();
            break;
        case operation_t::absorb_one:
            _operation = operation_t::zero_result_two;
            break;
        case operation_t::zero_result_two:
            _data_register = 0;
            _operation = operation_t::zero_result_one;
            break;
        case operation_t::zero_result_one:
            finish_command();
            break;
        case operation_t::reset_pending:
            finish_command();
            break;
        case operation_t::memory_dump:
            if (++_index == 1024u)
                finish_command();
            else
                _data_register = dsp3_data_word(_index);
            break;
        case operation_t::rom_version:
            _data_register = 0x0300u;
            _operation = operation_t::reset_pending;
            break;
        case operation_t::decode_count:
            _codeword_count = _data_register;
            _operation = operation_t::decode_output_count;
            break;
        case operation_t::decode_output_count:
            _output_word_count = _data_register;
            _bit_count = 0;
            _bits_left = 0;
            _symbol = 0;
            _index = 0;
            _bit_command = 0xffffu;
            _operation = operation_t::decode_symbols;
            _status = 0xc0u;
            break;
        case operation_t::decode_symbols:
            decode_symbols_access();
            break;
        case operation_t::decode_tree:
            decode_tree_access();
            break;
        case operation_t::decode_data:
            decode_data_access();
            break;
        case operation_t::set_start:
            _start_column = static_cast<uint8_t>(_data_register);
            _start_row = static_cast<uint8_t>(_data_register >> 8u);
            _x = _start_column;
            _y = _start_row;
            compute_cell_offset();
            _terrain[_data_register] = 0;
            _cost[_data_register] = 0xffu;
            _weight[_data_register] = 0;
            _maximum_search_radius = 0;
            _maximum_path_radius = 0;
            _operation = operation_t::zero_result_one;
            break;
        case operation_t::coordinate:
            ++_index;
            if (_index == 3u && _data_register == 0xffffu)
                finish_command();
            else if (_index == 4u)
                _x = _data_register;
            else if (_index == 5u)
            {
                _y = _data_register;
                _data_register = 1;
            }
            else if (_index == 6u)
                _data_register = _x;
            else if (_index == 7u)
            {
                _data_register = _y;
                _index = 0;
            }
            break;
        case operation_t::path_radius:
            begin_path_search();
            break;
        case operation_t::path_terrain:
            _status = 0x84u;
            _operation = operation_t::path_cost;
            break;
        case operation_t::path_cost:
            _terrain[_path_cells[_path_index]] = static_cast<uint8_t>(_data_register);
            _status = 0x84u;
            _operation = operation_t::path_advance;
            break;
        case operation_t::path_advance:
            _cost[_path_cells[_path_index]] = static_cast<uint8_t>(_data_register);
            ++_path_index;
            present_path_cell();
            break;
        case operation_t::path_prepare_results:
            finish_path_inputs();
            _operation = operation_t::path_result_radius;
            break;
        case operation_t::path_result_radius:
            begin_path_results();
            break;
        case operation_t::path_results:
            ++_path_index;
            if (_path_index >= _path_count)
            {
                _data_register = 0xffffu;
                _status = 0x80u;
                _operation = operation_t::zero_result_one;
            }
            else
            {
                _data_register = _weight[_path_cells[_path_index]];
                _status = 0x84u;
            }
            break;
        case operation_t::unsupported:
            finish_command();
            break;
        }
    }
}
