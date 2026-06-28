//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Ppu.h"

#include <algorithm>

namespace
{
    constexpr std::array<uint8_t, 4> k_vram_increment_sizes{ 1, 32, 128, 128 };

    void set_window_bits(bool& one_invert,
                         bool& one_enable,
                         bool& two_invert,
                         bool& two_enable,
                         uint8_t data,
                         uint8_t shift) noexcept
    {
        one_invert = ((data >> shift) & 0x01u) != 0;
        one_enable = ((data >> (shift + 1u)) & 0x01u) != 0;
        two_invert = ((data >> (shift + 2u)) & 0x01u) != 0;
        two_enable = ((data >> (shift + 3u)) & 0x01u) != 0;
    }

    [[nodiscard]] bool crossed_raster_point(const clover::core::timing_snapshot_t& previous_timing,
                                            const clover::core::timing_snapshot_t& current_timing,
                                            uint16_t target_scanline,
                                            uint16_t target_dot) noexcept
    {
        const bool wrapped_frame{
            current_timing.master_clock >= previous_timing.master_clock
            && current_timing.raster.scanline < previous_timing.raster.scanline
        };

        if (wrapped_frame)
        {
            if (target_scanline > previous_timing.raster.scanline)
                return true;

            if (target_scanline == previous_timing.raster.scanline
                && target_dot >= previous_timing.raster.dot)
                return true;

            if (target_scanline < current_timing.raster.scanline)
                return true;

            if (target_scanline == current_timing.raster.scanline && target_dot <= current_timing.raster.dot)
                return true;

            return false;
        }

        if (target_scanline < previous_timing.raster.scanline
            || target_scanline > current_timing.raster.scanline)
            return false;

        if (target_scanline == previous_timing.raster.scanline && target_dot < previous_timing.raster.dot)
            return false;

        if (target_scanline == current_timing.raster.scanline && target_dot > current_timing.raster.dot)
            return false;

        return previous_timing.master_clock != current_timing.master_clock;
    }

    [[nodiscard]] uint16_t mapped_vram_address(uint16_t address, uint8_t mapping) noexcept
    {
        switch (mapping & 0x03u)
        {
        case 0u:
            return address;
        case 1u:
            return static_cast<uint16_t>((address & 0xff00u) | ((address << 3u) & 0x00f8u) | ((address >> 5u) & 0x0007u));
        case 2u:
            return static_cast<uint16_t>((address & 0xfe00u) | ((address << 3u) & 0x01f8u) | ((address >> 6u) & 0x0007u));
        case 3u:
            return static_cast<uint16_t>((address & 0xfc00u) | ((address << 3u) & 0x03f8u) | ((address >> 7u) & 0x0007u));
        default:
            return address;
        }
    }

} // anonymous namespace

namespace clover::core
{
    uint16_t ppu_t::active_visible_scanlines() const noexcept
    {
        return _video_timing.active_visible_scanlines(_screen_state.overscan);
    }

    bool ppu_t::display_active_for_oam() const noexcept
    {
        if (_display.disabled)
            return false;

        const timing_snapshot_t snapshot{ timing() };
        return snapshot.raster.scanline < active_visible_scanlines();
    }

    bool ppu_t::display_active_for_vram() const noexcept
    {
        if (_display.disabled)
            return false;

        const timing_snapshot_t snapshot{ timing() };
        return snapshot.raster.scanline < active_visible_scanlines();
    }

    bool ppu_t::display_active_for_cgram() const noexcept
    {
        if (_display.disabled)
            return false;

        const timing_snapshot_t snapshot{ timing() };
        return snapshot.raster.scanline > 0
            && snapshot.raster.scanline < active_visible_scanlines()
            && snapshot.raster.dot >= 88
            && snapshot.raster.dot < 1096;
    }

    void ppu_t::power_on() noexcept
    {
        reset();
        render_placeholder_frame();
    }

    void ppu_t::reset() noexcept
    {
        _composed_frame.clear();
        std::fill(_registers.begin(), _registers.end(), 0);
        std::fill(_vram.begin(), _vram.end(), 0);
        std::fill(_oam.begin(), _oam.end(), 0);
        std::fill(_cgram.begin(), _cgram.end(), 0);
        _counter.reset();
        _frame_counter = 0;
        _display = {};
        _oam_state = {};
        _bg_state = {};
        _scroll_latches = {};
        _mosaic_state = {};
        _window_state = {};
        _object_layer_state = {};
        _color_math_state = {};
        _screen_state = {};
        _compositor_state = {};
        decode_render_state();
        _vram_state = {};
        _vram_state.increment_size = 1;
        _cgram_state = {};
        _counter_latch = {};
        _ppu1_mdr = 0;
        _ppu2_mdr = 0;
        _oam_state.latched_address = 0;
    }

    video_standard_t ppu_t::video_standard() const noexcept
    {
        return _video_timing.standard;
    }

    const video_timing_t& ppu_t::video_timing() const noexcept
    {
        return _video_timing;
    }

    uint16_t ppu_t::address_vram() const noexcept
    {
        return mapped_vram_address(_vram_state.address, _vram_state.mapping);
    }

    uint16_t ppu_t::read_vram_word() const noexcept
    {
        if (display_active_for_vram())
            return 0;

        return _vram[address_vram() & 0x7fffu];
    }

    void ppu_t::write_vram_byte(bool high_byte, uint8_t value) noexcept
    {
        if (display_active_for_vram())
            return;

        uint16_t& word{ _vram[address_vram() & 0x7fffu] };
        if (high_byte)
            word = static_cast<uint16_t>((word & 0x00ffu) | (value << 8u));
        else
            word = static_cast<uint16_t>((word & 0xff00u) | value);
    }

    uint8_t ppu_t::read_oam_byte(uint16_t address) const noexcept
    {
        if (display_active_for_oam())
            address = _oam_state.latched_address;

        return _oam[address % _oam.size()];
    }

    void ppu_t::write_oam_byte(uint16_t address, uint8_t value) noexcept
    {
        if (display_active_for_oam())
            address = _oam_state.latched_address;

        _oam[address % _oam.size()] = value;
    }

    uint8_t ppu_t::read_cgram_byte(bool high_byte, uint8_t address) const noexcept
    {
        if (display_active_for_cgram())
            address = _cgram_state.address;

        const uint16_t color{ _cgram[address] };
        if (high_byte)
            return static_cast<uint8_t>((color >> 8u) & 0x7fu);

        return static_cast<uint8_t>(color & 0x00ffu);
    }

    void ppu_t::write_cgram_word(uint8_t address, uint16_t value) noexcept
    {
        if (display_active_for_cgram())
            address = _cgram_state.address;

        _cgram[address] = static_cast<uint16_t>(value & 0x7fffu);
    }

    void ppu_t::latch_counters() noexcept
    {
        const timing_snapshot_t snapshot{ timing() };
        _counter_latch.hcounter = snapshot.raster.dot;
        _counter_latch.vcounter = snapshot.raster.scanline;
        _counter_latch.counters_latched = true;
        _counter_latch.hcounter_high_read = false;
        _counter_latch.vcounter_high_read = false;
    }

    void ppu_t::clear_compositor_state() noexcept
    {
        _compositor_state = {};
    }

    void ppu_t::decode_render_state() noexcept
    {
        using mode_t = ppu_background_render_state_t::mode_t;

        _screen_state.hires = _bg_state.mode == 5u || _bg_state.mode == 6u;

        for (size_t index{ 0 }; index < 4; ++index)
        {
            _bg_state.render_mode[index] = mode_t::inactive;
            _bg_state.active[index] = false;
            _bg_state.priority[index] = { 0, 0 };
        }

        switch (_bg_state.mode)
        {
        case 0u:
            _bg_state.render_mode = { mode_t::bpp2, mode_t::bpp2, mode_t::bpp2, mode_t::bpp2 };
            _bg_state.active = { true, true, true, true };
            _bg_state.priority[0] = { 8, 11 };
            _bg_state.priority[1] = { 7, 10 };
            _bg_state.priority[2] = { 2, 5 };
            _bg_state.priority[3] = { 1, 4 };
            break;
        case 1u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.render_mode[2] = mode_t::bpp2;
            _bg_state.active = { true, true, true, false };
            _bg_state.priority[0] = { 7, 10 };
            _bg_state.priority[1] = { 6, 9 };
            _bg_state.priority[2] = _bg_state.bg3_priority ? std::array<uint8_t, 2>{ 3, 11 } : std::array<uint8_t, 2>{ 1, 3 };
            break;
        case 2u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            break;
        case 3u:
            _bg_state.render_mode[0] = mode_t::bpp8;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            break;
        case 4u:
            _bg_state.render_mode[0] = mode_t::bpp8;
            _bg_state.render_mode[1] = mode_t::bpp2;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            break;
        case 5u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp2;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            break;
        case 6u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.active = { true, false, false, false };
            _bg_state.priority[0] = { 2, 5 };
            break;
        case 7u:
            _bg_state.render_mode[0] = mode_t::mode7;
            _bg_state.active[0] = true;
            _bg_state.priority[0] = { 3, 3 };
            if (_bg_state.bg3_priority)
            {
                _bg_state.render_mode[1] = mode_t::mode7;
                _bg_state.active[1] = true;
                _bg_state.priority[1] = { 1, 5 };
            }
            break;
        default:
            break;
        }
    }

    uint8_t ppu_t::read_register(uint16_t address) noexcept
    {
        const uint16_t offset{ static_cast<uint16_t>(address - 0x2100u) };
        if (offset >= _registers.size())
            return 0;

        switch (address)
        {
        case 0x2137u:
            latch_counters();
            return _ppu2_mdr;
        case 0x2138u:
            _ppu1_mdr = read_oam_byte(_oam_state.address++);
            _oam_state.address &= 0x03ffu;
            return _ppu1_mdr;
        case 0x2139u:
            _ppu1_mdr = static_cast<uint8_t>(_vram_state.read_latch & 0x00ffu);
            if (!_vram_state.increment_on_high)
            {
                _vram_state.read_latch = read_vram_word();
                _vram_state.address = static_cast<uint16_t>(_vram_state.address + _vram_state.increment_size);
            }
            return _ppu1_mdr;
        case 0x213au:
            _ppu1_mdr = static_cast<uint8_t>(_vram_state.read_latch >> 8u);
            if (_vram_state.increment_on_high)
            {
                _vram_state.read_latch = read_vram_word();
                _vram_state.address = static_cast<uint16_t>(_vram_state.address + _vram_state.increment_size);
            }
            return _ppu1_mdr;
        case 0x213bu:
            if (!_cgram_state.read_high_pending)
            {
                _ppu2_mdr = read_cgram_byte(false, _cgram_state.address);
                _cgram_state.read_high_pending = true;
            }
            else
            {
                _ppu2_mdr = static_cast<uint8_t>((_ppu2_mdr & 0x80u)
                    | (read_cgram_byte(true, _cgram_state.address) & 0x7fu));
                ++_cgram_state.address;
                _cgram_state.read_high_pending = false;
            }
            return _ppu2_mdr;
        case 0x213cu:
            if (!_counter_latch.hcounter_high_read)
            {
                _ppu2_mdr = static_cast<uint8_t>(_counter_latch.hcounter & 0x00ffu);
                _counter_latch.hcounter_high_read = true;
            }
            else
            {
                _ppu2_mdr = static_cast<uint8_t>((_ppu2_mdr & 0xfeu) | ((_counter_latch.hcounter >> 8u) & 0x01u));
            }
            return _ppu2_mdr;
        case 0x213du:
            if (!_counter_latch.vcounter_high_read)
            {
                _ppu2_mdr = static_cast<uint8_t>(_counter_latch.vcounter & 0x00ffu);
                _counter_latch.vcounter_high_read = true;
            }
            else
            {
                _ppu2_mdr = static_cast<uint8_t>((_ppu2_mdr & 0xfeu) | ((_counter_latch.vcounter >> 8u) & 0x01u));
            }
            return _ppu2_mdr;
        case 0x213eu:
            _ppu1_mdr = 0x01u;
            return _ppu1_mdr;
        case 0x213fu:
            _counter_latch.hcounter_high_read = false;
            _counter_latch.vcounter_high_read = false;
            _ppu2_mdr = static_cast<uint8_t>(0x03u
                | (_counter_latch.counters_latched ? 0x40u : 0x00u)
                | (_counter.odd_field ? 0x80u : 0x00u));
            _counter_latch.counters_latched = false;
            return _ppu2_mdr;
        default:
            return _registers[offset];
        }
    }

    void ppu_t::write_register(uint16_t address, uint8_t value) noexcept
    {
        const uint16_t offset{ static_cast<uint16_t>(address - 0x2100u) };
        if (offset >= _registers.size())
            return;

        _registers[offset] = value;

        switch (address)
        {
        case 0x2100u:
            _display.brightness = static_cast<uint8_t>(value & 0x0fu);
            _display.disabled = (value & 0x80u) != 0;
            return;
        case 0x2102u:
            _oam_state.base_address = static_cast<uint16_t>((_oam_state.base_address & 0x0200u) | (value << 1u));
            _oam_state.address = _oam_state.base_address;
            _oam_state.latched_address = _oam_state.address;
            return;
        case 0x2103u:
            _oam_state.base_address = static_cast<uint16_t>((_oam_state.base_address & 0x01feu) | ((value & 0x01u) << 9u));
            _oam_state.priority = (value & 0x80u) != 0;
            _oam_state.address = _oam_state.base_address;
            _oam_state.latched_address = _oam_state.address;
            return;
        case 0x2104u:
        {
            const uint16_t address_now{ static_cast<uint16_t>(_oam_state.address & 0x03ffu) };
            _oam_state.latched_address = address_now;
            ++_oam_state.address;
            _oam_state.address &= 0x03ffu;

            if ((address_now & 0x0200u) != 0)
            {
                write_oam_byte(address_now, value);
                return;
            }

            if ((address_now & 0x0001u) == 0)
            {
                _oam_state.write_latch = value;
                return;
            }

            write_oam_byte(static_cast<uint16_t>(address_now & ~1u), _oam_state.write_latch);
            write_oam_byte(address_now, value);
            return;
        }
        case 0x2105u:
            _bg_state.mode = static_cast<uint8_t>(value & 0x07u);
            _bg_state.bg3_priority = (value & 0x08u) != 0;
            _bg_state.large_tiles[0] = (value & 0x10u) != 0;
            _bg_state.large_tiles[1] = (value & 0x20u) != 0;
            _bg_state.large_tiles[2] = (value & 0x40u) != 0;
            _bg_state.large_tiles[3] = (value & 0x80u) != 0;
            decode_render_state();
            return;
        case 0x2106u:
            _mosaic_state.enabled[0] = (value & 0x01u) != 0;
            _mosaic_state.enabled[1] = (value & 0x02u) != 0;
            _mosaic_state.enabled[2] = (value & 0x04u) != 0;
            _mosaic_state.enabled[3] = (value & 0x08u) != 0;
            _mosaic_state.size = static_cast<uint8_t>((value >> 4u) + 1u);
            return;
        case 0x2107u:
        case 0x2108u:
        case 0x2109u:
        case 0x210au:
        {
            const size_t index{ static_cast<size_t>(address - 0x2107u) };
            _bg_state.screen_size[index] = static_cast<uint8_t>(value & 0x03u);
            _bg_state.screen_address[index] = static_cast<uint16_t>((value >> 2u) << 10u);
            return;
        }
        case 0x210bu:
            _bg_state.tiledata_address[0] = static_cast<uint16_t>((value & 0x0fu) << 12u);
            _bg_state.tiledata_address[1] = static_cast<uint16_t>(((value >> 4u) & 0x0fu) << 12u);
            return;
        case 0x210cu:
            _bg_state.tiledata_address[2] = static_cast<uint16_t>((value & 0x0fu) << 12u);
            _bg_state.tiledata_address[3] = static_cast<uint16_t>(((value >> 4u) & 0x0fu) << 12u);
            return;
        case 0x210du:
            _scroll_latches.mode7_hoffset = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            _bg_state.hoffset[0] = static_cast<uint16_t>((value << 8u)
                | (_scroll_latches.ppu1 & ~0x07u)
                | (_scroll_latches.ppu2 & 0x07u));
            _scroll_latches.ppu1 = value;
            _scroll_latches.ppu2 = value;
            return;
        case 0x210eu:
            _scroll_latches.mode7_voffset = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            _bg_state.voffset[0] = static_cast<uint16_t>((value << 8u) | _scroll_latches.ppu1);
            _scroll_latches.ppu1 = value;
            return;
        case 0x210fu:
            _bg_state.hoffset[1] = static_cast<uint16_t>((value << 8u)
                | (_scroll_latches.ppu1 & ~0x07u)
                | (_scroll_latches.ppu2 & 0x07u));
            _scroll_latches.ppu1 = value;
            _scroll_latches.ppu2 = value;
            return;
        case 0x2110u:
            _bg_state.voffset[1] = static_cast<uint16_t>((value << 8u) | _scroll_latches.ppu1);
            _scroll_latches.ppu1 = value;
            return;
        case 0x2111u:
            _bg_state.hoffset[2] = static_cast<uint16_t>((value << 8u)
                | (_scroll_latches.ppu1 & ~0x07u)
                | (_scroll_latches.ppu2 & 0x07u));
            _scroll_latches.ppu1 = value;
            _scroll_latches.ppu2 = value;
            return;
        case 0x2112u:
            _bg_state.voffset[2] = static_cast<uint16_t>((value << 8u) | _scroll_latches.ppu1);
            _scroll_latches.ppu1 = value;
            return;
        case 0x2113u:
            _bg_state.hoffset[3] = static_cast<uint16_t>((value << 8u)
                | (_scroll_latches.ppu1 & ~0x07u)
                | (_scroll_latches.ppu2 & 0x07u));
            _scroll_latches.ppu1 = value;
            _scroll_latches.ppu2 = value;
            return;
        case 0x2114u:
            _bg_state.voffset[3] = static_cast<uint16_t>((value << 8u) | _scroll_latches.ppu1);
            _scroll_latches.ppu1 = value;
            return;
        case 0x2115u:
            _vram_state.increment_size = k_vram_increment_sizes[value & 0x03u];
            _vram_state.mapping = static_cast<uint8_t>((value >> 2u) & 0x03u);
            _vram_state.increment_on_high = (value & 0x80u) != 0;
            return;
        case 0x2116u:
            _vram_state.address = static_cast<uint16_t>((_vram_state.address & 0xff00u) | value);
            _vram_state.read_latch = read_vram_word();
            return;
        case 0x2117u:
            _vram_state.address = static_cast<uint16_t>((_vram_state.address & 0x00ffu) | (value << 8u));
            _vram_state.read_latch = read_vram_word();
            return;
        case 0x2118u:
            write_vram_byte(false, value);
            if (!_vram_state.increment_on_high)
                _vram_state.address = static_cast<uint16_t>(_vram_state.address + _vram_state.increment_size);
            return;
        case 0x2119u:
            write_vram_byte(true, value);
            if (_vram_state.increment_on_high)
                _vram_state.address = static_cast<uint16_t>(_vram_state.address + _vram_state.increment_size);
            return;
        case 0x2121u:
            _cgram_state.address = value;
            _cgram_state.write_high_pending = false;
            _cgram_state.read_high_pending = false;
            return;
        case 0x2122u:
            if (!_cgram_state.write_high_pending)
            {
                _cgram_state.write_latch = value;
                _cgram_state.write_high_pending = true;
                return;
            }

            write_cgram_word(_cgram_state.address++,
                             static_cast<uint16_t>(((value & 0x7fu) << 8u) | _cgram_state.write_latch));
            _cgram_state.write_high_pending = false;
            return;
        case 0x2123u:
            set_window_bits(_window_state.one_invert[0], _window_state.one_enable[0],
                            _window_state.two_invert[0], _window_state.two_enable[0], value, 0);
            set_window_bits(_window_state.one_invert[1], _window_state.one_enable[1],
                            _window_state.two_invert[1], _window_state.two_enable[1], value, 4);
            return;
        case 0x2124u:
            set_window_bits(_window_state.one_invert[2], _window_state.one_enable[2],
                            _window_state.two_invert[2], _window_state.two_enable[2], value, 0);
            set_window_bits(_window_state.one_invert[3], _window_state.one_enable[3],
                            _window_state.two_invert[3], _window_state.two_enable[3], value, 4);
            return;
        case 0x2125u:
            set_window_bits(_window_state.one_invert[4], _window_state.one_enable[4],
                            _window_state.two_invert[4], _window_state.two_enable[4], value, 0);
            set_window_bits(_window_state.one_invert[5], _window_state.one_enable[5],
                            _window_state.two_invert[5], _window_state.two_enable[5], value, 4);
            return;
        case 0x2126u:
            _window_state.one_left = value;
            return;
        case 0x2127u:
            _window_state.one_right = value;
            return;
        case 0x2128u:
            _window_state.two_left = value;
            return;
        case 0x2129u:
            _window_state.two_right = value;
            return;
        case 0x212au:
            _bg_state.window_mask[0] = static_cast<uint8_t>(value & 0x03u);
            _bg_state.window_mask[1] = static_cast<uint8_t>((value >> 2u) & 0x03u);
            _bg_state.window_mask[2] = static_cast<uint8_t>((value >> 4u) & 0x03u);
            _bg_state.window_mask[3] = static_cast<uint8_t>((value >> 6u) & 0x03u);
            return;
        case 0x212bu:
            _window_state.object_mask = static_cast<uint8_t>(value & 0x03u);
            _window_state.color_mask = static_cast<uint8_t>((value >> 2u) & 0x03u);
            return;
        case 0x212cu:
            _bg_state.above_enabled[0] = (value & 0x01u) != 0;
            _bg_state.above_enabled[1] = (value & 0x02u) != 0;
            _bg_state.above_enabled[2] = (value & 0x04u) != 0;
            _bg_state.above_enabled[3] = (value & 0x08u) != 0;
            _object_layer_state.above_enabled = (value & 0x10u) != 0;
            return;
        case 0x212du:
            _bg_state.below_enabled[0] = (value & 0x01u) != 0;
            _bg_state.below_enabled[1] = (value & 0x02u) != 0;
            _bg_state.below_enabled[2] = (value & 0x04u) != 0;
            _bg_state.below_enabled[3] = (value & 0x08u) != 0;
            _object_layer_state.below_enabled = (value & 0x10u) != 0;
            return;
        case 0x212eu:
            _bg_state.window_above_enabled[0] = (value & 0x01u) != 0;
            _bg_state.window_above_enabled[1] = (value & 0x02u) != 0;
            _bg_state.window_above_enabled[2] = (value & 0x04u) != 0;
            _bg_state.window_above_enabled[3] = (value & 0x08u) != 0;
            _object_layer_state.window_above_enabled = (value & 0x10u) != 0;
            return;
        case 0x212fu:
            _bg_state.window_below_enabled[0] = (value & 0x01u) != 0;
            _bg_state.window_below_enabled[1] = (value & 0x02u) != 0;
            _bg_state.window_below_enabled[2] = (value & 0x04u) != 0;
            _bg_state.window_below_enabled[3] = (value & 0x08u) != 0;
            _object_layer_state.window_below_enabled = (value & 0x10u) != 0;
            return;
        case 0x2130u:
            _color_math_state.direct_color = (value & 0x01u) != 0;
            _color_math_state.blend_mode = (value & 0x02u) != 0;
            _window_state.color_mask_below = static_cast<uint8_t>((value >> 4u) & 0x03u);
            _window_state.color_mask_above = static_cast<uint8_t>((value >> 6u) & 0x03u);
            return;
        case 0x2131u:
            _color_math_state.bg_color_enable[0] = (value & 0x01u) != 0;
            _color_math_state.bg_color_enable[1] = (value & 0x02u) != 0;
            _color_math_state.bg_color_enable[2] = (value & 0x04u) != 0;
            _color_math_state.bg_color_enable[3] = (value & 0x08u) != 0;
            _color_math_state.obj_color_enable = (value & 0x10u) != 0;
            _color_math_state.backdrop_color_enable = (value & 0x20u) != 0;
            _color_math_state.color_halve = (value & 0x40u) != 0;
            _color_math_state.color_mode_subtract = (value & 0x80u) != 0;
            return;
        case 0x2132u:
            if ((value & 0x20u) != 0)
                _color_math_state.fixed_red = static_cast<uint8_t>(value & 0x1fu);
            if ((value & 0x40u) != 0)
                _color_math_state.fixed_green = static_cast<uint8_t>(value & 0x1fu);
            if ((value & 0x80u) != 0)
                _color_math_state.fixed_blue = static_cast<uint8_t>(value & 0x1fu);
            return;
        case 0x2133u:
            _screen_state.interlace = (value & 0x01u) != 0;
            _screen_state.overscan = (value & 0x04u) != 0;
            _screen_state.pseudo_hires = (value & 0x08u) != 0;
            return;
        case 0x211au:
            _screen_state.mode7_hflip = (value & 0x01u) != 0;
            _screen_state.mode7_vflip = (value & 0x02u) != 0;
            _screen_state.mode7_repeat = static_cast<uint8_t>((value >> 6u) & 0x03u);
            return;
        default:
            return;
        }
    }

    ppu_step_result_t ppu_t::step(master_clock_delta_t master_clocks) noexcept
    {
        const uint16_t previous_visible_scanlines{ active_visible_scanlines() };
        const timing_snapshot_t previous_timing{ timing() };
        _counter.advance(master_clocks, _video_timing);

        ppu_step_result_t result{};
        result.timing = timing();
        result.visible_scanlines = active_visible_scanlines();
        if (result.timing.raster.scanline < previous_timing.raster.scanline)
        {
            ++_frame_counter;
            render_placeholder_frame();
            result.frame_complete = true;
        }

        result.entered_scanline = result.timing.raster.scanline != previous_timing.raster.scanline;
        if (result.entered_scanline)
            clear_compositor_state();
        result.entered_frame_start = result.frame_complete
            || crossed_raster_point(previous_timing, result.timing, 0, 0);
        result.entered_hblank = !previous_timing.in_hblank && result.timing.in_hblank;
        const bool was_in_vblank{ previous_timing.raster.scanline >= previous_visible_scanlines };
        const bool now_in_vblank{ result.timing.raster.scanline >= result.visible_scanlines };
        result.entered_vblank = !was_in_vblank && now_in_vblank;
        result.nmi_requested = result.entered_vblank;
        result.hdma_setup_triggered = crossed_raster_point(previous_timing,
                                                           result.timing,
                                                           _video_timing.hdma_setup_scanline,
                                                           _video_timing.hdma_setup_dot);
        result.hdma_transfer_triggered = crossed_raster_point(previous_timing,
                                                              result.timing,
                                                              result.timing.raster.scanline,
                                                              _video_timing.hdma_trigger_dot)
            && result.timing.raster.scanline < result.visible_scanlines;

        return result;
    }

    timing_snapshot_t ppu_t::timing() const noexcept
    {
        return _counter.snapshot(_video_timing, active_visible_scanlines());
    }

    master_clock_delta_t ppu_t::current_scanline_clocks() const noexcept
    {
        return _counter.current_scanline_clocks(_video_timing);
    }

    ppu_render_state_snapshot_t ppu_t::render_state_snapshot() const noexcept
    {
        ppu_render_state_snapshot_t snapshot{};
        snapshot.display_disabled = _display.disabled;
        snapshot.brightness = _display.brightness;
        snapshot.bg_mode = _bg_state.mode;
        snapshot.bg3_priority = _bg_state.bg3_priority;
        snapshot.hires = _screen_state.hires;
        snapshot.mosaic_size = _mosaic_state.size;
        snapshot.pseudo_hires = _screen_state.pseudo_hires;
        snapshot.overscan = _screen_state.overscan;
        snapshot.interlace = _screen_state.interlace;
        snapshot.mode7_hoffset = _scroll_latches.mode7_hoffset;
        snapshot.mode7_voffset = _scroll_latches.mode7_voffset;
        snapshot.mode7_repeat = _screen_state.mode7_repeat;
        snapshot.mode7_hflip = _screen_state.mode7_hflip;
        snapshot.mode7_vflip = _screen_state.mode7_vflip;

        for (size_t index{ 0 }; index < 4; ++index)
        {
            snapshot.backgrounds[index].mode = _bg_state.render_mode[index];
            snapshot.backgrounds[index].active = _bg_state.active[index];
            snapshot.backgrounds[index].tiledata_address = _bg_state.tiledata_address[index];
            snapshot.backgrounds[index].screen_address = _bg_state.screen_address[index];
            snapshot.backgrounds[index].screen_size = _bg_state.screen_size[index];
            snapshot.backgrounds[index].large_tiles = _bg_state.large_tiles[index];
            snapshot.backgrounds[index].priority[0] = _bg_state.priority[index][0];
            snapshot.backgrounds[index].priority[1] = _bg_state.priority[index][1];
            snapshot.backgrounds[index].above_enabled = _bg_state.above_enabled[index];
            snapshot.backgrounds[index].below_enabled = _bg_state.below_enabled[index];
            snapshot.backgrounds[index].window_above_enabled = _bg_state.window_above_enabled[index];
            snapshot.backgrounds[index].window_below_enabled = _bg_state.window_below_enabled[index];
            snapshot.backgrounds[index].window_mask = _bg_state.window_mask[index];
            snapshot.backgrounds[index].hoffset = _bg_state.hoffset[index];
            snapshot.backgrounds[index].voffset = _bg_state.voffset[index];
        }

        snapshot.objects.above_enabled = _object_layer_state.above_enabled;
        snapshot.objects.below_enabled = _object_layer_state.below_enabled;
        snapshot.objects.window_above_enabled = _object_layer_state.window_above_enabled;
        snapshot.objects.window_below_enabled = _object_layer_state.window_below_enabled;
        snapshot.objects.window_mask = _window_state.object_mask;

        snapshot.color_math.direct_color = _color_math_state.direct_color;
        snapshot.color_math.blend_mode = _color_math_state.blend_mode;
        snapshot.color_math.color_halve = _color_math_state.color_halve;
        snapshot.color_math.color_mode_subtract = _color_math_state.color_mode_subtract;
        snapshot.color_math.obj_color_enable = _color_math_state.obj_color_enable;
        snapshot.color_math.backdrop_color_enable = _color_math_state.backdrop_color_enable;
        snapshot.color_math.fixed_red = _color_math_state.fixed_red;
        snapshot.color_math.fixed_green = _color_math_state.fixed_green;
        snapshot.color_math.fixed_blue = _color_math_state.fixed_blue;
        snapshot.color_math.window_mask_above = _window_state.color_mask_above;
        snapshot.color_math.window_mask_below = _window_state.color_mask_below;
        snapshot.color_math.color_window_mask = _window_state.color_mask;
        for (size_t index{ 0 }; index < 4; ++index)
            snapshot.color_math.bg_color_enable[index] = _color_math_state.bg_color_enable[index];

        snapshot.window.one_left = _window_state.one_left;
        snapshot.window.one_right = _window_state.one_right;
        snapshot.window.two_left = _window_state.two_left;
        snapshot.window.two_right = _window_state.two_right;
        for (size_t index{ 0 }; index < 6; ++index)
        {
            snapshot.window.one_invert[index] = _window_state.one_invert[index];
            snapshot.window.one_enable[index] = _window_state.one_enable[index];
            snapshot.window.two_invert[index] = _window_state.two_invert[index];
            snapshot.window.two_enable[index] = _window_state.two_enable[index];
        }

        return snapshot;
    }

    ppu_compositor_snapshot_t ppu_t::compositor_snapshot() const noexcept
    {
        ppu_compositor_snapshot_t snapshot{};
        snapshot.hires = _screen_state.hires;
        snapshot.pseudo_hires = _screen_state.pseudo_hires;
        snapshot.blend_mode = _color_math_state.blend_mode;
        snapshot.color_halve = _color_math_state.color_halve;
        snapshot.direct_color = _color_math_state.direct_color;
        snapshot.color_mode_subtract = _color_math_state.color_mode_subtract;
        snapshot.backdrop_color_enable = _color_math_state.backdrop_color_enable;
        snapshot.fixed_red = _color_math_state.fixed_red;
        snapshot.fixed_green = _color_math_state.fixed_green;
        snapshot.fixed_blue = _color_math_state.fixed_blue;
        for (size_t index{ 0 }; index < 4; ++index)
            snapshot.backgrounds[index] = _compositor_state.backgrounds[index];
        snapshot.objects = _compositor_state.objects;
        return snapshot;
    }

    void ppu_t::present(framebuffer_t& framebuffer, const ppu_presentation_options_t& options) const noexcept
    {
        static_cast<void>(options);
        std::copy(_composed_frame.data(),
                  _composed_frame.data() + framebuffer_t::k_pixel_count,
                  framebuffer.data());
    }

    void ppu_t::render_placeholder_frame() noexcept
    {
        const uint32_t color{ (_frame_counter & 1u) == 1 ? 0xff101820u : 0xff182840u };
        _composed_frame.clear(color);
    }
}
