//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cartridge.h"
#include "clover/core/snes/Console.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    struct bringup_summary_t
    {
        uint64_t steps{ 0 };
        uint64_t dma_steps{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t hblank_entries{ 0 };
        uint64_t vblank_entries{ 0 };
        uint64_t nmi_requests{ 0 };
        uint64_t irq_requests{ 0 };
        uint64_t hdma_setup_triggers{ 0 };
        uint64_t hdma_transfer_triggers{ 0 };
    };

    struct cpu_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t state{};
        uint8_t opcode{ 0 };
    };

    struct direct_page_watch_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t cpu{};
        uint8_t opcode{ 0 };
        uint8_t value_00{ 0 };
        uint8_t value_01{ 0 };
        uint8_t value_02{ 0 };
        uint8_t value_03{ 0 };
        uint8_t value_04{ 0 };
        uint8_t value_05{ 0 };
        uint8_t value_59{ 0 };
        uint8_t value_68{ 0 };
        uint8_t value_69{ 0 };
        uint8_t value_6a{ 0 };
        uint8_t value_f3{ 0 };
        uint8_t value_f4{ 0 };
        uint8_t value_f5{ 0 };
        uint8_t value_f9{ 0 };
        uint8_t value_fa{ 0 };
        uint8_t value_fb{ 0 };
        uint8_t value_fc{ 0 };
        uint8_t value_65{ 0 };
        uint8_t value_66{ 0 };
        uint8_t value_67{ 0 };
        uint32_t pointer_65y{ 0 };
        uint8_t pointer_byte_0{ 0 };
        uint8_t pointer_byte_1{ 0 };
        uint8_t pointer_byte_2{ 0 };
        uint32_t pointer_f3y{ 0 };
        uint8_t pointer_f3_byte_0{ 0 };
        uint8_t pointer_f3_byte_1{ 0 };
        uint8_t pointer_f3_byte_2{ 0 };
    };

    struct pointer_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_65{ 0 };
        uint8_t old_66{ 0 };
        uint8_t old_67{ 0 };
        uint8_t new_65{ 0 };
        uint8_t new_66{ 0 };
        uint8_t new_67{ 0 };
    };

    struct source_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_68{ 0 };
        uint8_t old_69{ 0 };
        uint8_t old_6a{ 0 };
        uint8_t new_68{ 0 };
        uint8_t new_69{ 0 };
        uint8_t new_6a{ 0 };
    };

    struct lowram_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        clover::core::hardware_slot_owner_t slot_owner{};
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_value{ 0 };
        uint8_t new_value{ 0 };
    };

    struct transfer_pointer_change_entry_t
    {
        uint64_t hardware_step{ 0 };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
        uint8_t old_f3{ 0 };
        uint8_t old_f4{ 0 };
        uint8_t old_f5{ 0 };
        uint8_t old_f6{ 0 };
        uint8_t old_f7{ 0 };
        uint8_t old_f8{ 0 };
        uint8_t new_f3{ 0 };
        uint8_t new_f4{ 0 };
        uint8_t new_f5{ 0 };
        uint8_t new_f6{ 0 };
        uint8_t new_f7{ 0 };
        uint8_t new_f8{ 0 };
    };

    struct hot_path_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t active_frame{ 0 };
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t cpu{};
        clover::core::apu_state_t apu{};
        uint8_t opcode{ 0 };
        uint32_t cpu_wram_address{ 0 };
        uint8_t apu_port_0{ 0 };
        uint8_t apu_port_1{ 0 };
        uint8_t apu_port_2{ 0 };
        uint8_t apu_port_3{ 0 };
        uint8_t dp_00{ 0 };
        uint8_t dp_01{ 0 };
        uint8_t dp_02{ 0 };
        uint8_t dp_03{ 0 };
        uint16_t effective_dp_03_address{ 0 };
        uint8_t effective_dp_03{ 0 };
        uint8_t dp_04{ 0 };
        uint8_t dp_05{ 0 };
        uint8_t dp_65{ 0 };
        uint8_t dp_66{ 0 };
        uint8_t dp_67{ 0 };
        uint8_t dp_68{ 0 };
        uint8_t dp_69{ 0 };
        uint8_t dp_6a{ 0 };
        uint8_t dp_f3{ 0 };
        uint8_t dp_f4{ 0 };
        uint8_t dp_f5{ 0 };
        uint8_t dp_f6{ 0 };
        uint8_t dp_f7{ 0 };
        uint8_t dp_f8{ 0 };
        uint8_t dp_f9{ 0 };
        uint8_t dp_fa{ 0 };
        uint8_t dp_fb{ 0 };
        uint8_t dp_fc{ 0 };
        uint8_t dma_source_bank{ 0 };
        uint32_t dma_source_0d84{ 0 };
        uint32_t dma_source_0d85{ 0 };
        uint32_t dma_source_0d8e{ 0 };
        uint32_t dma_source_0d8f{ 0 };
        uint8_t dma_byte_0d84{ 0 };
        uint8_t dma_byte_0d85{ 0 };
        uint8_t dma_byte_0d8e{ 0 };
        uint8_t dma_byte_0d8f{ 0 };
        uint8_t dma_byte_0d98{ 0 };
        uint8_t dma_byte_0d99{ 0 };
        uint8_t dma_control{ 0 };
        uint8_t dma_bbus{ 0 };
        uint16_t dma_source_address{ 0 };
        uint8_t dma_source_bank_register{ 0 };
        uint16_t dma_transfer_size{ 0 };
    };

    struct helper_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t active_frame{ 0 };
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t cpu{};
        uint8_t opcode{ 0 };
        uint32_t cpu_wram_address{ 0 };
        uint8_t dp_f3{ 0 };
        uint8_t dp_f4{ 0 };
        uint8_t dp_f5{ 0 };
        uint8_t dp_f6{ 0 };
        uint8_t dp_f7{ 0 };
        uint8_t dp_f8{ 0 };
        uint8_t dp_fc{ 0 };
        uint32_t source_address{ 0 };
        uint32_t destination_address{ 0 };
    };

    struct ff3_call_trace_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        uint64_t active_frame{ 0 };
        clover::core::timing_snapshot_t timing{};
        clover::core::cpu_state_t cpu{};
        uint8_t opcode{ 0 };
        uint32_t cpu_wram_address{ 0 };
        uint8_t dp_f3{ 0 };
        uint8_t dp_f4{ 0 };
        uint8_t dp_f5{ 0 };
        uint8_t dp_f6{ 0 };
        uint8_t dp_f7{ 0 };
        uint8_t dp_f8{ 0 };
        uint8_t dp_f9{ 0 };
        uint8_t dp_fa{ 0 };
        uint8_t dp_fb{ 0 };
        uint8_t dp_fc{ 0 };
        uint32_t source_address{ 0 };
        uint32_t destination_address{ 0 };
    };

    struct dma_transition_entry_t
    {
        uint64_t hardware_step{ 0 };
        uint64_t frame_completions{ 0 };
        clover::core::hardware_slot_owner_t slot_owner{};
        clover::core::timing_snapshot_t timing{};
        clover::core::dma_activity_t before_activity{ clover::core::dma_activity_t::idle };
        clover::core::dma_activity_t after_activity{ clover::core::dma_activity_t::idle };
        bool before_general_pending{ false };
        bool after_general_pending{ false };
        bool before_hdma_pending{ false };
        bool after_hdma_pending{ false };
        clover::core::cpu_state_t before_cpu{};
        uint8_t before_opcode{ 0 };
        clover::core::cpu_state_t after_cpu{};
        uint8_t after_opcode{ 0 };
    };

    struct hot_path_filter_t
    {
        bool enabled{ false };
        uint8_t pb{ 0x00u };
        uint16_t pc_min{ 0x0000u };
        uint16_t pc_max{ 0xffffu };
        uint64_t active_frame{ 0 };
    };

    struct watched_write_filter_t
    {
        bool enabled{ false };
        uint32_t address_min{ 0x000000u };
        uint32_t address_max{ 0xffffffu };
        uint64_t frame_min{ 0u };
        uint64_t frame_max{ UINT64_MAX };
    };

    struct bus_window_filter_t
    {
        bool enabled{ false };
        uint32_t address{ 0x000000u };
        uint16_t count{ 0u };
    };

    struct generic_trace_filter_t
    {
        bool enabled{ false };
        uint64_t active_frame{ 0u };
        bool has_pb{ false };
        uint8_t pb{ 0x00u };
        bool has_pc_min{ false };
        uint16_t pc_min{ 0x0000u };
        bool has_pc_max{ false };
        uint16_t pc_max{ 0xffffu };
        bool has_scanline_min{ false };
        uint16_t scanline_min{ 0u };
        bool has_scanline_max{ false };
        uint16_t scanline_max{ 261u };
        bool has_dot_min{ false };
        uint16_t dot_min{ 0u };
        bool has_dot_max{ false };
        uint16_t dot_max{ 1363u };
        std::filesystem::path output_path{};
    };

    struct ppu_probe_filter_t
    {
        bool enabled{ false };
        uint64_t active_frame{ 0u };
        uint16_t scanline_min{ 0u };
        uint16_t scanline_max{ 0u };
        uint16_t dot_min{ 0u };
        uint16_t dot_max{ 1363u };
        uint16_t x_min{ 0u };
        uint16_t x_max{ 7u };
    };

    [[nodiscard]] std::string mapping_mode_name(clover::core::cartridge_mapping_mode_t mode)
    {
        using mode_t = clover::core::cartridge_mapping_mode_t;
        switch (mode)
        {
        case mode_t::none:
            return "none";
        case mode_t::bootstrap:
            return "bootstrap";
        case mode_t::lorom:
            return "lorom";
        case mode_t::hirom:
            return "hirom";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] uint64_t parse_u64_env(const char* name, uint64_t fallback) noexcept
    {
        const char* raw{ std::getenv(name) };
        if (raw == nullptr || *raw == '\0')
            return fallback;

        return std::strtoull(raw, nullptr, 0);
    }

    [[nodiscard]] bool parse_bool_env(const char* name, bool fallback = false) noexcept
    {
        const char* raw{ std::getenv(name) };
        if (raw == nullptr || *raw == '\0')
            return fallback;

        if (std::string_view{ raw } == "0"
            || std::string_view{ raw } == "false"
            || std::string_view{ raw } == "FALSE"
            || std::string_view{ raw } == "off"
            || std::string_view{ raw } == "OFF")
        {
            return false;
        }

        return true;
    }

    struct startup_entropy_config_t
    {
        clover::core::startup_entropy_mode_t mode{ clover::core::startup_entropy_mode_t::none };
        bool seed_enabled{ false };
        uint32_t seed{ 0u };
        uint32_t sequence{ 0u };
    };

    [[nodiscard]] startup_entropy_config_t load_startup_entropy_config() noexcept
    {
        startup_entropy_config_t config{};

        const char* mode_raw{ std::getenv("CLOVER_STARTUP_ENTROPY") };
        if (mode_raw == nullptr)
            mode_raw = std::getenv("CLOVER_PPU_ENTROPY");
        if (mode_raw != nullptr)
        {
            const std::string_view mode{ mode_raw };
            if (mode == "low" || mode == "LOW")
                config.mode = clover::core::startup_entropy_mode_t::low;
            else if (mode == "high" || mode == "HIGH")
                config.mode = clover::core::startup_entropy_mode_t::high;
        }

        const char* seed_raw{ std::getenv("CLOVER_STARTUP_ENTROPY_SEED") };
        if (seed_raw == nullptr)
            seed_raw = std::getenv("CLOVER_PPU_ENTROPY_SEED");
        if (seed_raw != nullptr)
        {
            if (*seed_raw != '\0')
            {
                config.seed_enabled = true;
                config.seed = static_cast<uint32_t>(std::strtoull(seed_raw, nullptr, 0));
                config.sequence = static_cast<uint32_t>(
                    parse_u64_env("CLOVER_STARTUP_ENTROPY_SEQUENCE",
                                  parse_u64_env("CLOVER_PPU_ENTROPY_SEQUENCE", 0u)));
            }
        }

        return config;
    }

    [[nodiscard]] ppu_probe_filter_t load_ppu_probe_filter()
    {
        ppu_probe_filter_t filter{};
        const char* frame_raw{ std::getenv("CLOVER_PPU_PROBE_FRAME") };
        if (frame_raw == nullptr || *frame_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.active_frame = parse_u64_env("CLOVER_PPU_PROBE_FRAME", 0u);
        filter.scanline_min = static_cast<uint16_t>(parse_u64_env("CLOVER_PPU_PROBE_SCANLINE_MIN", 0u) & 0xffffu);
        filter.scanline_max = static_cast<uint16_t>(
            parse_u64_env("CLOVER_PPU_PROBE_SCANLINE_MAX", filter.scanline_min) & 0xffffu
        );
        if (filter.scanline_min > filter.scanline_max)
            std::swap(filter.scanline_min, filter.scanline_max);
        filter.dot_min = static_cast<uint16_t>(parse_u64_env("CLOVER_PPU_PROBE_DOT_MIN", 0u) & 0xffffu);
        filter.dot_max = static_cast<uint16_t>(
            parse_u64_env("CLOVER_PPU_PROBE_DOT_MAX", filter.dot_min) & 0xffffu
        );
        if (filter.dot_min > filter.dot_max)
            std::swap(filter.dot_min, filter.dot_max);
        filter.x_min = static_cast<uint16_t>(parse_u64_env("CLOVER_PPU_PROBE_X_MIN", 0u) & 0xffffu);
        filter.x_max = static_cast<uint16_t>(
            parse_u64_env("CLOVER_PPU_PROBE_X_MAX", static_cast<uint64_t>(filter.x_min + 7u)) & 0xffffu
        );
        if (filter.x_min > filter.x_max)
            std::swap(filter.x_min, filter.x_max);
        return filter;
    }

    [[nodiscard]] const char* pixel_source_name(clover::core::ppu_pixel_source_t source) noexcept
    {
        using source_t = clover::core::ppu_pixel_source_t;
        switch (source)
        {
        case source_t::background_1:
            return "bg1";
        case source_t::background_2:
            return "bg2";
        case source_t::background_3:
            return "bg3";
        case source_t::background_4:
            return "bg4";
        case source_t::objects:
            return "obj";
        case source_t::backdrop:
            return "backdrop";
        default:
            return "none";
        }
    }

    void print_pixel_candidate(const char* label,
                               const clover::core::ppu_pixel_candidate_t& candidate) noexcept
    {
        std::printf("%s pri=%u pal=%u grp=%u math=%u src=%s",
                    label,
                    candidate.priority,
                    candidate.palette,
                    candidate.palette_group,
                    candidate.color_math_enabled ? 1u : 0u,
                    pixel_source_name(candidate.source));
    }

    void print_ppu_probe_snapshot(uint64_t active_frame,
                                  const clover::core::timing_snapshot_t& timing,
                                  const clover::core::ppu_render_state_snapshot_t& ppu_state,
                                  const clover::core::ppu_compositor_snapshot_t& compositor_state,
                                  uint16_t cgram0,
                                  const ppu_probe_filter_t& filter) noexcept
    {
        constexpr const char* k_probe_header_format =
            "PPU probe: frame=%llu scanline=%u dot=%u bg_mode=%u obj_first=%u "
            "obj_eval_first=%u obj_tiles=%u obj_render_tiles=%u "
            "obj_fetched_tiles=%u obj_eval=%u render_scan=%u fetched_scan=%u "
            "mosaic=%u enables=%u%u%u%u voffset=%u\n";
        std::printf(k_probe_header_format,
                    static_cast<unsigned long long>(active_frame),
                    static_cast<unsigned>(timing.raster.scanline),
                    static_cast<unsigned>(timing.raster.dot),
                    static_cast<unsigned>(ppu_state.bg_mode),
                    static_cast<unsigned>(ppu_state.objects.first_sprite),
                    static_cast<unsigned>(ppu_state.objects.evaluation_first_sprite),
                    static_cast<unsigned>(ppu_state.objects.tile_count),
                    static_cast<unsigned>(ppu_state.objects.render_tile_count),
                    static_cast<unsigned>(ppu_state.objects.fetched_tile_count),
                    static_cast<unsigned>(ppu_state.objects.evaluation_count),
                    static_cast<unsigned>(ppu_state.objects.rendered_scanline),
                    static_cast<unsigned>(ppu_state.objects.fetched_scanline),
                    static_cast<unsigned>(ppu_state.mosaic_size),
                    ppu_state.mosaic_enabled[0] ? 1u : 0u,
                    ppu_state.mosaic_enabled[1] ? 1u : 0u,
                    ppu_state.mosaic_enabled[2] ? 1u : 0u,
                    ppu_state.mosaic_enabled[3] ? 1u : 0u,
                    static_cast<unsigned>(ppu_state.mosaic_voffset));
        std::printf("  math: blend=%u halve=%u direct=%u subtract=%u backdrop_math=%u fixed=%u,%u,%u\n",
                    compositor_state.blend_mode ? 1u : 0u,
                    compositor_state.color_halve ? 1u : 0u,
                    compositor_state.direct_color ? 1u : 0u,
                    compositor_state.color_mode_subtract ? 1u : 0u,
                    compositor_state.backdrop_color_enable ? 1u : 0u,
                    static_cast<unsigned>(compositor_state.fixed_red),
                    static_cast<unsigned>(compositor_state.fixed_green),
                    static_cast<unsigned>(compositor_state.fixed_blue));
        std::printf("  cgram0=%04x\n", static_cast<unsigned>(cgram0));
        std::printf("  obj_eval_indices:");
        for (size_t index{ 0 }; index < std::size(ppu_state.objects.evaluation_indices); ++index)
            std::printf(" %u", ppu_state.objects.evaluation_indices[index]);
        std::printf("\n");
        const size_t evaluated_object_count{
            std::min<size_t>(ppu_state.objects.evaluation_count, std::size(ppu_state.objects.evaluation_indices))
        };
        for (size_t index{ 0 }; index < evaluated_object_count; ++index)
        {
            const uint8_t object_index{ ppu_state.objects.evaluation_indices[index] };
            const auto& object{ ppu_state.objects.samples[object_index] };
            std::printf("  objstate[%u] x=%u y=%u chr=%u w=%u h=%u nameselect=%u pal=%u pri=%u hflip=%u vflip=%u size=%u\n",
                        static_cast<unsigned>(object_index),
                        static_cast<unsigned>(object.x),
                        static_cast<unsigned>(object.y),
                        static_cast<unsigned>(object.character),
                        static_cast<unsigned>(object.width),
                        static_cast<unsigned>(object.height),
                        object.nameselect ? 1u : 0u,
                        static_cast<unsigned>(object.palette),
                        static_cast<unsigned>(object.priority),
                        object.hflip ? 1u : 0u,
                        object.vflip ? 1u : 0u,
                        object.size_select ? 1u : 0u);
        }
        for (size_t background_index{ 0 }; background_index < 4u; ++background_index)
        {
            const auto& background{ ppu_state.backgrounds[background_index] };
            std::printf("  bg%zu: active=%u mode=%u eval_scan=%u tiles=%u hoff=%u voff=%u "
                        "above=%u below=%u win_above=%u win_below=%u win_mask=%u\n",
                        background_index + 1u,
                        background.active ? 1u : 0u,
                        static_cast<unsigned>(background.mode),
                        static_cast<unsigned>(background.evaluation_scanline),
                        static_cast<unsigned>(background.tile_count),
                        static_cast<unsigned>(background.hoffset),
                        static_cast<unsigned>(background.voffset),
                        background.above_enabled ? 1u : 0u,
                        background.below_enabled ? 1u : 0u,
                        background.window_above_enabled ? 1u : 0u,
                        background.window_below_enabled ? 1u : 0u,
                        static_cast<unsigned>(background.window_mask));
            for (size_t tile_index{ 0 }; tile_index < background.tile_count
                && tile_index < std::size(background.tiles);
                ++tile_index)
            {
                const auto& tile{ background.tiles[tile_index] };
                const uint16_t tile_right{ static_cast<uint16_t>(tile.screen_x + 7u) };
                if (tile_right < filter.x_min || tile.screen_x > filter.x_max)
                    continue;
                std::printf("    tile[%zu] screen_x=%u src=%u,%u map=%04x entry=%04x chr=%u "
                            "vram=%04x fine=%u,%u pal=%u pri=%u hflip=%u vflip=%u "
                            "row=%04x,%04x,%04x,%04x\n",
                            tile_index,
                            static_cast<unsigned>(tile.screen_x),
                            static_cast<unsigned>(tile.source_x),
                            static_cast<unsigned>(tile.source_y),
                            static_cast<unsigned>(tile.tilemap_address),
                            static_cast<unsigned>(tile.tilemap_entry),
                            static_cast<unsigned>(tile.character),
                            static_cast<unsigned>(tile.vram_address),
                            static_cast<unsigned>(tile.fine_x),
                            static_cast<unsigned>(tile.fine_y),
                            static_cast<unsigned>(tile.palette_group),
                            static_cast<unsigned>(tile.priority),
                            tile.hmirror ? 1u : 0u,
                            tile.vmirror ? 1u : 0u,
                            static_cast<unsigned>(tile.row_data[0]),
                            static_cast<unsigned>(tile.row_data[1]),
                            static_cast<unsigned>(tile.row_data[2]),
                            static_cast<unsigned>(tile.row_data[3]));
            }
        }
        const size_t sample_begin{
            std::min<size_t>(filter.x_min, std::size(compositor_state.output_color) - 1u)
        };
        const size_t sample_end{
            std::min<size_t>(filter.x_max, std::size(compositor_state.output_color) - 1u)
        };
        for (size_t sample_x{ sample_begin }; sample_x <= sample_end; ++sample_x)
        {
            std::printf("  x=%zu out=%04x above=%04x below=%04x math=%u sub=%u fixed=%u halve=%u cwa=%u cwb=%u objA=",
                        sample_x,
                        compositor_state.output_color[sample_x],
                        compositor_state.above_color[sample_x],
                        compositor_state.below_color[sample_x],
                        compositor_state.math_enable[sample_x] ? 1u : 0u,
                        compositor_state.math_uses_subscreen[sample_x] ? 1u : 0u,
                        compositor_state.math_uses_fixed_color[sample_x] ? 1u : 0u,
                        compositor_state.color_halve_active[sample_x] ? 1u : 0u,
                        compositor_state.color_enable_above[sample_x] ? 1u : 0u,
                        compositor_state.color_enable_below[sample_x] ? 1u : 0u);
            print_pixel_candidate("", compositor_state.objects.above_samples[sample_x]);
            std::printf(" objB=");
            print_pixel_candidate("", compositor_state.objects.below_samples[sample_x]);
            std::printf(" topA=");
            print_pixel_candidate("", compositor_state.above_samples[sample_x]);
            std::printf(" topB=");
            print_pixel_candidate("", compositor_state.below_samples[sample_x]);
            std::printf(" bg1A=");
            print_pixel_candidate("", compositor_state.backgrounds[0].above_samples[sample_x]);
            std::printf(" bg1B=");
            print_pixel_candidate("", compositor_state.backgrounds[0].below_samples[sample_x]);
            std::printf(" bg2A=");
            print_pixel_candidate("", compositor_state.backgrounds[1].above_samples[sample_x]);
            std::printf(" bg2B=");
            print_pixel_candidate("", compositor_state.backgrounds[1].below_samples[sample_x]);
            std::printf(" bg3A=");
            print_pixel_candidate("", compositor_state.backgrounds[2].above_samples[sample_x]);
            std::printf(" bg3B=");
            print_pixel_candidate("", compositor_state.backgrounds[2].below_samples[sample_x]);
            std::printf(" bg4A=");
            print_pixel_candidate("", compositor_state.backgrounds[3].above_samples[sample_x]);
            std::printf(" bg4B=");
            print_pixel_candidate("", compositor_state.backgrounds[3].below_samples[sample_x]);
            std::printf("\n");
        }

        const uint8_t object_tile_count{ std::min<uint8_t>(ppu_state.objects.tile_count, 16u) };
        for (uint8_t tile_index{ 0 }; tile_index < object_tile_count; ++tile_index)
        {
            const auto& tile{ ppu_state.objects.tiles[tile_index] };
            std::printf("  objtile[%u] obj=%u x=%u tile_x=%u src_y=%u fine_y=%u vram=%04x palbase=%u pri=%u hflip=%u row0=%04x row1=%04x\n",
                        tile_index,
                        tile.object_index,
                        tile.x,
                        tile.tile_x,
                        tile.source_y,
                        tile.fine_y,
                        tile.vram_address,
                        tile.palette_base,
                        tile.priority,
                        tile.hflip ? 1u : 0u,
                        tile.row_data[0],
                        tile.row_data[1]);
        }
        const uint8_t render_tile_count{ std::min<uint8_t>(ppu_state.objects.render_tile_count, 16u) };
        for (uint8_t tile_index{ 0 }; tile_index < render_tile_count; ++tile_index)
        {
            const auto& tile{ ppu_state.objects.render_tiles[tile_index] };
            std::printf("  objrender[%u] obj=%u x=%u tile_x=%u src_y=%u fine_y=%u vram=%04x palbase=%u pri=%u hflip=%u row0=%04x row1=%04x\n",
                        tile_index,
                        tile.object_index,
                        tile.x,
                        tile.tile_x,
                        tile.source_y,
                        tile.fine_y,
                        tile.vram_address,
                        tile.palette_base,
                        tile.priority,
                        tile.hflip ? 1u : 0u,
                        tile.row_data[0],
                        tile.row_data[1]);
        }
    }

    [[nodiscard]] generic_trace_filter_t load_generic_trace_filter()
    {
        generic_trace_filter_t filter{};
        const char* frame_raw{ std::getenv("CLOVER_TRACE_FRAME") };
        const char* path_raw{ std::getenv("CLOVER_TRACE_FILE") };
        if (frame_raw == nullptr || *frame_raw == '\0' || path_raw == nullptr || *path_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.active_frame = parse_u64_env("CLOVER_TRACE_FRAME", 0u);
        filter.output_path = path_raw;

        if (const char* pb_raw{ std::getenv("CLOVER_TRACE_PB") }; pb_raw != nullptr && *pb_raw != '\0')
        {
            filter.has_pb = true;
            filter.pb = static_cast<uint8_t>(std::strtoull(pb_raw, nullptr, 0) & 0xffu);
        }

        if (const char* pc_min_raw{ std::getenv("CLOVER_TRACE_PC_MIN") }; pc_min_raw != nullptr && *pc_min_raw != '\0')
        {
            filter.has_pc_min = true;
            filter.pc_min = static_cast<uint16_t>(std::strtoull(pc_min_raw, nullptr, 0) & 0xffffu);
        }

        if (const char* pc_max_raw{ std::getenv("CLOVER_TRACE_PC_MAX") }; pc_max_raw != nullptr && *pc_max_raw != '\0')
        {
            filter.has_pc_max = true;
            filter.pc_max = static_cast<uint16_t>(std::strtoull(pc_max_raw, nullptr, 0) & 0xffffu);
        }

        if (filter.has_pc_min && filter.has_pc_max && filter.pc_min > filter.pc_max)
            std::swap(filter.pc_min, filter.pc_max);

        if (const char* scanline_min_raw{ std::getenv("CLOVER_TRACE_SCANLINE_MIN") };
            scanline_min_raw != nullptr && *scanline_min_raw != '\0')
        {
            filter.has_scanline_min = true;
            filter.scanline_min = static_cast<uint16_t>(std::strtoull(scanline_min_raw, nullptr, 0) & 0xffffu);
        }

        if (const char* scanline_max_raw{ std::getenv("CLOVER_TRACE_SCANLINE_MAX") };
            scanline_max_raw != nullptr && *scanline_max_raw != '\0')
        {
            filter.has_scanline_max = true;
            filter.scanline_max = static_cast<uint16_t>(std::strtoull(scanline_max_raw, nullptr, 0) & 0xffffu);
        }

        if (filter.has_scanline_min && filter.has_scanline_max && filter.scanline_min > filter.scanline_max)
            std::swap(filter.scanline_min, filter.scanline_max);

        if (const char* dot_min_raw{ std::getenv("CLOVER_TRACE_DOT_MIN") }; dot_min_raw != nullptr && *dot_min_raw != '\0')
        {
            filter.has_dot_min = true;
            filter.dot_min = static_cast<uint16_t>(std::strtoull(dot_min_raw, nullptr, 0) & 0xffffu);
        }

        if (const char* dot_max_raw{ std::getenv("CLOVER_TRACE_DOT_MAX") }; dot_max_raw != nullptr && *dot_max_raw != '\0')
        {
            filter.has_dot_max = true;
            filter.dot_max = static_cast<uint16_t>(std::strtoull(dot_max_raw, nullptr, 0) & 0xffffu);
        }

        if (filter.has_dot_min && filter.has_dot_max && filter.dot_min > filter.dot_max)
            std::swap(filter.dot_min, filter.dot_max);

        return filter;
    }

    [[nodiscard]] bool should_emit_generic_trace(const generic_trace_filter_t& filter,
                                                 const clover::core::cpu_state_t& cpu,
                                                 const clover::core::hardware_timing_snapshot_t& snapshot,
                                                 uint64_t active_frame) noexcept
    {
        if (!filter.enabled)
            return false;

        if (filter.active_frame != 0u && active_frame != filter.active_frame)
            return false;

        if (filter.has_pb && cpu.pb != filter.pb)
            return false;

        if (filter.has_pc_min && cpu.pc < filter.pc_min)
            return false;

        if (filter.has_pc_max && cpu.pc > filter.pc_max)
            return false;

        if (filter.has_scanline_min && snapshot.ppu_timing.raster.scanline < filter.scanline_min)
            return false;

        if (filter.has_scanline_max && snapshot.ppu_timing.raster.scanline > filter.scanline_max)
            return false;

        if (filter.has_dot_min && snapshot.ppu_timing.raster.dot < filter.dot_min)
            return false;

        if (filter.has_dot_max && snapshot.ppu_timing.raster.dot > filter.dot_max)
            return false;

        return true;
    }

    void write_generic_trace_header(std::FILE* output, const generic_trace_filter_t& filter)
    {
        if (output == nullptr)
            return;

        std::fprintf(output,
                     "clover trace frame=%llu pb=%s%02x pc_min=%s%04x pc_max=%s%04x "
                     "scanline_min=%s%u scanline_max=%s%u dot_min=%s%u dot_max=%s%u\n",
                     static_cast<unsigned long long>(filter.active_frame),
                     filter.has_pb ? "" : "*",
                     filter.pb,
                     filter.has_pc_min ? "" : "*",
                     filter.pc_min,
                     filter.has_pc_max ? "" : "*",
                     filter.pc_max,
                     filter.has_scanline_min ? "" : "*",
                     filter.scanline_min,
                     filter.has_scanline_max ? "" : "*",
                     filter.scanline_max,
                     filter.has_dot_min ? "" : "*",
                     filter.dot_min,
                     filter.has_dot_max ? "" : "*",
                     filter.dot_max);
    }

    [[nodiscard]] hot_path_filter_t load_hot_path_filter() noexcept
    {
        hot_path_filter_t filter{};
        const char* pc_min_raw{ std::getenv("CLOVER_HOTPATH_PC_MIN") };
        const char* pc_max_raw{ std::getenv("CLOVER_HOTPATH_PC_MAX") };
        const char* frame_raw{ std::getenv("CLOVER_HOTPATH_FRAME") };
        const char* pb_raw{ std::getenv("CLOVER_HOTPATH_PB") };
        if (pc_min_raw == nullptr && pc_max_raw == nullptr && frame_raw == nullptr && pb_raw == nullptr)
            return filter;

        filter.enabled = true;
        filter.pb = static_cast<uint8_t>(parse_u64_env("CLOVER_HOTPATH_PB", 0x00u) & 0xffu);
        filter.pc_min = static_cast<uint16_t>(parse_u64_env("CLOVER_HOTPATH_PC_MIN", 0x0000u) & 0xffffu);
        filter.pc_max = static_cast<uint16_t>(parse_u64_env("CLOVER_HOTPATH_PC_MAX", 0xffffu) & 0xffffu);
        filter.active_frame = parse_u64_env("CLOVER_HOTPATH_FRAME", 0u);
        if (filter.pc_min > filter.pc_max)
            std::swap(filter.pc_min, filter.pc_max);
        return filter;
    }

    [[nodiscard]] watched_write_filter_t load_watched_write_filter() noexcept
    {
        watched_write_filter_t filter{};
        const char* address_min_raw{ std::getenv("CLOVER_WATCH_ADDR_MIN") };
        const char* address_max_raw{ std::getenv("CLOVER_WATCH_ADDR_MAX") };
        const char* frame_min_raw{ std::getenv("CLOVER_WATCH_FRAME_MIN") };
        const char* frame_max_raw{ std::getenv("CLOVER_WATCH_FRAME_MAX") };
        if (address_min_raw == nullptr && address_max_raw == nullptr
            && frame_min_raw == nullptr && frame_max_raw == nullptr)
        {
            return filter;
        }

        filter.enabled = true;
        filter.address_min = static_cast<uint32_t>(parse_u64_env("CLOVER_WATCH_ADDR_MIN", 0x000000u) & 0xffffffu);
        filter.address_max = static_cast<uint32_t>(parse_u64_env("CLOVER_WATCH_ADDR_MAX", 0xffffffu) & 0xffffffu);
        filter.frame_min = parse_u64_env("CLOVER_WATCH_FRAME_MIN", 0u);
        filter.frame_max = parse_u64_env("CLOVER_WATCH_FRAME_MAX", UINT64_MAX);
        if (filter.address_min > filter.address_max)
            std::swap(filter.address_min, filter.address_max);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

    [[nodiscard]] bus_window_filter_t load_bus_window_filter() noexcept
    {
        bus_window_filter_t filter{};
        const char* address_raw{ std::getenv("CLOVER_BUS_WINDOW_ADDR") };
        if (address_raw == nullptr || *address_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.address = static_cast<uint32_t>(parse_u64_env("CLOVER_BUS_WINDOW_ADDR", 0u) & 0xffffffu);
        filter.count = static_cast<uint16_t>(parse_u64_env("CLOVER_BUS_WINDOW_COUNT", 0x20u) & 0xffffu);
        if (filter.count == 0u)
            filter.count = 0x20u;
        return filter;
    }

    [[nodiscard]] bool cgram_trace_enabled() noexcept
    {
        return parse_bool_env("CLOVER_CAPTURE_CGRAM_TRACE");
    }

    [[nodiscard]] uint64_t cgram_trace_start_frame(uint64_t dump_start_frame) noexcept
    {
        const uint64_t fallback{ dump_start_frame > 0 ? dump_start_frame - 1u : 0u };
        return parse_u64_env("CLOVER_CAPTURE_CGRAM_TRACE_START_FRAME", fallback);
    }

    [[nodiscard]] bool oam_trace_enabled() noexcept
    {
        return parse_bool_env("CLOVER_CAPTURE_OAM_TRACE");
    }

    [[nodiscard]] uint64_t oam_trace_start_frame(uint64_t dump_start_frame) noexcept
    {
        const uint64_t fallback{ dump_start_frame > 0 ? dump_start_frame - 1u : 0u };
        return parse_u64_env("CLOVER_CAPTURE_OAM_TRACE_START_FRAME", fallback);
    }

    [[nodiscard]] clover::core::ppu_presentation_source_t load_dump_source() noexcept
    {
        if (const char* raw{ std::getenv("CLOVER_DUMP_SOURCE") }; raw != nullptr && *raw != '\0')
        {
            const std::string_view value{ raw };
            if (value == "presented" || value == "PRESENTED")
                return clover::core::ppu_presentation_source_t::presented;
            if (value == "composed" || value == "COMPOSED")
                return clover::core::ppu_presentation_source_t::composed;
        }

        return clover::core::ppu_presentation_source_t::composed;
    }

    struct wram_dump_config_t
    {
        bool enabled{ false };
        uint32_t offset{ 0 };
        uint32_t length{ 0 };
    };

    [[nodiscard]] wram_dump_config_t load_wram_dump_config() noexcept
    {
        wram_dump_config_t config{};
        const char* offset_raw{ std::getenv("CLOVER_DUMP_WRAM_OFFSET") };
        const char* length_raw{ std::getenv("CLOVER_DUMP_WRAM_LENGTH") };
        if (offset_raw == nullptr || length_raw == nullptr)
            return config;

        config.enabled = true;
        config.offset = static_cast<uint32_t>(parse_u64_env("CLOVER_DUMP_WRAM_OFFSET", 0u) & 0x1ffffu);
        config.length = static_cast<uint32_t>(parse_u64_env("CLOVER_DUMP_WRAM_LENGTH", 0u) & 0x1ffffu);
        return config;
    }

    [[nodiscard]] bool is_hot_path_pc(const clover::core::cpu_state_t& cpu,
                                      uint64_t active_frame,
                                      const hot_path_filter_t& filter) noexcept
    {
        if (!filter.enabled)
            return false;

        if (cpu.pb != filter.pb)
            return false;

        if (filter.active_frame != 0 && active_frame != filter.active_frame)
            return false;

        return cpu.pc >= filter.pc_min && cpu.pc <= filter.pc_max;
    }

    [[nodiscard]] std::vector<std::byte> read_file_bytes(const std::string& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return {};

        const std::vector<char> raw{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        std::vector<std::byte> bytes(raw.size());
        std::transform(raw.begin(), raw.end(), bytes.begin(), [](char value) noexcept
            {
                return static_cast<std::byte>(static_cast<unsigned char>(value));
            });
        return bytes;
    }

    void print_usage(const char* executable)
    {
        std::fprintf(stderr,
                     "Usage: %s <rom-path> [frames] [step-limit] [dump-dir] [dump-count] [dump-start-frame]\n"
                     "Example: %s roms/local/Super\\ Mario\\ World\\ \\(USA\\).sfc 180 10000000 dumps 3 120\n"
                     "Debug env: CLOVER_HOTPATH_PB, CLOVER_HOTPATH_PC_MIN, CLOVER_HOTPATH_PC_MAX, CLOVER_HOTPATH_FRAME\n",
                     executable,
                     executable);
    }

    [[nodiscard]] bool write_framebuffer_ppm(const std::filesystem::path& path,
                                             const clover::core::framebuffer_t& framebuffer)
    {
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output << "P6\n"
               << clover::core::framebuffer_t::k_width << ' ' << clover::core::framebuffer_t::k_height << "\n255\n";

        const uint32_t* const pixels{ framebuffer.data() };
        for (int index{ 0 }; index < clover::core::framebuffer_t::k_pixel_count; ++index)
        {
            const uint32_t rgba8{ pixels[index] };
            const unsigned char red{ static_cast<unsigned char>((rgba8 >> 16u) & 0xffu) };
            const unsigned char green{ static_cast<unsigned char>((rgba8 >> 8u) & 0xffu) };
            const unsigned char blue{ static_cast<unsigned char>(rgba8 & 0xffu) };
            output.write(reinterpret_cast<const char*>(&red), 1);
            output.write(reinterpret_cast<const char*>(&green), 1);
            output.write(reinterpret_cast<const char*>(&blue), 1);
        }

        return static_cast<bool>(output);
    }

    template <typename value_t, size_t size_v>
    [[nodiscard]] bool write_binary_blob(const std::filesystem::path& path,
                                         const std::array<value_t, size_v>& values)
    {
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output.write(reinterpret_cast<const char*>(values.data()),
                     static_cast<std::streamsize>(sizeof(value_t) * values.size()));
        return static_cast<bool>(output);
    }

    [[nodiscard]] bool write_binary_blob(const std::filesystem::path& path,
                                         std::span<const uint8_t> values)
    {
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output.write(reinterpret_cast<const char*>(values.data()),
                     static_cast<std::streamsize>(values.size()));
        return static_cast<bool>(output);
    }

    [[nodiscard]] std::string hex_label(uint64_t value, unsigned width)
    {
        std::string label(width, '0');
        std::snprintf(label.data(), label.size() + 1u, "%0*llx", static_cast<int>(width), static_cast<unsigned long long>(value));
        return label;
    }

    void print_cpu_state(const clover::core::cpu_state_t& cpu)
    {
        std::printf("CPU: PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                    cpu.pb,
                    cpu.pc,
                    cpu.a,
                    cpu.x,
                    cpu.y,
                    cpu.sp,
                    cpu.d,
                    cpu.db,
                    cpu.p,
                    cpu.emulation_mode ? 1u : 0u);
    }

    void print_cpu_trace(const std::deque<cpu_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("CPU trace:\n");
        for (const cpu_trace_entry_t& entry : trace)
        {
            std::printf("  step=%llu PB:%02x PC:%04x OP:%02x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.state.pb,
                        entry.state.pc,
                        entry.opcode,
                        entry.state.a,
                        entry.state.x,
                        entry.state.y,
                        entry.state.sp,
                        entry.state.d,
                        entry.state.db,
                        entry.state.p,
                        entry.state.emulation_mode ? 1u : 0u);
        }
    }

    void print_direct_page_watch(const std::deque<direct_page_watch_entry_t>& watch)
    {
        if (watch.empty())
            return;

        std::printf("Direct page watch:\n");
        for (const direct_page_watch_entry_t& entry : watch)
        {
            std::printf("  step=%llu PB:%02x PC:%04x OP:%02x 00:%02x 01:%02x 02:%02x 03:%02x 04:%02x 05:%02x 59:%02x 68:%02x 69:%02x 6a:%02x "
                        "f3:%02x f4:%02x f5:%02x f9:%02x fa:%02x fb:%02x fc:%02x 65:%02x 66:%02x 67:%02x ptr65:%06x [%02x %02x %02x] ptrf3:%06x [%02x %02x %02x]\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.value_00,
                        entry.value_01,
                        entry.value_02,
                        entry.value_03,
                        entry.value_04,
                        entry.value_05,
                        entry.value_59,
                        entry.value_68,
                        entry.value_69,
                        entry.value_6a,
                        entry.value_f3,
                        entry.value_f4,
                        entry.value_f5,
                        entry.value_f9,
                        entry.value_fa,
                        entry.value_fb,
                        entry.value_fc,
                        entry.value_65,
                        entry.value_66,
                        entry.value_67,
                        entry.pointer_65y,
                        entry.pointer_byte_0,
                        entry.pointer_byte_1,
                        entry.pointer_byte_2,
                        entry.pointer_f3y,
                        entry.pointer_f3_byte_0,
                        entry.pointer_f3_byte_1,
                        entry.pointer_f3_byte_2);
        }
    }

    void print_pointer_changes(const std::deque<pointer_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Pointer changes:\n");
        for (const pointer_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x/%02x/%02x -> %02x/%02x/%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_65,
                        entry.old_66,
                        entry.old_67,
                        entry.new_65,
                        entry.new_66,
                        entry.new_67);
        }
    }

    void print_source_changes(const std::deque<source_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Source changes:\n");
        for (const source_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x/%02x/%02x -> %02x/%02x/%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_68,
                        entry.old_69,
                        entry.old_6a,
                        entry.new_68,
                        entry.new_69,
                        entry.new_6a);
        }
    }

    void print_lowram_changes(const std::deque<lowram_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Low WRAM $0003 changes:\n");
        for (const lowram_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu frame=%llu slot=%s scanline=%u dot=%u before=%02x:%04x op=%02x after=%02x:%04x op=%02x %02x->%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        entry.slot_owner == clover::core::hardware_slot_owner_t::dma ? "dma" : "cpu",
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_value,
                        entry.new_value);
        }
    }

    void print_transfer_pointer_changes(const std::deque<transfer_pointer_change_entry_t>& changes)
    {
        if (changes.empty())
            return;

        std::printf("Transfer pointer changes:\n");
        for (const transfer_pointer_change_entry_t& entry : changes)
        {
            std::printf("  step=%llu before=%02x:%04x op=%02x after=%02x:%04x op=%02x "
                        "src %02x:%02x%02x -> %02x:%02x%02x "
                        "dst %02x:%02x%02x -> %02x:%02x%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode,
                        entry.old_f5,
                        entry.old_f4,
                        entry.old_f3,
                        entry.new_f5,
                        entry.new_f4,
                        entry.new_f3,
                        entry.old_f8,
                        entry.old_f7,
                        entry.old_f6,
                        entry.new_f8,
                        entry.new_f7,
                        entry.new_f6);
        }
    }

    void print_hot_path_trace(const std::deque<hot_path_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("Hot path trace:\n");
        for (const hot_path_trace_entry_t& entry : trace)
        {
            std::printf("  step=%llu completed=%llu active_frame=%llu scanline=%u dot=%u PB:%02x PC:%04x OP:%02x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x "
                        "apu[PC:%04x OP:%02x A:%02x X:%02x Y:%02x SP:%02x PSW:%02x T0:%u/%u/%u/%u line=%u en=%u tgt=%u T1:%u/%u/%u/%u line=%u en=%u tgt=%u T2:%u/%u/%u/%u line=%u en=%u tgt=%u] "
                        "wmadd=%05x "
                        "ports=%02x,%02x,%02x,%02x dp00-05=%02x,%02x,%02x,%02x,%02x,%02x effdp03[%04x]=%02x dp65-6a=%02x,%02x,%02x,%02x,%02x,%02x dpf3-f8=%02x,%02x,%02x,%02x,%02x,%02x dpf9-fc=%02x,%02x,%02x,%02x "
                        "dma[ch2 ctl=%02x bbus=%02x src=%02x:%04x size=%04x] "
                        "dma_bank=%02x src[%06x]=%02x src[%06x]=%02x src[%06x]=%02x src[%06x]=%02x src98=%02x src99=%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        static_cast<unsigned long long>(entry.active_frame),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.d,
                        entry.cpu.db,
                        entry.cpu.p,
                        entry.apu.pc,
                        entry.apu.last_opcode,
                        entry.apu.a,
                        entry.apu.x,
                        entry.apu.y,
                        entry.apu.sp,
                        entry.apu.psw,
                        entry.apu.timer0.stage0,
                        entry.apu.timer0.stage1,
                        entry.apu.timer0.stage2,
                        entry.apu.timer0.stage3,
                        entry.apu.timer0.line ? 1u : 0u,
                        entry.apu.timer0.enable ? 1u : 0u,
                        entry.apu.timer0.target,
                        entry.apu.timer1.stage0,
                        entry.apu.timer1.stage1,
                        entry.apu.timer1.stage2,
                        entry.apu.timer1.stage3,
                        entry.apu.timer1.line ? 1u : 0u,
                        entry.apu.timer1.enable ? 1u : 0u,
                        entry.apu.timer1.target,
                        entry.apu.timer2.stage0,
                        entry.apu.timer2.stage1,
                        entry.apu.timer2.stage2,
                        entry.apu.timer2.stage3,
                        entry.apu.timer2.line ? 1u : 0u,
                        entry.apu.timer2.enable ? 1u : 0u,
                        entry.apu.timer2.target,
                        entry.cpu_wram_address,
                        entry.apu_port_0,
                        entry.apu_port_1,
                        entry.apu_port_2,
                        entry.apu_port_3,
                        entry.dp_00,
                        entry.dp_01,
                        entry.dp_02,
                        entry.dp_03,
                        entry.dp_04,
                        entry.dp_05,
                        entry.effective_dp_03_address,
                        entry.effective_dp_03,
                        entry.dp_65,
                        entry.dp_66,
                        entry.dp_67,
                        entry.dp_68,
                        entry.dp_69,
                        entry.dp_6a,
                        entry.dp_f3,
                        entry.dp_f4,
                        entry.dp_f5,
                        entry.dp_f6,
                        entry.dp_f7,
                        entry.dp_f8,
                        entry.dp_f9,
                        entry.dp_fa,
                        entry.dp_fb,
                        entry.dp_fc,
                        entry.dma_control,
                        entry.dma_bbus,
                        entry.dma_source_bank_register,
                        entry.dma_source_address,
                        entry.dma_transfer_size,
                        entry.dma_source_bank,
                        entry.dma_source_0d84,
                        entry.dma_byte_0d84,
                        entry.dma_source_0d85,
                        entry.dma_byte_0d85,
                        entry.dma_source_0d8e,
                        entry.dma_byte_0d8e,
                        entry.dma_source_0d8f,
                        entry.dma_byte_0d8f,
                        entry.dma_byte_0d98,
                        entry.dma_byte_0d99);
        }
    }

    void print_helper_trace(const std::deque<helper_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("FF3 upload helper trace:\n");
        for (const helper_trace_entry_t& entry : trace)
        {
            const char* phase{ "pc" };
            if (entry.cpu.pb == 0xc0u && entry.cpu.pc == 0x046cu)
                phase = "entry";
            else if (entry.cpu.pb == 0xc0u && entry.cpu.pc == 0x04f7u)
                phase = "exit";

            std::printf("  phase=%s step=%llu completed=%llu active_frame=%llu scanline=%u dot=%u PB:%02x PC:%04x OP:%02x "
                        "A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x wmadd=%05x "
                        "src=%02x:%02x%02x dst=%02x:%02x%02x fc=%02x src_addr=%06x dst_addr=%06x\n",
                        phase,
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        static_cast<unsigned long long>(entry.active_frame),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.d,
                        entry.cpu.db,
                        entry.cpu.p,
                        entry.cpu_wram_address,
                        entry.dp_f5,
                        entry.dp_f4,
                        entry.dp_f3,
                        entry.dp_f8,
                        entry.dp_f7,
                        entry.dp_f6,
                        entry.dp_fc,
                        entry.source_address,
                        entry.destination_address);
        }
    }

    void print_ff3_call_trace(const std::deque<ff3_call_trace_entry_t>& trace)
    {
        if (trace.empty())
            return;

        std::printf("FF3 caller/helper control trace:\n");
        for (const ff3_call_trace_entry_t& entry : trace)
        {
            std::printf("  step=%llu completed=%llu active_frame=%llu scanline=%u dot=%u "
                        "PB:%02x PC:%04x OP:%02x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x "
                        "wmadd=%05x src=%02x:%02x%02x dst=%02x:%02x%02x f9-fc=%02x,%02x,%02x,%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        static_cast<unsigned long long>(entry.active_frame),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.opcode,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.d,
                        entry.cpu.db,
                        entry.cpu.p,
                        entry.cpu_wram_address,
                        entry.dp_f5,
                        entry.dp_f4,
                        entry.dp_f3,
                        entry.dp_f8,
                        entry.dp_f7,
                        entry.dp_f6,
                        entry.dp_f9,
                        entry.dp_fa,
                        entry.dp_fb,
                        entry.dp_fc);
        }
    }

    void print_timing(const char* label, const clover::core::timing_snapshot_t& timing)
    {
        std::printf("%s: master=%llu scanline=%u dot=%u hblank=%u vblank=%u\n",
                    label,
                    static_cast<unsigned long long>(timing.master_clock),
                    timing.raster.scanline,
                    timing.raster.dot,
                    timing.in_hblank ? 1u : 0u,
                    timing.in_vblank ? 1u : 0u);
    }

    void print_interrupts(const clover::core::interrupt_state_t& interrupts)
    {
        std::printf("Interrupts: nmi_line=%u nmi_pending=%u irq_line=%u irq_pending=%u irq_lock=%u\n",
                    interrupts.nmi_line ? 1u : 0u,
                    interrupts.nmi_pending ? 1u : 0u,
                    interrupts.irq_line ? 1u : 0u,
                    interrupts.irq_pending ? 1u : 0u,
                    interrupts.irq_lock ? 1u : 0u);
    }

    void print_apu_ports(clover::core::console_t& console)
    {
        std::printf("APU ports: 2140=%02x 2141=%02x 2142=%02x 2143=%02x\n",
                    console.read_u8(0x002140u),
                    console.read_u8(0x002141u),
                    console.read_u8(0x002142u),
                    console.read_u8(0x002143u));
    }

    void print_apu_state(const clover::core::apu_state_t& apu)
    {
        std::printf("APU: PC=%04x A=%02x X=%02x Y=%02x SP=%02x PSW=%02x IPL=%u halted=%u last=%02x waits=%u/%u timers=%u/%u trace=%u io_trace=%u credit=%lld\n",
                    apu.pc,
                    apu.a,
                    apu.x,
                    apu.y,
                    apu.sp,
                    apu.psw,
                    apu.ipl_rom_enabled ? 1u : 0u,
                    apu.halted ? 1u : 0u,
                    apu.last_opcode,
                    apu.external_wait_states,
                    apu.internal_wait_states,
                    apu.timers_enable ? 1u : 0u,
                    apu.timers_disable ? 1u : 0u,
                    apu.instruction_trace_count,
                    apu.io_trace_count,
                    static_cast<long long>(apu.smp_clock_credit));
        std::printf("APU timers: T0 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u | T1 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u | T2 s0=%u s1=%u s2=%u s3=%u line=%u en=%u tgt=%u\n",
                    apu.timer0.stage0,
                    apu.timer0.stage1,
                    apu.timer0.stage2,
                    apu.timer0.stage3,
                    apu.timer0.line ? 1u : 0u,
                    apu.timer0.enable ? 1u : 0u,
                    apu.timer0.target,
                    apu.timer1.stage0,
                    apu.timer1.stage1,
                    apu.timer1.stage2,
                    apu.timer1.stage3,
                    apu.timer1.line ? 1u : 0u,
                    apu.timer1.enable ? 1u : 0u,
                    apu.timer1.target,
                    apu.timer2.stage0,
                    apu.timer2.stage1,
                    apu.timer2.stage2,
                    apu.timer2.stage3,
                    apu.timer2.line ? 1u : 0u,
                    apu.timer2.enable ? 1u : 0u,
                    apu.timer2.target);
    }

    void print_apu_window(const clover::core::console_t& console, const clover::core::apu_state_t& apu)
    {
        std::printf("APU window:");
        for (int offset{ -8 }; offset <= 8; ++offset)
        {
            const uint16_t address{ static_cast<uint16_t>(apu.pc + offset) };
            std::printf(" %04x=%02x", address, console.apu_peek_ram(address));
        }
        std::printf("\n");
    }

    void print_apu_instruction_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_instruction_trace() };
        const uint16_t trace_count{ console.apu_instruction_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU instruction trace:\n");
        for (uint16_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  clk=%llu PC=%04x OP=%02x A=%02x X=%02x Y=%02x SP=%02x PSW=%02x T0=%u/%u ports=%02x,%02x,%02x,%02x\n",
                        static_cast<unsigned long long>(entry.master_clock),
                        entry.pc,
                        entry.opcode,
                        entry.a,
                        entry.x,
                        entry.y,
                        entry.sp,
                        entry.psw,
                        entry.timer0_stage2,
                        entry.timer0_stage3,
                        entry.port0,
                        entry.port1,
                        entry.port2,
                        entry.port3);
        }
    }

    void print_apu_io_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_io_trace() };
        const uint16_t trace_count{ console.apu_io_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU IO trace:\n");
        for (uint16_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  clk=%llu %c addr=%04x value=%02x PC=%04x OP=%02x A=%02x X=%02x Y=%02x PSW=%02x credit=%lld\n",
                        static_cast<unsigned long long>(entry.master_clock),
                        entry.is_write ? 'W' : 'R',
                        entry.address,
                        entry.value,
                        entry.pc,
                        entry.opcode,
                        entry.a,
                        entry.x,
                        entry.y,
                        entry.psw,
                        static_cast<long long>(entry.smp_clock_credit));
        }
    }

    void print_direct_page_window(clover::core::console_t& console,
                                  uint16_t start,
                                  uint16_t count)
    {
        std::printf("Direct page:");
        for (uint16_t index{ 0 }; index < count; ++index)
        {
            const uint16_t address{ static_cast<uint16_t>(start + index) };
            std::printf(" %02x:%02x", address & 0x00ffu, console.read_u8(address));
        }
        std::printf("\n");
    }

    void print_bus_window(clover::core::console_t& console,
                          const char* label,
                          uint32_t start,
                          uint16_t count)
    {
        std::printf("%s:", label);
        for (uint16_t index{ 0 }; index < count; ++index)
        {
            const uint32_t address{ start + index };
            std::printf(" %06x:%02x", address, console.read_u8(address));
        }
        std::printf("\n");
    }

    void print_ppu_summary(const clover::core::ppu_render_state_snapshot_t& ppu)
    {
        std::printf("PPU: display_disabled=%u brightness=%u bg_mode=%u hires=%u overscan=%u interlace=%u\n",
                    ppu.display_disabled ? 1u : 0u,
                    ppu.brightness,
                    ppu.bg_mode,
                    ppu.hires ? 1u : 0u,
                    ppu.overscan ? 1u : 0u,
                    ppu.interlace ? 1u : 0u);
        std::printf("OBJ: first=%u eval_first=%u eval_count=%u tile_count=%u range_over=%u time_over=%u\n",
                    ppu.objects.first_sprite,
                    ppu.objects.evaluation_first_sprite,
                    ppu.objects.evaluation_count,
                    ppu.objects.tile_count,
                    ppu.objects.range_over ? 1u : 0u,
                    ppu.objects.time_over ? 1u : 0u);
        std::printf("BG1: active=%u tiledata=%04x screen=%04x hoff=%u voff=%u\n",
                    ppu.backgrounds[0].active ? 1u : 0u,
                    ppu.backgrounds[0].tiledata_address,
                    ppu.backgrounds[0].screen_address,
                    ppu.backgrounds[0].hoffset,
                    ppu.backgrounds[0].voffset);
        std::printf("Mode7: A=%04x B=%04x C=%04x D=%04x X=%04x Y=%04x\n",
                    ppu.mode7_a,
                    ppu.mode7_b,
                    ppu.mode7_c,
                    ppu.mode7_d,
                    ppu.mode7_x,
                    ppu.mode7_y);
        if (ppu.display_write_count > 0)
        {
            std::printf("INIDISP writes:\n");
            for (uint8_t index{ 0 }; index < ppu.display_write_count; ++index)
            {
                const auto& write{ ppu.recent_display_writes[index] };
                std::printf("  frame=%llu scanline=%u dot=%u value=%02x disabled=%u brightness=%u\n",
                            static_cast<unsigned long long>(write.frame_index),
                            write.scanline,
                            write.dot,
                            write.value,
                            (write.value & 0x80u) != 0 ? 1u : 0u,
                            write.value & 0x0fu);
            }
        }
    }

    void print_compositor_summary(const clover::core::ppu_compositor_snapshot_t& compositor)
    {
    std::printf("Compositor: above_pri=%u below_pri=%u obj_above_pri=%u obj_below_pri=%u out0=%04x\n",
                compositor.above.priority,
                compositor.below.priority,
                compositor.objects.above.priority,
                compositor.objects.below.priority,
                compositor.output_color[0]);
    }

    [[nodiscard]] bool is_ff3_call_trace_pc(const clover::core::cpu_state_t& cpu) noexcept
    {
        if (cpu.pb != 0xc0u)
            return false;

        return (cpu.pc >= 0x2789u && cpu.pc <= 0x278fu)
            || (cpu.pc >= 0x046cu && cpu.pc <= 0x048eu)
            || (cpu.pc >= 0x04edu && cpu.pc <= 0x04f7u);
    }

    void print_ppu_register_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.ppu_register_write_trace() };
        const uint8_t trace_count{ console.ppu_register_write_trace_count() };
        bool printed_header{ false };
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            const uint16_t address{ static_cast<uint16_t>(entry.address & 0xffffu) };
            const bool interesting_register{
                address == 0x2100u
                || address == 0x2102u
                || address == 0x2103u
                || address == 0x2104u
                || address == 0x2115u
                || address == 0x2116u
                || address == 0x2117u
                || address == 0x2118u
                || address == 0x2119u
                || address == 0x2121u
                || address == 0x2122u
            };
            if (!interesting_register)
                continue;

            if (!printed_header)
            {
                std::printf("PPU register writes:\n");
                printed_header = true;
            }

            std::printf("  addr=%04x value=%02x scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        address,
                        entry.value,
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }

    void print_system_register_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.system_register_write_trace() };
        const uint8_t trace_count{ console.system_register_write_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("System register writes:\n");
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  addr=%04x value=%02x frame=%llu scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        static_cast<unsigned>(entry.address & 0xffffu),
                        entry.value,
                        static_cast<unsigned long long>(entry.frame_index),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }

    void print_watched_write_trace(const clover::core::console_t& console,
                                   const watched_write_filter_t& filter)
    {
        const auto& trace{ console.watched_write_trace() };
        const uint8_t trace_count{ console.watched_write_trace_count() };
        bool printed_header{ false };
        for (uint8_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            if (filter.enabled)
            {
                if (entry.address < filter.address_min || entry.address > filter.address_max)
                    continue;

                if (entry.frame_index < filter.frame_min || entry.frame_index > filter.frame_max)
                    continue;
            }
            if (!printed_header)
            {
                std::printf("Watched WRAM writes:\n");
                printed_header = true;
            }

            std::printf("  frame=%llu addr=%06x value=%02x scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                        static_cast<unsigned long long>(entry.frame_index),
                        entry.address,
                        entry.value,
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.d,
                        entry.cpu.db,
                        entry.cpu.p,
                        entry.cpu.emulation_mode ? 1u : 0u);
        }
    }

    void print_cgram_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.ppu_cgram_write_trace() };
        const std::size_t trace_count{ console.ppu_cgram_write_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("PPU CGRAM writes:\n");
        for (std::size_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  frame=%llu scanline=%u dot=%u req=%02x eff=%02x latch=%02x value=%04x redirected=%u\n",
                        static_cast<unsigned long long>(entry.frame_index),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.requested_address,
                        entry.effective_address,
                        entry.latched_address,
                        entry.value,
                        entry.redirected ? 1u : 0u);
        }
    }

    void print_oam_write_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.ppu_oam_write_trace() };
        const std::size_t trace_count{ console.ppu_oam_write_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("PPU OAM writes:\n");
        for (std::size_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  frame=%llu scanline=%u dot=%u req=%03x eff=%03x latch=%03x value=%02x redirected=%u\n",
                        static_cast<unsigned long long>(entry.frame_index),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.requested_address,
                        entry.effective_address,
                        entry.latched_address,
                        entry.value,
                        entry.redirected ? 1u : 0u);
        }
    }

    void print_apu_port_trace(const clover::core::console_t& console)
    {
        const auto& trace{ console.apu_port_trace() };
        const uint16_t trace_count{ console.apu_port_trace_count() };
        if (trace_count == 0)
            return;

        std::printf("APU port trace:\n");
        for (uint16_t index{ 0 }; index < trace_count; ++index)
        {
            const auto& entry{ trace[index] };
            std::printf("  %c frame=%llu addr=%04x value=%02x apply=%u clk=%llu scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x\n",
                        entry.is_write ? 'W' : 'R',
                        static_cast<unsigned long long>(entry.frame_index),
                        static_cast<uint16_t>(entry.address & 0xffffu),
                        entry.value,
                        entry.apply_after_clocks,
                        static_cast<unsigned long long>(entry.timing.master_clock),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        entry.cpu.pb,
                        entry.cpu.pc,
                        entry.cpu.a,
                        entry.cpu.x,
                        entry.cpu.y,
                        entry.cpu.sp,
                        entry.cpu.p);
        }
    }

    [[nodiscard]] const char* dma_activity_name(clover::core::dma_activity_t activity) noexcept
    {
        switch (activity)
        {
        case clover::core::dma_activity_t::idle:
            return "idle";
        case clover::core::dma_activity_t::general_dma:
            return "general";
        case clover::core::dma_activity_t::hdma_setup:
            return "hdma_setup";
        case clover::core::dma_activity_t::hdma_transfer:
            return "hdma_transfer";
        default:
            return "unknown";
        }
    }

    [[nodiscard]] const char* slot_owner_name(clover::core::hardware_slot_owner_t owner) noexcept
    {
        switch (owner)
        {
        case clover::core::hardware_slot_owner_t::cpu:
            return "cpu";
        case clover::core::hardware_slot_owner_t::dma:
            return "dma";
        default:
            return "unknown";
        }
    }

    void print_dma_transitions(const std::deque<dma_transition_entry_t>& transitions)
    {
        if (transitions.empty())
            return;

        std::printf("DMA transitions:\n");
        for (const dma_transition_entry_t& entry : transitions)
        {
            std::printf("  step=%llu frame=%llu slot=%s scanline=%u dot=%u %s/%u/%u -> %s/%u/%u before=%02x:%04x op=%02x after=%02x:%04x op=%02x\n",
                        static_cast<unsigned long long>(entry.hardware_step),
                        static_cast<unsigned long long>(entry.frame_completions),
                        slot_owner_name(entry.slot_owner),
                        entry.timing.raster.scanline,
                        entry.timing.raster.dot,
                        dma_activity_name(entry.before_activity),
                        entry.before_general_pending ? 1u : 0u,
                        entry.before_hdma_pending ? 1u : 0u,
                        dma_activity_name(entry.after_activity),
                        entry.after_general_pending ? 1u : 0u,
                        entry.after_hdma_pending ? 1u : 0u,
                        entry.before_cpu.pb,
                        entry.before_cpu.pc,
                        entry.before_opcode,
                        entry.after_cpu.pb,
                        entry.after_cpu.pc,
                        entry.after_opcode);
        }
    }
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 7)
    {
        print_usage(argv[0]);
        return 1;
    }

    const std::string rom_path{ argv[1] };
    uint64_t target_frames{ 3 };
    if (argc >= 3)
    {
        target_frames = std::strtoull(argv[2], nullptr, 10);
        if (target_frames == 0)
            target_frames = 1;
    }

    uint64_t step_limit{ 2'000'000u };
    if (argc >= 4)
    {
        step_limit = std::strtoull(argv[3], nullptr, 10);
        if (step_limit == 0)
            step_limit = 2'000'000u;
    }

    std::filesystem::path dump_directory{};
    uint64_t dump_count{ 0 };
    uint64_t dump_start_frame{ 1 };
    if (argc >= 5)
        dump_directory = argv[4];

    if (argc >= 6)
        dump_count = std::strtoull(argv[5], nullptr, 10);

    if (argc >= 7)
    {
        dump_start_frame = std::strtoull(argv[6], nullptr, 10);
        if (dump_start_frame == 0)
            dump_start_frame = 1;
    }

    const std::vector<std::byte> rom_bytes{ read_file_bytes(rom_path) };
    if (rom_bytes.empty())
    {
        std::fprintf(stderr, "Failed to read ROM: %s\n", rom_path.c_str());
        return 1;
    }

    clover::core::cartridge_t cartridge_probe{};
    if (!cartridge_probe.load(rom_bytes))
    {
        std::fprintf(stderr, "Cartridge detection failed: %s\n", rom_path.c_str());
        return 1;
    }

    const startup_entropy_config_t startup_entropy_config{ load_startup_entropy_config() };
    clover::core::console_t console{};
    console.set_startup_entropy_mode(startup_entropy_config.mode);
    if (startup_entropy_config.seed_enabled)
        console.set_startup_entropy_seed(startup_entropy_config.seed, startup_entropy_config.sequence);
    if (!console.load_cartridge(rom_bytes))
    {
        std::fprintf(stderr, "Console load failed: %s\n", rom_path.c_str());
        return 1;
    }

    console.power_on();
    const hot_path_filter_t hot_path_filter{ load_hot_path_filter() };
    const watched_write_filter_t watched_write_filter{ load_watched_write_filter() };
    const bus_window_filter_t bus_window_filter{ load_bus_window_filter() };
    const generic_trace_filter_t generic_trace_filter{ load_generic_trace_filter() };
    const ppu_probe_filter_t ppu_probe_filter{ load_ppu_probe_filter() };
    const bool verbose_output{ parse_bool_env("CLOVER_BRINGUP_VERBOSE") };
    const bool capture_cgram_trace{ cgram_trace_enabled() };
    const uint64_t cgram_trace_start{ cgram_trace_start_frame(dump_start_frame) };
    const bool capture_oam_trace{ oam_trace_enabled() };
    const uint64_t oam_trace_start{ oam_trace_start_frame(dump_start_frame) };
    const clover::core::ppu_presentation_source_t dump_source{ load_dump_source() };
    const wram_dump_config_t wram_dump_config{ load_wram_dump_config() };
    const bool capture_direct_page_watch{ parse_bool_env("CLOVER_CAPTURE_DIRECT_PAGE") };
    const bool capture_transfer_pointer_changes{ parse_bool_env("CLOVER_CAPTURE_TRANSFER_POINTERS") };
    const bool capture_helper_trace{ parse_bool_env("CLOVER_CAPTURE_HELPER_TRACE") };
    const bool capture_ff3_call_trace{ parse_bool_env("CLOVER_CAPTURE_FF3_CALL_TRACE") };
    std::FILE* generic_trace_file{ nullptr };
    if (generic_trace_filter.enabled)
    {
        generic_trace_file = std::fopen(generic_trace_filter.output_path.string().c_str(), "w");
        if (generic_trace_file == nullptr)
        {
            std::fprintf(stderr, "Failed to open trace file: %s\n", generic_trace_filter.output_path.string().c_str());
            return 1;
        }
        write_generic_trace_header(generic_trace_file, generic_trace_filter);
    }
    const bool dump_frames{ !dump_directory.empty() && dump_count > 0 };
    if (dump_frames)
    {
        std::error_code error{};
        std::filesystem::create_directories(dump_directory, error);
        if (error)
        {
            std::fprintf(stderr, "Failed to create dump directory: %s\n", dump_directory.string().c_str());
            return 1;
        }
        console.set_frame_capture_enabled(true);
    }
    if (capture_cgram_trace)
        console.set_ppu_cgram_write_trace_start_frame(cgram_trace_start);
    if (capture_oam_trace)
        console.set_ppu_oam_write_trace_start_frame(oam_trace_start);

    bringup_summary_t summary{};
    std::deque<cpu_trace_entry_t> cpu_trace{};
    std::deque<direct_page_watch_entry_t> direct_page_watch{};
    std::deque<pointer_change_entry_t> pointer_changes{};
    std::deque<source_change_entry_t> source_changes{};
    std::deque<lowram_change_entry_t> lowram_03_changes{};
    std::deque<transfer_pointer_change_entry_t> transfer_pointer_changes{};
    std::deque<hot_path_trace_entry_t> hot_path_trace{};
    std::deque<helper_trace_entry_t> helper_trace{};
    std::deque<ff3_call_trace_entry_t> ff3_call_trace{};
    std::deque<dma_transition_entry_t> dma_transitions{};
    clover::core::cpu_state_t last_recorded_cpu{};
    bool have_last_recorded_cpu{ false };
    direct_page_watch_entry_t last_direct_page_watch{};
    bool have_last_direct_page_watch{ false };
    bool terminal_pc_detected{ false };
    uint64_t dumped_frames{ 0 };
    while (summary.steps < step_limit && summary.frame_completions < target_frames)
    {
        const clover::core::cpu_state_t current_cpu{ console.cpu_state() };
        const uint8_t current_opcode{ console.read_u8((static_cast<uint32_t>(current_cpu.pb) << 16u) | current_cpu.pc) };
        const uint64_t active_frame{ summary.frame_completions + 1u };
        const clover::core::hardware_timing_snapshot_t timing_snapshot{ console.capture_timing_snapshot() };
        if (should_emit_generic_trace(generic_trace_filter, current_cpu, timing_snapshot, active_frame))
        {
            const uint8_t dp_00{ console.read_u8(0x000000u) };
            const uint8_t dp_01{ console.read_u8(0x000001u) };
            const uint8_t dp_02{ console.read_u8(0x000002u) };
            const uint8_t dp_03{ console.read_u8(0x000003u) };
            const uint8_t dp_04{ console.read_u8(0x000004u) };
            const uint8_t dp_05{ console.read_u8(0x000005u) };
            const uint8_t dp_12{ console.read_u8(0x000012u) };
            const uint16_t effective_dp_00_address{ static_cast<uint16_t>(current_cpu.d + 0x0000u) };
            const uint16_t effective_dp_0c_address{ static_cast<uint16_t>(current_cpu.d + 0x000cu) };
            const uint8_t effective_dp_00{ console.read_u8(effective_dp_00_address) };
            const uint8_t effective_dp_0c{ console.read_u8(effective_dp_0c_address) };
            const uint8_t dp_f3{ console.read_u8(0x0000f3u) };
            const uint8_t dp_f4{ console.read_u8(0x0000f4u) };
            const uint8_t dp_f5{ console.read_u8(0x0000f5u) };
            const uint8_t dp_f6{ console.read_u8(0x0000f6u) };
            const uint8_t dp_f7{ console.read_u8(0x0000f7u) };
            const uint8_t dp_f8{ console.read_u8(0x0000f8u) };
            std::fprintf(generic_trace_file,
                         "CPUHOT step=%llu frame=%llu scanline=%u dot=%u PB:%02x PC:%04x OP:%02x "
                         "A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u "
                         "wmadd=%05x irq=%u irq_pending=%u irq_transition=%u irq_lock=%u "
                         "nmi=%u nmi_pending=%u dma=%u hdma=%u gdma=%u in_hblank=%u in_vblank=%u "
                         "dp00-05=%02x,%02x,%02x,%02x,%02x,%02x dp12=%02x effdp00[%04x]=%02x effdp0c[%04x]=%02x "
                         "src=%02x:%02x%02x dst=%02x:%02x%02x f9-fc=%02x,%02x,%02x,%02x\n",
                         static_cast<unsigned long long>(summary.steps),
                         static_cast<unsigned long long>(active_frame),
                         timing_snapshot.ppu_timing.raster.scanline,
                         timing_snapshot.ppu_timing.raster.dot,
                         current_cpu.pb,
                         current_cpu.pc,
                         current_opcode,
                         current_cpu.a,
                         current_cpu.x,
                         current_cpu.y,
                         current_cpu.sp,
                         current_cpu.d,
                         current_cpu.db,
                         current_cpu.p,
                         current_cpu.emulation_mode ? 1u : 0u,
                         console.cpu_wram_address(),
                         timing_snapshot.interrupts.irq_line ? 1u : 0u,
                         timing_snapshot.interrupts.irq_pending ? 1u : 0u,
                         timing_snapshot.interrupts.irq_transition ? 1u : 0u,
                         timing_snapshot.interrupts.irq_lock ? 1u : 0u,
                         timing_snapshot.interrupts.nmi_line ? 1u : 0u,
                         timing_snapshot.interrupts.nmi_pending ? 1u : 0u,
                         static_cast<unsigned>(timing_snapshot.dma_activity),
                         timing_snapshot.hdma_pending ? 1u : 0u,
                         timing_snapshot.general_dma_pending ? 1u : 0u,
                         timing_snapshot.ppu_timing.in_hblank ? 1u : 0u,
                         timing_snapshot.ppu_timing.in_vblank ? 1u : 0u,
                         dp_00,
                         dp_01,
                         dp_02,
                         dp_03,
                         dp_04,
                         dp_05,
                         dp_12,
                         effective_dp_00_address,
                         effective_dp_00,
                         effective_dp_0c_address,
                         effective_dp_0c,
                         dp_f5,
                         dp_f4,
                         dp_f3,
                         dp_f8,
                         dp_f7,
                         dp_f6,
                         console.read_u8(0x0000f9u),
                         console.read_u8(0x0000fau),
                         console.read_u8(0x0000fbu),
                         console.read_u8(0x0000fcu));
        }
        if (is_hot_path_pc(current_cpu, active_frame, hot_path_filter))
        {
            const clover::core::apu_state_t current_apu{ console.apu_state() };
            const uint8_t dma_source_bank{ static_cast<uint8_t>(current_cpu.y & 0x00ffu) };
            const uint16_t effective_dp_03_address{ static_cast<uint16_t>(current_cpu.d + 0x0003u) };
            const uint32_t dma_source_0d84{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d84u
            };
            const uint32_t dma_source_0d85{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d85u
            };
            const uint32_t dma_source_0d8e{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d8eu
            };
            const uint32_t dma_source_0d8f{
                (static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d8fu
            };
            hot_path_trace.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .active_frame = active_frame,
                .timing = console.timing(),
                .cpu = current_cpu,
                .apu = current_apu,
                .opcode = current_opcode,
                .cpu_wram_address = console.cpu_wram_address(),
                .apu_port_0 = console.read_u8(0x002140u),
                .apu_port_1 = console.read_u8(0x002141u),
                .apu_port_2 = console.read_u8(0x002142u),
                .apu_port_3 = console.read_u8(0x002143u),
                .dp_00 = console.read_u8(0x000000u),
                .dp_01 = console.read_u8(0x000001u),
                .dp_02 = console.read_u8(0x000002u),
                .dp_03 = console.read_u8(0x000003u),
                .effective_dp_03_address = effective_dp_03_address,
                .effective_dp_03 = console.read_u8(effective_dp_03_address),
                .dp_04 = console.read_u8(0x000004u),
                .dp_05 = console.read_u8(0x000005u),
                .dp_65 = console.read_u8(0x000065u),
                .dp_66 = console.read_u8(0x000066u),
                .dp_67 = console.read_u8(0x000067u),
                .dp_68 = console.read_u8(0x000068u),
                .dp_69 = console.read_u8(0x000069u),
                .dp_6a = console.read_u8(0x00006au),
                .dp_f3 = console.read_u8(0x0000f3u),
                .dp_f4 = console.read_u8(0x0000f4u),
                .dp_f5 = console.read_u8(0x0000f5u),
                .dp_f6 = console.read_u8(0x0000f6u),
                .dp_f7 = console.read_u8(0x0000f7u),
                .dp_f8 = console.read_u8(0x0000f8u),
                .dp_f9 = console.read_u8(0x0000f9u),
                .dp_fa = console.read_u8(0x0000fau),
                .dp_fb = console.read_u8(0x0000fbu),
                .dp_fc = console.read_u8(0x0000fcu),
                .dma_source_bank = dma_source_bank,
                .dma_source_0d84 = dma_source_0d84,
                .dma_source_0d85 = dma_source_0d85,
                .dma_source_0d8e = dma_source_0d8e,
                .dma_source_0d8f = dma_source_0d8f,
                .dma_byte_0d84 = console.read_u8(dma_source_0d84),
                .dma_byte_0d85 = console.read_u8(dma_source_0d85),
                .dma_byte_0d8e = console.read_u8(dma_source_0d8e),
                .dma_byte_0d8f = console.read_u8(dma_source_0d8f),
                .dma_byte_0d98 = console.read_u8((static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d98u),
                .dma_byte_0d99 = console.read_u8((static_cast<uint32_t>(dma_source_bank) << 16u) | 0x0d99u),
                .dma_control = console.read_u8(0x004320u),
                .dma_bbus = console.read_u8(0x004321u),
                .dma_source_address = static_cast<uint16_t>(
                    console.read_u8(0x004322u) | (console.read_u8(0x004323u) << 8u)
                ),
                .dma_source_bank_register = console.read_u8(0x004324u),
                .dma_transfer_size = static_cast<uint16_t>(
                    console.read_u8(0x004325u) | (console.read_u8(0x004326u) << 8u)
                )
            });
            if (hot_path_trace.size() > 2048)
                hot_path_trace.pop_front();
        }
        if (capture_helper_trace
            && current_cpu.pb == 0xc0u
            && (current_cpu.pc == 0x046cu || current_cpu.pc == 0x04f7u))
        {
            const uint8_t dp_f3{ console.read_u8(0x0000f3u) };
            const uint8_t dp_f4{ console.read_u8(0x0000f4u) };
            const uint8_t dp_f5{ console.read_u8(0x0000f5u) };
            const uint8_t dp_f6{ console.read_u8(0x0000f6u) };
            const uint8_t dp_f7{ console.read_u8(0x0000f7u) };
            const uint8_t dp_f8{ console.read_u8(0x0000f8u) };
            helper_trace.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .active_frame = active_frame,
                .timing = console.timing(),
                .cpu = current_cpu,
                .opcode = current_opcode,
                .cpu_wram_address = console.cpu_wram_address(),
                .dp_f3 = dp_f3,
                .dp_f4 = dp_f4,
                .dp_f5 = dp_f5,
                .dp_f6 = dp_f6,
                .dp_f7 = dp_f7,
                .dp_f8 = dp_f8,
                .dp_fc = console.read_u8(0x0000fcu),
                .source_address = (static_cast<uint32_t>(dp_f5) << 16u)
                    | (static_cast<uint32_t>(dp_f4) << 8u)
                    | dp_f3,
                .destination_address = (static_cast<uint32_t>(dp_f8) << 16u)
                    | (static_cast<uint32_t>(dp_f7) << 8u)
                    | dp_f6
            });
            if (helper_trace.size() > 512)
                helper_trace.pop_front();
        }
        if (capture_ff3_call_trace && is_ff3_call_trace_pc(current_cpu))
        {
            const uint8_t dp_f3{ console.read_u8(0x0000f3u) };
            const uint8_t dp_f4{ console.read_u8(0x0000f4u) };
            const uint8_t dp_f5{ console.read_u8(0x0000f5u) };
            const uint8_t dp_f6{ console.read_u8(0x0000f6u) };
            const uint8_t dp_f7{ console.read_u8(0x0000f7u) };
            const uint8_t dp_f8{ console.read_u8(0x0000f8u) };
            ff3_call_trace.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .active_frame = active_frame,
                .timing = console.timing(),
                .cpu = current_cpu,
                .opcode = current_opcode,
                .cpu_wram_address = console.cpu_wram_address(),
                .dp_f3 = dp_f3,
                .dp_f4 = dp_f4,
                .dp_f5 = dp_f5,
                .dp_f6 = dp_f6,
                .dp_f7 = dp_f7,
                .dp_f8 = dp_f8,
                .dp_f9 = console.read_u8(0x0000f9u),
                .dp_fa = console.read_u8(0x0000fau),
                .dp_fb = console.read_u8(0x0000fbu),
                .dp_fc = console.read_u8(0x0000fcu),
                .source_address = (static_cast<uint32_t>(dp_f5) << 16u)
                    | (static_cast<uint32_t>(dp_f4) << 8u)
                    | dp_f3,
                .destination_address = (static_cast<uint32_t>(dp_f8) << 16u)
                    | (static_cast<uint32_t>(dp_f7) << 8u)
                    | dp_f6
            });
            if (ff3_call_trace.size() > 1024)
                ff3_call_trace.pop_front();
        }
        if (!have_last_recorded_cpu
            || current_cpu.pc != last_recorded_cpu.pc
            || current_cpu.pb != last_recorded_cpu.pb
            || current_cpu.sp != last_recorded_cpu.sp
            || current_cpu.a != last_recorded_cpu.a
            || current_cpu.x != last_recorded_cpu.x
            || current_cpu.y != last_recorded_cpu.y
            || current_cpu.d != last_recorded_cpu.d
            || current_cpu.db != last_recorded_cpu.db
            || current_cpu.p != last_recorded_cpu.p
            || current_cpu.emulation_mode != last_recorded_cpu.emulation_mode)
        {
            cpu_trace.push_back({
                .hardware_step = summary.steps,
                .state = current_cpu,
                .opcode = current_opcode
            });
            if (cpu_trace.size() > 64)
                cpu_trace.pop_front();
            last_recorded_cpu = current_cpu;
            have_last_recorded_cpu = true;
        }

        uint8_t value_65{ 0 };
        uint8_t value_66{ 0 };
        uint8_t value_67{ 0 };
        uint8_t value_68{ 0 };
        uint8_t value_69{ 0 };
        uint8_t value_6a{ 0 };
        uint8_t value_f3{ 0 };
        uint8_t value_f4{ 0 };
        uint8_t value_f5{ 0 };
        uint8_t value_f6{ 0 };
        uint8_t value_f7{ 0 };
        uint8_t value_f8{ 0 };
        direct_page_watch_entry_t current_direct_page_watch{};
        if (capture_direct_page_watch || capture_transfer_pointer_changes)
        {
            value_65 = console.read_u8(0x000065u);
            value_66 = console.read_u8(0x000066u);
            value_67 = console.read_u8(0x000067u);
            value_68 = console.read_u8(0x000068u);
            value_69 = console.read_u8(0x000069u);
            value_6a = console.read_u8(0x00006au);
            value_f3 = console.read_u8(0x0000f3u);
            value_f4 = console.read_u8(0x0000f4u);
            value_f5 = console.read_u8(0x0000f5u);
            value_f6 = console.read_u8(0x0000f6u);
            value_f7 = console.read_u8(0x0000f7u);
            value_f8 = console.read_u8(0x0000f8u);
        }
        if (capture_direct_page_watch)
        {
            const uint32_t pointer_base_65{
                static_cast<uint32_t>(value_65)
                | (static_cast<uint32_t>(value_66) << 8u)
                | (static_cast<uint32_t>(value_67) << 16u)
            };
            const uint32_t pointer_base_f3{
                static_cast<uint32_t>(value_f3)
                | (static_cast<uint32_t>(value_f4) << 8u)
                | (static_cast<uint32_t>(value_f5) << 16u)
            };
            const uint32_t pointer_65y{ (pointer_base_65 + current_cpu.y) & 0x00ffffffu };
            const uint32_t pointer_f3y{ (pointer_base_f3 + current_cpu.y) & 0x00ffffffu };
            current_direct_page_watch = {
                .hardware_step = summary.steps,
                .cpu = current_cpu,
                .opcode = current_opcode,
                .value_00 = console.read_u8(0x000000u),
                .value_01 = console.read_u8(0x000001u),
                .value_02 = console.read_u8(0x000002u),
                .value_03 = console.read_u8(0x000003u),
                .value_04 = console.read_u8(0x000004u),
                .value_05 = console.read_u8(0x000005u),
                .value_59 = console.read_u8(0x000059u),
                .value_68 = value_68,
                .value_69 = value_69,
                .value_6a = value_6a,
                .value_f3 = value_f3,
                .value_f4 = value_f4,
                .value_f5 = value_f5,
                .value_f9 = console.read_u8(0x0000f9u),
                .value_fa = console.read_u8(0x0000fau),
                .value_fb = console.read_u8(0x0000fbu),
                .value_fc = console.read_u8(0x0000fcu),
                .value_65 = value_65,
                .value_66 = value_66,
                .value_67 = value_67,
                .pointer_65y = pointer_65y,
                .pointer_byte_0 = console.read_u8(pointer_65y),
                .pointer_byte_1 = console.read_u8((pointer_65y + 1u) & 0x00ffffffu),
                .pointer_byte_2 = console.read_u8((pointer_65y + 2u) & 0x00ffffffu),
                .pointer_f3y = pointer_f3y,
                .pointer_f3_byte_0 = console.read_u8(pointer_f3y),
                .pointer_f3_byte_1 = console.read_u8((pointer_f3y + 1u) & 0x00ffffffu),
                .pointer_f3_byte_2 = console.read_u8((pointer_f3y + 2u) & 0x00ffffffu)
            };
            if (!have_last_direct_page_watch
                || current_direct_page_watch.value_00 != last_direct_page_watch.value_00
                || current_direct_page_watch.value_01 != last_direct_page_watch.value_01
                || current_direct_page_watch.value_02 != last_direct_page_watch.value_02
                || current_direct_page_watch.value_03 != last_direct_page_watch.value_03
                || current_direct_page_watch.value_04 != last_direct_page_watch.value_04
                || current_direct_page_watch.value_05 != last_direct_page_watch.value_05
                || current_direct_page_watch.value_59 != last_direct_page_watch.value_59
                || current_direct_page_watch.value_68 != last_direct_page_watch.value_68
                || current_direct_page_watch.value_69 != last_direct_page_watch.value_69
                || current_direct_page_watch.value_6a != last_direct_page_watch.value_6a
                || current_direct_page_watch.value_f3 != last_direct_page_watch.value_f3
                || current_direct_page_watch.value_f4 != last_direct_page_watch.value_f4
                || current_direct_page_watch.value_f5 != last_direct_page_watch.value_f5
                || current_direct_page_watch.value_f9 != last_direct_page_watch.value_f9
                || current_direct_page_watch.value_fa != last_direct_page_watch.value_fa
                || current_direct_page_watch.value_fb != last_direct_page_watch.value_fb
                || current_direct_page_watch.value_fc != last_direct_page_watch.value_fc
                || current_direct_page_watch.value_65 != last_direct_page_watch.value_65
                || current_direct_page_watch.value_66 != last_direct_page_watch.value_66
                || current_direct_page_watch.value_67 != last_direct_page_watch.value_67
                || current_direct_page_watch.pointer_65y != last_direct_page_watch.pointer_65y
                || current_direct_page_watch.pointer_byte_0 != last_direct_page_watch.pointer_byte_0
                || current_direct_page_watch.pointer_byte_1 != last_direct_page_watch.pointer_byte_1
                || current_direct_page_watch.pointer_byte_2 != last_direct_page_watch.pointer_byte_2
                || current_direct_page_watch.pointer_f3y != last_direct_page_watch.pointer_f3y
                || current_direct_page_watch.pointer_f3_byte_0 != last_direct_page_watch.pointer_f3_byte_0
                || current_direct_page_watch.pointer_f3_byte_1 != last_direct_page_watch.pointer_f3_byte_1
                || current_direct_page_watch.pointer_f3_byte_2 != last_direct_page_watch.pointer_f3_byte_2)
            {
                direct_page_watch.push_back(current_direct_page_watch);
                if (direct_page_watch.size() > 64)
                    direct_page_watch.pop_front();
                last_direct_page_watch = current_direct_page_watch;
                have_last_direct_page_watch = true;
            }
        }

        const clover::core::dma_activity_t before_dma_activity{ console.dma_activity() };
        const bool before_general_dma_pending{ console.general_dma_pending() };
        const bool before_hdma_pending{ console.hdma_pending() };
        const clover::core::hardware_step_result_t step{ console.step_hardware() };
        ++summary.steps;
        summary.dma_steps += step.slot_owner == clover::core::hardware_slot_owner_t::dma ? 1u : 0u;
        summary.frame_completions += step.ppu.frame_complete ? 1u : 0u;
        summary.hblank_entries += step.ppu.entered_hblank ? 1u : 0u;
        summary.vblank_entries += step.ppu.entered_vblank ? 1u : 0u;
        summary.nmi_requests += step.ppu.nmi_requested ? 1u : 0u;
        summary.irq_requests += step.ppu.irq_requested ? 1u : 0u;
        summary.hdma_setup_triggers += step.ppu.hdma_setup_triggered ? 1u : 0u;
        summary.hdma_transfer_triggers += step.ppu.hdma_transfer_triggered ? 1u : 0u;
        if (ppu_probe_filter.enabled
            && active_frame == ppu_probe_filter.active_frame
            && step.ppu.timing.raster.scanline >= ppu_probe_filter.scanline_min
            && step.ppu.timing.raster.scanline <= ppu_probe_filter.scanline_max
            && step.ppu.timing.raster.dot >= ppu_probe_filter.dot_min
            && step.ppu.timing.raster.dot <= ppu_probe_filter.dot_max)
        {
            print_ppu_probe_snapshot(active_frame,
                                     step.ppu.timing,
                                     console.ppu_render_state(),
                                     console.ppu_compositor_state(),
                                     console.ppu_cgram()[0],
                                     ppu_probe_filter);
        }
        const bool should_dump_frame{
            dump_frames
            && active_frame >= dump_start_frame
            && active_frame < dump_start_frame + dump_count
        };
        if (step.ppu.entered_vblank && should_dump_frame)
        {
            console.refresh_framebuffer({
                .source = dump_source
            });

            const std::string frame_basename{ "frame_" + std::to_string(active_frame) };
            const std::filesystem::path frame_path{
                dump_directory / (frame_basename + ".ppm")
            };
            if (!write_framebuffer_ppm(frame_path, console.framebuffer()))
            {
                std::fprintf(stderr, "Failed to write frame dump: %s\n", frame_path.string().c_str());
                return 1;
            }

            ++dumped_frames;
        }

        if (step.ppu.entered_frame_start && should_dump_frame)
        {
            const std::string frame_basename{ "frame_" + std::to_string(active_frame) };
            const std::filesystem::path vram_path{ dump_directory / (frame_basename + ".vram.bin") };
            if (!write_binary_blob(vram_path, console.ppu_vram()))
            {
                std::fprintf(stderr, "Failed to write VRAM dump: %s\n", vram_path.string().c_str());
                return 1;
            }

            const std::filesystem::path oam_path{ dump_directory / (frame_basename + ".oam.bin") };
            if (!write_binary_blob(oam_path, console.ppu_oam()))
            {
                std::fprintf(stderr, "Failed to write OAM dump: %s\n", oam_path.string().c_str());
                return 1;
            }

            const std::filesystem::path cgram_path{ dump_directory / (frame_basename + ".cgram.bin") };
            if (!write_binary_blob(cgram_path, console.ppu_cgram()))
            {
                std::fprintf(stderr, "Failed to write CGRAM dump: %s\n", cgram_path.string().c_str());
                return 1;
            }

            if (wram_dump_config.enabled && wram_dump_config.length != 0)
            {
                const std::span<const uint8_t> wram_dump{
                    console.wram_span(wram_dump_config.offset, wram_dump_config.length)
                };
                const std::filesystem::path wram_path{
                    dump_directory
                    / (frame_basename + ".wram_"
                        + hex_label(static_cast<uint64_t>(wram_dump_config.offset), 5u)
                        + "_" + hex_label(static_cast<uint64_t>(wram_dump.size()), 5u)
                        + ".bin")
                };
                if (!write_binary_blob(wram_path, wram_dump))
                {
                    std::fprintf(stderr, "Failed to write WRAM dump: %s\n", wram_path.string().c_str());
                    return 1;
                }
            }
        }

        uint8_t updated_65{ 0 };
        uint8_t updated_66{ 0 };
        uint8_t updated_67{ 0 };
        uint8_t updated_68{ 0 };
        uint8_t updated_69{ 0 };
        uint8_t updated_6a{ 0 };
        uint8_t updated_f3{ 0 };
        uint8_t updated_f4{ 0 };
        uint8_t updated_f5{ 0 };
        uint8_t updated_f6{ 0 };
        uint8_t updated_f7{ 0 };
        uint8_t updated_f8{ 0 };
        if (capture_direct_page_watch || capture_transfer_pointer_changes)
        {
            updated_65 = console.read_u8(0x000065u);
            updated_66 = console.read_u8(0x000066u);
            updated_67 = console.read_u8(0x000067u);
            updated_68 = console.read_u8(0x000068u);
            updated_69 = console.read_u8(0x000069u);
            updated_6a = console.read_u8(0x00006au);
            updated_f3 = console.read_u8(0x0000f3u);
            updated_f4 = console.read_u8(0x0000f4u);
            updated_f5 = console.read_u8(0x0000f5u);
            updated_f6 = console.read_u8(0x0000f6u);
            updated_f7 = console.read_u8(0x0000f7u);
            updated_f8 = console.read_u8(0x0000f8u);
        }
        const clover::core::cpu_state_t updated_cpu{ console.cpu_state() };
        const clover::core::dma_activity_t after_dma_activity{ console.dma_activity() };
        const bool after_general_dma_pending{ console.general_dma_pending() };
        const bool after_hdma_pending{ console.hdma_pending() };
        if (after_dma_activity != before_dma_activity
            || after_general_dma_pending != before_general_dma_pending
            || after_hdma_pending != before_hdma_pending)
        {
            dma_transitions.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .slot_owner = step.slot_owner,
                .timing = step.ppu.timing,
                .before_activity = before_dma_activity,
                .after_activity = after_dma_activity,
                .before_general_pending = before_general_dma_pending,
                .after_general_pending = after_general_dma_pending,
                .before_hdma_pending = before_hdma_pending,
                .after_hdma_pending = after_hdma_pending,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc)
            });
            if (dma_transitions.size() > 128)
                dma_transitions.pop_front();
        }
        if (capture_direct_page_watch && (updated_65 != value_65 || updated_66 != value_66 || updated_67 != value_67))
        {
            pointer_changes.push_back({
                .hardware_step = summary.steps,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_65 = value_65,
                .old_66 = value_66,
                .old_67 = value_67,
                .new_65 = updated_65,
                .new_66 = updated_66,
                .new_67 = updated_67
            });
            if (pointer_changes.size() > 64)
                pointer_changes.pop_front();
        }

        if (capture_direct_page_watch && (updated_68 != value_68 || updated_69 != value_69 || updated_6a != value_6a))
        {
            source_changes.push_back({
                .hardware_step = summary.steps,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_68 = value_68,
                .old_69 = value_69,
                .old_6a = value_6a,
                .new_68 = updated_68,
                .new_69 = updated_69,
                .new_6a = updated_6a
            });
            if (source_changes.size() > 64)
                source_changes.pop_front();
        }
        if (capture_transfer_pointer_changes
            && (updated_f3 != value_f3
                || updated_f4 != value_f4
                || updated_f5 != value_f5
                || updated_f6 != value_f6
                || updated_f7 != value_f7
                || updated_f8 != value_f8))
        {
            transfer_pointer_changes.push_back({
                .hardware_step = summary.steps,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_f3 = value_f3,
                .old_f4 = value_f4,
                .old_f5 = value_f5,
                .old_f6 = value_f6,
                .old_f7 = value_f7,
                .old_f8 = value_f8,
                .new_f3 = updated_f3,
                .new_f4 = updated_f4,
                .new_f5 = updated_f5,
                .new_f6 = updated_f6,
                .new_f7 = updated_f7,
                .new_f8 = updated_f8
            });
            if (transfer_pointer_changes.size() > 256)
                transfer_pointer_changes.pop_front();
        }

        const uint8_t updated_03{
            capture_direct_page_watch ? console.read_u8(0x000003u) : static_cast<uint8_t>(0u)
        };
        if (capture_direct_page_watch && updated_03 != current_direct_page_watch.value_03)
        {
            lowram_03_changes.push_back({
                .hardware_step = summary.steps,
                .frame_completions = summary.frame_completions,
                .slot_owner = step.slot_owner,
                .timing = step.ppu.timing,
                .before_cpu = current_cpu,
                .before_opcode = current_opcode,
                .after_cpu = updated_cpu,
                .after_opcode = console.read_u8((static_cast<uint32_t>(updated_cpu.pb) << 16u) | updated_cpu.pc),
                .old_value = current_direct_page_watch.value_03,
                .new_value = updated_03
            });
            if (lowram_03_changes.size() > 64)
                lowram_03_changes.pop_front();
        }

        const clover::core::cpu_state_t stepped_cpu{ console.cpu_state() };
        if (stepped_cpu.pb == 0x00u && stepped_cpu.pc == 0xffffu)
        {
            terminal_pc_detected = true;
            cpu_trace.push_back({
                .hardware_step = summary.steps,
                .state = stepped_cpu,
                .opcode = console.read_u8(0x00ffffu)
            });
            if (cpu_trace.size() > 64)
                cpu_trace.pop_front();
            break;
        }
    }

    if (generic_trace_file != nullptr)
        std::fclose(generic_trace_file);

    const clover::core::timing_snapshot_t ppu_timing{ console.timing() };
    const clover::core::timing_snapshot_t cpu_timing{ console.cpu_timing() };
    const clover::core::hardware_timing_snapshot_t timing_snapshot{ console.capture_timing_snapshot() };
    const clover::core::ppu_render_state_snapshot_t ppu_state{ console.ppu_render_state() };
    const clover::core::ppu_compositor_snapshot_t compositor_state{ console.ppu_compositor_state() };

    std::printf("ROM: %s\n", rom_path.c_str());
    std::printf("ROM size: %zu bytes\n", rom_bytes.size());
    std::printf("Cartridge: mapping=%s raw_map_mode=%02x reset_vector=%04x\n",
                mapping_mode_name(cartridge_probe.mapping_mode()).c_str(),
                cartridge_probe.header().raw_map_mode,
                cartridge_probe.header().reset_vector);
    std::printf("Run: target_frames=%llu frames_completed=%llu steps=%llu dma_steps=%llu step_limit_hit=%u\n",
                static_cast<unsigned long long>(target_frames),
                static_cast<unsigned long long>(summary.frame_completions),
                static_cast<unsigned long long>(summary.steps),
                static_cast<unsigned long long>(summary.dma_steps),
                summary.steps >= step_limit ? 1u : 0u);
    std::printf("Diagnostics: terminal_pc=%u cpu_placeholder_opcodes=%llu\n",
                terminal_pc_detected ? 1u : 0u,
                static_cast<unsigned long long>(console.cpu_placeholder_opcode_count()));
    std::printf("Events: hblank=%llu vblank=%llu nmi=%llu irq=%llu hdma_setup=%llu hdma_transfer=%llu\n",
                static_cast<unsigned long long>(summary.hblank_entries),
                static_cast<unsigned long long>(summary.vblank_entries),
                static_cast<unsigned long long>(summary.nmi_requests),
                static_cast<unsigned long long>(summary.irq_requests),
                static_cast<unsigned long long>(summary.hdma_setup_triggers),
                static_cast<unsigned long long>(summary.hdma_transfer_triggers));

    if (!verbose_output && !terminal_pc_detected)
    {
        if (dump_frames)
        {
            std::printf("Frame dumps: directory=%s dumped=%llu start_frame=%llu\n",
                        dump_directory.string().c_str(),
                        static_cast<unsigned long long>(dumped_frames),
                        static_cast<unsigned long long>(dump_start_frame));
        }
        return 0;
    }

    print_cpu_state(console.cpu_state());
    print_timing("CPU timing", cpu_timing);
    print_timing("PPU timing", ppu_timing);
    print_timing("CPU NMI delay", timing_snapshot.cpu_timing_nmi_delay);
    print_timing("CPU IRQ delay", timing_snapshot.cpu_timing_irq_delay);
    print_interrupts(console.interrupts());
    print_cpu_trace(cpu_trace);
    print_lowram_changes(lowram_03_changes);
    if (terminal_pc_detected)
    {
        print_direct_page_watch(direct_page_watch);
        print_pointer_changes(pointer_changes);
        print_source_changes(source_changes);
        print_transfer_pointer_changes(transfer_pointer_changes);
        print_direct_page_window(console, 0x0000u, 8u);
    }
    print_hot_path_trace(hot_path_trace);
    print_helper_trace(helper_trace);
    print_ff3_call_trace(ff3_call_trace);
    print_apu_ports(console);
    print_apu_state(console.apu_state());
    print_apu_window(console, console.apu_state());
    print_apu_instruction_trace(console);
    print_apu_io_trace(console);
    print_apu_port_trace(console);
    print_dma_transitions(dma_transitions);
    std::printf("DMA: activity=%u hdma_pending=%u general_pending=%u open_bus=%02x frame_index=%llu\n",
                static_cast<unsigned>(console.dma_activity()),
                console.hdma_pending() ? 1u : 0u,
                console.general_dma_pending() ? 1u : 0u,
                console.open_bus(),
                static_cast<unsigned long long>(console.frame_index()));
    print_bus_window(console, "WRAM window", 0x000000u, 0x06u);
    if (terminal_pc_detected)
    {
        const uint16_t stack_base{ static_cast<uint16_t>(console.cpu_state().sp & 0xfff0u) };
        print_bus_window(console, "WRAM window", 0x000800u, 0x20u);
        print_bus_window(console, "WRAM window", stack_base, 0x20u);
        print_bus_window(console, "Bus window", 0x7ff800u, 0x20u);
        print_bus_window(console, "Bus window", 0x7ffbe0u, 0x30u);
    }
    print_bus_window(console, "WRAM window", 0x000d84u, 0x16u);
    if (bus_window_filter.enabled)
        print_bus_window(console, "Bus window", bus_window_filter.address, bus_window_filter.count);
    print_ppu_summary(ppu_state);
    print_system_register_write_trace(console);
    print_ppu_register_write_trace(console);
    print_watched_write_trace(console, watched_write_filter);
    if (capture_cgram_trace)
        print_cgram_write_trace(console);
    if (capture_oam_trace)
        print_oam_write_trace(console);
    print_compositor_summary(compositor_state);
    if (dump_frames)
    {
        std::printf("Frame dumps: directory=%s dumped=%llu start_frame=%llu\n",
                    dump_directory.string().c_str(),
                    static_cast<unsigned long long>(dumped_frames),
                    static_cast<unsigned long long>(dump_start_frame));
    }

    return 0;
}
