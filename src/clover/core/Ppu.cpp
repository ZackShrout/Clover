//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Ppu.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace
{
    constexpr uint8_t k_ppu1_version{ 0x01u };
    constexpr uint8_t k_ppu2_version{ 0x03u };
    constexpr std::array<uint8_t, 4> k_vram_increment_sizes{ 1, 32, 128, 128 };
    constexpr uint16_t k_vram_word_mask{ 0x7fffu };

    struct presentation_lut_t
    {
        std::array<std::array<uint32_t, 32768>, 16> colors{};
    };

    [[nodiscard]] uint16_t present_channel(uint8_t brightness, uint8_t value) noexcept
    {
        const double luma{ static_cast<double>(brightness) / 15.0 };
        const uint8_t adjusted{
            static_cast<uint8_t>(std::clamp<int>(static_cast<int>(luma * static_cast<double>(value) + 0.5), 0, 31))
        };

        uint16_t expanded{ static_cast<uint16_t>((adjusted << 3u) | (adjusted >> 2u)) };
        expanded = static_cast<uint16_t>((expanded << 8u) | expanded);

        constexpr double k_gamma{ 1.5 };
        constexpr double k_reciprocal{ 1.0 / 32767.0 };
        if (expanded <= 32767u)
            expanded = static_cast<uint16_t>(32767.0 * std::pow(static_cast<double>(expanded) * k_reciprocal, k_gamma));
        return expanded;
    }

    [[nodiscard]] const presentation_lut_t& presentation_lut() noexcept
    {
        static const presentation_lut_t table = []() noexcept
        {
            presentation_lut_t lut{};
            for (uint32_t brightness{ 0 }; brightness < lut.colors.size(); ++brightness)
            {
                for (uint32_t color{ 0 }; color < lut.colors[brightness].size(); ++color)
                {
                    const uint8_t red5{ static_cast<uint8_t>(color & 0x001fu) };
                    const uint8_t green5{ static_cast<uint8_t>((color >> 5u) & 0x001fu) };
                    const uint8_t blue5{ static_cast<uint8_t>((color >> 10u) & 0x001fu) };
                    const uint8_t red8{ static_cast<uint8_t>(present_channel(static_cast<uint8_t>(brightness), red5) >> 8u) };
                    const uint8_t green8{
                        static_cast<uint8_t>(present_channel(static_cast<uint8_t>(brightness), green5) >> 8u)
                    };
                    const uint8_t blue8{
                        static_cast<uint8_t>(present_channel(static_cast<uint8_t>(brightness), blue5) >> 8u)
                    };
                    lut.colors[brightness][color] = 0xff000000u
                        | (static_cast<uint32_t>(red8) << 16u)
                        | (static_cast<uint32_t>(green8) << 8u)
                        | blue8;
                }
            }
            return lut;
        }();
        return table;
    }

    [[nodiscard]] uint32_t snes_color_to_rgba8(uint16_t color, uint8_t brightness) noexcept
    {
        return presentation_lut().colors[brightness & 0x0fu][color & 0x7fffu];
    }

    struct render_write_trace_filter_t
    {
        bool enabled{ false };
        uint64_t frame{ 0 };
        uint16_t scanline{ 0 };
        uint16_t x{ 0 };
    };

    struct inidisp_trace_filter_t
    {
        bool enabled{ false };
        uint64_t frame_min{ 0 };
        uint64_t frame_max{ 0 };
    };

    struct obj_tile_trace_filter_t
    {
        bool enabled{ false };
        uint64_t frame{ 0 };
        uint16_t scanline{ 0 };
    };

    struct vram_write_trace_filter_t
    {
        bool enabled{ false };
        uint16_t address_min{ 0 };
        uint16_t address_max{ 0 };
        uint64_t frame_min{ 0 };
        uint64_t frame_max{ 0 };
    };

    [[nodiscard]] uint64_t parse_trace_u64_env(const char* name, uint64_t fallback) noexcept
    {
        const char* raw{ std::getenv(name) };
        if (raw == nullptr || *raw == '\0')
            return fallback;

        char* end{ nullptr };
        const unsigned long long parsed{ std::strtoull(raw, &end, 0) };
        if (end == raw)
            return fallback;
        return static_cast<uint64_t>(parsed);
    }

    [[nodiscard]] render_write_trace_filter_t load_render_write_trace_filter() noexcept
    {
        render_write_trace_filter_t filter{};
        const char* frame_raw{ std::getenv("CLOVER_RENDER_WRITE_TRACE_FRAME") };
        if (frame_raw == nullptr || *frame_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.frame = parse_trace_u64_env("CLOVER_RENDER_WRITE_TRACE_FRAME", 0u);
        filter.scanline = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_RENDER_WRITE_TRACE_SCANLINE", 0u) & 0xffffu);
        filter.x = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_RENDER_WRITE_TRACE_X", 0u) & 0xffffu);
        return filter;
    }

    [[nodiscard]] inidisp_trace_filter_t load_inidisp_trace_filter() noexcept
    {
        inidisp_trace_filter_t filter{};
        const char* frame_raw{ std::getenv("CLOVER_INIDISP_TRACE_FRAME_MIN") };
        if (frame_raw == nullptr || *frame_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.frame_min = parse_trace_u64_env("CLOVER_INIDISP_TRACE_FRAME_MIN", 0u);
        filter.frame_max = parse_trace_u64_env("CLOVER_INIDISP_TRACE_FRAME_MAX", filter.frame_min);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

    [[nodiscard]] obj_tile_trace_filter_t load_obj_tile_trace_filter() noexcept
    {
        obj_tile_trace_filter_t filter{};
        const char* frame_raw{ std::getenv("CLOVER_OBJ_TILE_TRACE_FRAME") };
        if (frame_raw == nullptr || *frame_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.frame = parse_trace_u64_env("CLOVER_OBJ_TILE_TRACE_FRAME", 0u);
        filter.scanline = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_OBJ_TILE_TRACE_SCANLINE", 0u) & 0xffffu);
        return filter;
    }

    [[nodiscard]] vram_write_trace_filter_t load_vram_write_trace_filter() noexcept
    {
        vram_write_trace_filter_t filter{};
        const char* addr_min_raw{ std::getenv("CLOVER_VRAM_WRITE_TRACE_ADDR_MIN") };
        if (addr_min_raw == nullptr || *addr_min_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.address_min = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_VRAM_WRITE_TRACE_ADDR_MIN", 0u) & 0x7fffu);
        filter.address_max = static_cast<uint16_t>(
            parse_trace_u64_env("CLOVER_VRAM_WRITE_TRACE_ADDR_MAX", filter.address_min) & 0x7fffu);
        filter.frame_min = parse_trace_u64_env("CLOVER_VRAM_WRITE_TRACE_FRAME_MIN", 0u);
        filter.frame_max = parse_trace_u64_env("CLOVER_VRAM_WRITE_TRACE_FRAME_MAX", UINT64_MAX);
        if (filter.address_min > filter.address_max)
            std::swap(filter.address_min, filter.address_max);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

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

    [[nodiscard]] uint8_t screen_size_bit(uint8_t screen_size, uint8_t bit_index) noexcept
    {
        return static_cast<uint8_t>((screen_size >> bit_index) & 0x01u);
    }

    [[nodiscard]] uint8_t background_mode_index(clover::core::ppu_background_render_state_t::mode_t mode) noexcept
    {
        using mode_t = clover::core::ppu_background_render_state_t::mode_t;

        switch (mode)
        {
        case mode_t::bpp2:
            return 0u;
        case mode_t::bpp4:
            return 1u;
        case mode_t::bpp8:
            return 2u;
        default:
            return 0u;
        }
    }

    struct background_addressing_t
    {
        uint16_t horizontal_source{ 0 };
        uint16_t vertical_source{ 0 };
        uint16_t tilemap_address{ 0 };
    };

    struct background_geometry_t
    {
        bool hires{ false };
        bool large_tiles{ false };
        uint8_t screen_size{ 0 };
        uint16_t screen_address{ 0 };
    };

    struct background_address_window_t
    {
        uint16_t horizontal_mask{ 0 };
        uint16_t vertical_mask{ 0 };
        uint8_t horizontal_tile_shift{ 0 };
        uint8_t vertical_tile_shift{ 0 };
        uint16_t hscreen{ 0 };
        uint16_t vscreen{ 0 };
    };

    [[nodiscard]] background_address_window_t build_background_address_window(
        const background_geometry_t& geometry,
        bool offset_cache_lookup) noexcept
    {
        const uint16_t width{ static_cast<uint16_t>(256u << (geometry.hires ? 1u : 0u)) };
        const uint16_t horizontal_span{
            static_cast<uint16_t>(width << (geometry.large_tiles ? 1u : 0u))
        };
        const uint16_t vertical_span{
            static_cast<uint16_t>(256u << (geometry.large_tiles ? 1u : 0u)
                << screen_size_bit(geometry.screen_size, 1u))
        };

        return {
            .horizontal_mask = static_cast<uint16_t>(
                (horizontal_span << screen_size_bit(geometry.screen_size, 0u)) - 1u),
            .vertical_mask = static_cast<uint16_t>(vertical_span - 1u),
            .horizontal_tile_shift = static_cast<uint8_t>(
                offset_cache_lookup
                    ? (geometry.hires ? 3u : (geometry.large_tiles ? 4u : 3u))
                    : (geometry.hires ? 4u : (geometry.large_tiles ? 4u : 3u))),
            .vertical_tile_shift = static_cast<uint8_t>(geometry.large_tiles ? 4u : 3u),
            .hscreen = static_cast<uint16_t>(
                screen_size_bit(geometry.screen_size, 0u) != 0 ? (32u << 5u) : 0u),
            .vscreen = static_cast<uint16_t>(
                screen_size_bit(geometry.screen_size, 1u) != 0
                    ? static_cast<uint16_t>(32u << (5u + screen_size_bit(geometry.screen_size, 0u)))
                    : 0u)
        };
    }

    [[nodiscard]] background_addressing_t compute_background_addressing(
        const background_geometry_t& geometry,
        const background_address_window_t& window,
        uint16_t horizontal_source,
        uint16_t vertical_source) noexcept
    {
        horizontal_source &= window.horizontal_mask;
        vertical_source &= window.vertical_mask;

        const uint16_t htile{ static_cast<uint16_t>(horizontal_source >> window.horizontal_tile_shift) };
        const uint16_t vtile{ static_cast<uint16_t>(vertical_source >> window.vertical_tile_shift) };
        uint16_t tile_offset{
            static_cast<uint16_t>((htile & 0x001fu) | ((vtile & 0x001fu) << 5u))
        };
        if ((htile & 0x0020u) != 0)
            tile_offset = static_cast<uint16_t>(tile_offset + window.hscreen);
        if ((vtile & 0x0020u) != 0)
            tile_offset = static_cast<uint16_t>(tile_offset + window.vscreen);

        return {
            .horizontal_source = horizontal_source,
            .vertical_source = vertical_source,
            .tilemap_address = static_cast<uint16_t>((geometry.screen_address + tile_offset) & k_vram_word_mask)
        };
    }

    [[nodiscard]] uint16_t background_vertical_pixel(uint16_t evaluation_scanline,
                                                     uint16_t mosaic_vertical_offset,
                                                     bool hires,
                                                     bool interlace,
                                                     bool odd_field,
                                                     bool mosaic_enabled) noexcept
    {
        const bool interlace_field_offset{ hires && interlace && odd_field && !mosaic_enabled };
        return static_cast<uint16_t>(
            static_cast<uint16_t>(
                (evaluation_scanline << (hires && interlace ? 1u : 0u))
                | (interlace_field_offset ? 0x0001u : 0x0000u))
            - static_cast<uint16_t>(mosaic_vertical_offset << (hires && interlace ? 1u : 0u))
        );
    }

    [[nodiscard]] uint16_t decode_bitplane_pair(uint16_t data, bool reverse_bits) noexcept
    {
        if (reverse_bits)
        {
            data = static_cast<uint16_t>((data >> 4u & 0x0f0fu) | (data << 4u & 0xf0f0u));
            data = static_cast<uint16_t>((data >> 2u & 0x3333u) | (data << 2u & 0xccccu));
            data = static_cast<uint16_t>((data >> 1u & 0x5555u) | (data << 1u & 0xaaaau));
        }

        return static_cast<uint16_t>(
            ((((static_cast<uint64_t>(static_cast<uint8_t>(data >> 0u)) * 0x0101010101010101ull)
                & 0x8040201008040201ull) * 0x0102040810204081ull >> 49u) & 0x5555u)
            | ((((static_cast<uint64_t>(static_cast<uint8_t>(data >> 8u)) * 0x0101010101010101ull)
                & 0x8040201008040201ull) * 0x0102040810204081ull >> 48u) & 0xaaaau)
        );
    }

    [[nodiscard]] uint8_t extract_row_pair_pixel(uint16_t row_data, uint8_t fine_x) noexcept
    {
        return static_cast<uint8_t>((row_data >> (fine_x << 1u)) & 0x03u);
    }

    template <typename TileArray>
    void maybe_trace_obj_tiles(uint64_t frame_index,
                               uint16_t scanline,
                               const char* stage,
                               const TileArray& tiles,
                               uint8_t tile_count,
                               uint8_t pipeline_tile_count,
                               uint8_t visible_tile_count) noexcept
    {
        static const obj_tile_trace_filter_t filter{ load_obj_tile_trace_filter() };
        if (!filter.enabled || filter.frame != frame_index || filter.scanline != scanline)
            return;

        std::printf("OBJ tile trace: frame=%llu scanline=%u stage=%s tile_count=%u pipeline=%u visible=%u\n",
                    static_cast<unsigned long long>(frame_index),
                    static_cast<unsigned>(scanline),
                    stage,
                    static_cast<unsigned>(tile_count),
                    static_cast<unsigned>(pipeline_tile_count),
                    static_cast<unsigned>(visible_tile_count));
        for (size_t index{ 0 }; index < tiles.size(); ++index)
        {
            const auto& entry{ tiles[index] };
            if (!entry.valid)
            {
                std::printf("  tile[%zu]: invalid\n", index);
                break;
            }

            const auto& tile{ entry.candidate };
            std::printf(
                "  tile[%zu]: obj=%u x=%u tile_x=%u src_y=%u fine_y=%u vram=%04x pal=%u pri=%u hflip=%u data=%08x row0=%04x row1=%04x\n",
                index,
                static_cast<unsigned>(tile.object_index),
                static_cast<unsigned>(tile.x),
                static_cast<unsigned>(tile.tile_x),
                static_cast<unsigned>(tile.source_y),
                static_cast<unsigned>(tile.fine_y),
                static_cast<unsigned>(tile.vram_address),
                static_cast<unsigned>(tile.palette_base),
                static_cast<unsigned>(tile.priority),
                tile.hflip ? 1u : 0u,
                static_cast<unsigned>(tile.data),
                static_cast<unsigned>(tile.row_data[0]),
                static_cast<unsigned>(tile.row_data[1]));
        }
    }

    [[nodiscard]] bool window_test(bool one_enable,
                                   bool one,
                                   bool two_enable,
                                   bool two,
                                   uint8_t mask) noexcept
    {
        if (!one_enable)
            return two && two_enable;

        if (!two_enable)
            return one;

        switch (mask & 0x03u)
        {
        case 0u:
            return one || two;
        case 1u:
            return one && two;
        case 2u:
            return one != two;
        case 3u:
            return one == two;
        default:
            return false;
        }
    }

    [[nodiscard]] bool window_hit(uint8_t x,
                                  uint8_t one_left,
                                  uint8_t one_right,
                                  uint8_t two_left,
                                  uint8_t two_right,
                                  bool one_invert,
                                  bool one_enable,
                                  bool two_invert,
                                  bool two_enable,
                                  uint8_t mask) noexcept
    {
        const bool one{
            one_enable
                && ((x >= one_left && x <= one_right) != one_invert)
        };
        const bool two{
            two_enable
                && ((x >= two_left && x <= two_right) != two_invert)
        };

        return window_test(one_enable, one, two_enable, two, mask);
    }

    [[nodiscard]] clover::core::ppu_pixel_source_t background_pixel_source(uint8_t background_index) noexcept
    {
        using source_t = clover::core::ppu_pixel_source_t;

        switch (background_index)
        {
        case 0u:
            return source_t::background_1;
        case 1u:
            return source_t::background_2;
        case 2u:
            return source_t::background_3;
        case 3u:
            return source_t::background_4;
        default:
            return source_t::none;
        }
    }

    [[nodiscard]] bool source_allows_color_math(const clover::core::ppu_pixel_candidate_t& candidate,
                                                bool backdrop_color_enable) noexcept
    {
        using source_t = clover::core::ppu_pixel_source_t;

        switch (candidate.source)
        {
        case source_t::background_1:
        case source_t::background_2:
        case source_t::background_3:
        case source_t::background_4:
            return candidate.color_math_enabled;
        case source_t::objects:
            return candidate.color_math_enabled && candidate.palette >= 192u;
        case source_t::backdrop:
            return backdrop_color_enable;
        case source_t::none:
        default:
            return false;
        }
    }

    [[nodiscard]] uint16_t direct_color(uint8_t palette, uint8_t palette_group) noexcept
    {
        return static_cast<uint16_t>(
            ((palette << 7u) & 0x6000u)
            + ((palette_group << 10u) & 0x1000u)
            + ((palette << 4u) & 0x0380u)
            + ((palette_group << 5u) & 0x0040u)
            + ((palette << 2u) & 0x001cu)
            + ((palette_group << 1u) & 0x0002u));
    }

    [[nodiscard]] uint16_t blend_colors(uint16_t lhs,
                                        uint16_t rhs,
                                        bool subtract,
                                        bool halve) noexcept
    {
        if (!subtract)
        {
            if (!halve)
            {
                const uint32_t sum{ static_cast<uint32_t>(lhs) + static_cast<uint32_t>(rhs) };
                const uint32_t carry{ (sum - ((lhs ^ rhs) & 0x0421u)) & 0x8420u };
                return static_cast<uint16_t>((sum - carry) | (carry - (carry >> 5u)));
            }

            return static_cast<uint16_t>((lhs + rhs - ((lhs ^ rhs) & 0x0421u)) >> 1u);
        }

        const uint32_t diff{ static_cast<uint32_t>(lhs) - static_cast<uint32_t>(rhs) + 0x8420u };
        const uint32_t borrow{ (diff - ((lhs ^ rhs) & 0x8420u)) & 0x8420u };
        if (!halve)
            return static_cast<uint16_t>((diff - borrow) & (borrow - (borrow >> 5u)));

        return static_cast<uint16_t>((((diff - borrow) & (borrow - (borrow >> 5u))) & 0x7bdeu) >> 1u);
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
        initialize(false);
        render_placeholder_frame();
    }

    void ppu_t::reset() noexcept
    {
        initialize(true);
    }

    void ppu_t::initialize(bool warm_reset) noexcept
    {
        const std::array<uint16_t, 32 * 1024> preserved_vram{
            warm_reset && _entropy_mode != ppu_entropy_mode_t::none ? _vram : std::array<uint16_t, 32 * 1024>{}
        };

        _composed_frame.clear();
        _presented_frame.clear();
        std::fill(_registers.begin(), _registers.end(), 0);
        std::fill(_vram.begin(), _vram.end(), 0);
        std::fill(_oam.begin(), _oam.end(), 0);
        std::fill(_cgram.begin(), _cgram.end(), 0);
        _counter.reset();
        _timing_interlace = false;
        _frame_counter = 0;
        _display = {};
        _display.disabled = true;
        _oam_state = {};
        _bg_state = {};
        _scroll_latches = {};
        _mosaic_state = {};
        _window_state = {};
        _background_layer_state = {};
        _object_layer_state = {};
        _color_math_state = {};
        _screen_state = {};
        _compositor_state = {};
        _pipeline_state = {};
        _display_write_history = {};
        decode_render_state();
        _vram_state = {};
        _vram_state.increment_size = 1;
        _cgram_state = {};
        _cgram_write_trace = {};
        _cgram_write_trace_count = 0;
        _cgram_write_trace_start_frame = 0;
        _oam_write_trace = {};
        _oam_write_trace_count = 0;
        _oam_write_trace_start_frame = 0;
        _counter_latch = {};
        _external_latch_enabled = false;
        _ppu1_mdr = 0;
        _ppu2_mdr = 0;
        _oam_state.latched_address = 0;

        if (warm_reset && _entropy_mode != ppu_entropy_mode_t::none)
            _vram = preserved_vram;

        apply_startup_entropy(warm_reset);
    }

    void ppu_t::set_entropy_mode(ppu_entropy_mode_t mode) noexcept
    {
        _entropy_mode = mode;
    }

    ppu_entropy_mode_t ppu_t::entropy_mode() const noexcept
    {
        return _entropy_mode;
    }

    void ppu_t::set_entropy_seed(uint32_t seed, uint32_t sequence) noexcept
    {
        _entropy_seed_override_enabled = true;
        _entropy_seed = seed;
        _entropy_sequence = sequence;
    }

    void ppu_t::clear_entropy_seed() noexcept
    {
        _entropy_seed_override_enabled = false;
        _entropy_seed = 0u;
        _entropy_sequence = 0u;
    }

    void ppu_t::apply_startup_entropy(bool warm_reset) noexcept
    {
        if (_entropy_mode == ppu_entropy_mode_t::none)
            return;

        const uint32_t seed{
            _entropy_seed_override_enabled ? _entropy_seed : default_startup_entropy_seed()
        };
        const uint32_t sequence{
            _entropy_seed_override_enabled
                ? _entropy_sequence
                : static_cast<uint32_t>(
                    reinterpret_cast<uintptr_t>(this) ^ static_cast<uintptr_t>(_frame_counter))
        };
        startup_entropy_generator_t entropy{ seed, sequence };

        if (!warm_reset)
        {
            auto* const vram_bytes{ reinterpret_cast<uint8_t*>(_vram.data()) };
            fill_entropy_buffer(_entropy_mode, entropy, vram_bytes, sizeof(uint16_t) * _vram.size());
        }

        auto* const cgram_bytes{ reinterpret_cast<uint8_t*>(_cgram.data()) };
        fill_entropy_buffer(_entropy_mode, entropy, cgram_bytes, sizeof(uint16_t) * _cgram.size());
        for (uint16_t& word : _cgram)
            word &= 0x7fffu;

        _ppu1_mdr = entropy.random_u8();
        _ppu2_mdr = entropy.random_u8();
        _oam_state.base_address = static_cast<uint16_t>(entropy.random_u16() & 0x03feu);
        _oam_state.address = static_cast<uint16_t>(entropy.random_u16() & 0x03ffu);
        _oam_state.priority = entropy.random_bool();
        _oam_state.write_latch = entropy.random_u8();
        _oam_state.latched_address = 0u;
        update_first_sprite();

        _scroll_latches.ppu1 = entropy.random_u8();
        _scroll_latches.ppu2 = entropy.random_u8();
        _scroll_latches.mode7 = entropy.random_u8();
        _scroll_latches.mode7_hoffset = entropy.random_u16();
        _scroll_latches.mode7_voffset = entropy.random_u16();

        _object_layer_state.base_size = static_cast<uint8_t>(entropy.random_u8() & 0x07u);
        _object_layer_state.nameselect = static_cast<uint8_t>(entropy.random_u8() & 0x03u);
        _object_layer_state.tiledata_address =
            static_cast<uint16_t>((entropy.random_u8() & 0x07u) << 13u);
        _object_layer_state.interlace = entropy.random_bool();

        _color_math_state.direct_color = entropy.random_bool();
        _color_math_state.blend_mode = entropy.random_bool();
        _color_math_state.color_halve = entropy.random_bool();
        _color_math_state.color_mode_subtract = entropy.random_bool();
        for (bool& enabled : _color_math_state.bg_color_enable)
            enabled = entropy.random_bool();
        _color_math_state.obj_color_enable = entropy.random_bool();
        _color_math_state.backdrop_color_enable = entropy.random_bool();
        _color_math_state.fixed_red = static_cast<uint8_t>(entropy.random_u8() & 0x1fu);
        _color_math_state.fixed_green = static_cast<uint8_t>(entropy.random_u8() & 0x1fu);
        _color_math_state.fixed_blue = static_cast<uint8_t>(entropy.random_u8() & 0x1fu);

        _screen_state.pseudo_hires = entropy.random_bool();
        _screen_state.overscan = false;
        _screen_state.interlace = false;
        _screen_state.mode7_repeat = static_cast<uint8_t>(entropy.random_u8() & 0x03u);
        _screen_state.mode7_hflip = entropy.random_bool();
        _screen_state.mode7_vflip = entropy.random_bool();
        _screen_state.mode7_a = entropy.random_u16();
        _screen_state.mode7_b = entropy.random_u16();
        _screen_state.mode7_c = entropy.random_u16();
        _screen_state.mode7_d = entropy.random_u16();
        _screen_state.mode7_x = entropy.random_u16();
        _screen_state.mode7_y = entropy.random_u16();

        _vram_state.increment_size = 1u;
        _vram_state.mapping = static_cast<uint8_t>(entropy.random_u8() & 0x03u);
        _vram_state.increment_on_high = entropy.random_bool();
        _vram_state.address = entropy.random_u16();
        _vram_state.read_latch = entropy.random_u16();

        _cgram_state.address = entropy.random_u8();
        _cgram_state.latched_address = 0u;
        _cgram_state.write_high_pending = false;
        _cgram_state.read_high_pending = false;
        _cgram_state.write_latch = entropy.random_u8();

        decode_render_state();
    }

    video_standard_t ppu_t::video_standard() const noexcept
    {
        return _video_timing.standard;
    }

    const video_timing_t& ppu_t::video_timing() const noexcept
    {
        return _video_timing;
    }

    void ppu_t::set_frame_capture_enabled(bool enabled) noexcept
    {
        _frame_capture_enabled = enabled;
    }

    void ppu_t::set_cgram_write_trace_start_frame(uint64_t frame_index) noexcept
    {
        _cgram_write_trace_start_frame = frame_index;
        _cgram_write_trace = {};
        _cgram_write_trace_count = 0;
    }

    void ppu_t::set_oam_write_trace_start_frame(uint64_t frame_index) noexcept
    {
        _oam_write_trace_start_frame = frame_index;
        _oam_write_trace = {};
        _oam_write_trace_count = 0;
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

        const uint16_t effective_address{ static_cast<uint16_t>(address_vram() & 0x7fffu) };
        uint16_t& word{ _vram[effective_address] };
        const uint16_t previous_word{ word };
        if (high_byte)
            word = static_cast<uint16_t>((word & 0x00ffu) | (value << 8u));
        else
            word = static_cast<uint16_t>((word & 0xff00u) | value);

        static const vram_write_trace_filter_t trace_filter{ load_vram_write_trace_filter() };
        if (trace_filter.enabled
            && _frame_counter >= trace_filter.frame_min
            && _frame_counter <= trace_filter.frame_max
            && effective_address >= trace_filter.address_min
            && effective_address <= trace_filter.address_max)
        {
            const timing_snapshot_t current_timing{ timing() };
            std::printf("VRAM write: frame=%llu scanline=%u dot=%u addr=%04x %s=%02x before=%04x after=%04x raw_vmadd=%04x inc=%u high_inc=%u\n",
                        static_cast<unsigned long long>(_frame_counter),
                        static_cast<unsigned>(current_timing.raster.scanline),
                        static_cast<unsigned>(current_timing.raster.dot),
                        static_cast<unsigned>(effective_address),
                        high_byte ? "hi" : "lo",
                        static_cast<unsigned>(value),
                        static_cast<unsigned>(previous_word),
                        static_cast<unsigned>(word),
                        static_cast<unsigned>(_vram_state.address),
                        static_cast<unsigned>(_vram_state.increment_size),
                        _vram_state.increment_on_high ? 1u : 0u);
        }
    }

    uint8_t ppu_t::read_oam_byte(uint16_t address) const noexcept
    {
        if (display_active_for_oam())
            address = _oam_state.latched_address;

        return _oam[address % _oam.size()];
    }

    void ppu_t::write_oam_byte(uint16_t address, uint8_t value) noexcept
    {
        const uint16_t requested_address{ address };
        const uint16_t latched_address{ _oam_state.latched_address };
        const bool redirected{ display_active_for_oam() };
        if (redirected)
            address = latched_address;

        const uint16_t decoded_address{ static_cast<uint16_t>(address % _oam.size()) };
        _oam[decoded_address] = value;

        if (_frame_counter >= _oam_write_trace_start_frame)
        {
            const ppu_oam_write_trace_t entry{
                .frame_index = _frame_counter,
                .timing = timing(),
                .requested_address = requested_address,
                .effective_address = decoded_address,
                .latched_address = latched_address,
                .value = value,
                .redirected = redirected
            };

            if (_oam_write_trace_count < _oam_write_trace.size())
            {
                _oam_write_trace[_oam_write_trace_count++] = entry;
            }
            else
            {
                std::move(std::begin(_oam_write_trace) + 1u,
                          std::end(_oam_write_trace),
                          std::begin(_oam_write_trace));
                _oam_write_trace[_oam_write_trace.size() - 1u] = entry;
            }
        }

        if ((decoded_address & 0x0200u) == 0)
        {
            decode_oam_object(static_cast<uint8_t>(decoded_address >> 2u));
            return;
        }

        decode_oam_group(static_cast<uint8_t>(decoded_address & 0x001fu));
    }

    void ppu_t::decode_oam_object(uint8_t object_index) noexcept
    {
        auto& object{ _object_layer_state.objects[object_index & 0x7fu] };
        const uint16_t base_address{ static_cast<uint16_t>((object_index & 0x7fu) << 2u) };
        object.x = static_cast<uint16_t>((object.x & 0x0100u) | _oam[base_address]);
        object.y = _oam[base_address + 1u];
        object.character = _oam[base_address + 2u];

        const uint8_t attributes{ _oam[base_address + 3u] };
        object.nameselect = (attributes & 0x01u) != 0;
        object.palette = static_cast<uint8_t>((attributes >> 1u) & 0x07u);
        object.priority = static_cast<uint8_t>((attributes >> 4u) & 0x03u);
        object.hflip = (attributes & 0x40u) != 0;
        object.vflip = (attributes & 0x80u) != 0;
        object.width = object_width(object.size_select);
        object.height = object_height(object.size_select);
    }

    void ppu_t::decode_oam_group(uint8_t group_index) noexcept
    {
        const uint16_t address{ static_cast<uint16_t>(0x0200u | (group_index & 0x001fu)) };
        const uint8_t data{ _oam[address] };
        const uint8_t base_object{ static_cast<uint8_t>((group_index & 0x1fu) << 2u) };

        for (uint8_t offset{ 0 }; offset < 4u; ++offset)
        {
            auto& object{ _object_layer_state.objects[(base_object + offset) & 0x7fu] };
            object.x = static_cast<uint16_t>((object.x & 0x00ffu)
                | (((data >> (offset << 1u)) & 0x01u) << 8u));
            object.size_select = ((data >> ((offset << 1u) + 1u)) & 0x01u) != 0;
            object.width = object_width(object.size_select);
            object.height = object_height(object.size_select);
        }
    }

    uint8_t ppu_t::object_width(bool size_select) const noexcept
    {
        static constexpr std::array<uint8_t, 8> k_small_width{ 8, 8, 8, 16, 16, 32, 16, 16 };
        static constexpr std::array<uint8_t, 8> k_large_width{ 16, 32, 64, 32, 64, 64, 32, 32 };
        return size_select ? k_large_width[_object_layer_state.base_size] : k_small_width[_object_layer_state.base_size];
    }

    uint8_t ppu_t::object_height(bool size_select) const noexcept
    {
        static constexpr std::array<uint8_t, 8> k_small_height{ 8, 8, 8, 16, 16, 32, 32, 32 };
        static constexpr std::array<uint8_t, 8> k_large_height{ 16, 32, 64, 32, 64, 64, 64, 32 };
        if (!size_select)
        {
            if (_object_layer_state.interlace && _object_layer_state.base_size >= 6u)
                return 16u;

            return k_small_height[_object_layer_state.base_size];
        }

        return k_large_height[_object_layer_state.base_size];
    }

    bool ppu_t::object_on_scanline(const ppu_object_render_state_t::decoded_object_t& object,
                                   uint16_t scanline) const noexcept
    {
        if (object.x > 256u && static_cast<uint16_t>(object.x + object.width - 1u) < 512u)
            return false;

        uint16_t height{ object.height };
        if (_object_layer_state.interlace)
            height = static_cast<uint16_t>(height >> 1u);

        if (scanline >= object.y && scanline < static_cast<uint16_t>(object.y + height))
            return true;

        return static_cast<uint16_t>(object.y + height) >= 256u
            && scanline < static_cast<uint16_t>((object.y + height) & 0x00ffu);
    }

    void ppu_t::evaluate_background_scanline(uint16_t scanline) noexcept
    {
        for (uint8_t background_index{ 0 }; background_index < 4u; ++background_index)
        {
            auto& background{ _background_layer_state[background_index] };
            background.evaluation_scanline = scanline;
            background.tile_count = 0;
            background.offset_hoffset.fill(0);
            background.offset_voffset.fill(0);
            background.samples.fill({});
            background.tiles.fill({});
        }

        if (_display.disabled || scanline == 0u || scanline >= active_visible_scanlines())
            return;

        populate_background_offset_cache(scanline);

        for (uint8_t background_index{ 0 }; background_index < 4u; ++background_index)
        {
            if (!_bg_state.active[background_index]
                || _bg_state.render_mode[background_index] == ppu_background_render_state_t::mode_t::mode7
                || _bg_state.render_mode[background_index] == ppu_background_render_state_t::mode_t::inactive)
            {
                continue;
            }

            evaluate_background_tiles(background_index);
            fetch_background_tile_rows(background_index);
            synthesize_background_layer_candidate(background_index);
        }
    }

    void ppu_t::populate_background_offset_cache(uint16_t scanline) noexcept
    {
        if (_bg_state.mode != 2u && _bg_state.mode != 4u && _bg_state.mode != 6u)
            return;

        auto& background{ _background_layer_state[2] };
        const background_geometry_t geometry{
            .hires = _screen_state.hires,
            .large_tiles = _bg_state.large_tiles[2],
            .screen_size = _bg_state.screen_size[2],
            .screen_address = _bg_state.screen_address[2]
        };
        const background_address_window_t window{
            build_background_address_window(geometry, true)
        };

        for (uint8_t tile_slot{ 0 }; tile_slot < background.offset_hoffset.size(); ++tile_slot)
        {
            const uint16_t screen_x{ static_cast<uint16_t>(tile_slot << 3u) };
            const uint16_t hires_hoffset{
                static_cast<uint16_t>(_bg_state.hoffset[2] << (geometry.hires ? 1u : 0u))
            };
            const uint16_t horizontal_source{
                static_cast<uint16_t>(screen_x + (hires_hoffset & ~0x0007u))
            };
            background.offset_hoffset[tile_slot] = _vram[
                compute_background_addressing(geometry, window, horizontal_source, _bg_state.voffset[2]).tilemap_address
            ];
            background.offset_voffset[tile_slot] = _vram[
                compute_background_addressing(
                    geometry,
                    window,
                    horizontal_source,
                    static_cast<uint16_t>(_bg_state.voffset[2] + 8u)).tilemap_address
            ];
        }

        background.evaluation_scanline = scanline;
    }

    void ppu_t::evaluate_background_tiles(uint8_t background_index) noexcept
    {
        auto& background{ _background_layer_state[background_index] };
        const uint8_t mode_index{ background_mode_index(_bg_state.render_mode[background_index]) };
        const background_geometry_t geometry{
            .hires = _screen_state.hires,
            .large_tiles = _bg_state.large_tiles[background_index],
            .screen_size = _bg_state.screen_size[background_index],
            .screen_address = _bg_state.screen_address[background_index]
        };
        const background_address_window_t window{
            build_background_address_window(geometry, false)
        };
        const uint16_t mosaic_vertical_offset{
            static_cast<uint16_t>(_mosaic_state.enabled[background_index] ? mosaic_voffset() : 0u)
        };
        const uint16_t base_vertical_pixel{
            background_vertical_pixel(_background_layer_state[background_index].evaluation_scanline,
                                      mosaic_vertical_offset,
                                      geometry.hires,
                                      _screen_state.interlace,
                                      _counter.odd_field,
                                      _mosaic_state.enabled[background_index])
        };
        const uint16_t base_vertical_source{
            static_cast<uint16_t>(base_vertical_pixel
                + _bg_state.voffset[background_index])
        };

        const uint16_t character_mask{ static_cast<uint16_t>(k_vram_word_mask >> (3u + mode_index)) };
        const uint16_t character_index{
            static_cast<uint16_t>(_bg_state.tiledata_address[background_index] >> (3u + mode_index))
        };
        const uint8_t palette_size_shift{ static_cast<uint8_t>(2u << mode_index) };
        const uint8_t palette_offset{
            static_cast<uint8_t>(_bg_state.mode == 0u ? static_cast<uint8_t>(background_index << 5u) : 0u)
        };
        const bool offset_per_tile_mode{
            _bg_state.mode == 2u || _bg_state.mode == 4u || _bg_state.mode == 6u
        };
        const bool apply_offset_lookup{ offset_per_tile_mode && background_index < 2u };

        for (uint8_t tile_slot{ 0 }; tile_slot < background.tiles.size(); ++tile_slot)
        {
            const uint16_t screen_x{ static_cast<uint16_t>(tile_slot << 3u) };
            const uint16_t hires_hoffset{
                static_cast<uint16_t>(_bg_state.hoffset[background_index] << (geometry.hires ? 1u : 0u))
            };
            uint16_t horizontal_source{ static_cast<uint16_t>(screen_x + hires_hoffset) };
            uint16_t vertical_source{ base_vertical_source };

            if (apply_offset_lookup)
            {
                const uint16_t hlookup{ _background_layer_state[2].offset_hoffset[tile_slot] };
                const uint16_t vlookup{ _background_layer_state[2].offset_voffset[tile_slot] };
                const uint16_t valid_mask{ static_cast<uint16_t>(1u << (13u + background_index)) };

                if (_bg_state.mode == 4u)
                {
                    if ((hlookup & valid_mask) != 0)
                    {
                        if ((hlookup & 0x8000u) == 0)
                            horizontal_source = static_cast<uint16_t>(screen_x + (hlookup & ~0x0007u) + (hires_hoffset & 0x0007u));
                        else
                            vertical_source = static_cast<uint16_t>(base_vertical_pixel + hlookup);
                    }
                }
                else
                {
                    if ((hlookup & valid_mask) != 0)
                        horizontal_source = static_cast<uint16_t>(screen_x + (hlookup & ~0x0007u) + (hires_hoffset & 0x0007u));
                    if ((vlookup & valid_mask) != 0)
                        vertical_source = static_cast<uint16_t>(base_vertical_pixel + vlookup);
                }
            }

            const auto addressing{
                compute_background_addressing(geometry, window, horizontal_source, vertical_source)
            };

            auto& tile{ background.tiles[tile_slot] };
            tile.screen_x = screen_x;
            tile.source_x = addressing.horizontal_source;
            tile.source_y = addressing.vertical_source;
            tile.tilemap_address = addressing.tilemap_address;
            tile.tilemap_entry = _vram[tile.tilemap_address];
            tile.tiledata_address = _bg_state.tiledata_address[background_index];
            tile.palette_group = static_cast<uint8_t>((tile.tilemap_entry >> 10u) & 0x07u);
            tile.priority = _bg_state.priority[background_index][(tile.tilemap_entry >> 13u) & 0x01u];
            tile.hmirror = (tile.tilemap_entry & 0x4000u) != 0;
            tile.vmirror = (tile.tilemap_entry & 0x8000u) != 0;
            tile.character = static_cast<uint16_t>(tile.tilemap_entry & 0x03ffu);
            tile.fine_x = static_cast<uint8_t>(addressing.horizontal_source & 0x07u);
            tile.fine_y = static_cast<uint8_t>(addressing.vertical_source & 0x07u);

            const bool hires_character_select{ geometry.hires };
            const bool large_tile_horizontal_select{ geometry.large_tiles };
            if ((hires_character_select || large_tile_horizontal_select)
                && (((addressing.horizontal_source & 0x0008u) != 0u) != tile.hmirror))
            {
                tile.character = static_cast<uint16_t>(tile.character + 1u);
            }
            if (geometry.large_tiles && (((addressing.vertical_source & 0x0008u) != 0u) != tile.vmirror))
                tile.character = static_cast<uint16_t>(tile.character + 16u);

            if (tile.vmirror)
                tile.fine_y = static_cast<uint8_t>(tile.fine_y ^ 0x07u);

            const uint16_t origin{
                static_cast<uint16_t>((tile.character + character_index) & character_mask)
            };
            tile.vram_address = static_cast<uint16_t>((origin << (3u + mode_index)) + tile.fine_y);
            tile.palette_base = static_cast<uint8_t>(palette_offset + (tile.palette_group << palette_size_shift));
        }

        background.tile_count = static_cast<uint8_t>(background.tiles.size());
    }

    void ppu_t::fetch_background_tile_rows(uint8_t background_index) noexcept
    {
        auto& background{ _background_layer_state[background_index] };
        const uint8_t mode_index{ background_mode_index(_bg_state.render_mode[background_index]) };
        const uint8_t row_pair_count{ static_cast<uint8_t>(1u << mode_index) };

        for (uint8_t tile_slot{ 0 }; tile_slot < background.tile_count; ++tile_slot)
        {
            auto& tile{ background.tiles[tile_slot] };
            tile.row_pair_count = row_pair_count;
            for (uint8_t pair_index{ 0 }; pair_index < row_pair_count; ++pair_index)
            {
                tile.row_data[pair_index] = decode_bitplane_pair(
                    _vram[(tile.vram_address + (pair_index << 3u)) & k_vram_word_mask],
                    !tile.hmirror);
            }
        }
    }

    void ppu_t::synthesize_background_layer_candidate(uint8_t background_index) noexcept
    {
        auto& background{ _background_layer_state[background_index] };
        if (background.tile_count == 0u)
            return;

        const size_t sample_count{ sample_pixel_count() };
        for (uint8_t tile_index{ 0 }; tile_index < background.tile_count; ++tile_index)
        {
            const auto& tile{ background.tiles[tile_index] };
            for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
            {
                if (sample_x < static_cast<size_t>(tile.screen_x)
                    || sample_x >= static_cast<size_t>(tile.screen_x + 8u))
                    continue;

                const uint8_t lane_fine_x{ static_cast<uint8_t>(sample_x - static_cast<size_t>(tile.screen_x)) };
                uint8_t color{ 0 };
                for (uint8_t pair_index{ 0 }; pair_index < tile.row_pair_count; ++pair_index)
                {
                    color |= static_cast<uint8_t>(extract_row_pair_pixel(tile.row_data[pair_index], lane_fine_x)
                        << (pair_index << 1u));
                }

                if (color == 0u)
                    continue;

                const ppu_pixel_candidate_t candidate{
                    .priority = tile.priority,
                    .palette = static_cast<uint8_t>(tile.palette_base + color),
                    .palette_group = tile.palette_group,
                    .color_math_enabled = _color_math_state.bg_color_enable[background_index],
                    .source = background_pixel_source(background_index)
                };

                if (candidate.priority >= background.samples[sample_x].priority)
                    background.samples[sample_x] = candidate;
            }
        }

        auto& layer{ _compositor_state.backgrounds[background_index] };
        for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
        {
            if (_bg_state.above_enabled[background_index])
                layer.above_samples[sample_x] = background.samples[sample_x];
            if (_bg_state.below_enabled[background_index])
                layer.below_samples[sample_x] = background.samples[sample_x];
        }

        layer.above = layer.above_samples[0];
        layer.below = layer.below_samples[0];
    }

    void ppu_t::begin_object_scanline(uint16_t scanline) noexcept
    {
        // Clover initializes the scanline pipeline from the current raster line,
        // whereas bsnes' OBJ fetch state is consumed by the following visible line.
        // Translate that phase here so the bsnes-style temporary OBJ pipeline lines
        // up with Clover's raster scheduler.
        _object_layer_state.evaluation_scanline =
            static_cast<uint16_t>((scanline + 1u) % _video_timing.scanlines_per_frame);
        _object_layer_state.pipeline_x = 0u;
        _object_layer_state.evaluation_first_sprite = _object_layer_state.first_sprite;
        _object_layer_state.evaluation_count = 0;
        _object_layer_state.evaluation_progress = 0;
        _object_layer_state.evaluation_indices.fill(0);
        _object_layer_state.tile_count = 0;
        _object_layer_state.tiles.fill({});
        _object_layer_state.active_buffer = !_object_layer_state.active_buffer;
        const size_t active_buffer_index{ _object_layer_state.active_buffer ? 1u : 0u };
        for (auto& item : _object_layer_state.items[active_buffer_index])
            item = {};
        for (auto& tile : _object_layer_state.tile_buffers[active_buffer_index])
            tile = {};

        if (scanline == active_visible_scanlines() && !_display.disabled)
            reset_oam_address();
    }

    void ppu_t::clear_scanline_compositor_outputs() noexcept
    {
        _compositor_state.above = {};
        _compositor_state.below = {};
        _compositor_state.above_samples.fill({});
        _compositor_state.below_samples.fill({});
        _compositor_state.color_enable_above.fill(false);
        _compositor_state.color_enable_below.fill(false);
        _compositor_state.math_enable.fill(false);
        _compositor_state.math_uses_subscreen.fill(false);
        _compositor_state.math_uses_fixed_color.fill(false);
        _compositor_state.color_halve_active.fill(false);
        _compositor_state.above_transparent.fill(false);
        _compositor_state.below_transparent.fill(false);
        _compositor_state.above_color.fill(0);
        _compositor_state.below_color.fill(0);
        _compositor_state.math_rhs_color.fill(0);
        _compositor_state.output_color.fill(0);
        for (auto& background : _compositor_state.backgrounds)
        {
            background.above = {};
            background.below = {};
            background.above_samples.fill({});
            background.below_samples.fill({});
        }
        _compositor_state.objects.above = {};
        _compositor_state.objects.below = {};
        _compositor_state.objects.above_samples.fill({});
        _compositor_state.objects.below_samples.fill({});
    }

    void ppu_t::initialize_scanline_pipeline(uint16_t scanline) noexcept
    {
        if (_pipeline_state.initialized_scanline == scanline)
            return;

        if (scanline == 0u)
        {
            _object_layer_state.time_over = false;
            _object_layer_state.range_over = false;
        }

        advance_mosaic_scanline(scanline);
        evaluate_background_scanline(scanline);
        clear_scanline_compositor_outputs();
        begin_object_scanline(scanline);
        const size_t render_buffer_index{ _object_layer_state.active_buffer ? 0u : 1u };
        for (uint8_t background_index{ 0 }; background_index < _background_layer_state.size(); ++background_index)
        {
            auto& background{ _background_layer_state[background_index] };
            background.render_tiles = background.tiles;
            background.rendering_index = 0u;
            background.pixel_counter = static_cast<uint8_t>(_bg_state.hoffset[background_index] & 0x07u);
            background.mosaic_hcounter = _mosaic_state.size;
            background.mosaic_pixel = {};
            if (background.tile_count != 0u)
            {
                for (uint8_t pair_index{ 0 }; pair_index < background.render_tiles[0].row_pair_count; ++pair_index)
                {
                    background.render_tiles[0].row_data[pair_index] = static_cast<uint16_t>(
                        background.render_tiles[0].row_data[pair_index]
                        >> (background.pixel_counter << 1u));
                }
            }
        }

        _object_layer_state.rendered_scanline = scanline;
        _object_layer_state.render_tile_count = 0u;
        _object_layer_state.render_tiles.fill({});
        for (uint8_t tile_index{ 0 }; tile_index < _object_layer_state.render_tiles.size(); ++tile_index)
        {
            const auto& tile{ _object_layer_state.tile_buffers[render_buffer_index][tile_index] };
            if (!tile.valid)
                break;
            _object_layer_state.render_tiles[_object_layer_state.render_tile_count++] = tile.candidate;
        }
        _object_layer_state.fetched_scanline = _object_layer_state.evaluation_scanline;
        _object_layer_state.fetched_tile_count = 0u;
        _object_layer_state.fetched_tiles.fill({});

        _pipeline_state.initialized_scanline = scanline;
        _pipeline_state.next_object_evaluate_dot = 0u;
        _pipeline_state.next_pixel_dot = 58u;
        _pipeline_state.next_pixel_x = 0u;
        _pipeline_state.object_fetch_completed = false;
    }

    void ppu_t::evaluate_object_slot(uint16_t scanline, uint8_t slot) noexcept
    {
        if (_display.disabled || scanline >= active_visible_scanlines() - 1u)
            return;

        // Match bsnes OBJ evaluation: once the 33rd in-range sprite is found,
        // range-over latches and later slots no longer update the OAM latch.
        if (_object_layer_state.evaluation_count > 32u)
            return;

        const uint8_t sprite_index{
            static_cast<uint8_t>((_object_layer_state.evaluation_first_sprite + slot) & 0x7fu)
        };
        if (!object_on_scanline(_object_layer_state.objects[sprite_index],
                                _object_layer_state.evaluation_scanline))
            return;

        _oam_state.latched_address = sprite_index;
        if (_object_layer_state.evaluation_count < _object_layer_state.evaluation_indices.size())
        {
            _object_layer_state.evaluation_indices[_object_layer_state.evaluation_count] = sprite_index;
            auto& item{
                _object_layer_state.items[_object_layer_state.active_buffer ? 1u : 0u][_object_layer_state.evaluation_count]
            };
            item.valid = true;
            item.index = sprite_index;
        }
        ++_object_layer_state.evaluation_count;
        _object_layer_state.range_over = _object_layer_state.range_over
            || _object_layer_state.evaluation_count > 32u;
    }

    void ppu_t::finalize_object_fetch(uint16_t scanline) noexcept
    {
        if (_pipeline_state.object_fetch_completed)
            return;

        _pipeline_state.object_fetch_completed = true;
        if (_display.disabled || scanline >= active_visible_scanlines() - 1u)
            return;

        _object_layer_state.evaluation_count = std::min<uint8_t>(_object_layer_state.evaluation_count, 32u);
        evaluate_object_tiles();
        fetch_object_tile_rows();
        _object_layer_state.fetched_scanline = _object_layer_state.evaluation_scanline;
        _object_layer_state.fetched_tile_count = _object_layer_state.tile_count;
        _object_layer_state.fetched_tiles = _object_layer_state.tiles;
    }

    [[nodiscard]] ppu_pixel_candidate_t ppu_t::resolve_object_pixel_candidate(uint16_t x) const noexcept
    {
        ppu_pixel_candidate_t candidate{};
        const size_t render_buffer_index{ _object_layer_state.active_buffer ? 0u : 1u };
        for (const auto& fetched_tile : _object_layer_state.tile_buffers[render_buffer_index])
        {
            if (!fetched_tile.valid)
                break;

            const auto& tile{ fetched_tile.candidate };
            const int16_t pixel_x{ static_cast<int16_t>(x) };
            const int16_t screen_x{
                static_cast<int16_t>(
                    tile.x >= 256u
                        ? static_cast<int32_t>(tile.x) - 512
                        : static_cast<int32_t>(tile.x))
            };
            if (pixel_x < screen_x || pixel_x >= static_cast<int16_t>(screen_x + 8))
                continue;

            const uint8_t lane_fine_x{ static_cast<uint8_t>(pixel_x - screen_x) };
            const uint8_t shift{
                static_cast<uint8_t>(tile.hflip ? lane_fine_x : static_cast<uint8_t>(7u - lane_fine_x))
            };
            const uint8_t color{
                static_cast<uint8_t>(
                    ((tile.data >> (shift + 0u)) & 0x01u)
                    | ((tile.data >> (shift + 7u)) & 0x02u)
                    | ((tile.data >> (shift + 14u)) & 0x04u)
                    | ((tile.data >> (shift + 21u)) & 0x08u))
            };
            if (color == 0u)
                continue;

            candidate = {
                .priority = _object_layer_state.priority[tile.priority],
                .palette = static_cast<uint8_t>(tile.palette_base + color),
                .palette_group = 0u,
                .color_math_enabled = _color_math_state.obj_color_enable,
                .source = ppu_pixel_source_t::objects
            };
        }

        return candidate;
    }

    [[nodiscard]] ppu_pixel_candidate_t ppu_t::resolve_background_pixel_candidate(uint8_t background_index,
                                                                                   uint16_t x) const noexcept
    {
        if (_display.disabled || background_index >= _background_layer_state.size())
            return {};

        if (!_bg_state.active[background_index]
            || _bg_state.render_mode[background_index] == ppu_background_render_state_t::mode_t::inactive
            || _bg_state.render_mode[background_index] == ppu_background_render_state_t::mode_t::mode7)
        {
            return {};
        }

        auto& background{ const_cast<ppu_background_layer_state_t&>(_background_layer_state[background_index]) };
        if (background.tile_count == 0u)
            return {};

        if (_screen_state.hires)
            return background.samples[static_cast<size_t>(x)];

        if (background.rendering_index >= background.tile_count)
            return {};

        auto& tile{ background.render_tiles[background.rendering_index] };
        uint8_t color{ 0u };
        for (uint8_t pair_index{ 0 }; pair_index < tile.row_pair_count; ++pair_index)
        {
            color |= static_cast<uint8_t>((tile.row_data[pair_index] & 0x03u) << (pair_index << 1u));
            tile.row_data[pair_index] = static_cast<uint16_t>(tile.row_data[pair_index] >> 2u);
        }

        ppu_pixel_candidate_t candidate{};
        if (color != 0u)
        {
            candidate = {
                .priority = tile.priority,
                .palette = static_cast<uint8_t>(tile.palette_base + color),
                .palette_group = tile.palette_group,
                .color_math_enabled = _color_math_state.bg_color_enable[background_index],
                .source = background_pixel_source(background_index)
            };
        }

        background.pixel_counter = static_cast<uint8_t>((background.pixel_counter + 1u) & 0x07u);
        if (background.pixel_counter == 0u)
            ++background.rendering_index;

        if (x == 0u || !_mosaic_state.enabled[background_index])
        {
            background.mosaic_hcounter = _mosaic_state.size;
            background.mosaic_pixel = candidate;
        }
        else if (background.mosaic_hcounter > 1u)
        {
            --background.mosaic_hcounter;
            candidate = background.mosaic_pixel;
        }
        else
        {
            background.mosaic_hcounter = _mosaic_state.size;
            background.mosaic_pixel = candidate;
        }

        return candidate;
    }

    void ppu_t::resolve_pixel_layers(uint16_t x,
                                     const ppu_pixel_candidate_t& object_candidate,
                                     ppu_pixel_candidate_t& above,
                                     ppu_pixel_candidate_t& below) noexcept
    {
        const uint8_t pixel_x{ static_cast<uint8_t>(x) };
        const size_t sample_x{ static_cast<size_t>(x) };

        for (uint8_t background_index{ 0 }; background_index < 4u; ++background_index)
        {
            ppu_pixel_candidate_t background_above{};
            ppu_pixel_candidate_t background_below{};
            if (_bg_state.active[background_index])
            {
                const auto candidate{ resolve_background_pixel_candidate(background_index, x) };
                _background_layer_state[background_index].samples[sample_x] = candidate;
                background_above = _bg_state.above_enabled[background_index] ? candidate : ppu_pixel_candidate_t{};
                background_below = _bg_state.below_enabled[background_index] ? candidate : ppu_pixel_candidate_t{};

                const bool background_window_hit{
                    window_hit(pixel_x,
                               _window_state.one_left,
                               _window_state.one_right,
                               _window_state.two_left,
                               _window_state.two_right,
                               _window_state.one_invert[background_index],
                               _window_state.one_enable[background_index],
                               _window_state.two_invert[background_index],
                               _window_state.two_enable[background_index],
                               _bg_state.window_mask[background_index])
                };
                if (background_window_hit && _bg_state.window_above_enabled[background_index])
                    background_above = {};
                if (background_window_hit && _bg_state.window_below_enabled[background_index])
                    background_below = {};
            }

            _compositor_state.backgrounds[background_index].above_samples[sample_x] = background_above;
            _compositor_state.backgrounds[background_index].below_samples[sample_x] = background_below;
            if (background_above.priority > above.priority)
                above = background_above;
            if (background_below.priority > below.priority)
                below = background_below;
        }

        ppu_pixel_candidate_t object_above{
            _object_layer_state.above_enabled ? object_candidate : ppu_pixel_candidate_t{}
        };
        ppu_pixel_candidate_t object_below{
            _object_layer_state.below_enabled ? object_candidate : ppu_pixel_candidate_t{}
        };
        const bool object_window_hit{
            window_hit(pixel_x,
                       _window_state.one_left,
                       _window_state.one_right,
                       _window_state.two_left,
                       _window_state.two_right,
                       _window_state.one_invert[4],
                       _window_state.one_enable[4],
                       _window_state.two_invert[4],
                       _window_state.two_enable[4],
                       _window_state.object_mask)
        };
        if (object_window_hit && _object_layer_state.window_above_enabled)
            object_above = {};
        if (object_window_hit && _object_layer_state.window_below_enabled)
            object_below = {};

        _compositor_state.objects.above_samples[sample_x] = object_above;
        _compositor_state.objects.below_samples[sample_x] = object_below;
        if (object_above.priority > above.priority)
            above = object_above;
        if (object_below.priority > below.priority)
            below = object_below;

        const bool color_window_hit{
            window_hit(pixel_x,
                       _window_state.one_left,
                       _window_state.one_right,
                       _window_state.two_left,
                       _window_state.two_right,
                       _window_state.one_invert[5],
                       _window_state.one_enable[5],
                       _window_state.two_invert[5],
                       _window_state.two_enable[5],
                       _window_state.color_mask)
        };
        const std::array<bool, 4> color_enable{
            true,
            color_window_hit,
            !color_window_hit,
            false
        };
        _compositor_state.color_enable_above[sample_x] =
            color_enable[_window_state.color_mask_above & 0x03u];
        _compositor_state.color_enable_below[sample_x] =
            color_enable[_window_state.color_mask_below & 0x03u];
    }

    void ppu_t::resolve_pixel_color_math(uint16_t x,
                                         const ppu_pixel_candidate_t& above_candidate,
                                         const ppu_pixel_candidate_t& below_candidate) noexcept
    {
        const size_t sample_x{ static_cast<size_t>(x) };
        const bool hires{ _screen_state.pseudo_hires || _bg_state.mode == 5u || _bg_state.mode == 6u };
        const bool above_transparent{ above_candidate.priority == 0u };
        const bool below_transparent{ below_candidate.priority == 0u };
        _compositor_state.above_transparent[sample_x] = above_transparent;
        _compositor_state.below_transparent[sample_x] = below_transparent;

        const auto resolve_candidate_color = [this](const ppu_pixel_candidate_t& candidate) noexcept -> uint16_t
        {
            if (candidate.priority == 0u)
            {
                _cgram_state.latched_address = 0u;
                return _cgram[0];
            }

            if (_color_math_state.direct_color
                && candidate.source == ppu_pixel_source_t::background_1
                && (_bg_state.mode == 3u || _bg_state.mode == 4u || _bg_state.mode == 7u))
            {
                return direct_color(candidate.palette, candidate.palette_group);
            }

            _cgram_state.latched_address = candidate.palette;
            return _cgram[candidate.palette];
        };

        // Match bsnes screen evaluation order so active-display CGRAM access
        // sees the final above/main-screen palette latch for the pixel.
        const uint16_t below_color{ below_transparent ? _cgram[0] : resolve_candidate_color(below_candidate) };
        const uint16_t above_color{ above_transparent ? _cgram[0] : resolve_candidate_color(above_candidate) };
        const uint16_t fixed_color{
            static_cast<uint16_t>(
                (_color_math_state.fixed_blue << 10u)
                | (_color_math_state.fixed_green << 5u)
                | _color_math_state.fixed_red)
        };
        ppu_pixel_candidate_t math_source{ .source = ppu_pixel_source_t::backdrop };
        if (!above_transparent)
            math_source = above_candidate;

        bool math_below_color_enable{
            source_allows_color_math(math_source, _color_math_state.backdrop_color_enable)
        };
        if (!_compositor_state.color_enable_below[sample_x])
            math_below_color_enable = false;

        const bool math_above_color_enable{ _compositor_state.color_enable_above[sample_x] };
        bool uses_subscreen{ false };
        bool uses_fixed_color{ false };
        bool color_halve_active{ false };
        bool math_enabled{ false };
        uint16_t math_rhs_color{ fixed_color };
        uint16_t output_color{
            static_cast<uint16_t>(math_above_color_enable ? above_color : 0u)
        };

        if (math_below_color_enable)
        {
            bool blend_mode{ _color_math_state.blend_mode };
            if (_color_math_state.blend_mode && below_transparent)
            {
                blend_mode = false;
                color_halve_active = false;
            }
            else
            {
                color_halve_active = _color_math_state.color_halve && math_above_color_enable;
            }

            uses_subscreen = blend_mode;
            uses_fixed_color = !blend_mode;
            math_rhs_color = blend_mode ? below_color : fixed_color;
            math_enabled = true;
            output_color = blend_colors(static_cast<uint16_t>(math_above_color_enable ? above_color : 0u),
                                        math_rhs_color,
                                        _color_math_state.color_mode_subtract,
                                        color_halve_active);
        }

        static_cast<void>(hires);

        _compositor_state.math_enable[sample_x] = math_enabled;
        _compositor_state.math_uses_subscreen[sample_x] = uses_subscreen;
        _compositor_state.math_uses_fixed_color[sample_x] = uses_fixed_color;
        _compositor_state.color_halve_active[sample_x] = color_halve_active;
        _compositor_state.above_color[sample_x] = above_color;
        _compositor_state.below_color[sample_x] = below_color;
        _compositor_state.math_rhs_color[sample_x] = math_rhs_color;
        _compositor_state.output_color[sample_x] = output_color;
    }

    void ppu_t::render_pixel(uint16_t scanline, uint16_t x) noexcept
    {
        if (scanline == 0u || scanline >= active_visible_scanlines() || x >= framebuffer_t::k_width)
            return;

        ppu_pixel_candidate_t above{};
        ppu_pixel_candidate_t below{};
        const ppu_pixel_candidate_t object_candidate{ resolve_object_pixel_candidate(x) };
        resolve_pixel_layers(x, object_candidate, above, below);
        resolve_pixel_color_math(x, above, below);

        const size_t sample_x{ static_cast<size_t>(x) };
        _compositor_state.above_samples[sample_x] = above;
        _compositor_state.below_samples[sample_x] = below;
        if (x == 0u)
        {
            _compositor_state.above = above;
            _compositor_state.below = below;
            for (auto& background : _compositor_state.backgrounds)
            {
                background.above = background.above_samples[0];
                background.below = background.below_samples[0];
            }
            _compositor_state.objects.above = _compositor_state.objects.above_samples[0];
            _compositor_state.objects.below = _compositor_state.objects.below_samples[0];
        }

        if (!_frame_capture_enabled)
            return;

        constexpr size_t k_non_overscan_top_border{ 9u };
        const size_t row_index{
            static_cast<size_t>(scanline - 1u) + (_screen_state.overscan ? 0u : k_non_overscan_top_border)
        };
        if (row_index >= framebuffer_t::k_height)
            return;

        const uint32_t rgba8{
            snes_color_to_rgba8(_compositor_state.output_color[sample_x], _display.brightness)
        };
        _composed_frame.data()[row_index * framebuffer_t::k_width + sample_x] = rgba8;

        static const render_write_trace_filter_t trace_filter{ load_render_write_trace_filter() };
        if (trace_filter.enabled
            && _frame_counter == trace_filter.frame
            && scanline == trace_filter.scanline
            && x == trace_filter.x)
        {
            const auto& object_above{ _compositor_state.objects.above_samples[sample_x] };
            const auto& object_below{ _compositor_state.objects.below_samples[sample_x] };
            std::printf(
                "PPU render write: frame=%llu scanline=%u x=%u out=%04x rgba=%08x "
                "above=%04x below=%04x objA(pri=%u pal=%u grp=%u math=%u src=%u) "
                "objB(pri=%u pal=%u grp=%u math=%u src=%u) "
                "topA(pri=%u pal=%u grp=%u math=%u src=%u) "
                "topB(pri=%u pal=%u grp=%u math=%u src=%u)\n",
                static_cast<unsigned long long>(_frame_counter),
                static_cast<unsigned>(scanline),
                static_cast<unsigned>(x),
                static_cast<unsigned>(_compositor_state.output_color[sample_x]),
                static_cast<unsigned>(rgba8),
                static_cast<unsigned>(_compositor_state.above_color[sample_x]),
                static_cast<unsigned>(_compositor_state.below_color[sample_x]),
                static_cast<unsigned>(object_above.priority),
                static_cast<unsigned>(object_above.palette),
                static_cast<unsigned>(object_above.palette_group),
                object_above.color_math_enabled ? 1u : 0u,
                static_cast<unsigned>(object_above.source),
                static_cast<unsigned>(object_below.priority),
                static_cast<unsigned>(object_below.palette),
                static_cast<unsigned>(object_below.palette_group),
                object_below.color_math_enabled ? 1u : 0u,
                static_cast<unsigned>(object_below.source),
                static_cast<unsigned>(_compositor_state.above_samples[sample_x].priority),
                static_cast<unsigned>(_compositor_state.above_samples[sample_x].palette),
                static_cast<unsigned>(_compositor_state.above_samples[sample_x].palette_group),
                _compositor_state.above_samples[sample_x].color_math_enabled ? 1u : 0u,
                static_cast<unsigned>(_compositor_state.above_samples[sample_x].source),
                static_cast<unsigned>(_compositor_state.below_samples[sample_x].priority),
                static_cast<unsigned>(_compositor_state.below_samples[sample_x].palette),
                static_cast<unsigned>(_compositor_state.below_samples[sample_x].palette_group),
                _compositor_state.below_samples[sample_x].color_math_enabled ? 1u : 0u,
                static_cast<unsigned>(_compositor_state.below_samples[sample_x].source));
        }
    }

    void ppu_t::process_scanline_range(uint16_t scanline, uint16_t start_dot, uint16_t end_dot) noexcept
    {
        while (_pipeline_state.next_object_evaluate_dot <= end_dot
            && _pipeline_state.next_object_evaluate_dot <= 1016u)
        {
            if (_pipeline_state.next_object_evaluate_dot >= start_dot
                && _object_layer_state.evaluation_progress < 128u)
            {
                evaluate_object_slot(scanline, _object_layer_state.evaluation_progress);
                ++_object_layer_state.evaluation_progress;
            }
            _pipeline_state.next_object_evaluate_dot =
                static_cast<uint16_t>(_pipeline_state.next_object_evaluate_dot + 8u);
        }

        if (!_pipeline_state.object_fetch_completed
            && start_dot <= 1080u
            && end_dot >= 1080u)
        {
            finalize_object_fetch(scanline);
        }

        while (_pipeline_state.next_pixel_dot <= end_dot
            && _pipeline_state.next_pixel_dot <= 1078u
            && _pipeline_state.next_pixel_x < framebuffer_t::k_width)
        {
            if (_pipeline_state.next_pixel_dot >= start_dot)
                render_pixel(scanline, _pipeline_state.next_pixel_x);

            ++_pipeline_state.next_pixel_x;
            _pipeline_state.next_pixel_dot =
                static_cast<uint16_t>(_pipeline_state.next_pixel_dot + 4u);
        }
    }

    void ppu_t::process_pipeline_range(const timing_snapshot_t& previous_timing,
                                       const timing_snapshot_t& current_timing) noexcept
    {
        uint16_t scanline{ previous_timing.raster.scanline };
        for (uint16_t remaining{ _video_timing.scanlines_per_frame }; remaining > 0; --remaining)
        {
            initialize_scanline_pipeline(scanline);

            const uint16_t scanline_end_dot{
                static_cast<uint16_t>(_video_timing.scanline_clocks(scanline,
                                                                    _counter.odd_field,
                                                                    _timing_interlace) - 1u)
            };
            const uint16_t start_dot{
                static_cast<uint16_t>(scanline == previous_timing.raster.scanline
                    ? previous_timing.raster.dot
                    : 0u)
            };
            const uint16_t end_dot{
                scanline == current_timing.raster.scanline ? current_timing.raster.dot : scanline_end_dot
            };
            if (end_dot >= start_dot)
                process_scanline_range(scanline, start_dot, end_dot);

            if (scanline == current_timing.raster.scanline)
                break;

            scanline = static_cast<uint16_t>((scanline + 1u) % _video_timing.scanlines_per_frame);
        }
    }

    void ppu_t::evaluate_object_scanline(uint16_t scanline) noexcept
    {
        begin_object_scanline(scanline);

        if (_display.disabled || scanline >= active_visible_scanlines() - 1u)
            return;

        uint8_t visible_count{ 0 };
        for (uint16_t index{ 0 }; index < 128u; ++index)
        {
            const uint8_t sprite_index{
                static_cast<uint8_t>((_object_layer_state.evaluation_first_sprite + index) & 0x7fu)
            };

            if (!object_on_scanline(_object_layer_state.objects[sprite_index], scanline))
                continue;

            _oam_state.latched_address = sprite_index;
            if (visible_count < _object_layer_state.evaluation_indices.size())
                _object_layer_state.evaluation_indices[visible_count] = sprite_index;

            ++visible_count;
        }

        _object_layer_state.evaluation_count = std::min<uint8_t>(visible_count, 32u);
        _object_layer_state.range_over = visible_count > 32u;
        evaluate_object_tiles();
        fetch_object_tile_rows();
    }

    void ppu_t::evaluate_object_tiles() noexcept
    {
        const size_t active_buffer_index{ _object_layer_state.active_buffer ? 1u : 0u };
        auto& active_tiles{ _object_layer_state.tile_buffers[active_buffer_index] };
        uint8_t visible_tile_count{ 0 };
        uint8_t pipeline_tile_count{ 0 };
        _object_layer_state.time_over = false;
        for (auto& tile : active_tiles)
            tile = {};

        for (int object_slot{ 31 };
             object_slot >= 0;
             --object_slot)
        {
            const auto& item{ _object_layer_state.items[active_buffer_index][static_cast<size_t>(object_slot)] };
            if (!item.valid)
                continue;

            const uint8_t object_index{ item.index };
            _oam_state.latched_address = static_cast<uint16_t>(0x0200u + (object_index >> 2u));
            const auto& object{ _object_layer_state.objects[object_index] };
            static const obj_tile_trace_filter_t trace_filter{ load_obj_tile_trace_filter() };
            if (trace_filter.enabled
                && trace_filter.frame == _frame_counter
                && trace_filter.scanline == _object_layer_state.evaluation_scanline)
            {
                const uint16_t base_address{ static_cast<uint16_t>(object_index << 2u) };
                const uint16_t high_address{ static_cast<uint16_t>(0x0200u | (object_index >> 2u)) };
                std::printf(
                    "  objsrc[%u]: raw=%02x %02x %02x %02x high=%02x decoded_x=%u decoded_y=%u w=%u h=%u hflip=%u vflip=%u size=%u\n",
                    static_cast<unsigned>(object_index),
                    static_cast<unsigned>(_oam[base_address + 0u]),
                    static_cast<unsigned>(_oam[base_address + 1u]),
                    static_cast<unsigned>(_oam[base_address + 2u]),
                    static_cast<unsigned>(_oam[base_address + 3u]),
                    static_cast<unsigned>(_oam[high_address]),
                    static_cast<unsigned>(object.x),
                    static_cast<unsigned>(object.y),
                    static_cast<unsigned>(object.width),
                    static_cast<unsigned>(object.height),
                    object.hflip ? 1u : 0u,
                    object.vflip ? 1u : 0u,
                    object.size_select ? 1u : 0u);
            }
            const uint8_t tile_width{ static_cast<uint8_t>(object.width >> 3u) };
            const uint16_t masked_x{ static_cast<uint16_t>(object.x & 0x01ffu) };
            uint16_t source_y{
                static_cast<uint16_t>((_object_layer_state.evaluation_scanline - object.y) & 0x00ffu)
            };
            if (_object_layer_state.interlace)
            {
                source_y = static_cast<uint16_t>(source_y << 1u);
                source_y = static_cast<uint16_t>(
                    object.vflip
                        ? source_y - (_counter.odd_field ? 1u : 0u)
                        : source_y + (_counter.odd_field ? 1u : 0u));
            }

            if (object.vflip)
            {
                if (object.width == object.height)
                {
                    source_y = static_cast<uint16_t>(object.height - 1u - source_y);
                }
                else if (source_y < object.width)
                {
                    source_y = static_cast<uint16_t>(object.width - 1u - source_y);
                }
                else
                {
                    source_y = static_cast<uint16_t>(object.width + (object.width - 1u) - (source_y - object.width));
                }
            }

            uint16_t tiledata_address{ _object_layer_state.tiledata_address };
            if (object.nameselect)
                tiledata_address = static_cast<uint16_t>(tiledata_address + ((1u + _object_layer_state.nameselect) << 12u));

            const uint16_t character_x{ static_cast<uint16_t>(object.character & 0x0fu) };
            const uint16_t character_y{
                static_cast<uint16_t>(((object.character >> 4u) + (source_y >> 3u)) & 0x0fu)
            };

            for (uint8_t tx{ 0 }; tx < tile_width; ++tx)
            {
                const uint16_t tile_x{ static_cast<uint16_t>((masked_x + (tx << 3u)) & 0x01ffu) };
                if (masked_x != 256u && tile_x >= 256u && tile_x + 7u < 512u)
                    continue;

                const uint8_t tile_index{ pipeline_tile_count++ };
                if (tile_index >= active_tiles.size())
                    break;

                if (tile_index >= _object_layer_state.tiles.size())
                    continue;

                auto& tile{ active_tiles[tile_index] };
                tile.valid = true;
                auto& candidate{ tile.candidate };
                candidate.object_index = object_index;
                candidate.x = tile_x;
                candidate.tile_x = object.hflip
                    ? static_cast<uint8_t>(tile_width - 1u - tx)
                    : tx;
                candidate.source_y = static_cast<uint8_t>(source_y & 0x00ffu);
                candidate.fine_y = static_cast<uint8_t>(source_y & 0x07u);
                candidate.tiledata_address = tiledata_address;
                const uint16_t pos{
                    static_cast<uint16_t>(tiledata_address
                        + (((character_y << 4u) + ((character_x + candidate.tile_x) & 0x0fu)) << 4u))
                };
                candidate.vram_address = static_cast<uint16_t>((pos & 0xfff0u) + candidate.fine_y);
                candidate.palette_base = static_cast<uint8_t>(128u + (object.palette << 4u));
                candidate.priority = object.priority;
                candidate.hflip = object.hflip;
                ++visible_tile_count;
            }
        }

        _object_layer_state.time_over = pipeline_tile_count > active_tiles.size();
        _object_layer_state.tile_count = visible_tile_count;
        _object_layer_state.tiles.fill({});
        for (uint8_t tile_index{ 0 }; tile_index < visible_tile_count; ++tile_index)
            _object_layer_state.tiles[tile_index] = active_tiles[tile_index].candidate;
        maybe_trace_obj_tiles(_frame_counter,
                              _object_layer_state.evaluation_scanline,
                              "pre-fetch",
                              active_tiles,
                              _object_layer_state.tile_count,
                              pipeline_tile_count,
                              visible_tile_count);
    }

    void ppu_t::fetch_object_tile_rows() noexcept
    {
        const size_t active_buffer_index{ _object_layer_state.active_buffer ? 1u : 0u };
        auto& active_tiles{ _object_layer_state.tile_buffers[active_buffer_index] };
        for (auto& tile_entry : active_tiles)
        {
            if (!tile_entry.valid)
                break;

            auto& tile{ tile_entry.candidate };
            tile.row_pair_count = 2u;
            const uint16_t plane_lo{ _vram[tile.vram_address & k_vram_word_mask] };
            const uint16_t plane_hi{ _vram[(tile.vram_address + 8u) & k_vram_word_mask] };
            tile.row_data[0] = decode_bitplane_pair(plane_lo, !tile.hflip);
            tile.row_data[1] = decode_bitplane_pair(plane_hi, !tile.hflip);
            tile.data = static_cast<uint32_t>(plane_lo)
                | (static_cast<uint32_t>(plane_hi) << 16u);
        }
        _object_layer_state.tiles.fill({});
        for (uint8_t tile_index{ 0 }; tile_index < _object_layer_state.tile_count; ++tile_index)
            _object_layer_state.tiles[tile_index] = active_tiles[tile_index].candidate;
        maybe_trace_obj_tiles(_frame_counter,
                              _object_layer_state.evaluation_scanline,
                              "post-fetch",
                              active_tiles,
                              _object_layer_state.tile_count,
                              _object_layer_state.tile_count,
                              _object_layer_state.tile_count);
    }

    void ppu_t::synthesize_object_layer_candidate() noexcept
    {
        const size_t sample_count{ sample_pixel_count() };
        for (uint8_t tile_index{ 0 }; tile_index < _object_layer_state.tile_count; ++tile_index)
        {
            const auto& tile{ _object_layer_state.tiles[tile_index] };
            const int16_t screen_x{
                static_cast<int16_t>(
                    tile.x >= 256u
                        ? static_cast<int32_t>(tile.x) - 512
                        : static_cast<int32_t>(tile.x))
            };
            if (screen_x >= static_cast<int16_t>(sample_count))
                continue;

            for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
            {
                if (sample_x < static_cast<size_t>(screen_x)
                    || sample_x >= static_cast<size_t>(screen_x + 8))
                    continue;

                const uint8_t lane_fine_x{ static_cast<uint8_t>(sample_x - static_cast<size_t>(screen_x)) };
                const uint8_t color{
                    static_cast<uint8_t>(extract_row_pair_pixel(tile.row_data[0], lane_fine_x)
                        | (extract_row_pair_pixel(tile.row_data[1], lane_fine_x) << 2u))
                };
                if (color == 0u)
                    continue;

                const ppu_pixel_candidate_t candidate{
                    .priority = _object_layer_state.priority[tile.priority],
                    .palette = static_cast<uint8_t>(tile.palette_base + color),
                    .palette_group = 0u,
                    .color_math_enabled = _color_math_state.obj_color_enable,
                    .source = ppu_pixel_source_t::objects
                };

                if (_object_layer_state.above_enabled)
                    _compositor_state.objects.above_samples[sample_x] = candidate;
                if (_object_layer_state.below_enabled)
                    _compositor_state.objects.below_samples[sample_x] = candidate;
            }
        }

        _compositor_state.objects.above = _compositor_state.objects.above_samples[0];
        _compositor_state.objects.below = _compositor_state.objects.below_samples[0];
    }

    void ppu_t::apply_window_masks() noexcept
    {
        const size_t sample_count{ sample_pixel_count() };
        for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
        {
            const uint8_t x{ static_cast<uint8_t>(sample_x) };

            for (uint8_t background_index{ 0 }; background_index < 4u; ++background_index)
            {
                const bool background_window_hit{
                    window_hit(x,
                               _window_state.one_left,
                               _window_state.one_right,
                               _window_state.two_left,
                               _window_state.two_right,
                               _window_state.one_invert[background_index],
                               _window_state.one_enable[background_index],
                               _window_state.two_invert[background_index],
                               _window_state.two_enable[background_index],
                               _bg_state.window_mask[background_index])
                };

                auto& background{ _compositor_state.backgrounds[background_index] };
                if (background_window_hit && _bg_state.window_above_enabled[background_index])
                    background.above_samples[sample_x] = {};
                if (background_window_hit && _bg_state.window_below_enabled[background_index])
                    background.below_samples[sample_x] = {};
            }

            const bool object_window_hit{
                window_hit(x,
                           _window_state.one_left,
                           _window_state.one_right,
                           _window_state.two_left,
                           _window_state.two_right,
                           _window_state.one_invert[4],
                           _window_state.one_enable[4],
                           _window_state.two_invert[4],
                           _window_state.two_enable[4],
                           _window_state.object_mask)
            };
            if (object_window_hit && _object_layer_state.window_above_enabled)
                _compositor_state.objects.above_samples[sample_x] = {};
            if (object_window_hit && _object_layer_state.window_below_enabled)
                _compositor_state.objects.below_samples[sample_x] = {};

            const bool color_window_hit{
                window_hit(x,
                           _window_state.one_left,
                           _window_state.one_right,
                           _window_state.two_left,
                           _window_state.two_right,
                           _window_state.one_invert[5],
                           _window_state.one_enable[5],
                           _window_state.two_invert[5],
                           _window_state.two_enable[5],
                           _window_state.color_mask)
            };
            const std::array<bool, 4> color_enable{
                true,
                color_window_hit,
                !color_window_hit,
                false
            };
            _compositor_state.color_enable_above[sample_x] =
                color_enable[_window_state.color_mask_above & 0x03u];
            _compositor_state.color_enable_below[sample_x] =
                color_enable[_window_state.color_mask_below & 0x03u];
        }

        for (auto& background : _compositor_state.backgrounds)
        {
            background.above = background.above_samples[0];
            background.below = background.below_samples[0];
        }
        _compositor_state.objects.above = _compositor_state.objects.above_samples[0];
        _compositor_state.objects.below = _compositor_state.objects.below_samples[0];
    }

    void ppu_t::resolve_compositor_candidates() noexcept
    {
        _compositor_state.above = {};
        _compositor_state.below = {};
        _compositor_state.above_samples.fill({});
        _compositor_state.below_samples.fill({});

        const size_t sample_count{ sample_pixel_count() };
        for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
        {
            for (uint8_t background_index{ 0 }; background_index < 4u; ++background_index)
            {
                const auto& background{ _compositor_state.backgrounds[background_index] };
                if (background.above_samples[sample_x].priority > _compositor_state.above_samples[sample_x].priority)
                    _compositor_state.above_samples[sample_x] = background.above_samples[sample_x];
                if (background.below_samples[sample_x].priority > _compositor_state.below_samples[sample_x].priority)
                    _compositor_state.below_samples[sample_x] = background.below_samples[sample_x];
            }

            if (_compositor_state.objects.above_samples[sample_x].priority
                > _compositor_state.above_samples[sample_x].priority)
            {
                _compositor_state.above_samples[sample_x] = _compositor_state.objects.above_samples[sample_x];
            }

            if (_compositor_state.objects.below_samples[sample_x].priority
                > _compositor_state.below_samples[sample_x].priority)
            {
                _compositor_state.below_samples[sample_x] = _compositor_state.objects.below_samples[sample_x];
            }
        }

        _compositor_state.above = _compositor_state.above_samples[0];
        _compositor_state.below = _compositor_state.below_samples[0];
    }

    void ppu_t::resolve_color_math_state() noexcept
    {
        const size_t sample_count{ sample_pixel_count() };
        for (size_t sample_x{ 0 }; sample_x < sample_count; ++sample_x)
        {
            const auto& above_candidate{ _compositor_state.above_samples[sample_x] };
            const auto& below_candidate{ _compositor_state.below_samples[sample_x] };

            const bool above_transparent{ above_candidate.priority == 0u };
            const bool below_transparent{ below_candidate.priority == 0u };
            _compositor_state.above_transparent[sample_x] = above_transparent;
            _compositor_state.below_transparent[sample_x] = below_transparent;

            ppu_pixel_candidate_t math_source{
                .source = ppu_pixel_source_t::backdrop
            };
            if (!above_transparent)
                math_source = above_candidate;

            bool math_enabled{
                source_allows_color_math(math_source, _color_math_state.backdrop_color_enable)
                && _compositor_state.color_enable_below[sample_x]
            };
            const bool uses_subscreen{
                math_enabled
                && _color_math_state.blend_mode
                && !above_transparent
                && !below_transparent
            };
            const bool uses_fixed_color{ math_enabled && !uses_subscreen };
            const bool color_halve_active{
                math_enabled
                && _color_math_state.color_halve
                && !above_transparent
            };

            const auto resolve_candidate_color = [this](const ppu_pixel_candidate_t& candidate) noexcept -> uint16_t
            {
                if (candidate.priority == 0u)
                {
                    _cgram_state.latched_address = 0u;
                    return _cgram[0];
                }

                if (_color_math_state.direct_color
                    && candidate.source == ppu_pixel_source_t::background_1
                    && (_bg_state.mode == 3u || _bg_state.mode == 4u || _bg_state.mode == 7u))
                {
                    return direct_color(candidate.palette, candidate.palette_group);
                }

                _cgram_state.latched_address = candidate.palette;
                return _cgram[candidate.palette];
            };

            const uint16_t above_color{
                above_transparent ? _cgram[0] : resolve_candidate_color(above_candidate)
            };
            const uint16_t below_color{
                below_transparent ? _cgram[0] : resolve_candidate_color(below_candidate)
            };
            const uint16_t fixed_color{
                static_cast<uint16_t>(
                    (_color_math_state.fixed_blue << 10u)
                    | (_color_math_state.fixed_green << 5u)
                    | _color_math_state.fixed_red)
            };
            const uint16_t math_rhs_color{ uses_subscreen ? below_color : fixed_color };
            const uint16_t visible_above_color{
                static_cast<uint16_t>(
                    _compositor_state.color_enable_above[sample_x] ? above_color : 0u)
            };
            const uint16_t output_color{
                math_enabled
                    ? blend_colors(visible_above_color,
                                   math_rhs_color,
                                   _color_math_state.color_mode_subtract,
                                   color_halve_active)
                    : visible_above_color
            };

            _compositor_state.math_enable[sample_x] = math_enabled;
            _compositor_state.math_uses_subscreen[sample_x] = uses_subscreen;
            _compositor_state.math_uses_fixed_color[sample_x] = uses_fixed_color;
            _compositor_state.color_halve_active[sample_x] = color_halve_active;
            _compositor_state.above_color[sample_x] = above_color;
            _compositor_state.below_color[sample_x] = below_color;
            _compositor_state.math_rhs_color[sample_x] = math_rhs_color;
            _compositor_state.output_color[sample_x] = output_color;
        }
    }

    uint8_t ppu_t::read_cgram_byte(bool high_byte, uint8_t address) const noexcept
    {
        if (display_active_for_cgram())
            address = _cgram_state.latched_address;

        const uint16_t color{ _cgram[address] };
        if (high_byte)
            return static_cast<uint8_t>((color >> 8u) & 0x7fu);

        return static_cast<uint8_t>(color & 0x00ffu);
    }

    void ppu_t::write_cgram_word(uint8_t address, uint16_t value) noexcept
    {
        const uint8_t requested_address{ address };
        const uint8_t latched_address{ _cgram_state.latched_address };
        const bool redirected{ display_active_for_cgram() };
        if (redirected)
            address = latched_address;

        _cgram[address] = static_cast<uint16_t>(value & 0x7fffu);

        const ppu_cgram_write_trace_t entry{
            .frame_index = _frame_counter,
            .timing = timing(),
            .requested_address = requested_address,
            .effective_address = address,
            .latched_address = latched_address,
            .value = static_cast<uint16_t>(value & 0x7fffu),
            .redirected = redirected
        };
        if (_frame_counter < _cgram_write_trace_start_frame)
            return;

        if (_cgram_write_trace_count < _cgram_write_trace.size())
        {
            _cgram_write_trace[_cgram_write_trace_count++] = entry;
        }
        else
        {
            std::move(std::begin(_cgram_write_trace) + 1u,
                      std::end(_cgram_write_trace),
                      std::begin(_cgram_write_trace));
            _cgram_write_trace[_cgram_write_trace.size() - 1u] = entry;
        }
    }

    void ppu_t::reset_oam_address() noexcept
    {
        _oam_state.address = static_cast<uint16_t>(_oam_state.base_address & 0x03ffu);
        update_first_sprite();
    }

    void ppu_t::update_first_sprite() noexcept
    {
        _object_layer_state.first_sprite = _oam_state.priority
            ? static_cast<uint8_t>((_oam_state.address >> 2u) & 0x7fu)
            : 0u;
    }

    bool ppu_t::mosaic_enabled() const noexcept
    {
        return _mosaic_state.enabled[0]
            || _mosaic_state.enabled[1]
            || _mosaic_state.enabled[2]
            || _mosaic_state.enabled[3];
    }

    uint8_t ppu_t::mosaic_voffset() const noexcept
    {
        return _mosaic_state.vcounter > _mosaic_state.size
            ? 0u
            : static_cast<uint8_t>(_mosaic_state.size - _mosaic_state.vcounter);
    }

    void ppu_t::advance_mosaic_scanline(uint16_t scanline) noexcept
    {
        if (scanline == 1u)
            _mosaic_state.vcounter = mosaic_enabled() ? static_cast<uint8_t>(_mosaic_state.size + 1u) : 0u;

        if (_mosaic_state.vcounter != 0u)
        {
            --_mosaic_state.vcounter;
            if (_mosaic_state.vcounter == 0u)
                _mosaic_state.vcounter = mosaic_enabled() ? _mosaic_state.size : 0u;
        }
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

    void ppu_t::latch_counters_external() noexcept
    {
        latch_counters();
    }

    void ppu_t::set_external_latch_enabled(bool enabled) noexcept
    {
        _external_latch_enabled = enabled;
    }

    void ppu_t::clear_compositor_state() noexcept
    {
        _compositor_state = {};
    }

    size_t ppu_t::sample_pixel_count() const noexcept
    {
        return framebuffer_t::k_width;
    }

    void ppu_t::render_scanline(uint16_t scanline) noexcept
    {
        if (!_frame_capture_enabled)
            return;

        if (scanline == 0u || scanline >= active_visible_scanlines())
            return;

        constexpr size_t k_non_overscan_top_border{ 9u };
        const size_t row_index{
            static_cast<size_t>(scanline - 1u) + (_screen_state.overscan ? 0u : k_non_overscan_top_border)
        };
        if (row_index >= framebuffer_t::k_height)
            return;
        uint32_t* const pixels{ _composed_frame.data() + (row_index * framebuffer_t::k_width) };
        for (size_t x{ 0 }; x < framebuffer_t::k_width; ++x)
            pixels[x] = snes_color_to_rgba8(_compositor_state.output_color[x], _display.brightness);
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
        _object_layer_state.priority = { 0, 0, 0, 0 };

        switch (_bg_state.mode)
        {
        case 0u:
            _bg_state.render_mode = { mode_t::bpp2, mode_t::bpp2, mode_t::bpp2, mode_t::bpp2 };
            _bg_state.active = { true, true, true, true };
            _bg_state.priority[0] = { 8, 11 };
            _bg_state.priority[1] = { 7, 10 };
            _bg_state.priority[2] = { 2, 5 };
            _bg_state.priority[3] = { 1, 4 };
            _object_layer_state.priority = { 3, 6, 9, 12 };
            break;
        case 1u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.render_mode[2] = mode_t::bpp2;
            _bg_state.active = { true, true, true, false };
            _bg_state.priority[0] = _bg_state.bg3_priority
                ? std::array<uint8_t, 2>{ 5, 8 }
                : std::array<uint8_t, 2>{ 6, 9 };
            _bg_state.priority[1] = _bg_state.bg3_priority
                ? std::array<uint8_t, 2>{ 4, 7 }
                : std::array<uint8_t, 2>{ 5, 8 };
            _bg_state.priority[2] = _bg_state.bg3_priority
                ? std::array<uint8_t, 2>{ 1, 10 }
                : std::array<uint8_t, 2>{ 1, 3 };
            _object_layer_state.priority = _bg_state.bg3_priority
                ? std::array<uint8_t, 4>{ 2, 3, 6, 9 }
                : std::array<uint8_t, 4>{ 2, 4, 7, 10 };
            break;
        case 2u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            _object_layer_state.priority = { 2, 4, 6, 8 };
            break;
        case 3u:
            _bg_state.render_mode[0] = mode_t::bpp8;
            _bg_state.render_mode[1] = mode_t::bpp4;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            _object_layer_state.priority = { 2, 4, 6, 8 };
            break;
        case 4u:
            _bg_state.render_mode[0] = mode_t::bpp8;
            _bg_state.render_mode[1] = mode_t::bpp2;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            _object_layer_state.priority = { 2, 4, 6, 8 };
            break;
        case 5u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.render_mode[1] = mode_t::bpp2;
            _bg_state.active = { true, true, false, false };
            _bg_state.priority[0] = { 3, 7 };
            _bg_state.priority[1] = { 1, 5 };
            _object_layer_state.priority = { 2, 4, 6, 8 };
            break;
        case 6u:
            _bg_state.render_mode[0] = mode_t::bpp4;
            _bg_state.active = { true, false, false, false };
            _bg_state.priority[0] = { 2, 5 };
            _object_layer_state.priority = { 1, 3, 4, 6 };
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
                _object_layer_state.priority = { 2, 4, 6, 7 };
            }
            else
                _object_layer_state.priority = { 1, 3, 4, 5 };
            break;
        default:
            break;
        }
    }

    uint8_t ppu_t::read_register(uint16_t address, uint8_t open_bus) noexcept
    {
        const uint16_t offset{ static_cast<uint16_t>(address - 0x2100u) };
        if (offset >= _registers.size())
            return 0;

        switch (address)
        {
        case 0x2104u:
        case 0x2105u:
        case 0x2106u:
        case 0x2108u:
        case 0x2109u:
        case 0x210au:
        case 0x2114u:
        case 0x2115u:
        case 0x2116u:
        case 0x2118u:
        case 0x2119u:
        case 0x211au:
        case 0x2124u:
        case 0x2125u:
        case 0x2126u:
        case 0x2128u:
        case 0x2129u:
        case 0x212au:
            return _ppu1_mdr;
        case 0x2134u:
        case 0x2135u:
        case 0x2136u:
        {
            const int32_t product{
                static_cast<int32_t>(static_cast<int16_t>(_screen_state.mode7_a))
                * static_cast<int32_t>(static_cast<int8_t>(_screen_state.mode7_b >> 8u))
            };
            const uint32_t result{ static_cast<uint32_t>(product) & 0x00ff'ffffu };
            const uint8_t shift{ static_cast<uint8_t>((address - 0x2134u) << 3u) };
            _ppu1_mdr = static_cast<uint8_t>(result >> shift);
            return _ppu1_mdr;
        }
        case 0x2137u:
            if (_external_latch_enabled)
                latch_counters();
            return open_bus;
        case 0x2138u:
            _ppu1_mdr = read_oam_byte(_oam_state.address++);
            _oam_state.address &= 0x03ffu;
            update_first_sprite();
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
            _ppu1_mdr = static_cast<uint8_t>((_ppu1_mdr & 0x10u)
                | k_ppu1_version
                | (_object_layer_state.range_over ? 0x40u : 0x00u)
                | (_object_layer_state.time_over ? 0x80u : 0x00u));
            return _ppu1_mdr;
        case 0x213fu:
            _counter_latch.hcounter_high_read = false;
            _counter_latch.vcounter_high_read = false;
            _ppu2_mdr = static_cast<uint8_t>((_ppu2_mdr & 0x20u)
                | k_ppu2_version
                | ((!_external_latch_enabled || _counter_latch.counters_latched) ? 0x40u : 0x00u)
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
        case 0x2101u:
            _object_layer_state.tiledata_address = static_cast<uint16_t>((value & 0x07u) << 13u);
            _object_layer_state.nameselect = static_cast<uint8_t>((value >> 3u) & 0x03u);
            _object_layer_state.base_size = static_cast<uint8_t>((value >> 5u) & 0x07u);
            for (auto& object : _object_layer_state.objects)
            {
                object.width = object_width(object.size_select);
                object.height = object_height(object.size_select);
            }
            return;
        case 0x2100u:
        {
            if (_display.disabled && timing().raster.scanline == active_visible_scanlines())
                reset_oam_address();

            const auto record_display_write = [this, value]() noexcept
            {
                const ppu_render_state_snapshot_t::display_write_t entry{
                    .frame_index = _frame_counter,
                    .scanline = timing().raster.scanline,
                    .dot = timing().raster.dot,
                    .value = value
                };

                if (_display_write_history.count < std::size(_display_write_history.entries))
                {
                    _display_write_history.entries[_display_write_history.count++] = entry;
                    return;
                }

                std::move(std::begin(_display_write_history.entries) + 1u,
                          std::end(_display_write_history.entries),
                          std::begin(_display_write_history.entries));
                _display_write_history.entries[std::size(_display_write_history.entries) - 1u] = entry;
            };
            record_display_write();

            static const inidisp_trace_filter_t trace_filter{ load_inidisp_trace_filter() };
            if (trace_filter.enabled
                && _frame_counter >= trace_filter.frame_min
                && _frame_counter <= trace_filter.frame_max)
            {
                std::printf("PPU INIDISP write: frame=%llu scanline=%u dot=%u value=%02x brightness=%u disabled=%u\n",
                            static_cast<unsigned long long>(_frame_counter),
                            static_cast<unsigned>(timing().raster.scanline),
                            static_cast<unsigned>(timing().raster.dot),
                            static_cast<unsigned>(value),
                            static_cast<unsigned>(value & 0x0fu),
                            (value & 0x80u) != 0 ? 1u : 0u);
            }

            _display.brightness = static_cast<uint8_t>(value & 0x0fu);
            _display.disabled = (value & 0x80u) != 0;
            return;
        }
        case 0x2102u:
            _oam_state.base_address = static_cast<uint16_t>((_oam_state.base_address & 0x0200u) | (value << 1u));
            reset_oam_address();
            return;
        case 0x2103u:
            _oam_state.base_address = static_cast<uint16_t>((_oam_state.base_address & 0x01feu) | ((value & 0x01u) << 9u));
            _oam_state.priority = (value & 0x80u) != 0;
            reset_oam_address();
            return;
        case 0x2104u:
        {
            const uint16_t address_now{ static_cast<uint16_t>(_oam_state.address & 0x03ffu) };
            ++_oam_state.address;
            _oam_state.address &= 0x03ffu;
            update_first_sprite();

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
        {
            const bool mosaic_was_enabled{ mosaic_enabled() };
            _mosaic_state.enabled[0] = (value & 0x01u) != 0;
            _mosaic_state.enabled[1] = (value & 0x02u) != 0;
            _mosaic_state.enabled[2] = (value & 0x04u) != 0;
            _mosaic_state.enabled[3] = (value & 0x08u) != 0;
            _mosaic_state.size = static_cast<uint8_t>((value >> 4u) + 1u);
            if (!mosaic_was_enabled && mosaic_enabled())
                _mosaic_state.vcounter = static_cast<uint8_t>(_mosaic_state.size + 1u);
            return;
        }
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
        case 0x211au:
            _screen_state.mode7_hflip = (value & 0x01u) != 0;
            _screen_state.mode7_vflip = (value & 0x02u) != 0;
            _screen_state.mode7_repeat = static_cast<uint8_t>((value >> 6u) & 0x03u);
            return;
        case 0x211bu:
            _screen_state.mode7_a = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            return;
        case 0x211cu:
            _screen_state.mode7_b = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            return;
        case 0x211du:
            _screen_state.mode7_c = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            return;
        case 0x211eu:
            _screen_state.mode7_d = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            return;
        case 0x211fu:
            _screen_state.mode7_x = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
            return;
        case 0x2120u:
            _screen_state.mode7_y = static_cast<uint16_t>((value << 8u) | _scroll_latches.mode7);
            _scroll_latches.mode7 = value;
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
            _timing_interlace = _screen_state.interlace;
            _object_layer_state.interlace = (value & 0x02u) != 0;
            for (auto& object : _object_layer_state.objects)
                object.height = object_height(object.size_select);
            return;
        default:
            return;
        }
    }

    ppu_step_result_t ppu_t::step(master_clock_delta_t master_clocks) noexcept
    {
        const uint16_t previous_visible_scanlines{ active_visible_scanlines() };
        const timing_snapshot_t previous_timing{ timing() };
        _counter.advance(master_clocks, _video_timing, _timing_interlace);

        ppu_step_result_t result{};
        result.timing = timing();
        result.visible_scanlines = active_visible_scanlines();
        result.interlace = _timing_interlace;
        if (result.timing.raster.scanline < previous_timing.raster.scanline)
        {
            ++_frame_counter;
            _presented_frame = _composed_frame;
            render_placeholder_frame();
            result.frame_complete = true;
        }

        process_pipeline_range(previous_timing, result.timing);
        result.entered_scanline = result.timing.raster.scanline != previous_timing.raster.scanline;
        result.entered_frame_start = result.frame_complete
            || crossed_raster_point(previous_timing, result.timing, 0, 0);
        result.entered_hblank = !previous_timing.in_hblank && result.timing.in_hblank;
        const bool was_in_vblank{ previous_timing.raster.scanline >= previous_visible_scanlines };
        const bool now_in_vblank{ result.timing.raster.scanline >= result.visible_scanlines };
        result.entered_vblank = !was_in_vblank && now_in_vblank;
        result.nmi_requested = result.entered_vblank;
        const uint16_t hdma_setup_dot{
            hdma_setup_dot_v2(dma_phase_from_master_clock(result.timing.master_clock - result.timing.raster.dot))
        };
        result.hdma_setup_triggered = crossed_raster_point(previous_timing,
                                                           result.timing,
                                                           0,
                                                           hdma_setup_dot);
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

    uint64_t ppu_t::frame_index() const noexcept
    {
        return _frame_counter;
    }

    master_clock_delta_t ppu_t::current_scanline_clocks() const noexcept
    {
        return _counter.current_scanline_clocks(_video_timing, _timing_interlace);
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
        for (size_t index{ 0 }; index < 4; ++index)
            snapshot.mosaic_enabled[index] = _mosaic_state.enabled[index];
        snapshot.mosaic_voffset = mosaic_voffset();
        snapshot.pseudo_hires = _screen_state.pseudo_hires;
        snapshot.overscan = _screen_state.overscan;
        snapshot.interlace = _screen_state.interlace;
        snapshot.mode7_hoffset = _scroll_latches.mode7_hoffset;
        snapshot.mode7_voffset = _scroll_latches.mode7_voffset;
        snapshot.mode7_a = _screen_state.mode7_a;
        snapshot.mode7_b = _screen_state.mode7_b;
        snapshot.mode7_c = _screen_state.mode7_c;
        snapshot.mode7_d = _screen_state.mode7_d;
        snapshot.mode7_x = _screen_state.mode7_x;
        snapshot.mode7_y = _screen_state.mode7_y;
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
            snapshot.backgrounds[index].evaluation_scanline = _background_layer_state[index].evaluation_scanline;
            snapshot.backgrounds[index].tile_count = _background_layer_state[index].tile_count;
            for (size_t sample_index{ 0 }; sample_index < std::size(snapshot.backgrounds[index].samples); ++sample_index)
                snapshot.backgrounds[index].samples[sample_index] = _background_layer_state[index].samples[sample_index];
            for (size_t tile_index{ 0 }; tile_index < snapshot.backgrounds[index].tile_count
                && tile_index < std::size(snapshot.backgrounds[index].tiles);
                ++tile_index)
            {
                snapshot.backgrounds[index].tiles[tile_index] = _background_layer_state[index].tiles[tile_index];
            }
        }

        snapshot.objects.above_enabled = _object_layer_state.above_enabled;
        snapshot.objects.below_enabled = _object_layer_state.below_enabled;
        snapshot.objects.window_above_enabled = _object_layer_state.window_above_enabled;
        snapshot.objects.window_below_enabled = _object_layer_state.window_below_enabled;
        snapshot.objects.window_mask = _window_state.object_mask;
        snapshot.objects.tiledata_address = _object_layer_state.tiledata_address;
        snapshot.objects.nameselect = _object_layer_state.nameselect;
        snapshot.objects.base_size = _object_layer_state.base_size;
        snapshot.objects.first_sprite = _object_layer_state.first_sprite;
        snapshot.objects.interlace = _object_layer_state.interlace;
        snapshot.objects.range_over = _object_layer_state.range_over;
        snapshot.objects.time_over = _object_layer_state.time_over;
        snapshot.objects.evaluation_scanline = _object_layer_state.evaluation_scanline;
        snapshot.objects.rendered_scanline = _object_layer_state.rendered_scanline;
        snapshot.objects.fetched_scanline = _object_layer_state.fetched_scanline;
        snapshot.objects.evaluation_first_sprite = _object_layer_state.evaluation_first_sprite;
        snapshot.objects.evaluation_count = _object_layer_state.evaluation_count;
        snapshot.objects.tile_count = _object_layer_state.tile_count;
        snapshot.objects.render_tile_count = _object_layer_state.render_tile_count;
        snapshot.objects.fetched_tile_count = _object_layer_state.fetched_tile_count;
        for (size_t index{ 0 }; index < 4; ++index)
        {
            snapshot.objects.priority[index] = _object_layer_state.priority[index];
        }
        for (size_t index{ 0 }; index < 32; ++index)
        {
            snapshot.objects.evaluation_indices[index] = _object_layer_state.evaluation_indices[index];
        }
        for (size_t index{ 0 }; index < 34; ++index)
        {
            snapshot.objects.tiles[index] = _object_layer_state.tiles[index];
            snapshot.objects.render_tiles[index] = _object_layer_state.render_tiles[index];
            snapshot.objects.fetched_tiles[index] = _object_layer_state.fetched_tiles[index];
        }
        for (size_t index{ 0 }; index < 128; ++index)
        {
            snapshot.objects.samples[index] = _object_layer_state.objects[index];
        }

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

        snapshot.display_write_count = _display_write_history.count;
        for (size_t index{ 0 }; index < _display_write_history.count; ++index)
            snapshot.recent_display_writes[index] = _display_write_history.entries[index];

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
        snapshot.above = _compositor_state.above;
        snapshot.below = _compositor_state.below;
        for (size_t sample_index{ 0 }; sample_index < std::size(snapshot.above_samples); ++sample_index)
        {
            snapshot.above_samples[sample_index] = _compositor_state.above_samples[sample_index];
            snapshot.below_samples[sample_index] = _compositor_state.below_samples[sample_index];
            snapshot.color_enable_above[sample_index] = _compositor_state.color_enable_above[sample_index];
            snapshot.color_enable_below[sample_index] = _compositor_state.color_enable_below[sample_index];
            snapshot.math_enable[sample_index] = _compositor_state.math_enable[sample_index];
            snapshot.math_uses_subscreen[sample_index] = _compositor_state.math_uses_subscreen[sample_index];
            snapshot.math_uses_fixed_color[sample_index] = _compositor_state.math_uses_fixed_color[sample_index];
            snapshot.color_halve_active[sample_index] = _compositor_state.color_halve_active[sample_index];
            snapshot.above_transparent[sample_index] = _compositor_state.above_transparent[sample_index];
            snapshot.below_transparent[sample_index] = _compositor_state.below_transparent[sample_index];
            snapshot.above_color[sample_index] = _compositor_state.above_color[sample_index];
            snapshot.below_color[sample_index] = _compositor_state.below_color[sample_index];
            snapshot.math_rhs_color[sample_index] = _compositor_state.math_rhs_color[sample_index];
            snapshot.output_color[sample_index] = _compositor_state.output_color[sample_index];
        }
        for (size_t index{ 0 }; index < 4; ++index)
        {
            snapshot.backgrounds[index].above = _compositor_state.backgrounds[index].above;
            snapshot.backgrounds[index].below = _compositor_state.backgrounds[index].below;
            for (size_t sample_index{ 0 }; sample_index < std::size(snapshot.backgrounds[index].above_samples); ++sample_index)
            {
                snapshot.backgrounds[index].above_samples[sample_index]
                    = _compositor_state.backgrounds[index].above_samples[sample_index];
                snapshot.backgrounds[index].below_samples[sample_index]
                    = _compositor_state.backgrounds[index].below_samples[sample_index];
            }
        }
        snapshot.objects.above = _compositor_state.objects.above;
        snapshot.objects.below = _compositor_state.objects.below;
        for (size_t sample_index{ 0 }; sample_index < std::size(snapshot.objects.above_samples); ++sample_index)
        {
            snapshot.objects.above_samples[sample_index] = _compositor_state.objects.above_samples[sample_index];
            snapshot.objects.below_samples[sample_index] = _compositor_state.objects.below_samples[sample_index];
        }
        return snapshot;
    }

    std::size_t ppu_t::cgram_write_trace_count() const noexcept
    {
        return _cgram_write_trace_count;
    }

    const std::array<ppu_cgram_write_trace_t, ppu_cgram_write_trace_capacity>&
    ppu_t::cgram_write_trace() const noexcept
    {
        return _cgram_write_trace;
    }

    std::size_t ppu_t::oam_write_trace_count() const noexcept
    {
        return _oam_write_trace_count;
    }

    const std::array<ppu_oam_write_trace_t, ppu_oam_write_trace_capacity>&
    ppu_t::oam_write_trace() const noexcept
    {
        return _oam_write_trace;
    }

    const std::array<uint16_t, 32 * 1024>& ppu_t::vram() const noexcept
    {
        return _vram;
    }

    const std::array<uint8_t, 544>& ppu_t::oam() const noexcept
    {
        return _oam;
    }

    const std::array<uint16_t, 256>& ppu_t::cgram() const noexcept
    {
        return _cgram;
    }

    void ppu_t::present(framebuffer_t& framebuffer, const ppu_presentation_options_t& options) const noexcept
    {
        static_cast<void>(options);
        const framebuffer_t& source{
            options.source == ppu_presentation_source_t::composed ? _composed_frame : _presented_frame
        };
        std::copy(source.data(),
                  source.data() + framebuffer_t::k_pixel_count,
                  framebuffer.data());
    }

    void ppu_t::render_placeholder_frame() noexcept
    {
        _composed_frame.clear(snes_color_to_rgba8(_cgram[0], _display.brightness));
    }
}
