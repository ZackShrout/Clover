//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/FrameBuffer.h"
#include "clover/core/Timing.h"

#include <array>
#include <cstdint>

namespace clover::core
{
    enum class ppu_debug_view_t : uint8_t
    {
        composed,
        background_1,
        background_2,
        background_3,
        background_4,
        objects
    };

    struct ppu_presentation_options_t
    {
        static constexpr uint8_t k_all_layers_visible{ 0x1fu };

        uint8_t visible_layer_mask{ k_all_layers_visible };
        ppu_debug_view_t debug_view{ ppu_debug_view_t::composed };
    };

    struct ppu_background_render_state_t
    {
        enum class mode_t : uint8_t
        {
            bpp2,
            bpp4,
            bpp8,
            mode7,
            inactive
        };

        mode_t mode{ mode_t::inactive };
        bool active{ false };
        uint16_t tiledata_address{ 0 };
        uint16_t screen_address{ 0 };
        uint8_t screen_size{ 0 };
        bool large_tiles{ false };
        uint8_t priority[2]{ 0, 0 };
        bool above_enabled{ false };
        bool below_enabled{ false };
        bool window_above_enabled{ false };
        bool window_below_enabled{ false };
        uint8_t window_mask{ 0 };
        uint16_t hoffset{ 0 };
        uint16_t voffset{ 0 };
    };

    struct ppu_object_render_state_t
    {
        bool above_enabled{ false };
        bool below_enabled{ false };
        bool window_above_enabled{ false };
        bool window_below_enabled{ false };
        uint8_t window_mask{ 0 };
    };

    struct ppu_color_math_render_state_t
    {
        bool direct_color{ false };
        bool blend_mode{ false };
        bool color_halve{ false };
        bool color_mode_subtract{ false };
        bool bg_color_enable[4]{ false, false, false, false };
        bool obj_color_enable{ false };
        bool backdrop_color_enable{ false };
        uint8_t fixed_red{ 0 };
        uint8_t fixed_green{ 0 };
        uint8_t fixed_blue{ 0 };
        uint8_t window_mask_above{ 0 };
        uint8_t window_mask_below{ 0 };
        uint8_t color_window_mask{ 0 };
    };

    struct ppu_window_render_state_t
    {
        bool one_invert[6]{ false, false, false, false, false, false };
        bool one_enable[6]{ false, false, false, false, false, false };
        bool two_invert[6]{ false, false, false, false, false, false };
        bool two_enable[6]{ false, false, false, false, false, false };
        uint8_t one_left{ 0 };
        uint8_t one_right{ 0 };
        uint8_t two_left{ 0 };
        uint8_t two_right{ 0 };
    };

    struct ppu_pixel_candidate_t
    {
        uint8_t priority{ 0 };
        uint8_t palette{ 0 };
        uint8_t palette_group{ 0 };
        bool color_math_enabled{ false };
    };

    struct ppu_layer_compositor_state_t
    {
        ppu_pixel_candidate_t above{};
        ppu_pixel_candidate_t below{};
    };

    struct ppu_compositor_snapshot_t
    {
        bool hires{ false };
        bool pseudo_hires{ false };
        bool blend_mode{ false };
        bool color_halve{ false };
        bool direct_color{ false };
        bool color_mode_subtract{ false };
        bool backdrop_color_enable{ false };
        uint8_t fixed_red{ 0 };
        uint8_t fixed_green{ 0 };
        uint8_t fixed_blue{ 0 };
        ppu_layer_compositor_state_t backgrounds[4]{};
        ppu_layer_compositor_state_t objects{};
    };

    struct ppu_render_state_snapshot_t
    {
        bool display_disabled{ false };
        uint8_t brightness{ 0 };
        uint8_t bg_mode{ 0 };
        bool bg3_priority{ false };
        bool hires{ false };
        uint8_t mosaic_size{ 1 };
        bool pseudo_hires{ false };
        bool overscan{ false };
        bool interlace{ false };
        uint16_t mode7_hoffset{ 0 };
        uint16_t mode7_voffset{ 0 };
        uint8_t mode7_repeat{ 0 };
        bool mode7_hflip{ false };
        bool mode7_vflip{ false };
        ppu_background_render_state_t backgrounds[4]{};
        ppu_object_render_state_t objects{};
        ppu_color_math_render_state_t color_math{};
        ppu_window_render_state_t window{};
    };

    struct ppu_t
    {
    public:
        void power_on() noexcept;
        void reset() noexcept;
        [[nodiscard]] video_standard_t video_standard() const noexcept;
        [[nodiscard]] const video_timing_t& video_timing() const noexcept;
        [[nodiscard]] uint8_t read_register(uint16_t address) noexcept;
        void write_register(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] ppu_step_result_t step(master_clock_delta_t master_clocks) noexcept;
        [[nodiscard]] timing_snapshot_t timing() const noexcept;
        [[nodiscard]] master_clock_delta_t current_scanline_clocks() const noexcept;
        [[nodiscard]] ppu_render_state_snapshot_t render_state_snapshot() const noexcept;
        [[nodiscard]] ppu_compositor_snapshot_t compositor_snapshot() const noexcept;
        void present(framebuffer_t& framebuffer,
                     const ppu_presentation_options_t& options = {}) const noexcept;

    private:
        [[nodiscard]] uint16_t active_visible_scanlines() const noexcept;
        [[nodiscard]] bool display_active_for_oam() const noexcept;
        [[nodiscard]] bool display_active_for_vram() const noexcept;
        [[nodiscard]] bool display_active_for_cgram() const noexcept;
        [[nodiscard]] uint16_t address_vram() const noexcept;
        [[nodiscard]] uint16_t read_vram_word() const noexcept;
        void write_vram_byte(bool high_byte, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_oam_byte(uint16_t address) const noexcept;
        void write_oam_byte(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_cgram_byte(bool high_byte, uint8_t address) const noexcept;
        void write_cgram_word(uint8_t address, uint16_t value) noexcept;
        void latch_counters() noexcept;
        void decode_render_state() noexcept;
        void clear_compositor_state() noexcept;
        void render_placeholder_frame() noexcept;

        struct ppu_display_state_t
        {
            bool disabled{ false };
            uint8_t brightness{ 0 };
        };

        struct ppu_oam_state_t
        {
            uint16_t base_address{ 0 };
            uint16_t address{ 0 };
            uint16_t latched_address{ 0 };
            bool priority{ false };
            uint8_t write_latch{ 0 };
        };

        struct ppu_bg_state_t
        {
            using mode_t = ppu_background_render_state_t::mode_t;

            uint8_t mode{ 0 };
            bool bg3_priority{ false };
            std::array<bool, 4> large_tiles{};
            std::array<mode_t, 4> render_mode{
                mode_t::inactive,
                mode_t::inactive,
                mode_t::inactive,
                mode_t::inactive
            };
            std::array<bool, 4> active{};
            std::array<uint16_t, 4> tiledata_address{};
            std::array<uint16_t, 4> screen_address{};
            std::array<uint8_t, 4> screen_size{};
            std::array<std::array<uint8_t, 2>, 4> priority{};
            std::array<uint16_t, 4> hoffset{};
            std::array<uint16_t, 4> voffset{};
            std::array<bool, 4> above_enabled{};
            std::array<bool, 4> below_enabled{};
            std::array<bool, 4> window_above_enabled{};
            std::array<bool, 4> window_below_enabled{};
            std::array<uint8_t, 4> window_mask{};
        };

        struct ppu_scroll_latch_state_t
        {
            uint8_t ppu1{ 0 };
            uint8_t ppu2{ 0 };
            uint8_t mode7{ 0 };
            uint16_t mode7_hoffset{ 0 };
            uint16_t mode7_voffset{ 0 };
        };

        struct ppu_mosaic_state_t
        {
            std::array<bool, 4> enabled{};
            uint8_t size{ 1 };
        };

        struct ppu_window_state_t
        {
            std::array<bool, 6> one_invert{};
            std::array<bool, 6> one_enable{};
            std::array<bool, 6> two_invert{};
            std::array<bool, 6> two_enable{};
            uint8_t one_left{ 0 };
            uint8_t one_right{ 0 };
            uint8_t two_left{ 0 };
            uint8_t two_right{ 0 };
            uint8_t object_mask{ 0 };
            uint8_t color_mask{ 0 };
            uint8_t color_mask_above{ 0 };
            uint8_t color_mask_below{ 0 };
        };

        struct ppu_object_layer_state_t
        {
            bool above_enabled{ false };
            bool below_enabled{ false };
            bool window_above_enabled{ false };
            bool window_below_enabled{ false };
        };

        struct ppu_color_math_state_t
        {
            bool direct_color{ false };
            bool blend_mode{ false };
            bool color_halve{ false };
            bool color_mode_subtract{ false };
            std::array<bool, 4> bg_color_enable{};
            bool obj_color_enable{ false };
            bool backdrop_color_enable{ false };
            uint8_t fixed_red{ 0 };
            uint8_t fixed_green{ 0 };
            uint8_t fixed_blue{ 0 };
        };

        struct ppu_screen_state_t
        {
            bool hires{ false };
            bool pseudo_hires{ false };
            bool overscan{ false };
            bool interlace{ false };
            uint8_t mode7_repeat{ 0 };
            bool mode7_hflip{ false };
            bool mode7_vflip{ false };
        };

        struct ppu_compositor_state_t
        {
            std::array<ppu_layer_compositor_state_t, 4> backgrounds{};
            ppu_layer_compositor_state_t objects{};
        };

        struct ppu_vram_state_t
        {
            uint8_t increment_size{ 1 };
            uint8_t mapping{ 0 };
            bool increment_on_high{ false };
            uint16_t address{ 0 };
            uint16_t read_latch{ 0 };
        };

        struct ppu_cgram_state_t
        {
            uint8_t address{ 0 };
            bool write_high_pending{ false };
            bool read_high_pending{ false };
            uint8_t write_latch{ 0 };
        };

        struct ppu_counter_latch_state_t
        {
            bool counters_latched{ false };
            uint16_t hcounter{ 0 };
            uint16_t vcounter{ 0 };
            bool hcounter_high_read{ false };
            bool vcounter_high_read{ false };
        };

        framebuffer_t _composed_frame{};
        std::array<uint8_t, 0x40> _registers{};
        std::array<uint16_t, 32 * 1024> _vram{};
        std::array<uint8_t, 544> _oam{};
        std::array<uint16_t, 256> _cgram{};
        video_timing_t _video_timing{ k_ntsc_video_timing };
        raster_counter_t _counter{};
        uint64_t _frame_counter{ 0 };
        ppu_display_state_t _display{};
        ppu_oam_state_t _oam_state{};
        ppu_bg_state_t _bg_state{};
        ppu_scroll_latch_state_t _scroll_latches{};
        ppu_mosaic_state_t _mosaic_state{};
        ppu_window_state_t _window_state{};
        ppu_object_layer_state_t _object_layer_state{};
        ppu_color_math_state_t _color_math_state{};
        ppu_screen_state_t _screen_state{};
        ppu_compositor_state_t _compositor_state{};
        ppu_vram_state_t _vram_state{};
        ppu_cgram_state_t _cgram_state{};
        ppu_counter_latch_state_t _counter_latch{};
        uint8_t _ppu1_mdr{ 0 };
        uint8_t _ppu2_mdr{ 0 };
    };
}
