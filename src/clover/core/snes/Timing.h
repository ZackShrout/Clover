//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>

namespace clover::core
{
    using master_clock_count_t = uint64_t;
    using master_clock_delta_t = uint32_t;

    enum class video_standard_t : uint8_t
    {
        ntsc,
        pal
    };

    struct video_timing_t
    {
        video_standard_t standard{ video_standard_t::ntsc };
        master_clock_delta_t master_clocks_per_scanline{ 1364 };
        uint16_t scanlines_per_frame{ 262 };
        uint16_t visible_scanlines{ 225 };
        uint16_t overscan_visible_scanlines{ 240 };
        uint16_t short_scanline{ 240 };
        master_clock_delta_t short_scanline_clocks{ 1360 };
        uint16_t hblank_start_dot{ 1096 };
        uint16_t hdma_trigger_dot{ 1104 };

        [[nodiscard]] constexpr bool operator==(const video_timing_t&) const noexcept = default;

        [[nodiscard]] constexpr master_clock_delta_t scanline_clocks(uint16_t scanline,
                                                                     bool odd_field,
                                                                     bool interlace) const noexcept
        {
            if (standard == video_standard_t::ntsc && !interlace && odd_field && scanline == short_scanline)
                return short_scanline_clocks;

            return master_clocks_per_scanline;
        }

        [[nodiscard]] constexpr uint16_t field_scanlines(bool odd_field,
                                                         bool interlace) const noexcept
        {
            return static_cast<uint16_t>(scanlines_per_frame + (interlace && !odd_field ? 1u : 0u));
        }

        [[nodiscard]] constexpr master_clock_delta_t master_clocks_per_frame(bool odd_field = false,
                                                                             bool interlace = false) const noexcept
        {
            return static_cast<master_clock_delta_t>(
                master_clocks_per_scanline * field_scanlines(odd_field, interlace)
                    - (standard == video_standard_t::ntsc && !interlace && odd_field
                        ? master_clocks_per_scanline - short_scanline_clocks
                        : 0)
            );
        }

        [[nodiscard]] constexpr uint16_t active_visible_scanlines(bool overscan_enabled) const noexcept
        {
            return overscan_enabled ? overscan_visible_scanlines : visible_scanlines;
        }
    };

    constexpr video_timing_t k_ntsc_video_timing{};
    constexpr video_timing_t k_pal_video_timing{
        .standard = video_standard_t::pal,
        .master_clocks_per_scanline = 1364,
        .scanlines_per_frame = 312,
        .visible_scanlines = 225,
        .overscan_visible_scanlines = 240,
        .short_scanline = 0,
        .short_scanline_clocks = 1364,
        .hblank_start_dot = 1096,
        .hdma_trigger_dot = 1104,
    };

    [[nodiscard]] constexpr const video_timing_t& video_timing_for(video_standard_t standard) noexcept
    {
        return standard == video_standard_t::pal ? k_pal_video_timing : k_ntsc_video_timing;
    }

    [[nodiscard]] constexpr uint32_t master_clock_frequency_hz(video_standard_t standard) noexcept
    {
        return standard == video_standard_t::pal ? 21'281'370u : 21'477'272u;
    }
    constexpr master_clock_delta_t k_cpu_dram_refresh_stall_clocks{ 40 };

    [[nodiscard]] constexpr uint16_t dma_phase_from_master_clock(master_clock_count_t master_clock) noexcept
    {
        return static_cast<uint16_t>(master_clock & 7u);
    }

    [[nodiscard]] constexpr uint16_t hdma_setup_dot_v2(uint16_t dma_phase) noexcept
    {
        return static_cast<uint16_t>(12u + (dma_phase & 7u));
    }

    [[nodiscard]] constexpr uint16_t dram_refresh_dot_v2(uint16_t dma_phase) noexcept
    {
        return static_cast<uint16_t>(538u - (dma_phase & 7u));
    }

    struct raster_position_t
    {
        uint16_t scanline{ 0 };
        uint16_t dot{ 0 };

        [[nodiscard]] constexpr bool operator==(const raster_position_t&) const noexcept = default;
    };

    struct timing_snapshot_t
    {
        master_clock_count_t master_clock{ 0 };
        raster_position_t raster{};
        bool in_hblank{ false };
        bool in_vblank{ false };

        [[nodiscard]] constexpr bool operator==(const timing_snapshot_t&) const noexcept = default;
    };

    struct raster_counter_t
    {
        master_clock_count_t master_clock{ 0 };
        uint16_t scanline{ 0 };
        uint16_t dot{ 0 };
        bool odd_field{ false };

        [[nodiscard]] constexpr bool operator==(const raster_counter_t&) const noexcept = default;

        void reset() noexcept
        {
            master_clock = 0;
            scanline = 0;
            dot = 0;
            odd_field = false;
        }

        void advance(master_clock_delta_t master_clocks,
                     const video_timing_t& video_timing,
                     bool interlace) noexcept
        {
            master_clock += master_clocks;

            uint32_t next_dot{ static_cast<uint32_t>(dot) + master_clocks };
            while (next_dot >= video_timing.scanline_clocks(scanline, odd_field, interlace))
            {
                next_dot -= video_timing.scanline_clocks(scanline, odd_field, interlace);
                ++scanline;
                if (scanline >= video_timing.field_scanlines(odd_field, interlace))
                {
                    scanline = 0;
                    odd_field = !odd_field;
                }
            }

            dot = static_cast<uint16_t>(next_dot);
        }

        [[nodiscard]] master_clock_delta_t current_scanline_clocks(const video_timing_t& video_timing,
                                                                   bool interlace) const noexcept
        {
            return video_timing.scanline_clocks(scanline, odd_field, interlace);
        }

        void rewind(master_clock_delta_t master_clocks,
                    const video_timing_t& video_timing,
                    bool interlace) noexcept
        {
            if (master_clocks >= master_clock)
            {
                reset();
                return;
            }

            master_clock -= master_clocks;

            uint32_t remaining{ master_clocks };
            while (remaining > 0)
            {
                if (remaining <= dot)
                {
                    dot = static_cast<uint16_t>(dot - remaining);
                    remaining = 0;
                    continue;
                }

                remaining -= static_cast<uint32_t>(dot);

                if (scanline == 0)
                {
                    odd_field = !odd_field;
                    scanline = static_cast<uint16_t>(
                        video_timing.field_scanlines(odd_field, interlace) - 1u
                    );
                }
                else
                {
                    --scanline;
                }

                dot = static_cast<uint16_t>(video_timing.scanline_clocks(scanline, odd_field, interlace));
            }
        }

        [[nodiscard]] timing_snapshot_t snapshot_delayed(const video_timing_t& video_timing,
                                                         uint16_t visible_scanlines,
                                                         master_clock_delta_t delay,
                                                         bool interlace) const noexcept
        {
            raster_counter_t delayed{ *this };
            delayed.rewind(delay, video_timing, interlace);
            return delayed.snapshot(video_timing, visible_scanlines);
        }

        [[nodiscard]] timing_snapshot_t snapshot(const video_timing_t& video_timing,
                                                 uint16_t visible_scanlines) const noexcept
        {
            return {
                .master_clock = master_clock,
                .raster = {
                    .scanline = scanline,
                    .dot = dot
                },
                .in_hblank = dot >= video_timing.hblank_start_dot,
                .in_vblank = scanline >= visible_scanlines
            };
        }
    };

    enum class hardware_slot_owner_t : uint8_t
    {
        cpu,
        dma
    };

    struct ppu_step_result_t
    {
        timing_snapshot_t timing{};
        uint16_t visible_scanlines{ 225 };
        bool interlace{ false };
        bool frame_complete{ false };
        uint32_t frames_completed{ 0 };
        bool entered_scanline{ false };
        bool entered_frame_start{ false };
        bool entered_hblank{ false };
        bool entered_vblank{ false };
        bool hdma_setup_triggered{ false };
        bool hdma_transfer_triggered{ false };
        bool nmi_requested{ false };
        bool irq_requested{ false };
    };

    enum class cpu_step_boundary_t : uint8_t
    {
        none,
        instruction_retired,
        reset_completed,
        interrupt_entered,
        waiting,
        stopped
    };

    struct cpu_step_result_t
    {
        master_clock_delta_t master_clocks{ 0 };
        ppu_step_result_t ppu{};
        cpu_step_boundary_t boundary{ cpu_step_boundary_t::none };
        bool stepped_hardware{ false };
    };

    struct dma_step_result_t
    {
        master_clock_delta_t master_clocks{ 0 };
        bool consumed_alignment{ false };
    };

    struct hardware_step_result_t
    {
        hardware_slot_owner_t slot_owner{ hardware_slot_owner_t::cpu };
        master_clock_delta_t elapsed_master_clocks{ 0 };
        ppu_step_result_t ppu{};
        cpu_step_boundary_t cpu_boundary{ cpu_step_boundary_t::none };
    };

    enum class cpu_boundary_step_status_t : uint8_t
    {
        complete,
        not_powered
    };

    struct cpu_boundary_step_result_t
    {
        cpu_boundary_step_status_t status{ cpu_boundary_step_status_t::not_powered };
        cpu_step_boundary_t boundary{ cpu_step_boundary_t::none };
        master_clock_count_t elapsed_master_clocks{ 0 };
    };
}
