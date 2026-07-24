//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/SuperFx.h"

namespace clover::core
{
    namespace
    {
        [[nodiscard]] constexpr int16_t signed_word(uint16_t value) noexcept
        {
            return static_cast<int16_t>(value);
        }

        [[nodiscard]] constexpr int8_t signed_byte(uint8_t value) noexcept
        {
            return static_cast<int8_t>(value);
        }
    }

    void super_fx_t::power_on(std::span<const uint8_t> rom,
                              std::span<uint8_t> ram) noexcept
    {
        _rom = rom;
        _ram = ram;
        _r.fill(0);
        _sfr = 0;
        _pbr = 0;
        _rombr = 0;
        _rambr = 0;
        _cbr = 0;
        _scbr = 0;
        _scmr = 0;
        _colr = 0;
        _por = 0;
        _bramr = 0;
        _vcr = 0x04;
        _cfgr = 0;
        _clsr = 0;
        _pipeline = 0x01;
        _last_ram_address = 0;
        _rom_data = 0;
        _rom_buffer_clocks = 0;
        _ram_buffer_clocks = 0;
        _source_register = 0;
        _destination_register = 0;
        _r14_modified = false;
        _r15_modified = false;
        _waiting_for_bus = false;
        _ram_written = false;
        _clock_credit = 0;
        _cache.fill(0);
        _cache_valid.fill(false);
        _pixel_cache = {};
        _pixel_cache[0].offset = 0xffffu;
        _pixel_cache[1].offset = 0xffffu;
    }

    void super_fx_t::step_master_clocks(master_clock_delta_t clocks) noexcept
    {
        _clock_credit += clocks;
        if (!running())
        {
            advance_buffer_clocks(clocks);
            _clock_credit = 0;
            return;
        }
        if (!opcode_bus_available())
        {
            advance_buffer_clocks(clocks);
            _clock_credit = 0;
            return;
        }

        // An instruction can finish slightly beyond this synchronization
        // boundary. The resulting negative credit is carried into the next
        // call, preserving long-term GSU/master-clock phase.
        while (running()
               && opcode_bus_available()
               && _clock_credit >= next_opcode_fetch_clocks())
        {
            _waiting_for_bus = false;
            execute_one();
            if (_waiting_for_bus)
            {
                _clock_credit = 0;
                break;
            }
        }
    }

    bool super_fx_t::running() const noexcept
    {
        return flag(k_flag_go);
    }

    bool super_fx_t::owns_rom_bus() const noexcept
    {
        return running() && (_scmr & 0x10u) != 0;
    }

    bool super_fx_t::owns_ram_bus() const noexcept
    {
        return running() && (_scmr & 0x08u) != 0;
    }

    bool super_fx_t::irq_pending() const noexcept
    {
        return flag(k_flag_irq) && (_cfgr & 0x80u) == 0;
    }

    bool super_fx_t::take_ram_written() noexcept
    {
        const bool result{ _ram_written };
        _ram_written = false;
        return result;
    }

    uint8_t super_fx_t::cpu_read_register(uint16_t address,
                                          uint8_t open_bus) noexcept
    {
        address = static_cast<uint16_t>(0x3000u | (address & 0x03ffu));
        if (address >= 0x3100u && address <= 0x32ffu)
            return read_cache(static_cast<uint16_t>(address - 0x3100u));

        if (address <= 0x301fu)
        {
            const uint8_t index{ static_cast<uint8_t>((address >> 1u) & 15u) };
            return static_cast<uint8_t>(_r[index] >> ((address & 1u) * 8u));
        }

        switch (address)
        {
        case 0x3030u:
            return static_cast<uint8_t>(_sfr & k_sfr_visible_mask);
        case 0x3031u:
        {
            const uint8_t result{ static_cast<uint8_t>(
                (_sfr & k_sfr_visible_mask) >> 8u
            ) };
            set_flag(k_flag_irq, false);
            return result;
        }
        case 0x3034u:
            return _pbr;
        case 0x3036u:
            return _rombr;
        case 0x303bu:
            return _vcr;
        case 0x303cu:
            return _rambr;
        case 0x303eu:
            return static_cast<uint8_t>(_cbr);
        case 0x303fu:
            return static_cast<uint8_t>(_cbr >> 8u);
        default:
            return open_bus;
        }
    }

    void super_fx_t::cpu_write_register(uint16_t address, uint8_t value) noexcept
    {
        address = static_cast<uint16_t>(0x3000u | (address & 0x03ffu));
        if (address >= 0x3100u && address <= 0x32ffu)
        {
            write_cache(static_cast<uint16_t>(address - 0x3100u), value);
            return;
        }

        if (address <= 0x301fu)
        {
            const uint8_t index{ static_cast<uint8_t>((address >> 1u) & 15u) };
            const uint16_t shifted{ static_cast<uint16_t>(
                static_cast<uint16_t>(value) << ((address & 1u) * 8u)
            ) };
            const uint16_t mask{ static_cast<uint16_t>((address & 1u) != 0 ? 0x00ffu : 0xff00u) };
            write_register(index, static_cast<uint16_t>((_r[index] & mask) | shifted));
            if (index == 14u)
                update_rom_buffer();
            if (address == 0x301fu)
                set_flag(k_flag_go, true);
            return;
        }

        switch (address)
        {
        case 0x3030u:
        {
            const bool was_running{ running() };
            _sfr = static_cast<uint16_t>((_sfr & 0xff00u) | value);
            if (was_running && !running())
            {
                _cbr = 0;
                invalidate_cache();
            }
            break;
        }
        case 0x3031u:
            _sfr = static_cast<uint16_t>((_sfr & 0x00ffu)
                | (static_cast<uint16_t>(value) << 8u));
            break;
        case 0x3033u:
            _bramr = static_cast<uint8_t>(value & 1u);
            break;
        case 0x3034u:
            _pbr = static_cast<uint8_t>(value & 0x7fu);
            invalidate_cache();
            break;
        case 0x3037u:
            _cfgr = static_cast<uint8_t>(value & 0xa0u);
            break;
        case 0x3038u:
            _scbr = value;
            break;
        case 0x3039u:
            _clsr = static_cast<uint8_t>(value & 1u);
            break;
        case 0x303au:
            _scmr = value;
            break;
        default:
            break;
        }
    }

    void super_fx_t::execute_one() noexcept
    {
        const uint8_t opcode{ peek_pipeline() };
        execute(opcode);

        if (_r14_modified)
        {
            _r14_modified = false;
            update_rom_buffer();
        }
        if (_r15_modified)
        {
            _r15_modified = false;
        }
        else
        {
            _r[15] = static_cast<uint16_t>(_r[15] + 1u);
        }
    }

    void super_fx_t::execute(uint8_t opcode) noexcept
    {
        const uint8_t n{ static_cast<uint8_t>(opcode & 0x0fu) };
        const uint8_t alt{ current_alt() };

        if (opcode == 0x00u)
        {
            if ((_cfgr & 0x80u) == 0)
                set_flag(k_flag_irq, true);
            set_flag(k_flag_go, false);
            _pipeline = 0x01u;
            reset_prefixes();
            return;
        }
        if (opcode == 0x01u)
        {
            reset_prefixes();
            return;
        }
        if (opcode == 0x02u)
        {
            const uint16_t base{ static_cast<uint16_t>(_r[15] & 0xfff0u) };
            if (_cbr != base)
            {
                _cbr = base;
                invalidate_cache();
            }
            reset_prefixes();
            return;
        }
        if (opcode == 0x03u)
        {
            set_flag(k_flag_carry, (source() & 1u) != 0);
            write_destination(static_cast<uint16_t>(source() >> 1u));
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode == 0x04u)
        {
            const bool carry{ (source() & 0x8000u) != 0 };
            write_destination(static_cast<uint16_t>((source() << 1u)
                | (flag(k_flag_carry) ? 1u : 0u)));
            set_sz(_r[_destination_register]);
            set_flag(k_flag_carry, carry);
            reset_prefixes();
            return;
        }
        if (opcode >= 0x05u && opcode <= 0x0fu)
        {
            const int8_t displacement{ signed_byte(immediate_byte()) };
            bool take{ false };
            switch (opcode)
            {
            case 0x05u: take = true; break;
            case 0x06u: take = flag(k_flag_sign) == flag(k_flag_overflow); break;
            case 0x07u: take = flag(k_flag_sign) != flag(k_flag_overflow); break;
            case 0x08u: take = !flag(k_flag_zero); break;
            case 0x09u: take = flag(k_flag_zero); break;
            case 0x0au: take = !flag(k_flag_sign); break;
            case 0x0bu: take = flag(k_flag_sign); break;
            case 0x0cu: take = !flag(k_flag_carry); break;
            case 0x0du: take = flag(k_flag_carry); break;
            case 0x0eu: take = !flag(k_flag_overflow); break;
            case 0x0fu: take = flag(k_flag_overflow); break;
            default: break;
            }
            if (take)
                write_register(15u, static_cast<uint16_t>(_r[15] + displacement));
            return;
        }
        if (opcode >= 0x10u && opcode <= 0x1fu)
        {
            if (!flag(k_flag_with))
                _destination_register = n;
            else
            {
                write_register(n, source());
                reset_prefixes();
            }
            return;
        }
        if (opcode >= 0x20u && opcode <= 0x2fu)
        {
            _source_register = n;
            _destination_register = n;
            set_flag(k_flag_with, true);
            return;
        }
        if (opcode >= 0x30u && opcode <= 0x3bu)
        {
            _last_ram_address = _r[n];
            write_ram(_last_ram_address, static_cast<uint8_t>(source()));
            if (!flag(k_flag_alt1))
                write_ram(static_cast<uint16_t>(_last_ram_address ^ 1u),
                          static_cast<uint8_t>(source() >> 8u));
            reset_prefixes();
            return;
        }
        if (opcode == 0x3cu)
        {
            write_register(12u, static_cast<uint16_t>(_r[12] - 1u));
            set_sz(_r[12]);
            if (_r[12] != 0)
                write_register(15u, _r[13]);
            reset_prefixes();
            return;
        }
        if (opcode == 0x3du || opcode == 0x3eu || opcode == 0x3fu)
        {
            set_flag(k_flag_with, false);
            if (opcode != 0x3eu)
                set_flag(k_flag_alt1, true);
            if (opcode != 0x3du)
                set_flag(k_flag_alt2, true);
            return;
        }
        if (opcode >= 0x40u && opcode <= 0x4bu)
        {
            _last_ram_address = _r[n];
            uint16_t value{ read_ram(_last_ram_address) };
            if (!flag(k_flag_alt1))
                value |= static_cast<uint16_t>(
                    read_ram(static_cast<uint16_t>(_last_ram_address ^ 1u))
                ) << 8u;
            write_destination(value);
            reset_prefixes();
            return;
        }
        if (opcode == 0x4cu)
        {
            if (!flag(k_flag_alt1))
            {
                plot(static_cast<uint8_t>(_r[1]), static_cast<uint8_t>(_r[2]));
                write_register(1u, static_cast<uint16_t>(_r[1] + 1u));
            }
            else
            {
                write_destination(read_pixel(static_cast<uint8_t>(_r[1]),
                                             static_cast<uint8_t>(_r[2])));
                set_sz(_r[_destination_register]);
            }
            reset_prefixes();
            return;
        }
        if (opcode == 0x4du)
        {
            write_destination(static_cast<uint16_t>((source() >> 8u) | (source() << 8u)));
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode == 0x4eu)
        {
            if (!flag(k_flag_alt1))
                _colr = apply_color_mode(static_cast<uint8_t>(source()));
            else
                _por = static_cast<uint8_t>(source() & 0x1fu);
            reset_prefixes();
            return;
        }
        if (opcode == 0x4fu)
        {
            write_destination(static_cast<uint16_t>(~source()));
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode >= 0x50u && opcode <= 0x5fu)
        {
            const uint16_t operand{ static_cast<uint16_t>(
                flag(k_flag_alt2) ? n : _r[n]
            ) };
            const uint32_t left{ source() };
            const uint32_t carry{ flag(k_flag_alt1) && flag(k_flag_carry) ? 1u : 0u };
            const uint32_t result{ left + operand + carry };
            write_destination(static_cast<uint16_t>(result));
            set_flag(k_flag_overflow,
                     ((~(left ^ operand) & (operand ^ result)) & 0x8000u) != 0);
            set_flag(k_flag_carry, result > 0xffffu);
            set_sz(static_cast<uint16_t>(result));
            reset_prefixes();
            return;
        }
        if (opcode >= 0x60u && opcode <= 0x6fu)
        {
            const uint16_t operand{ static_cast<uint16_t>(
                (!flag(k_flag_alt2) || flag(k_flag_alt1)) ? _r[n] : n
            ) };
            const int32_t borrow{ !flag(k_flag_alt2) && flag(k_flag_alt1)
                && !flag(k_flag_carry) ? 1 : 0 };
            const int32_t left{ source() };
            const int32_t result{ left - operand - borrow };
            set_flag(k_flag_overflow,
                     (((left ^ operand) & (left ^ result)) & 0x8000) != 0);
            set_flag(k_flag_carry, result >= 0);
            set_sz(static_cast<uint16_t>(result));
            if (alt != 3u)
                write_destination(static_cast<uint16_t>(result));
            reset_prefixes();
            return;
        }
        if (opcode == 0x70u)
        {
            const uint16_t value{ static_cast<uint16_t>(
                (_r[7] & 0xff00u) | (_r[8] >> 8u)
            ) };
            write_destination(value);
            set_flag(k_flag_overflow, (value & 0xc0c0u) != 0);
            set_flag(k_flag_sign, (value & 0x8080u) != 0);
            set_flag(k_flag_carry, (value & 0xe0e0u) != 0);
            set_flag(k_flag_zero, (value & 0xf0f0u) != 0);
            reset_prefixes();
            return;
        }
        if (opcode >= 0x71u && opcode <= 0x7fu)
        {
            const uint16_t operand{ static_cast<uint16_t>(
                flag(k_flag_alt2) ? n : _r[n]
            ) };
            const uint16_t value{ static_cast<uint16_t>(
                source() & (flag(k_flag_alt1) ? ~operand : operand)
            ) };
            write_destination(value);
            set_sz(value);
            reset_prefixes();
            return;
        }
        if (opcode >= 0x80u && opcode <= 0x8fu)
        {
            const uint8_t operand{ static_cast<uint8_t>(
                flag(k_flag_alt2) ? n : _r[n]
            ) };
            uint16_t result{};
            if (flag(k_flag_alt1))
                result = static_cast<uint16_t>(static_cast<uint8_t>(source()) * operand);
            else
                result = static_cast<uint16_t>(
                    signed_byte(static_cast<uint8_t>(source()))
                    * signed_byte(operand)
                );
            write_destination(result);
            set_sz(result);
            reset_prefixes();
            if ((_cfgr & 0x20u) == 0)
                consume(_clsr != 0 ? 1u : 2u);
            return;
        }
        if (opcode == 0x90u)
        {
            write_ram(_last_ram_address, static_cast<uint8_t>(source()));
            write_ram(static_cast<uint16_t>(_last_ram_address ^ 1u),
                      static_cast<uint8_t>(source() >> 8u));
            reset_prefixes();
            return;
        }
        if (opcode >= 0x91u && opcode <= 0x94u)
        {
            write_register(11u, static_cast<uint16_t>(_r[15] + n));
            reset_prefixes();
            return;
        }
        if (opcode == 0x95u)
        {
            write_destination(static_cast<uint16_t>(
                static_cast<int16_t>(signed_byte(static_cast<uint8_t>(source())))
            ));
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode == 0x96u)
        {
            const uint16_t original{ source() };
            set_flag(k_flag_carry, (original & 1u) != 0);
            int32_t result{ signed_word(original) >> 1u };
            if (flag(k_flag_alt1) && original == 0xffffu)
                result += 1;
            write_destination(static_cast<uint16_t>(result));
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode == 0x97u)
        {
            const uint16_t original{ source() };
            write_destination(static_cast<uint16_t>((original >> 1u)
                | (flag(k_flag_carry) ? 0x8000u : 0u)));
            set_flag(k_flag_carry, (original & 1u) != 0);
            set_sz(_r[_destination_register]);
            reset_prefixes();
            return;
        }
        if (opcode >= 0x98u && opcode <= 0x9du)
        {
            if (!flag(k_flag_alt1))
            {
                write_register(15u, _r[n]);
            }
            else
            {
                _pbr = static_cast<uint8_t>(_r[n] & 0x7fu);
                write_register(15u, source());
                _cbr = static_cast<uint16_t>(_r[15] & 0xfff0u);
                invalidate_cache();
            }
            reset_prefixes();
            return;
        }
        if (opcode == 0x9eu)
        {
            write_destination(static_cast<uint16_t>(source() & 0xffu));
            set_flag(k_flag_sign, (_r[_destination_register] & 0x0080u) != 0);
            set_flag(k_flag_zero, _r[_destination_register] == 0);
            reset_prefixes();
            return;
        }
        if (opcode == 0x9fu)
        {
            const int32_t product{ static_cast<int32_t>(signed_word(source()))
                * static_cast<int32_t>(signed_word(_r[6])) };
            if (flag(k_flag_alt1))
                write_register(4u, static_cast<uint16_t>(product));
            write_destination(static_cast<uint16_t>(
                static_cast<uint32_t>(product) >> 16u
            ));
            set_sz(_r[_destination_register]);
            set_flag(k_flag_carry, (product & 0x8000) != 0);
            reset_prefixes();
            consume(((_cfgr & 0x20u) != 0 ? 3u : 7u) * (_clsr != 0 ? 1u : 2u));
            return;
        }
        if (opcode >= 0xa0u && opcode <= 0xafu)
        {
            if (flag(k_flag_alt1))
            {
                _last_ram_address = static_cast<uint16_t>(immediate_byte() << 1u);
                const uint16_t value{ static_cast<uint16_t>(
                    read_ram(_last_ram_address)
                    | (static_cast<uint16_t>(
                        read_ram(static_cast<uint16_t>(_last_ram_address ^ 1u))
                    ) << 8u)
                ) };
                write_register(n, value);
            }
            else if (flag(k_flag_alt2))
            {
                _last_ram_address = static_cast<uint16_t>(immediate_byte() << 1u);
                write_ram(_last_ram_address, static_cast<uint8_t>(_r[n]));
                write_ram(static_cast<uint16_t>(_last_ram_address ^ 1u),
                          static_cast<uint8_t>(_r[n] >> 8u));
            }
            else
            {
                write_register(n, static_cast<uint16_t>(
                    static_cast<int16_t>(signed_byte(immediate_byte()))
                ));
            }
            reset_prefixes();
            return;
        }
        if (opcode >= 0xb0u && opcode <= 0xbfu)
        {
            if (!flag(k_flag_with))
            {
                _source_register = n;
            }
            else
            {
                write_destination(_r[n]);
                set_flag(k_flag_overflow, (_r[_destination_register] & 0x0080u) != 0);
                set_sz(_r[_destination_register]);
                reset_prefixes();
            }
            return;
        }
        if (opcode == 0xc0u)
        {
            write_destination(static_cast<uint16_t>(source() >> 8u));
            set_flag(k_flag_sign, (_r[_destination_register] & 0x80u) != 0);
            set_flag(k_flag_zero, _r[_destination_register] == 0);
            reset_prefixes();
            return;
        }
        if (opcode >= 0xc1u && opcode <= 0xcfu)
        {
            const uint16_t operand{ static_cast<uint16_t>(
                flag(k_flag_alt2) ? n : _r[n]
            ) };
            const uint16_t value{ static_cast<uint16_t>(
                flag(k_flag_alt1) ? source() ^ operand : source() | operand
            ) };
            write_destination(value);
            set_sz(value);
            reset_prefixes();
            return;
        }
        if (opcode >= 0xd0u && opcode <= 0xdeu)
        {
            write_register(n, static_cast<uint16_t>(_r[n] + 1u));
            set_sz(_r[n]);
            reset_prefixes();
            return;
        }
        if (opcode == 0xdfu)
        {
            if (!flag(k_flag_alt2))
                _colr = apply_color_mode(read_rom_buffer());
            else if (!flag(k_flag_alt1))
            {
                synchronize_ram_buffer();
                _rambr = static_cast<uint8_t>(source() & 1u);
            }
            else
            {
                synchronize_rom_buffer();
                _rombr = static_cast<uint8_t>(source() & 0x7fu);
            }
            reset_prefixes();
            return;
        }
        if (opcode >= 0xe0u && opcode <= 0xeeu)
        {
            write_register(n, static_cast<uint16_t>(_r[n] - 1u));
            set_sz(_r[n]);
            reset_prefixes();
            return;
        }
        if (opcode == 0xefu)
        {
            const uint8_t data{ read_rom_buffer() };
            uint16_t value{};
            switch (alt)
            {
            case 0u: value = data; break;
            case 1u: value = static_cast<uint16_t>((data << 8u) | (source() & 0xffu)); break;
            case 2u: value = static_cast<uint16_t>((source() & 0xff00u) | data); break;
            case 3u:
                value = static_cast<uint16_t>(
                    static_cast<int16_t>(signed_byte(data))
                );
                break;
            default: break;
            }
            write_destination(value);
            reset_prefixes();
            return;
        }
        if (opcode >= 0xf0u)
        {
            if (flag(k_flag_alt1))
            {
                _last_ram_address = immediate_byte();
                _last_ram_address |= static_cast<uint16_t>(immediate_byte()) << 8u;
                const uint16_t value{ static_cast<uint16_t>(
                    read_ram(_last_ram_address)
                    | (static_cast<uint16_t>(
                        read_ram(static_cast<uint16_t>(_last_ram_address ^ 1u))
                    ) << 8u)
                ) };
                write_register(n, value);
            }
            else if (flag(k_flag_alt2))
            {
                _last_ram_address = immediate_byte();
                _last_ram_address |= static_cast<uint16_t>(immediate_byte()) << 8u;
                write_ram(_last_ram_address, static_cast<uint8_t>(_r[n]));
                write_ram(static_cast<uint16_t>(_last_ram_address ^ 1u),
                          static_cast<uint8_t>(_r[n] >> 8u));
            }
            else
            {
                const uint16_t low{ immediate_byte() };
                write_register(n, static_cast<uint16_t>(
                    low | (static_cast<uint16_t>(immediate_byte()) << 8u)
                ));
            }
            reset_prefixes();
        }
    }

    uint16_t super_fx_t::source() const noexcept
    {
        return _r[_source_register];
    }

    void super_fx_t::write_destination(uint16_t value) noexcept
    {
        write_register(_destination_register, value);
    }

    void super_fx_t::write_register(uint8_t index, uint16_t value) noexcept
    {
        index &= 15u;
        _r[index] = value;
        if (index == 14u)
            _r14_modified = true;
        if (index == 15u)
            _r15_modified = true;
    }

    void super_fx_t::reset_prefixes() noexcept
    {
        set_flag(k_flag_with, false);
        set_flag(k_flag_alt1, false);
        set_flag(k_flag_alt2, false);
        _source_register = 0;
        _destination_register = 0;
    }

    void super_fx_t::set_sz(uint16_t value) noexcept
    {
        set_flag(k_flag_sign, (value & 0x8000u) != 0);
        set_flag(k_flag_zero, value == 0);
    }

    uint8_t super_fx_t::current_alt() const noexcept
    {
        return static_cast<uint8_t>((flag(k_flag_alt2) ? 2u : 0u)
            | (flag(k_flag_alt1) ? 1u : 0u));
    }

    bool super_fx_t::flag(uint16_t mask) const noexcept
    {
        return (_sfr & mask) != 0;
    }

    void super_fx_t::set_flag(uint16_t mask, bool value) noexcept
    {
        if (value)
            _sfr |= mask;
        else
            _sfr &= static_cast<uint16_t>(~mask);
    }

    void super_fx_t::consume(uint32_t clocks) noexcept
    {
        advance_buffer_clocks(clocks);
        _clock_credit -= static_cast<int64_t>(clocks);
    }

    void super_fx_t::advance_buffer_clocks(uint32_t clocks) noexcept
    {
        _rom_buffer_clocks = static_cast<uint8_t>(
            clocks >= _rom_buffer_clocks ? 0u : _rom_buffer_clocks - clocks
        );
        _ram_buffer_clocks = static_cast<uint8_t>(
            clocks >= _ram_buffer_clocks ? 0u : _ram_buffer_clocks - clocks
        );
        set_flag(k_flag_rom_pending, _rom_buffer_clocks != 0u);
    }

    void super_fx_t::synchronize_rom_buffer() noexcept
    {
        if (_rom_buffer_clocks != 0u)
            consume(_rom_buffer_clocks);
    }

    void super_fx_t::synchronize_ram_buffer() noexcept
    {
        if (_ram_buffer_clocks != 0u)
            consume(_ram_buffer_clocks);
    }

    uint32_t super_fx_t::next_opcode_fetch_clocks() const noexcept
    {
        const uint16_t relative{ static_cast<uint16_t>(_r[15] - _cbr) };
        if (relative < 512u)
            return _cache_valid[relative >> 4u]
                ? (_clsr != 0 ? 1u : 2u)
                : 16u * (_clsr != 0 ? 5u : 6u);
        return _clsr != 0 ? 5u : 6u;
    }

    bool super_fx_t::opcode_bus_available() const noexcept
    {
        const uint16_t relative{ static_cast<uint16_t>(_r[15] - _cbr) };
        if (relative < 512u && _cache_valid[relative >> 4u])
            return true;
        return _pbr <= 0x5fu
            ? (_scmr & 0x10u) != 0
            : (_scmr & 0x08u) != 0;
    }

    uint8_t super_fx_t::fetch_opcode(uint16_t address) noexcept
    {
        const uint16_t relative{ static_cast<uint16_t>(address - _cbr) };
        if (relative < 512u)
        {
            const uint8_t block{ static_cast<uint8_t>(relative >> 4u) };
            if (!_cache_valid[block])
            {
                uint16_t destination{ static_cast<uint16_t>(relative & 0xfff0u) };
                uint32_t source_address{ (static_cast<uint32_t>(_pbr) << 16u)
                    | static_cast<uint16_t>((_cbr + destination) & 0xfff0u) };
                for (uint8_t byte{ 0 }; byte < 16u; ++byte)
                {
                    consume(_clsr != 0 ? 5u : 6u);
                    _cache[destination++ & 0x01ffu] = read_memory(source_address++);
                }
                _cache_valid[block] = true;
            }
            else
            {
                consume(_clsr != 0 ? 1u : 2u);
            }
            return _cache[relative & 0x01ffu];
        }

        if (_pbr <= 0x5fu)
            synchronize_rom_buffer();
        else
            synchronize_ram_buffer();
        consume(_clsr != 0 ? 5u : 6u);
        return read_memory((static_cast<uint32_t>(_pbr) << 16u) | address);
    }

    uint8_t super_fx_t::peek_pipeline() noexcept
    {
        const uint8_t result{ _pipeline };
        _pipeline = fetch_opcode(_r[15]);
        _r15_modified = false;
        return result;
    }

    uint8_t super_fx_t::immediate_byte() noexcept
    {
        const uint8_t result{ _pipeline };
        _r[15] = static_cast<uint16_t>(_r[15] + 1u);
        _pipeline = fetch_opcode(_r[15]);
        _r15_modified = false;
        return result;
    }

    void super_fx_t::invalidate_cache() noexcept
    {
        _cache_valid.fill(false);
    }

    uint8_t super_fx_t::read_cache(uint16_t address) const noexcept
    {
        return _cache[(address + _cbr) & 0x01ffu];
    }

    void super_fx_t::write_cache(uint16_t address, uint8_t value) noexcept
    {
        const uint16_t target{ static_cast<uint16_t>((address + _cbr) & 0x01ffu) };
        _cache[target] = value;
        if ((target & 15u) == 15u)
            _cache_valid[target >> 4u] = true;
    }

    uint8_t super_fx_t::read_memory(uint32_t address) noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        if (bank <= 0x3fu)
        {
            if ((_scmr & 0x10u) == 0)
            {
                _waiting_for_bus = true;
                return 0;
            }
            if (_rom.empty())
                return 0;
            const uint32_t linear{ (static_cast<uint32_t>(bank) << 15u)
                | (offset & 0x7fffu) };
            return _rom[linear % _rom.size()];
        }
        if (bank >= 0x40u && bank <= 0x5fu)
        {
            if ((_scmr & 0x10u) == 0)
            {
                _waiting_for_bus = true;
                return 0;
            }
            if (_rom.empty())
                return 0;
            const uint32_t linear{ address & 0x1fffffu };
            return _rom[linear % _rom.size()];
        }
        if (bank >= 0x60u && bank <= 0x7fu)
        {
            if ((_scmr & 0x08u) == 0)
            {
                _waiting_for_bus = true;
                return 0;
            }
            if (_ram.empty())
                return 0;
            return _ram[address % _ram.size()];
        }
        return 0;
    }

    void super_fx_t::write_memory(uint32_t address, uint8_t value) noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        if (bank < 0x60u || bank > 0x7fu)
            return;
        if ((_scmr & 0x08u) == 0)
        {
            _waiting_for_bus = true;
            return;
        }
        if (!_ram.empty())
        {
            uint8_t& destination{ _ram[address % _ram.size()] };
            if (destination != value)
            {
                destination = value;
                _ram_written = true;
            }
        }
    }

    uint8_t super_fx_t::read_ram(uint16_t address) noexcept
    {
        synchronize_ram_buffer();
        return read_memory(0x700000u
            | (static_cast<uint32_t>(_rambr) << 16u) | address);
    }

    void super_fx_t::write_ram(uint16_t address, uint8_t value) noexcept
    {
        synchronize_ram_buffer();
        write_memory(0x700000u
            | (static_cast<uint32_t>(_rambr) << 16u) | address, value);
        _ram_buffer_clocks = static_cast<uint8_t>(_clsr != 0 ? 5u : 6u);
    }

    uint8_t super_fx_t::read_rom_buffer() noexcept
    {
        synchronize_rom_buffer();
        return _rom_data;
    }

    void super_fx_t::update_rom_buffer() noexcept
    {
        set_flag(k_flag_rom_pending, true);
        _rom_data = read_memory((static_cast<uint32_t>(_rombr) << 16u) | _r[14]);
        _rom_buffer_clocks = static_cast<uint8_t>(_clsr != 0 ? 5u : 6u);
    }

    uint8_t super_fx_t::apply_color_mode(uint8_t value) const noexcept
    {
        if ((_por & 0x04u) != 0)
            return static_cast<uint8_t>((_colr & 0xf0u) | (value >> 4u));
        if ((_por & 0x08u) != 0)
            return static_cast<uint8_t>((_colr & 0xf0u) | (value & 0x0fu));
        return value;
    }

    uint8_t super_fx_t::bits_per_pixel() const noexcept
    {
        static constexpr std::array<uint8_t, 4> bpp{ 2u, 4u, 4u, 8u };
        return bpp[_scmr & 3u];
    }

    uint32_t super_fx_t::pixel_address(uint8_t x, uint8_t y) const noexcept
    {
        const uint8_t height_mode{ static_cast<uint8_t>(
            (_por & 0x10u) != 0 ? 3u
            : (((_scmr & 0x20u) != 0 ? 2u : 0u) | ((_scmr & 0x04u) != 0 ? 1u : 0u))
        ) };
        uint16_t character{};
        switch (height_mode)
        {
        case 0u:
            character = static_cast<uint16_t>(((x & 0xf8u) << 1u) + ((y & 0xf8u) >> 3u));
            break;
        case 1u:
            character = static_cast<uint16_t>(((x & 0xf8u) << 1u)
                + ((x & 0xf8u) >> 1u) + ((y & 0xf8u) >> 3u));
            break;
        case 2u:
            character = static_cast<uint16_t>(((x & 0xf8u) << 1u)
                + (x & 0xf8u) + ((y & 0xf8u) >> 3u));
            break;
        default:
            character = static_cast<uint16_t>(((y & 0x80u) << 2u)
                + ((x & 0x80u) << 1u) + ((y & 0x78u) << 1u)
                + ((x & 0x78u) >> 3u));
            break;
        }
        return 0x700000u + static_cast<uint32_t>(character) * bits_per_pixel() * 8u
            + (static_cast<uint32_t>(_scbr) << 10u) + (y & 7u) * 2u;
    }

    void super_fx_t::plot(uint8_t x, uint8_t y) noexcept
    {
        if ((_por & 1u) == 0)
        {
            const bool eight_bpp{ (_scmr & 3u) == 3u };
            const bool transparent{ eight_bpp && (_por & 0x08u) == 0
                ? _colr == 0
                : (_colr & 0x0fu) == 0 };
            if (transparent)
                return;
        }

        uint8_t color{ _colr };
        if ((_por & 0x02u) != 0 && (_scmr & 3u) != 3u)
        {
            if (((x ^ y) & 1u) != 0)
                color >>= 4u;
            color &= 0x0fu;
        }

        const uint16_t offset{ static_cast<uint16_t>((y << 5u) + (x >> 3u)) };
        if (_pixel_cache[0].offset != offset)
        {
            flush_pixel_cache(_pixel_cache[1]);
            _pixel_cache[1] = _pixel_cache[0];
            _pixel_cache[0].offset = offset;
            _pixel_cache[0].pending = 0;
        }
        const uint8_t bit{ static_cast<uint8_t>((x & 7u) ^ 7u) };
        _pixel_cache[0].pixels[bit] = color;
        _pixel_cache[0].pending |= static_cast<uint8_t>(1u << bit);
        if (_pixel_cache[0].pending == 0xffu)
        {
            flush_pixel_cache(_pixel_cache[1]);
            _pixel_cache[1] = _pixel_cache[0];
            _pixel_cache[0].pending = 0;
        }
    }

    uint8_t super_fx_t::read_pixel(uint8_t x, uint8_t y) noexcept
    {
        flush_pixel_cache(_pixel_cache[1]);
        flush_pixel_cache(_pixel_cache[0]);
        const uint32_t base{ pixel_address(x, y) };
        const uint8_t bit{ static_cast<uint8_t>((x & 7u) ^ 7u) };
        uint8_t color{};
        for (uint8_t plane{ 0 }; plane < bits_per_pixel(); ++plane)
        {
            const uint8_t byte_offset{ static_cast<uint8_t>(
                (plane >> 1u) * 16u + (plane & 1u)
            ) };
            consume(_clsr != 0 ? 5u : 6u);
            color |= static_cast<uint8_t>(
                ((read_memory(base + byte_offset) >> bit) & 1u) << plane
            );
        }
        return color;
    }

    void super_fx_t::flush_pixel_cache(pixel_cache_t& cache) noexcept
    {
        if (cache.pending == 0)
            return;

        const uint8_t x{ static_cast<uint8_t>(cache.offset << 3u) };
        const uint8_t y{ static_cast<uint8_t>(cache.offset >> 5u) };
        const uint32_t base{ pixel_address(x, y) };
        for (uint8_t plane{ 0 }; plane < bits_per_pixel(); ++plane)
        {
            const uint8_t byte_offset{ static_cast<uint8_t>(
                (plane >> 1u) * 16u + (plane & 1u)
            ) };
            uint8_t planar{};
            for (uint8_t pixel{ 0 }; pixel < 8u; ++pixel)
                planar |= static_cast<uint8_t>(((cache.pixels[pixel] >> plane) & 1u) << pixel);
            if (cache.pending != 0xffu)
            {
                consume(_clsr != 0 ? 5u : 6u);
                planar = static_cast<uint8_t>((planar & cache.pending)
                    | (read_memory(base + byte_offset) & ~cache.pending));
            }
            consume(_clsr != 0 ? 5u : 6u);
            write_memory(base + byte_offset, planar);
        }
        cache.pending = 0;
    }
}
