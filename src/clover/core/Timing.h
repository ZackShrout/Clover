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
        uint16_t hdma_setup_scanline{ 0 };
        uint16_t hdma_setup_dot{ 12 };

        [[nodiscard]] constexpr master_clock_delta_t scanline_clocks(uint16_t scanline,
                                                                     bool odd_field,
                                                                     bool interlace) const noexcept
        {
            if (standard == video_standard_t::ntsc && !interlace && odd_field && scanline == short_scanline)
                return short_scanline_clocks;

            return master_clocks_per_scanline;
        }

        [[nodiscard]] constexpr master_clock_delta_t master_clocks_per_frame(bool odd_field = false,
                                                                             bool interlace = false) const noexcept
        {
            return static_cast<master_clock_delta_t>(
                master_clocks_per_scanline * scanlines_per_frame
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

    struct raster_position_t
    {
        uint16_t scanline{ 0 };
        uint16_t dot{ 0 };
    };

    struct timing_snapshot_t
    {
        master_clock_count_t master_clock{ 0 };
        raster_position_t raster{};
        bool in_hblank{ false };
        bool in_vblank{ false };
    };

    struct raster_counter_t
    {
        master_clock_count_t master_clock{ 0 };
        uint16_t scanline{ 0 };
        uint16_t dot{ 0 };
        bool odd_field{ false };

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
                if (scanline >= video_timing.scanlines_per_frame)
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
                    scanline = static_cast<uint16_t>(video_timing.scanlines_per_frame - 1u);
                    odd_field = !odd_field;
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

    struct cpu_step_result_t
    {
        master_clock_delta_t master_clocks{ 0 };
    };

    struct dma_step_result_t
    {
        master_clock_delta_t master_clocks{ 0 };
        bool consumed_alignment{ false };
    };

    struct ppu_step_result_t
    {
        timing_snapshot_t timing{};
        uint16_t visible_scanlines{ 225 };
        bool interlace{ false };
        bool frame_complete{ false };
        bool entered_scanline{ false };
        bool entered_frame_start{ false };
        bool entered_hblank{ false };
        bool entered_vblank{ false };
        bool hdma_setup_triggered{ false };
        bool hdma_transfer_triggered{ false };
        bool nmi_requested{ false };
        bool irq_requested{ false };
    };

    struct hardware_step_result_t
    {
        hardware_slot_owner_t slot_owner{ hardware_slot_owner_t::cpu };
        master_clock_delta_t elapsed_master_clocks{ 0 };
        ppu_step_result_t ppu{};
    };
}
