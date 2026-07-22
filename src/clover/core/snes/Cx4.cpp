//
// Created by Zack Shrout on 7/22/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cx4.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace clover::core
{
    namespace
    {
        constexpr double k_pi{3.1415926535897932384626433832795};
        constexpr std::array<uint8_t, 48> k_immediate_data{
            0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff, 0x00, 0x00, 0x00, 0xff,
            0xff, 0xff, 0x00, 0x00, 0xff, 0xff, 0x00, 0x00, 0x80, 0xff, 0xff, 0x7f,
            0x00, 0x80, 0x00, 0xff, 0x7f, 0x00, 0xff, 0x7f, 0xff, 0x7f, 0xff, 0xff,
            0x00, 0x00, 0x01, 0xff, 0xff, 0xfe, 0x00, 0x01, 0x00, 0xff, 0xfe, 0x00};

        constexpr std::array<uint16_t, 40> k_wave_offsets{
            0x0000, 0x0002, 0x0004, 0x0006, 0x0008, 0x000a, 0x000c, 0x000e, 0x0200, 0x0202,
            0x0204, 0x0206, 0x0208, 0x020a, 0x020c, 0x020e, 0x0400, 0x0402, 0x0404, 0x0406,
            0x0408, 0x040a, 0x040c, 0x040e, 0x0600, 0x0602, 0x0604, 0x0606, 0x0608, 0x060a,
            0x060c, 0x060e, 0x0800, 0x0802, 0x0804, 0x0806, 0x0808, 0x080a, 0x080c, 0x080e};

        [[nodiscard]] int32_t sign_extend_24(uint32_t value) noexcept
        {
            value &= 0x00ffffffu;
            return (value & 0x00800000u) != 0u ? static_cast<int32_t>(value | 0xff000000u)
                                               : static_cast<int32_t>(value);
        }

        [[nodiscard]] constexpr uint8_t register_write_mask(uint8_t address) noexcept
        {
            switch (address)
            {
            case 0x48u:
            case 0x51u:
            case 0x52u:
                return 0x01u;
            case 0x4cu:
                return 0x03u;
            case 0x4eu:
                return 0x7fu;
            case 0x50u:
                return 0x77u;
            default:
                return 0xffu;
            }
        }
    }

    void cx4_t::power_on(std::span<const uint8_t> program_rom) noexcept
    {
        _ram.fill(0);
        _registers.fill(0);
        _program_rom = program_rom;
        _x = _y = _z = _x2 = _y2 = _distance = _scale = 0;
    }

    uint8_t cx4_t::read(uint32_t address, uint8_t open_bus) const noexcept
    {
        const uint16_t local{static_cast<uint16_t>(address & 0x1fffu)};
        if (local < 0x0c00u || (local >= 0x1000u && local < 0x1c00u))
            return _ram[local & 0x0fffu];
        if (local < 0x1f40u)
            return open_bus;

        uint8_t reg{static_cast<uint8_t>(local)};
        if (reg >= 0xc0u && reg <= 0xefu)
            reg = static_cast<uint8_t>(reg - 0x40u);
        if ((reg >= 0xb0u && reg <= 0xbfu) || reg >= 0xf0u)
            return 0u;
        return _registers[reg];
    }

    void cx4_t::write(uint32_t address, uint8_t value) noexcept
    {
        const uint16_t local{static_cast<uint16_t>(address & 0x1fffu)};
        if (local < 0x0c00u || (local >= 0x1000u && local < 0x1c00u))
        {
            _ram[local & 0x0fffu] = value;
            return;
        }
        if (local < 0x1f40u)
            return;

        uint8_t reg{static_cast<uint8_t>(local)};
        if (reg >= 0xc0u && reg <= 0xefu)
            reg = static_cast<uint8_t>(reg - 0x40u);
        if ((reg >= 0xb0u && reg <= 0xbfu) || reg >= 0xf0u)
            return;

        _registers[reg] = static_cast<uint8_t>(value & register_write_mask(reg));
        if (local == 0x1f47u)
            transfer_data();
        else if (local == 0x1f4fu)
        {
            if (_registers[0x4d] == 0x0eu && (value & 0xc3u) == 0u)
                _registers[0x80] = value >> 2u;
            else
                execute(value);
        }
    }

    uint8_t cx4_t::bus_read(uint32_t address) const noexcept
    {
        const uint8_t bank{static_cast<uint8_t>(address >> 16u)};
        const uint16_t offset{static_cast<uint16_t>(address)};
        if ((bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu)) && offset >= 0x6000u &&
            offset <= 0x7fffu)
        {
            return read(address);
        }
        if (_program_rom.empty())
            return 0;
        const uint32_t rom_offset{
            (((static_cast<uint32_t>(bank) & 0x7fu) << 15u) | (offset & 0x7fffu)) %
            static_cast<uint32_t>(_program_rom.size())};
        return _program_rom[rom_offset];
    }

    uint8_t cx4_t::read_local(uint16_t address) const noexcept
    {
        return read(address);
    }
    uint16_t cx4_t::read_word(uint16_t address) const noexcept
    {
        return static_cast<uint16_t>(read_local(address) |
                                     (static_cast<uint16_t>(read_local(address + 1u)) << 8u));
    }
    uint32_t cx4_t::read_long(uint16_t address) const noexcept
    {
        return read_local(address) | (static_cast<uint32_t>(read_local(address + 1u)) << 8u) |
               (static_cast<uint32_t>(read_local(address + 2u)) << 16u);
    }
    void cx4_t::write_local(uint16_t address, uint8_t value) noexcept
    {
        write(address, value);
    }
    void cx4_t::write_word(uint16_t address, uint16_t value) noexcept
    {
        write_local(address, static_cast<uint8_t>(value));
        write_local(address + 1u, static_cast<uint8_t>(value >> 8u));
    }
    void cx4_t::write_long(uint16_t address, uint32_t value) noexcept
    {
        write_local(address, static_cast<uint8_t>(value));
        write_local(address + 1u, static_cast<uint8_t>(value >> 8u));
        write_local(address + 2u, static_cast<uint8_t>(value >> 16u));
    }

    uint32_t cx4_t::load_register(uint8_t index) const noexcept
    {
        const uint16_t address{static_cast<uint16_t>(0x1f80u + index * 3u)};
        return read_long(address);
    }
    void cx4_t::store_register(uint8_t index, uint32_t value) noexcept
    {
        write_long(static_cast<uint16_t>(0x1f80u + index * 3u), value);
    }

    int16_t cx4_t::sin_fixed(uint16_t angle) noexcept
    {
        return static_cast<int16_t>(
            std::lround(std::sin((angle & 0x1ffu) * 2.0 * k_pi / 512.0) * 32767.0));
    }
    int16_t cx4_t::cos_fixed(uint16_t angle) noexcept
    {
        return static_cast<int16_t>(
            std::lround(std::cos((angle & 0x1ffu) * 2.0 * k_pi / 512.0) * 32767.0));
    }
    uint32_t cx4_t::cx4_sin(uint32_t angle) noexcept
    {
        const int32_t value{static_cast<int32_t>(
            std::floor(std::sin((angle & 0x1ffu) * 2.0 * k_pi / 512.0) * 65536.0))};
        return static_cast<uint32_t>(value) & 0x00ffffffu;
    }
    void cx4_t::multiply_24(uint32_t x, uint32_t y, uint32_t& low, uint32_t& high) noexcept
    {
        const int64_t result{static_cast<int64_t>(sign_extend_24(x)) * sign_extend_24(y)};
        low = static_cast<uint32_t>(result) & 0x00ffffffu;
        high = static_cast<uint32_t>(result >> 24u) & 0x00ffffffu;
    }

    void cx4_t::immediate(uint32_t start) noexcept
    {
        uint32_t destination{load_register(0)};
        for (uint32_t index{start}; index < k_immediate_data.size(); ++index, ++destination)
            if ((destination & 0x0fffu) < _ram.size())
                _ram[destination & 0x0fffu] = k_immediate_data[index];
        store_register(0, destination);
    }

    void cx4_t::transfer_data() noexcept
    {
        uint32_t source{_registers[0x40] | (static_cast<uint32_t>(_registers[0x41]) << 8u) |
                        (static_cast<uint32_t>(_registers[0x42]) << 16u)};
        const uint16_t count{static_cast<uint16_t>(_registers[0x43] | (_registers[0x44] << 8u))};
        uint16_t destination{static_cast<uint16_t>(_registers[0x45] | (_registers[0x46] << 8u))};
        for (uint32_t index{0}; index < count; ++index)
            write_local(destination++, bus_read(source++));
    }

    void cx4_t::execute(uint8_t command) noexcept
    {
        uint32_t r0{}, r1{}, r2{}, r3{}, r4{}, r5{};
        switch (command)
        {
        case 0x00:
            command_sprite();
            break;
        case 0x01:
            std::fill(_ram.begin() + 0x300, _ram.end(), 0);
            draw_wireframe();
            break;
        case 0x05:
            write_word(0x1f80,
                       read_word(0x1f83) == 0u
                           ? 0u
                           : static_cast<uint16_t>(
                                 ((0x10000 / read_word(0x1f83)) * read_word(0x1f81)) >> 8u));
            break;
        case 0x0d:
        {
            _x = static_cast<int16_t>(read_word(0x1f80));
            _y = static_cast<int16_t>(read_word(0x1f83));
            const int16_t wanted{static_cast<int16_t>(read_word(0x1f86))};
            const double length{
                std::sqrt(static_cast<double>(_x) * _x + static_cast<double>(_y) * _y)};
            if (length != 0.0)
            {
                _x = static_cast<int16_t>(_x * wanted / length * 0.98);
                _y = static_cast<int16_t>(_y * wanted / length * 0.99);
            }
            write_word(0x1f89, static_cast<uint16_t>(_x));
            write_word(0x1f8c, static_cast<uint16_t>(_y));
            break;
        }
        case 0x10:
            r0 = load_register(0);
            r1 = load_register(1);
            r4 = r0 & 0x1ffu;
            r1 = (r1 & 0x8000u) ? (r1 | 0xffff8000u) : (r1 & 0x7fffu);
            multiply_24(cx4_sin(r4 + 0x80u), r1, r5, r2);
            r5 = (r5 >> 16u) & 0xffu;
            r2 = (r2 << 8u) + r5;
            multiply_24(cx4_sin(r4), r1, r5, r3);
            r5 = (r5 >> 16u) & 0xffu;
            r3 = (r3 << 8u) + r5;
            store_register(0, r0);
            store_register(1, r1);
            store_register(2, r2);
            store_register(3, r3);
            store_register(4, r4);
            store_register(5, r5);
            break;
        case 0x13:
            r0 = load_register(0);
            r1 = load_register(1);
            r4 = r0 & 0x1ffu;
            multiply_24(cx4_sin(r4 + 0x80u), r1, r5, r2);
            r5 = (r5 >> 8u) & 0xffffu;
            r2 = (r2 << 16u) + r5;
            multiply_24(cx4_sin(r4), r1, r5, r3);
            r5 = (r5 >> 8u) & 0xffffu;
            r3 = (r3 << 16u) + r5;
            store_register(0, r0);
            store_register(1, r1);
            store_register(2, r2);
            store_register(3, r3);
            store_register(4, r4);
            store_register(5, r5);
            break;
        case 0x15:
            _x = static_cast<int16_t>(read_word(0x1f80));
            _y = static_cast<int16_t>(read_word(0x1f83));
            write_word(0x1f80,
                       static_cast<uint16_t>(
                           std::sqrt(static_cast<double>(_x) * _x + static_cast<double>(_y) * _y)));
            break;
        case 0x1f:
        {
            _x = static_cast<int16_t>(read_word(0x1f80));
            _y = static_cast<int16_t>(read_word(0x1f83));
            int16_t angle{};
            if (_x == 0)
                angle = _y > 0 ? 0x80 : 0x180;
            else
            {
                angle = static_cast<int16_t>(std::atan(static_cast<double>(_y) / _x) /
                                             (2.0 * k_pi) * 512.0);
                if (_x < 0)
                    angle += 0x100;
                angle &= 0x1ff;
            }
            write_word(0x1f86, static_cast<uint16_t>(angle));
            break;
        }
        case 0x22:
        {
            const auto tangent = [](uint16_t a)
            {
                const int16_t c = cos_fixed(a), s = sin_fixed(a);
                return c ? static_cast<int32_t>((static_cast<int64_t>(s) << 16) / c)
                         : std::numeric_limits<int32_t>::min();
            };
            const int32_t t1 = tangent(read_word(0x1f8c) & 0x1ffu),
                          t2 = tangent(read_word(0x1f8f) & 0x1ffu);
            int16_t y = static_cast<int16_t>(read_word(0x1f83) - read_word(0x1f89));
            for (int j = 0; j < 225; ++j, ++y)
            {
                int16_t l = 1, r = 0;
                if (y >= 0)
                {
                    l = static_cast<int16_t>((t1 * y >> 16) - read_word(0x1f80) +
                                             read_word(0x1f86));
                    r = static_cast<int16_t>((t2 * y >> 16) - read_word(0x1f80) +
                                             read_word(0x1f86) + read_word(0x1f93));
                    if (l < 0 && r < 0)
                    {
                        l = 1;
                        r = 0;
                    }
                    else
                    {
                        l = std::clamp<int16_t>(l, 0, 255);
                        r = std::clamp<int16_t>(r, 0, 255);
                    }
                    if (l == 255 && r == 255)
                        r = 254;
                }
                _ram[0x800 + j] = static_cast<uint8_t>(l);
                _ram[0x900 + j] = static_cast<uint8_t>(r);
            }
            break;
        }
        case 0x25:
            r0 = load_register(0);
            r1 = load_register(1);
            multiply_24(r0, r1, r0, r1);
            store_register(0, r0);
            store_register(1, r1);
            break;
        case 0x2d:
            _x = static_cast<int16_t>(read_word(0x1f81));
            _y = static_cast<int16_t>(read_word(0x1f84));
            _z = static_cast<int16_t>(read_word(0x1f87));
            _x2 = read_local(0x1f89);
            _y2 = read_local(0x1f8a);
            _distance = read_local(0x1f8b);
            _scale = static_cast<int16_t>(read_word(0x1f90));
            transform_vector();
            write_word(0x1f80, _x);
            write_word(0x1f83, _y);
            break;
        case 0x40:
            r0 = 0;
            for (size_t i = 0; i < 0x800; ++i)
                r0 += _ram[i];
            store_register(0, r0);
            break;
        case 0x54:
            r0 = load_register(0);
            multiply_24(r0, r0, r1, r2);
            store_register(1, r1);
            store_register(2, r2);
            break;
        case 0x5c:
            store_register(0, 0);
            immediate(0);
            break;
        case 0x5e:
        case 0x60:
        case 0x62:
        case 0x64:
        case 0x66:
        case 0x68:
        case 0x6a:
        case 0x6c:
        case 0x6e:
        case 0x70:
        case 0x72:
        case 0x74:
        case 0x76:
        case 0x78:
        case 0x7a:
        case 0x7c:
            immediate((command - 0x5e) / 2 * 3);
            break;
        case 0x89:
            store_register(0, 0x054336);
            store_register(1, 0xffffff);
            break;
        default:
            break;
        }
    }

    void cx4_t::command_sprite() noexcept
    {
        switch (_registers[0x4d])
        {
        case 0x00:
            build_oam();
            break;
        case 0x03:
            scale_rotate(0);
            break;
        case 0x05:
            transform_lines();
            break;
        case 0x07:
            scale_rotate(64);
            break;
        case 0x08:
            draw_wireframe();
            break;
        case 0x0b:
            disintegrate();
            break;
        case 0x0c:
            wave();
            break;
        default:
            break;
        }
    }

    void cx4_t::build_oam() noexcept
    {
        uint32_t output{static_cast<uint32_t>(_ram[0x626]) << 2u};
        for (int32_t index{0x1fd}; index > static_cast<int32_t>(output) && index >= 0; index -= 4)
            _ram[index] = 0xe0;

        const int16_t global_x{static_cast<int16_t>(read_word(0x621))};
        const int16_t global_y{static_cast<int16_t>(read_word(0x623))};
        uint32_t high_table{0x200u + (_ram[0x626] >> 2u)};
        if (_ram[0x620] == 0u)
            return;

        uint8_t remaining{static_cast<uint8_t>(128u - _ram[0x626])};
        uint8_t shift{static_cast<uint8_t>((_ram[0x626] & 3u) * 2u)};
        uint32_t source{0x220};
        for (int object{_ram[0x620]}; object > 0 && remaining > 0; --object, source += 16)
        {
            const int16_t base_x{static_cast<int16_t>(read_word(source) - global_x)};
            const int16_t base_y{static_cast<int16_t>(read_word(source + 2) - global_y)};
            const uint8_t name{_ram[source + 5]};
            const uint8_t attributes{static_cast<uint8_t>(_ram[source + 4] | _ram[source + 6])};
            uint32_t sprite{read_long(source + 7)};
            const uint8_t pieces{bus_read(sprite)};
            if (pieces != 0u)
            {
                ++sprite;
                for (uint8_t piece{0}; piece < pieces && remaining > 0; ++piece, sprite += 4)
                {
                    const uint8_t flags{bus_read(sprite)};
                    int16_t x{static_cast<int8_t>(bus_read(sprite + 1))};
                    int16_t y{static_cast<int8_t>(bus_read(sprite + 2))};
                    if ((attributes & 0x40u) != 0u)
                        x = static_cast<int16_t>(-x - ((flags & 0x20u) ? 16 : 8));
                    if ((attributes & 0x80u) != 0u)
                        y = static_cast<int16_t>(-y - ((flags & 0x20u) ? 16 : 8));
                    x = static_cast<int16_t>(x + base_x);
                    y = static_cast<int16_t>(y + base_y);
                    if (x < -16 || x > 272 || y < -16 || y > 224)
                        continue;
                    _ram[output] = static_cast<uint8_t>(x);
                    _ram[output + 1] = static_cast<uint8_t>(y);
                    _ram[output + 2] = static_cast<uint8_t>(name + bus_read(sprite + 3));
                    _ram[output + 3] = static_cast<uint8_t>(attributes ^ (flags & 0xc0u));
                    _ram[high_table] &= static_cast<uint8_t>(~(3u << shift));
                    if ((x & 0x100) != 0)
                        _ram[high_table] |= static_cast<uint8_t>(1u << shift);
                    if ((flags & 0x20u) != 0)
                        _ram[high_table] |= static_cast<uint8_t>(2u << shift);
                    output += 4;
                    --remaining;
                    shift = static_cast<uint8_t>((shift + 2) & 6);
                    if (shift == 0)
                        ++high_table;
                }
            }
            else if (remaining > 0)
            {
                _ram[output] = static_cast<uint8_t>(base_x);
                _ram[output + 1] = static_cast<uint8_t>(base_y);
                _ram[output + 2] = name;
                _ram[output + 3] = attributes;
                _ram[high_table] &= static_cast<uint8_t>(~(3u << shift));
                _ram[high_table] |= static_cast<uint8_t>(((base_x & 0x100) ? 3u : 2u) << shift);
                output += 4;
                --remaining;
                shift = static_cast<uint8_t>((shift + 2) & 6);
                if (shift == 0)
                    ++high_table;
            }
        }
    }

    void cx4_t::transform_lines() noexcept
    {
        _x2 = read_local(0x1f83);
        _y2 = read_local(0x1f86);
        _distance = read_local(0x1f89);
        _scale = read_local(0x1f8c);
        uint32_t pointer{};
        for (int32_t count{read_word(0x1f80)}; count > 0; --count, pointer += 0x10)
        {
            _x = static_cast<int16_t>(read_word(pointer + 1));
            _y = static_cast<int16_t>(read_word(pointer + 5));
            _z = static_cast<int16_t>(read_word(pointer + 9));
            transform_perspective();
            write_word(pointer + 1, static_cast<uint16_t>(_x + 0x80));
            write_word(pointer + 5, static_cast<uint16_t>(_y + 0x50));
        }
        write_word(0x600, 23);
        write_word(0x602, 0x60);
        write_word(0x605, 0x40);
        write_word(0x608, 23);
        write_word(0x60a, 0x60);
        write_word(0x60d, 0x40);
        pointer = 0xb02;
        uint32_t out{};
        for (int32_t count{read_word(0xb00)}; count > 0; --count, pointer += 2, out += 8)
        {
            _x = static_cast<int16_t>(read_word((read_local(pointer) << 4) + 1));
            _y = static_cast<int16_t>(read_word((read_local(pointer) << 4) + 5));
            _x2 = static_cast<int16_t>(read_word((read_local(pointer + 1) << 4) + 1));
            _y2 = static_cast<int16_t>(read_word((read_local(pointer + 1) << 4) + 5));
            calculate_line();
            write_word(out + 0x600, _distance ? _distance : 1);
            write_word(out + 0x602, _x);
            write_word(out + 0x605, _y);
        }
    }

    void cx4_t::disintegrate() noexcept
    {
        const uint8_t width{read_local(0x1f89)}, height{read_local(0x1f8c)};
        const int32_t cx{static_cast<int16_t>(read_word(0x1f80))},
            cy{static_cast<int16_t>(read_word(0x1f83))};
        const int32_t sx{static_cast<int16_t>(read_word(0x1f86))},
            sy{static_cast<int16_t>(read_word(0x1f8f))};
        uint32_t start_x = static_cast<uint32_t>(-cx * sx + (cx << 8)),
                 start_y = static_cast<uint32_t>(-cy * sy + (cy << 8));
        std::fill_n(_ram.begin(), std::min<size_t>((width * height) >> 1u, _ram.size()), 0);
        uint32_t source = 0x600, y = start_y;
        for (int32_t row = 0; row < height; ++row, y += sy)
        {
            uint32_t x = start_x;
            for (int32_t column = 0; column < width; ++column, x += sx)
            {
                if ((x >> 8) < width && (y >> 8) < height && (y >> 8) * width + (x >> 8) < 0x2000)
                {
                    const uint8_t pixel =
                        (column & 1) ? static_cast<uint8_t>(_ram[source] >> 4) : _ram[source];
                    const uint32_t index =
                        (y >> 11) * width * 4 + (x >> 11) * 32 + ((y >> 8) & 7) * 2;
                    if (index + 17 < _ram.size())
                    {
                        const uint8_t mask = static_cast<uint8_t>(0x80u >> ((x >> 8) & 7));
                        if (pixel & 1)
                            _ram[index] |= mask;
                        if (pixel & 2)
                            _ram[index + 1] |= mask;
                        if (pixel & 4)
                            _ram[index + 16] |= mask;
                        if (pixel & 8)
                            _ram[index + 17] |= mask;
                    }
                }
                if (column & 1)
                    ++source;
            }
        }
    }

    void cx4_t::wave() noexcept
    {
        uint32_t destination{}, wave_pointer{read_local(0x1f83)};
        uint16_t mask1 = 0xc0c0, mask2 = 0x3f3f;
        for (int block = 0; block < 16; ++block)
        {
            for (int half = 0; half < 2; ++half)
            {
                do
                {
                    int16_t height =
                        static_cast<int16_t>(-static_cast<int8_t>(read_local(
                                                 static_cast<uint16_t>(wave_pointer + 0xb00))) -
                                             16);
                    for (uint16_t offset : k_wave_offsets)
                    {
                        uint16_t value =
                            read_word(static_cast<uint16_t>(destination + offset)) & mask2;
                        if (height >= 0)
                            value |= static_cast<uint16_t>(
                                mask1 & (height < 8 ? read_word(static_cast<uint16_t>(
                                                          0xa00 + half * 0x10 + height * 2))
                                                    : 0xff00));
                        write_word(static_cast<uint16_t>(destination + offset), value);
                        ++height;
                    }
                    wave_pointer = (wave_pointer + 1) & 0x7f;
                    mask1 = static_cast<uint16_t>((mask1 >> 2) | (mask1 << 6));
                    mask2 = static_cast<uint16_t>((mask2 >> 2) | (mask2 << 6));
                } while (mask1 != 0xc0c0);
                destination += 16;
            }
        }
    }

    void cx4_t::transform_perspective() noexcept
    {
        double x = _x, y = _y, z = static_cast<double>(_z) - 0x95;
        const auto rotate = [&](int16_t angle, double& a, double& b)
        {
            const double r = -angle * 2.0 * k_pi / 128.0;
            const double na = a * std::cos(r) - b * std::sin(r);
            b = a * std::sin(r) + b * std::cos(r);
            a = na;
        };
        rotate(_x2, y, z);
        double z2 = z;
        {
            const double r = -_y2 * 2.0 * k_pi / 128.0;
            const double nx = x * std::cos(r) + z2 * std::sin(r);
            z = x * -std::sin(r) + z2 * std::cos(r);
            x = nx;
        }
        rotate(_distance, x, y);
        const double denominator = 0x90 * (z + 0x95);
        if (denominator != 0.0)
        {
            _x = static_cast<int16_t>(x * _scale / denominator * 0x95);
            _y = static_cast<int16_t>(y * _scale / denominator * 0x95);
        }
    }

    void cx4_t::transform_vector() noexcept
    {
        double x = _x, y = _y, z = _z;
        const auto rotate = [&](int16_t angle, double& a, double& b)
        {
            const double r = -angle * 2.0 * k_pi / 128.0;
            const double na = a * std::cos(r) - b * std::sin(r);
            b = a * std::sin(r) + b * std::cos(r);
            a = na;
        };
        rotate(_x2, y, z);
        {
            const double r = -_y2 * 2.0 * k_pi / 128.0;
            const double nx = x * std::cos(r) + z * std::sin(r);
            z = x * -std::sin(r) + z * std::cos(r);
            x = nx;
        }
        rotate(_distance, x, y);
        _x = static_cast<int16_t>(x * _scale / 256.0);
        _y = static_cast<int16_t>(y * _scale / 256.0);
    }

    void cx4_t::calculate_line() noexcept
    {
        _x = static_cast<int16_t>(_x2 - _x);
        _y = static_cast<int16_t>(_y2 - _y);
        if (std::abs(_x) > std::abs(_y))
        {
            _distance = static_cast<int16_t>(std::abs(_x) + 1);
            _y = static_cast<int16_t>(256L * _y / std::abs(_x));
            _x = _x < 0 ? -256 : 256;
        }
        else if (_y != 0)
        {
            _distance = static_cast<int16_t>(std::abs(_y) + 1);
            _x = static_cast<int16_t>(256L * _x / std::abs(_y));
            _y = _y < 0 ? -256 : 256;
        }
        else
            _distance = 0;
    }

    void cx4_t::draw_wireframe() noexcept
    {
        uint32_t line{read_long(0x1f80)};
        for (int count = _ram[0x295]; count > 0; --count, line += 5)
        {
            uint32_t first;
            if (bus_read(line) == 0xff && bus_read(line + 1) == 0xff)
            {
                int32_t previous = static_cast<int32_t>(line) - 5;
                while (previous >= 0 && bus_read(previous + 2) == 0xff &&
                       bus_read(previous + 3) == 0xff)
                    previous -= 5;
                first = (static_cast<uint32_t>(read_local(0x1f82)) << 16) |
                        (static_cast<uint32_t>(bus_read(previous + 2)) << 8) |
                        bus_read(previous + 3);
            }
            else
                first = (static_cast<uint32_t>(read_local(0x1f82)) << 16) |
                        (static_cast<uint32_t>(bus_read(line)) << 8) | bus_read(line + 1);
            const uint32_t second = (static_cast<uint32_t>(read_local(0x1f82)) << 16) |
                                    (static_cast<uint32_t>(bus_read(line + 2)) << 8) |
                                    bus_read(line + 3);
            draw_line(static_cast<int16_t>((bus_read(first) << 8) | bus_read(first + 1)),
                      static_cast<int16_t>((bus_read(first + 2) << 8) | bus_read(first + 3)),
                      static_cast<int16_t>((bus_read(first + 4) << 8) | bus_read(first + 5)),
                      static_cast<int16_t>((bus_read(second) << 8) | bus_read(second + 1)),
                      static_cast<int16_t>((bus_read(second + 2) << 8) | bus_read(second + 3)),
                      static_cast<int16_t>((bus_read(second + 4) << 8) | bus_read(second + 5)),
                      bus_read(line + 4));
        }
    }

    void cx4_t::draw_line(int32_t x1,
                          int32_t y1,
                          int16_t z1,
                          int32_t x2,
                          int32_t y2,
                          int16_t z2,
                          uint8_t color) noexcept
    {
        _x = static_cast<int16_t>(x1);
        _y = static_cast<int16_t>(y1);
        _z = z1;
        _scale = read_local(0x1f90);
        _x2 = read_local(0x1f86);
        _y2 = read_local(0x1f87);
        _distance = read_local(0x1f88);
        transform_vector();
        x1 = (_x + 48) << 8;
        y1 = (_y + 48) << 8;
        _x = static_cast<int16_t>(x2);
        _y = static_cast<int16_t>(y2);
        _z = z2;
        transform_vector();
        x2 = (_x + 48) << 8;
        y2 = (_y + 48) << 8;
        _x = static_cast<int16_t>(x1 >> 8);
        _y = static_cast<int16_t>(y1 >> 8);
        _x2 = static_cast<int16_t>(x2 >> 8);
        _y2 = static_cast<int16_t>(y2 >> 8);
        calculate_line();
        x2 = _x;
        y2 = _y;
        for (int32_t count = _distance ? _distance : 1; count > 0; --count)
        {
            if (x1 > 0xff && y1 > 0xff && x1 < 0x6000 && y1 < 0x6000)
            {
                const uint16_t address =
                    static_cast<uint16_t>((((y1 >> 8) >> 3) << 8) - (((y1 >> 8) >> 3) << 6) +
                                          (((x1 >> 8) >> 3) << 4) + ((y1 >> 8) & 7) * 2);
                const uint8_t bit = static_cast<uint8_t>(0x80u >> ((x1 >> 8) & 7));
                _ram[address + 0x300] &= static_cast<uint8_t>(~bit);
                _ram[address + 0x301] &= static_cast<uint8_t>(~bit);
                if (color & 1)
                    _ram[address + 0x300] |= bit;
                if (color & 2)
                    _ram[address + 0x301] |= bit;
            }
            x1 += x2;
            y1 += y2;
        }
    }

    void cx4_t::scale_rotate(int row_padding) noexcept
    {
        int32_t xs = read_word(0x1f8f), ys = read_word(0x1f92);
        if (xs & 0x8000)
            xs = 0x7fff;
        if (ys & 0x8000)
            ys = 0x7fff;
        int16_t a{}, b{}, c{}, d{};
        const uint16_t angle = read_word(0x1f80);
        if (angle == 0)
        {
            a = xs;
            d = ys;
        }
        else if (angle == 128)
        {
            b = static_cast<int16_t>(-ys);
            c = xs;
        }
        else if (angle == 256)
        {
            a = static_cast<int16_t>(-xs);
            d = static_cast<int16_t>(-ys);
        }
        else if (angle == 384)
        {
            b = ys;
            c = static_cast<int16_t>(-xs);
        }
        else
        {
            a = static_cast<int16_t>(cos_fixed(angle) * xs >> 15);
            b = static_cast<int16_t>(-(sin_fixed(angle) * ys >> 15));
            c = static_cast<int16_t>(sin_fixed(angle) * xs >> 15);
            d = static_cast<int16_t>(cos_fixed(angle) * ys >> 15);
        }
        const uint8_t width = read_local(0x1f89) & ~7u, height = read_local(0x1f8c) & ~7u;
        std::fill_n(
            _ram.begin(), std::min<size_t>((width + row_padding / 4) * height / 2, _ram.size()), 0);
        const int32_t cx = static_cast<int16_t>(read_word(0x1f83)),
                      cy = static_cast<int16_t>(read_word(0x1f86));
        int32_t line_x = (cx << 12) - cx * a - cx * b, line_y = (cy << 12) - cy * c - cy * d,
                out = 0;
        uint8_t bit = 0x80;
        for (int y = 0; y < height; ++y)
        {
            uint32_t x = line_x, source_y = line_y;
            for (int column = 0; column < width; ++column)
            {
                uint8_t pixel = 0;
                if ((x >> 12) < width && (source_y >> 12) < height)
                {
                    const uint32_t address = (source_y >> 12) * width + (x >> 12);
                    pixel = read_local(static_cast<uint16_t>(0x600 + (address >> 1)));
                    if (address & 1)
                        pixel >>= 4;
                }
                if (out + 17 < static_cast<int>(_ram.size()))
                {
                    if (pixel & 1)
                        _ram[out] |= bit;
                    if (pixel & 2)
                        _ram[out + 1] |= bit;
                    if (pixel & 4)
                        _ram[out + 16] |= bit;
                    if (pixel & 8)
                        _ram[out + 17] |= bit;
                }
                bit >>= 1;
                if (bit == 0)
                {
                    bit = 0x80;
                    out += 32;
                }
                x += a;
                source_y += c;
            }
            out += 2 + row_padding;
            if (out & 0x10)
                out &= ~0x10;
            else
                out -= width * 4 + row_padding;
            line_x += b;
            line_y += d;
        }
    }
}
