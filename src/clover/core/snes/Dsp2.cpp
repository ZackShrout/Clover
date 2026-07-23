//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dsp2.h"

namespace clover::core
{
    void dsp2_t::power_on() noexcept
    {
        _input.fill(0);
        _output.fill(0);
        _input_stage = input_stage_t::command;
        _command = 0;
        _transparent_color = 0;
        _input_length = 0;
        _output_length = 0;
        _expected_input = 0;
        _input_index = 0;
        _output_count = 0;
        _output_index = 0;
    }

    uint8_t dsp2_t::read_data() noexcept
    {
        if (_output_index >= _output_count)
            return 0xffu;

        const uint8_t value{ _output[_output_index++] };
        if (_output_index == _output_count)
        {
            _output_index = 0;
            _output_count = 0;
        }
        return value;
    }

    void dsp2_t::write_data(uint8_t value) noexcept
    {
        if (_input_stage == input_stage_t::command)
        {
            begin_command(value);
            return;
        }

        _input[_input_index++] = value;
        if (_input_index == _expected_input)
            finish_input_stage();
    }

    void dsp2_t::begin_command(uint8_t command) noexcept
    {
        _command = command;
        _input.fill(0);
        _input_index = 0;
        _output_index = 0;
        _output_count = 0;
        _input_stage = input_stage_t::fixed_parameters;

        switch (_command)
        {
        case 0x01u:
            _expected_input = 32;
            break;
        case 0x03u:
        case 0x05u:
        case 0x06u:
            _expected_input = 1;
            break;
        case 0x09u:
            _expected_input = 4;
            break;
        case 0x0du:
            _expected_input = 2;
            break;
        default:
            _expected_input = 0;
            _input_stage = input_stage_t::command;
            break;
        }
    }

    void dsp2_t::finish_input_stage() noexcept
    {
        if (_input_stage == input_stage_t::fixed_parameters)
        {
            if (_command == 0x05u)
            {
                _input_length = _input[0];
                _expected_input = static_cast<size_t>(_input_length) * 2u;
                _input_index = 0;
                _input_stage = input_stage_t::bitmap_data;
                if (_expected_input == 0)
                    execute();
                return;
            }
            if (_command == 0x06u)
            {
                _input_length = _input[0];
                _expected_input = _input_length;
                _input_index = 0;
                _input_stage = input_stage_t::bitmap_data;
                if (_expected_input == 0)
                    execute();
                return;
            }
            if (_command == 0x0du)
            {
                _input_length = _input[0];
                _output_length = _input[1];
                _expected_input = (static_cast<size_t>(_input_length) + 1u) / 2u;
                _input_index = 0;
                _input_stage = input_stage_t::bitmap_data;
                if (_expected_input == 0)
                    execute();
                return;
            }
        }

        execute();
    }

    void dsp2_t::execute() noexcept
    {
        _output_index = 0;
        _output_count = 0;

        switch (_command)
        {
        case 0x01u:
            convert_bitmap();
            break;
        case 0x03u:
            _transparent_color = static_cast<uint8_t>(_input[0] & 0x0fu);
            break;
        case 0x05u:
            overlay_bitmap();
            break;
        case 0x06u:
            reverse_bitmap();
            break;
        case 0x09u:
            multiply();
            break;
        case 0x0du:
            scale_bitmap();
            break;
        default:
            break;
        }

        _input_stage = input_stage_t::command;
        _expected_input = 0;
        _input_index = 0;
    }

    void dsp2_t::convert_bitmap() noexcept
    {
        _output.fill(0);
        for (size_t row{}; row < 8; ++row)
        {
            for (size_t pixel{}; pixel < 8; ++pixel)
            {
                const uint8_t packed{ _input[row * 4u + pixel / 2u] };
                const uint8_t color{ static_cast<uint8_t>(
                    (pixel & 1u) == 0u ? packed >> 4u : packed & 0x0fu
                ) };
                const uint8_t pixel_mask{ static_cast<uint8_t>(0x80u >> pixel) };

                if ((color & 0x01u) != 0u)
                    _output[row * 2u] |= pixel_mask;
                if ((color & 0x02u) != 0u)
                    _output[row * 2u + 1u] |= pixel_mask;
                if ((color & 0x04u) != 0u)
                    _output[16u + row * 2u] |= pixel_mask;
                if ((color & 0x08u) != 0u)
                    _output[16u + row * 2u + 1u] |= pixel_mask;
            }
        }
        _output_count = 32;
    }

    void dsp2_t::overlay_bitmap() noexcept
    {
        for (size_t index{}; index < _input_length; ++index)
        {
            const uint8_t background{ _input[index] };
            const uint8_t foreground{ _input[static_cast<size_t>(_input_length) + index] };
            const uint8_t high{ static_cast<uint8_t>(
                (foreground >> 4u) == _transparent_color
                    ? background & 0xf0u : foreground & 0xf0u
            ) };
            const uint8_t low{ static_cast<uint8_t>(
                (foreground & 0x0fu) == _transparent_color
                    ? background & 0x0fu : foreground & 0x0fu
            ) };
            _output[index] = static_cast<uint8_t>(high | low);
        }
        _output_count = _input_length;
    }

    void dsp2_t::reverse_bitmap() noexcept
    {
        for (size_t index{}; index < _input_length; ++index)
        {
            const uint8_t value{ _input[index] };
            _output[static_cast<size_t>(_input_length) - index - 1u] =
                static_cast<uint8_t>((value << 4u) | (value >> 4u));
        }
        _output_count = _input_length;
    }

    void dsp2_t::multiply() noexcept
    {
        const uint16_t lhs{
            static_cast<uint16_t>(_input[0] | (static_cast<uint16_t>(_input[1]) << 8u))
        };
        const uint16_t rhs{
            static_cast<uint16_t>(_input[2] | (static_cast<uint16_t>(_input[3]) << 8u))
        };
        const uint32_t product{ static_cast<uint32_t>(lhs) * rhs };
        for (size_t byte{}; byte < 4; ++byte)
            _output[byte] = static_cast<uint8_t>(product >> (byte * 8u));
        _output_count = 4;
    }

    void dsp2_t::scale_bitmap() noexcept
    {
        if (_output_length == 0)
            return;

        const uint32_t step{ _input_length <= _output_length
            ? 0x10000u
            : (static_cast<uint32_t>(_input_length) << 17u)
                / (static_cast<uint32_t>(_output_length) * 2u + 1u) };
        uint32_t source_position{};

        for (size_t output_byte{}; output_byte < _output_length; ++output_byte)
        {
            uint8_t packed{};
            for (size_t half{}; half < 2; ++half)
            {
                const size_t source_pixel{ source_position >> 16u };
                const uint8_t source_byte{ _input[source_pixel / 2u] };
                const uint8_t pixel{ static_cast<uint8_t>(
                    (source_pixel & 1u) == 0u ? source_byte >> 4u : source_byte & 0x0fu
                ) };
                packed |= static_cast<uint8_t>(pixel << (half == 0u ? 4u : 0u));
                source_position += step;
            }
            _output[output_byte] = packed;
        }
        _output_count = _output_length;
    }
}
