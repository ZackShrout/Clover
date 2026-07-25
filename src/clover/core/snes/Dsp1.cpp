//
// Created by Zack Shrout on 7/22/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dsp1.h"

#include <algorithm>

// Fixed-point operations and the dumped DSP-1B data ROM are from Overload's
// reference library, used under the permission published with the source at:
// https://www.crazysmart.net.au/dsp/
namespace clover::core::dsp1_reference
{
#include "clover/core/snes/Dsp1Reference.inc"
}
#undef DSP1_VERSION

namespace clover::core
{
    namespace
    {
        [[nodiscard]] int16_t matrix_term(int16_t lhs, int16_t rhs) noexcept
        {
            return static_cast<int16_t>(static_cast<int32_t>(lhs) * rhs >> 15);
        }
    }

    void dsp1_t::power_on() noexcept
    {
        _parameters.fill(0);
        _results.fill(0);
        _matrices = {};
        _projection = {};
        _phase = phase_t::command;
        _command = 0;
        _word_index = 0;
        _byte_index = 0;
        _data_register = 0x0080u;
        _status_high_byte = false;
        _frozen = false;
        _raster_output_written = false;

    }

    uint8_t dsp1_t::read_status() noexcept
    {
        _status_high_byte = !_status_high_byte;
        if (_status_high_byte || _frozen)
            return 0;
        const uint8_t control{ static_cast<uint8_t>(_phase == phase_t::command ? 0x04u : 0u) };
        const uint8_t byte_phase{ static_cast<uint8_t>(_byte_index != 0u ? 0x10u : 0u) };
        return static_cast<uint8_t>(0x80u | control | byte_phase);
    }

    uint8_t dsp1_t::read_data() noexcept
    {
        uint8_t value{};
        access_data(true, value);
        return value;
    }

    void dsp1_t::write_data(uint8_t value) noexcept
    {
        access_data(false, value);
    }

    void dsp1_t::access_data(bool read, uint8_t& value) noexcept
    {
        if (_frozen)
            return;

        if (!read && _phase == phase_t::results && _command == 0x0au)
            _raster_output_written = true;

        if (read)
            value = _byte_index == 0u ? static_cast<uint8_t>(_data_register)
                                      : static_cast<uint8_t>(_data_register >> 8u);
        else if (_byte_index == 0u)
            _data_register = static_cast<uint16_t>((_data_register & 0xff00u) | value);
        else
            _data_register = static_cast<uint16_t>((_data_register & 0x00ffu) | (value << 8u));

        if (_phase == phase_t::command)
        {
            _command = static_cast<uint8_t>(_data_register);
            if ((_command & 0xc0u) != 0u)
                return;
            _word_index = 0;
            _byte_index = 0;
            if (_command == 0x1au || _command == 0x2au || _command == 0x3au)
            {
                _frozen = true;
                return;
            }
            _phase = phase_t::parameters;
            return;
        }

        _byte_index ^= 1u;
        if (_byte_index != 0u)
            return;

        if (_phase == phase_t::parameters)
        {
            _parameters[_word_index++] = static_cast<int16_t>(_data_register);
            if (_word_index < parameter_count(_command))
                return;

            execute();
            _word_index = 0;
            _raster_output_written = false;
            if (result_count(_command) == 0u)
            {
                _phase = phase_t::command;
                _data_register = 0x0080u;
            }
            else
            {
                _phase = phase_t::results;
                _data_register = current_result();
            }
            return;
        }

        finish_result_word();
    }

    uint16_t dsp1_t::current_result() const noexcept
    {
        const bool memory_dump{ (_command & 0x10u) != 0u
                                && ((_command & 0x0fu) == 0x07u
                                    || (_command & 0x0fu) == 0x0fu) };
        return memory_dump ? dsp1_reference::DATAROM[_word_index]
                           : static_cast<uint16_t>(_results[_word_index]);
    }

    uint8_t dsp1_t::parameter_count(uint8_t command) noexcept
    {
        static constexpr std::array<uint8_t, 16> counts{
            2, 4, 7, 3, 2, 4, 3, 1, 3, 3, 1, 3, 3, 3, 2, 1
        };
        if (command == 0x18u || command == 0x38u)
            return 4;
        if (command == 0x1cu || command == 0x3cu)
            return 6;
        return counts[command & 0x0fu]
            + ((command & 0x0fu) == 0x04u && (command & 0x10u) ? 4u : 0u);
    }

    uint16_t dsp1_t::result_count(uint8_t command) noexcept
    {
        static constexpr std::array<uint16_t, 16> counts{
            1, 0, 4, 3, 2, 0, 3, 1, 1, 3, 4, 1, 2, 3, 2, 1
        };
        const uint8_t operation{ static_cast<uint8_t>(command & 0x0fu) };
        if ((operation == 0x07u || operation == 0x0fu) && (command & 0x10u))
            return 1024;
        if (command == 0x10u || command == 0x30u)
            return 2;
        if (command == 0x1cu || command == 0x3cu)
            return 3;
        if (command == 0x08u)
            return 2;
        if (command == 0x14u || command == 0x34u)
            return 3;
        return counts[command & 0x0fu];
    }

    void dsp1_t::finish_result_word() noexcept
    {
        if (++_word_index < result_count(_command))
        {
            _data_register = current_result();
            return;
        }

        if (_command == 0x0au && !_raster_output_written)
        {
            ++_parameters[0];
            execute();
            _word_index = 0;
            _data_register = current_result();
            return;
        }
        _word_index = 0;
        _phase = phase_t::command;
        _data_register = 0x0080u;
    }

    void dsp1_t::set_matrix(size_t index, int16_t scale, int16_t az, int16_t ay, int16_t ax) noexcept
    {
        using namespace dsp1_reference;
        const int16_t sin_az{ DSP1_Sin(az) }, cos_az{ DSP1_Cos(az) };
        const int16_t sin_ay{ DSP1_Sin(ay) }, cos_ay{ DSP1_Cos(ay) };
        const int16_t sin_ax{ DSP1_Sin(ax) }, cos_ax{ DSP1_Cos(ax) };
        const int16_t m{ static_cast<int16_t>(scale >> 1) };
        auto& a{ _matrices[index] };
        a[0][0] = matrix_term(matrix_term(m, cos_az), cos_ay);
        a[0][1] = static_cast<int16_t>(-matrix_term(matrix_term(m, sin_az), cos_ay));
        a[0][2] = matrix_term(m, sin_ay);
        a[1][0] = static_cast<int16_t>(matrix_term(matrix_term(m, sin_az), cos_ax)
                                      + matrix_term(matrix_term(matrix_term(m, cos_az), sin_ax), sin_ay));
        a[1][1] = static_cast<int16_t>(matrix_term(matrix_term(m, cos_az), cos_ax)
                                      - matrix_term(matrix_term(matrix_term(m, sin_az), sin_ax), sin_ay));
        a[1][2] = static_cast<int16_t>(-matrix_term(matrix_term(m, sin_ax), cos_ay));
        a[2][0] = static_cast<int16_t>(matrix_term(matrix_term(m, sin_az), sin_ax)
                                      - matrix_term(matrix_term(matrix_term(m, cos_az), cos_ax), sin_ay));
        a[2][1] = static_cast<int16_t>(matrix_term(matrix_term(m, cos_az), sin_ax)
                                      + matrix_term(matrix_term(matrix_term(m, sin_az), cos_ax), sin_ay));
        a[2][2] = matrix_term(matrix_term(m, cos_ax), cos_ay);
    }

    void dsp1_t::objective(size_t index) noexcept
    {
        const auto& a{ _matrices[index] };
        for (size_t row{}; row < 3; ++row)
            _results[row] = static_cast<int16_t>(matrix_term(_parameters[0], a[row][0])
                                                 + matrix_term(_parameters[1], a[row][1])
                                                 + matrix_term(_parameters[2], a[row][2]));
    }

    void dsp1_t::subjective(size_t index) noexcept
    {
        const auto& a{ _matrices[index] };
        for (size_t column{}; column < 3; ++column)
            _results[column] = static_cast<int16_t>(matrix_term(_parameters[0], a[0][column])
                                                    + matrix_term(_parameters[1], a[1][column])
                                                    + matrix_term(_parameters[2], a[2][column]));
    }

    void dsp1_t::scalar(size_t index) noexcept
    {
        const auto& a{ _matrices[index] };
        const int32_t sum{ static_cast<int32_t>(_parameters[0]) * a[0][0]
                           + static_cast<int32_t>(_parameters[1]) * a[0][1]
                           + static_cast<int32_t>(_parameters[2]) * a[0][2] };
        _results[0] = static_cast<int16_t>(sum >> 15);
    }

    void dsp1_t::project() noexcept
    {
        using namespace dsp1_reference;

        std::array<int16_t, 3> point{};
        std::array<int16_t, 3> exponent{};
        DSP1_NormalizeDouble(static_cast<int32_t>(_parameters[0]) - _projection.Gx,
                             point[0], exponent[0]);
        DSP1_NormalizeDouble(static_cast<int32_t>(_parameters[1]) - _projection.Gy,
                             point[1], exponent[1]);
        DSP1_NormalizeDouble(static_cast<int32_t>(_parameters[2]) - _projection.Gz,
                             point[2], exponent[2]);
        for (size_t axis{}; axis < point.size(); ++axis)
        {
            point[axis] = static_cast<int16_t>(point[axis] >> 1);
            --exponent[axis];
        }

        const int16_t common_exponent{
            std::min({ exponent[0], exponent[1], exponent[2] })
        };
        for (size_t axis{}; axis < point.size(); ++axis)
        {
            const size_t shift{ static_cast<size_t>(exponent[axis] - common_exponent) };
            point[axis] = static_cast<int16_t>(
                static_cast<int32_t>(point[axis]) * DATAROM[0x0031u + shift] >> 15
            );
        }

        const int16_t view_depth_coefficient{ static_cast<int16_t>(
            -matrix_term(point[0], _projection.Nx)
            - matrix_term(point[1], _projection.Ny)
            - matrix_term(point[2], _projection.Nz)
        ) };
        int64_t view_depth_offset{ view_depth_coefficient };
        const int depth_shift{ 16 - common_exponent };
        if (depth_shift >= 0)
            view_depth_offset <<= depth_shift;
        else
            view_depth_offset >>= -depth_shift;
        if (view_depth_offset == -1)
            view_depth_offset = 0;
        view_depth_offset >>= 1;

        const int32_t view_depth{
            static_cast<int32_t>(static_cast<uint16_t>(_projection.Les_G))
            + static_cast<int32_t>(view_depth_offset)
        };
        int16_t depth_coefficient{};
        int16_t depth_exponent{};
        DSP1_NormalizeDouble(view_depth, depth_coefficient, depth_exponent);
        depth_exponent = static_cast<int16_t>(15 - depth_exponent);

        int16_t inverse_coefficient{};
        int16_t inverse_exponent{};
        DSP1_Inverse(depth_coefficient, 0, inverse_coefficient, inverse_exponent);
        const int16_t scale_coefficient{
            matrix_term(inverse_coefficient, _projection.Les_C)
        };

        const auto project_axis = [&](int16_t x_basis, int16_t y_basis, int16_t z_basis) noexcept
        {
            const int16_t dot{ static_cast<int16_t>(
                matrix_term(point[0], x_basis) + matrix_term(point[1], y_basis)
                + matrix_term(point[2], z_basis)
            ) };
            int16_t coefficient{};
            int16_t operation_exponent{};
            DSP1_Normalize(matrix_term(dot, scale_coefficient), coefficient, operation_exponent);
            return DSP1_Truncate(coefficient, static_cast<int16_t>(
                _projection.Les_E - depth_exponent + depth_shift + operation_exponent
            ));
        };

        // The horizontal screen basis is normalized through a Q15 multiply by
        // 0x7fff before the point is projected. Keeping the multiply explicit
        // preserves the DSP-1's one-bit truncation at cardinal directions.
        _results[0] = project_axis(matrix_term(_projection.CosAas, 0x7fff),
                                   matrix_term(_projection.SinAas, 0x7fff),
                                   0);
        _results[1] = project_axis(
            matrix_term(_projection.CosAzs,
                        static_cast<int16_t>(-_projection.SinAas)),
            matrix_term(_projection.CosAzs, _projection.CosAas),
            matrix_term(static_cast<int16_t>(-_projection.SinAzs), 0x7fff)
        );

        int16_t magnification_coefficient{};
        int16_t magnification_exponent{ inverse_exponent };
        DSP1_Normalize(scale_coefficient, magnification_coefficient, magnification_exponent);
        _results[2] = DSP1_Truncate(magnification_coefficient, static_cast<int16_t>(
            magnification_exponent + _projection.Les_E - depth_exponent - 7
        ));
    }

    void dsp1_t::execute() noexcept
    {
        using namespace dsp1_reference;
        const uint8_t op{ static_cast<uint8_t>(_command & 0x0fu) };
        const size_t matrix{ (_command & 0x30u) == 0x10u ? 1u
                             : (_command & 0x30u) == 0x20u ? 2u : 0u };
        if (_command == 0x10u || _command == 0x30u)
        {
            DSP1_Inverse(_parameters[0], _parameters[1], _results[0], _results[1]);
            return;
        }

        switch (op)
        {
        case 0x00:
            DSP1_Multiply(_parameters[0], _parameters[1], _results[0]);
            if ((_command & 0x20u) != 0u) ++_results[0];
            break;
        case 0x01:
        case 0x05:
            set_matrix(matrix, _parameters[0], _parameters[1], _parameters[2], _parameters[3]);
            break;
        case 0x02:
            DSP1_Parameter(_projection,
                           _parameters[0], _parameters[1], _parameters[2], _parameters[3],
                           _parameters[4], _parameters[5], _parameters[6], _results[0],
                           _results[1], _results[2], _results[3]);
            break;
        case 0x03: subjective(matrix); break;
        case 0x04:
            if ((_command & 0x10u) != 0u)
                DSP1_Gyrate(_parameters[0], _parameters[1], _parameters[2], _parameters[3],
                            _parameters[4], _parameters[5], _results[0], _results[1], _results[2]);
            else
                DSP1_Triangle(_parameters[0], _parameters[1], _results[0], _results[1]);
            break;
        case 0x06:
            project();
            break;
        case 0x07:
            if ((_command & 0x10u) != 0u)
                break;
            else
                _results[0] = (_command & 0x20u) != 0u ? 0x0100 : 0;
            break;
        case 0x08:
            if (_command == 0x28u)
                DSP1_Distance(_parameters[0], _parameters[1], _parameters[2], _results[0]);
            else if (_command == 0x18u || _command == 0x38u)
            {
                DSP1_Range(_parameters[0], _parameters[1], _parameters[2], _parameters[3], _results[0]);
                if (_command == 0x38u) ++_results[0];
            }
            else
            {
                int radius{};
                DSP1_Radius(_parameters[0], _parameters[1], _parameters[2], radius);
                _results[0] = static_cast<int16_t>(radius);
                _results[1] = static_cast<int16_t>(radius >> 16);
            }
            break;
        case 0x09:
        case 0x0d: objective(matrix); break;
        case 0x0a:
            DSP1_Raster(_projection, _parameters[0],
                        _results[0], _results[1], _results[2], _results[3]);
            break;
        case 0x0b: scalar(matrix); break;
        case 0x0c:
            if ((_command & 0x10u) != 0u)
                DSP1_Polar(_parameters[0], _parameters[1], _parameters[2], _parameters[3],
                           _parameters[4], _parameters[5], _results[0], _results[1], _results[2]);
            else
                DSP1_Rotate(_parameters[0], _parameters[1], _parameters[2], _results[0], _results[1]);
            break;
        case 0x0e:
            DSP1_Target(_projection, _parameters[0], _parameters[1],
                        _results[0], _results[1]);
            break;
        case 0x0f:
            if ((_command & 0x10u) != 0u)
                break;
            else
                _results[0] = (_command & 0x20u) != 0u ? 0x0100 : 0;
            break;
        default: break;
        }
    }
}
