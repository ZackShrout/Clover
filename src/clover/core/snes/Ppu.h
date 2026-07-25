//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/FrameBuffer.h"
#include "clover/core/snes/StartupEntropy.h"
#include "clover/core/snes/Timing.h"

#include <array>
#include <cstdint>
#include <deque>

namespace clover::core
{
    inline constexpr std::size_t ppu_cgram_write_trace_capacity{ 4096 };
    inline constexpr std::size_t ppu_oam_write_trace_capacity{ 4096 };

    using ppu_entropy_mode_t = startup_entropy_mode_t;

    enum class ppu_pixel_source_t : uint8_t
    {
        none,
        background_1,
        background_2,
        background_3,
        background_4,
        objects,
        backdrop
    };

    enum class ppu_debug_view_t : uint8_t
    {
        composed,
        background_1,
        background_2,
        background_3,
        background_4,
        objects
    };

    enum class ppu_presentation_source_t : uint8_t
    {
        presented,
        composed
    };

    struct ppu_presentation_options_t
    {
        static constexpr uint8_t k_all_layers_visible{ 0x1fu };

        uint8_t visible_layer_mask{ k_all_layers_visible };
        ppu_debug_view_t debug_view{ ppu_debug_view_t::composed };
        ppu_presentation_source_t source{ ppu_presentation_source_t::presented };
    };

    struct ppu_pixel_candidate_t
    {
        uint8_t priority{ 0 };
        uint8_t palette{ 0 };
        uint8_t palette_group{ 0 };
        bool color_math_enabled{ false };
        ppu_pixel_source_t source{ ppu_pixel_source_t::none };

        [[nodiscard]] bool operator==(const ppu_pixel_candidate_t&) const noexcept = default;
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

        struct tile_candidate_t
        {
            uint16_t screen_x{ 0 };
            uint16_t source_x{ 0 };
            uint16_t source_y{ 0 };
            uint16_t tilemap_address{ 0 };
            uint16_t tilemap_entry{ 0 };
            uint16_t tiledata_address{ 0 };
            uint16_t vram_address{ 0 };
            uint16_t character{ 0 };
            uint8_t fine_x{ 0 };
            uint8_t fine_y{ 0 };
            uint8_t palette_base{ 0 };
            uint8_t palette_group{ 0 };
            uint8_t priority{ 0 };
            uint8_t row_pair_count{ 0 };
            uint16_t row_data[4]{ 0, 0, 0, 0 };
            bool hmirror{ false };
            bool vmirror{ false };

            [[nodiscard]] bool operator==(const tile_candidate_t&) const noexcept = default;
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
        uint16_t evaluation_scanline{ 0 };
        uint8_t tile_count{ 0 };
        ppu_pixel_candidate_t samples[8]{};
        tile_candidate_t tiles[34]{};
    };

    struct ppu_object_render_state_t
    {
        struct decoded_object_t
        {
            uint16_t x{ 0 };
            uint8_t y{ 0 };
            uint8_t character{ 0 };
            bool nameselect{ false };
            bool vflip{ false };
            bool hflip{ false };
            uint8_t priority{ 0 };
            uint8_t palette{ 0 };
            bool size_select{ false };
            uint8_t width{ 0 };
            uint8_t height{ 0 };

            [[nodiscard]] bool operator==(const decoded_object_t&) const noexcept = default;
        };

        struct tile_candidate_t
        {
            uint8_t object_index{ 0 };
            uint16_t x{ 0 };
            uint8_t tile_x{ 0 };
            uint8_t source_y{ 0 };
            uint8_t fine_y{ 0 };
            uint16_t tiledata_address{ 0 };
            uint16_t vram_address{ 0 };
            uint8_t palette_base{ 0 };
            uint8_t priority{ 0 };
            uint8_t row_pair_count{ 0 };
            uint16_t row_data[2]{ 0, 0 };
            uint32_t data{ 0 };
            bool hflip{ false };

            [[nodiscard]] bool operator==(const tile_candidate_t&) const noexcept = default;
        };

        uint16_t tiledata_address{ 0 };
        uint8_t nameselect{ 0 };
        uint8_t base_size{ 0 };
        uint8_t first_sprite{ 0 };
        uint8_t priority[4]{ 0, 0, 0, 0 };
        bool interlace{ false };
        bool range_over{ false };
        bool time_over{ false };
        bool above_enabled{ false };
        bool below_enabled{ false };
        bool window_above_enabled{ false };
        bool window_below_enabled{ false };
        uint8_t window_mask{ 0 };
        uint16_t evaluation_scanline{ 0 };
        uint16_t rendered_scanline{ 0xffffu };
        uint16_t fetched_scanline{ 0xffffu };
        uint8_t evaluation_first_sprite{ 0 };
        uint8_t evaluation_count{ 0 };
        uint8_t evaluation_indices[32]{};
        uint8_t tile_count{ 0 };
        tile_candidate_t tiles[34]{};
        uint8_t render_tile_count{ 0 };
        tile_candidate_t render_tiles[34]{};
        uint8_t fetched_tile_count{ 0 };
        tile_candidate_t fetched_tiles[34]{};
        decoded_object_t samples[128]{};
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

    struct ppu_layer_compositor_state_t
    {
        ppu_pixel_candidate_t above{};
        ppu_pixel_candidate_t below{};
        ppu_pixel_candidate_t above_samples[framebuffer_t::k_width]{};
        ppu_pixel_candidate_t below_samples[framebuffer_t::k_width]{};
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
        ppu_pixel_candidate_t above{};
        ppu_pixel_candidate_t below{};
        ppu_pixel_candidate_t above_samples[framebuffer_t::k_width]{};
        ppu_pixel_candidate_t below_samples[framebuffer_t::k_width]{};
        bool color_enable_above[framebuffer_t::k_width]{};
        bool color_enable_below[framebuffer_t::k_width]{};
        bool math_enable[framebuffer_t::k_width]{};
        bool math_uses_subscreen[framebuffer_t::k_width]{};
        bool math_uses_fixed_color[framebuffer_t::k_width]{};
        bool color_halve_active[framebuffer_t::k_width]{};
        bool above_transparent[framebuffer_t::k_width]{};
        bool below_transparent[framebuffer_t::k_width]{};
        uint16_t above_color[framebuffer_t::k_width]{};
        uint16_t below_color[framebuffer_t::k_width]{};
        uint16_t math_rhs_color[framebuffer_t::k_width]{};
        uint16_t output_color[framebuffer_t::k_width]{};
        ppu_layer_compositor_state_t backgrounds[4]{};
        ppu_layer_compositor_state_t objects{};
    };

    struct ppu_render_state_snapshot_t
    {
        struct display_write_t
        {
            uint64_t frame_index{ 0 };
            uint16_t scanline{ 0 };
            uint16_t dot{ 0 };
            uint8_t value{ 0 };

            [[nodiscard]] bool operator==(const display_write_t&) const noexcept = default;
        };

        bool display_disabled{ false };
        uint8_t brightness{ 0 };
        uint8_t bg_mode{ 0 };
        bool bg3_priority{ false };
        bool mode7_extbg{ false };
        bool hires{ false };
        uint8_t mosaic_size{ 1 };
        bool mosaic_enabled[4]{ false, false, false, false };
        uint8_t mosaic_voffset{ 0 };
        bool pseudo_hires{ false };
        bool overscan{ false };
        bool interlace{ false };
        uint16_t mode7_hoffset{ 0 };
        uint16_t mode7_voffset{ 0 };
        uint16_t mode7_a{ 0 };
        uint16_t mode7_b{ 0 };
        uint16_t mode7_c{ 0 };
        uint16_t mode7_d{ 0 };
        uint16_t mode7_x{ 0 };
        uint16_t mode7_y{ 0 };
        uint8_t mode7_repeat{ 0 };
        bool mode7_hflip{ false };
        bool mode7_vflip{ false };
        ppu_background_render_state_t backgrounds[4]{};
        ppu_object_render_state_t objects{};
        ppu_color_math_render_state_t color_math{};
        ppu_window_render_state_t window{};
        uint8_t display_write_count{ 0 };
        display_write_t recent_display_writes[8]{};
    };

    struct ppu_cgram_write_trace_t
    {
        uint64_t frame_index{ 0 };
        timing_snapshot_t timing{};
        uint8_t requested_address{ 0 };
        uint8_t effective_address{ 0 };
        uint8_t latched_address{ 0 };
        uint16_t value{ 0 };
        bool redirected{ false };
    };

    struct ppu_oam_write_trace_t
    {
        uint64_t frame_index{ 0 };
        timing_snapshot_t timing{};
        uint16_t requested_address{ 0 };
        uint16_t effective_address{ 0 };
        uint16_t latched_address{ 0 };
        uint8_t value{ 0 };
        bool redirected{ false };
    };

    struct ppu_t
    {
    public:
        struct causal_state_t;

        void power_on() noexcept;
        void reset() noexcept;
        void configure_hardware(const video_timing_t& video_timing,
                                uint8_t ppu1_version,
                                uint8_t ppu2_version) noexcept;
        [[nodiscard]] video_standard_t video_standard() const noexcept;
        [[nodiscard]] const video_timing_t& video_timing() const noexcept;
        [[nodiscard]] uint8_t read_register(uint16_t address, uint8_t open_bus) noexcept;
        void write_register(uint16_t address, uint8_t value) noexcept;
        void latch_counters_external() noexcept;
        void set_external_latch_enabled(bool enabled) noexcept;
        [[nodiscard]] ppu_step_result_t step(master_clock_delta_t master_clocks) noexcept;
        [[nodiscard]] timing_snapshot_t timing() const noexcept;
        [[nodiscard]] uint64_t frame_index() const noexcept;
        [[nodiscard]] master_clock_delta_t current_scanline_clocks() const noexcept;
        [[nodiscard]] ppu_render_state_snapshot_t render_state_snapshot() const noexcept;
        [[nodiscard]] ppu_compositor_snapshot_t compositor_snapshot() const noexcept;
        [[nodiscard]] std::size_t cgram_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<ppu_cgram_write_trace_t, ppu_cgram_write_trace_capacity>&
            cgram_write_trace() const noexcept;
        [[nodiscard]] std::size_t oam_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<ppu_oam_write_trace_t, ppu_oam_write_trace_capacity>&
            oam_write_trace() const noexcept;
        [[nodiscard]] const std::array<uint16_t, 32 * 1024>& vram() const noexcept;
        [[nodiscard]] const std::array<uint8_t, 544>& oam() const noexcept;
        [[nodiscard]] const std::array<uint16_t, 256>& cgram() const noexcept;
        void present(framebuffer_t& framebuffer,
                     const ppu_presentation_options_t& options = {}) const noexcept;
        void set_presentation_layer_mask(uint8_t visible_layer_mask) noexcept;
        void set_frame_capture_enabled(bool enabled) noexcept;
        void set_completed_frame_queue_enabled(bool enabled) noexcept;
        [[nodiscard]] bool pop_completed_frame(framebuffer_t& framebuffer) noexcept;
        void set_entropy_mode(ppu_entropy_mode_t mode) noexcept;
        [[nodiscard]] ppu_entropy_mode_t entropy_mode() const noexcept;
        void set_entropy_seed(uint32_t seed, uint32_t sequence = 0u) noexcept;
        void clear_entropy_seed() noexcept;
        void set_cgram_write_trace_start_frame(uint64_t frame_index) noexcept;
        void set_oam_write_trace_start_frame(uint64_t frame_index) noexcept;
        void capture_causal_state(causal_state_t& state) const noexcept;
        [[nodiscard]] bool restore_causal_state(const causal_state_t& state) noexcept;

    private:
        void initialize(bool warm_reset) noexcept;
        void apply_startup_entropy(bool warm_reset) noexcept;
        [[nodiscard]] uint16_t active_visible_scanlines() const noexcept;
        [[nodiscard]] bool display_active_for_oam() const noexcept;
        [[nodiscard]] bool display_active_for_vram() const noexcept;
        [[nodiscard]] bool display_active_for_cgram() const noexcept;
        [[nodiscard]] uint16_t address_vram() const noexcept;
        [[nodiscard]] uint16_t read_vram_word() const noexcept;
        void write_vram_byte(bool high_byte, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_oam_byte(uint16_t address) const noexcept;
        void write_oam_byte(uint16_t address, uint8_t value) noexcept;
        void decode_oam_object(uint8_t object_index) noexcept;
        void decode_oam_group(uint8_t group_index) noexcept;
        [[nodiscard]] uint8_t object_width(bool size_select) const noexcept;
        [[nodiscard]] uint8_t object_height(bool size_select) const noexcept;
        [[nodiscard]] bool object_on_scanline(const ppu_object_render_state_t::decoded_object_t& object,
                                              uint16_t scanline) const noexcept;
        void evaluate_background_scanline(uint16_t scanline) noexcept;
        void begin_object_scanline(uint16_t scanline) noexcept;
        void prepare_background_scanline(uint16_t scanline) noexcept;
        void initialize_scanline_pipeline(uint16_t scanline) noexcept;
        void process_pipeline_range(const timing_snapshot_t& previous_timing,
                                    const timing_snapshot_t& current_timing) noexcept;
        void process_scanline_range(uint16_t scanline, uint16_t start_dot, uint16_t end_dot) noexcept;
        void evaluate_object_slot(uint16_t scanline, uint8_t slot) noexcept;
        void finalize_object_fetch(uint16_t scanline) noexcept;
        void process_object_fetch_range(uint16_t scanline,
                                        uint16_t start_dot,
                                        uint16_t end_dot) noexcept;
        void clear_scanline_compositor_outputs() noexcept;
        void render_pixel(uint16_t scanline, uint16_t x) noexcept;
        [[nodiscard]] ppu_pixel_candidate_t resolve_object_pixel_candidate(uint16_t x) const noexcept;
        void resolve_pixel_layers(uint16_t x,
                                  const ppu_pixel_candidate_t& object_candidate,
                                  ppu_pixel_candidate_t& above,
                                  ppu_pixel_candidate_t& below) noexcept;
        void resolve_pixel_color_math(uint16_t x,
                                      const ppu_pixel_candidate_t& above_candidate,
                                      const ppu_pixel_candidate_t& below_candidate) noexcept;
        [[nodiscard]] uint16_t presentation_pixel_color(uint16_t x) const noexcept;
        [[nodiscard]] ppu_pixel_candidate_t resolve_background_pixel_candidate(uint8_t background_index,
                                                                               uint16_t x) const noexcept;
        void evaluate_mode7_scanline(uint8_t background_index, uint16_t scanline) noexcept;
        void populate_background_offset_cache(uint16_t scanline) noexcept;
        void evaluate_background_tiles(uint8_t background_index) noexcept;
        void fetch_background_tile_rows(uint8_t background_index) noexcept;
        void synthesize_background_layer_candidate(uint8_t background_index) noexcept;
        void cycle_background_fetch(uint16_t scanline, uint16_t dot) noexcept;
        void cycle_background_name_table(uint8_t background_index,
                                         uint16_t scanline,
                                         uint16_t dot) noexcept;
        void cycle_background_offset(uint16_t scanline, uint16_t dot, uint16_t y) noexcept;
        void cycle_background_character(uint8_t background_index,
                                        uint16_t dot,
                                        uint8_t pair_index,
                                        bool half) noexcept;
        [[nodiscard]] ppu_pixel_candidate_t resolve_cycle_background_pixel_candidate(
            uint8_t background_index) noexcept;
        void evaluate_object_scanline(uint16_t scanline) noexcept;
        void evaluate_object_tiles() noexcept;
        void fetch_object_tile_rows() noexcept;
        void synthesize_object_layer_candidate() noexcept;
        void apply_window_masks() noexcept;
        void resolve_compositor_candidates() noexcept;
        void resolve_color_math_state() noexcept;
        [[nodiscard]] uint8_t read_cgram_byte(bool high_byte, uint8_t address) const noexcept;
        void write_cgram_word(uint8_t address, uint16_t value) noexcept;
        void reset_oam_address() noexcept;
        void update_first_sprite() noexcept;
        [[nodiscard]] bool mosaic_enabled() const noexcept;
        [[nodiscard]] uint8_t mosaic_voffset() const noexcept;
        void advance_mosaic_scanline(uint16_t scanline) noexcept;
        void latch_counters() noexcept;
        void decode_render_state() noexcept;
        void clear_compositor_state() noexcept;
        [[nodiscard]] size_t sample_pixel_count() const noexcept;
        void promote_framebuffer_geometry() noexcept;
        void render_scanline(uint16_t scanline) noexcept;
        void render_placeholder_frame() noexcept;

        struct ppu_display_state_t
        {
            bool disabled{ false };
            uint8_t brightness{ 0 };

            [[nodiscard]] bool operator==(const ppu_display_state_t&) const noexcept = default;
        };

        struct ppu_display_write_history_t
        {
            uint8_t count{ 0 };
            ppu_render_state_snapshot_t::display_write_t entries[8]{};

            [[nodiscard]] bool operator==(const ppu_display_write_history_t&) const noexcept = default;
        };

        bool _external_latch_enabled{ false };

        struct ppu_oam_state_t
        {
            uint16_t base_address{ 0 };
            uint16_t address{ 0 };
            uint16_t latched_address{ 0 };
            bool priority{ false };
            uint8_t write_latch{ 0 };

            [[nodiscard]] bool operator==(const ppu_oam_state_t&) const noexcept = default;
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

            [[nodiscard]] bool operator==(const ppu_bg_state_t&) const noexcept = default;
        };

        struct ppu_scroll_latch_state_t
        {
            uint8_t ppu1{ 0 };
            uint8_t ppu2{ 0 };
            uint8_t mode7{ 0 };
            uint16_t mode7_hoffset{ 0 };
            uint16_t mode7_voffset{ 0 };

            [[nodiscard]] bool operator==(const ppu_scroll_latch_state_t&) const noexcept = default;
        };

        struct ppu_mosaic_state_t
        {
            std::array<bool, 4> enabled{};
            uint8_t size{ 1 };
            uint8_t vcounter{ 0 };

            [[nodiscard]] bool operator==(const ppu_mosaic_state_t&) const noexcept = default;
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

            [[nodiscard]] bool operator==(const ppu_window_state_t&) const noexcept = default;
        };

        struct ppu_object_layer_state_t
        {
            using decoded_object_t = ppu_object_render_state_t::decoded_object_t;
            using tile_candidate_t = ppu_object_render_state_t::tile_candidate_t;
            static constexpr uint16_t k_uninitialized_scanline{ 0xffffu };
            struct evaluated_item_t
            {
                bool valid{ false };
                uint8_t index{ 0 };

                [[nodiscard]] bool operator==(const evaluated_item_t&) const noexcept = default;
            };

            struct fetched_tile_t
            {
                bool valid{ false };
                tile_candidate_t candidate{};

                [[nodiscard]] bool operator==(const fetched_tile_t&) const noexcept = default;
            };

            uint8_t base_size{ 0 };
            uint8_t nameselect{ 0 };
            uint16_t tiledata_address{ 0 };
            uint8_t first_sprite{ 0 };
            std::array<uint8_t, 4> priority{};
            bool interlace{ false };
            bool range_over{ false };
            bool time_over{ false };
            bool above_enabled{ false };
            bool below_enabled{ false };
            bool window_above_enabled{ false };
            bool window_below_enabled{ false };
            uint16_t evaluation_scanline{ 0 };
            uint16_t pipeline_x{ 0 };
            uint16_t rendered_scanline{ k_uninitialized_scanline };
            uint16_t fetched_scanline{ k_uninitialized_scanline };
            bool active_buffer{ false };
            uint8_t evaluation_first_sprite{ 0 };
            uint8_t evaluation_count{ 0 };
            uint8_t evaluation_progress{ 0 };
            std::array<uint8_t, 32> evaluation_indices{};
            std::array<std::array<evaluated_item_t, 32>, 2> items{};
            uint8_t tile_count{ 0 };
            std::array<tile_candidate_t, 34> tiles{};
            std::array<std::array<fetched_tile_t, 34>, 2> tile_buffers{};
            uint8_t render_tile_count{ 0 };
            std::array<tile_candidate_t, 34> render_tiles{};
            uint8_t fetched_tile_count{ 0 };
            std::array<tile_candidate_t, 34> fetched_tiles{};
            std::array<decoded_object_t, 128> objects{};

            [[nodiscard]] bool operator==(const ppu_object_layer_state_t&) const noexcept = default;
        };

        struct ppu_background_layer_state_t
        {
            using tile_candidate_t = ppu_background_render_state_t::tile_candidate_t;

            uint16_t evaluation_scanline{ 0 };
            uint8_t tile_count{ 0 };
            std::array<uint16_t, 66> offset_hoffset{};
            std::array<uint16_t, 66> offset_voffset{};
            std::array<ppu_pixel_candidate_t, framebuffer_t::k_max_width> samples{};
            std::array<tile_candidate_t, 66> tiles{};
            std::array<tile_candidate_t, 66> render_tiles{};
            uint8_t rendering_index{ 0 };
            uint8_t pixel_counter{ 0 };
            uint16_t mosaic_hcounter{ 1 };
            ppu_pixel_candidate_t mosaic_pixel{};
            std::array<tile_candidate_t, 66> cycle_tiles{};
            uint8_t cycle_rendering_index{ 0 };
            uint8_t cycle_pixel_counter{ 0 };
            uint16_t cycle_offset_hoffset{ 0 };
            uint16_t cycle_offset_voffset{ 0 };
            uint16_t cycle_mosaic_hcounter{ 1 };
            ppu_pixel_candidate_t cycle_mosaic_pixel{};
            ppu_pixel_candidate_t cycle_below_pixel{};

            [[nodiscard]] bool operator==(const ppu_background_layer_state_t&) const noexcept = default;
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

            [[nodiscard]] bool operator==(const ppu_color_math_state_t&) const noexcept = default;
        };

        struct ppu_screen_state_t
        {
            bool hires{ false };
            bool pseudo_hires{ false };
            bool overscan{ false };
            bool interlace{ false };
            bool mode7_extbg{ false };
            uint16_t mode7_a{ 0 };
            uint16_t mode7_b{ 0 };
            uint16_t mode7_c{ 0 };
            uint16_t mode7_d{ 0 };
            uint16_t mode7_x{ 0 };
            uint16_t mode7_y{ 0 };
            uint8_t mode7_repeat{ 0 };
            bool mode7_hflip{ false };
            bool mode7_vflip{ false };

            [[nodiscard]] bool operator==(const ppu_screen_state_t&) const noexcept = default;
        };

        struct ppu_internal_layer_compositor_state_t
        {
            ppu_pixel_candidate_t above{};
            ppu_pixel_candidate_t below{};
            std::array<ppu_pixel_candidate_t, framebuffer_t::k_max_width> above_samples{};
            std::array<ppu_pixel_candidate_t, framebuffer_t::k_max_width> below_samples{};

            [[nodiscard]] bool operator==(const ppu_internal_layer_compositor_state_t&) const noexcept = default;
        };

        struct ppu_compositor_state_t
        {
            ppu_pixel_candidate_t above{};
            ppu_pixel_candidate_t below{};
            std::array<ppu_pixel_candidate_t, framebuffer_t::k_max_width> above_samples{};
            std::array<ppu_pixel_candidate_t, framebuffer_t::k_max_width> below_samples{};
            std::array<bool, framebuffer_t::k_max_width> color_enable_above{};
            std::array<bool, framebuffer_t::k_max_width> color_enable_below{};
            std::array<bool, framebuffer_t::k_max_width> math_enable{};
            std::array<bool, framebuffer_t::k_max_width> math_uses_subscreen{};
            std::array<bool, framebuffer_t::k_max_width> math_uses_fixed_color{};
            std::array<bool, framebuffer_t::k_max_width> color_halve_active{};
            std::array<bool, framebuffer_t::k_max_width> above_transparent{};
            std::array<bool, framebuffer_t::k_max_width> below_transparent{};
            std::array<uint16_t, framebuffer_t::k_max_width> above_color{};
            std::array<uint16_t, framebuffer_t::k_max_width> below_color{};
            std::array<uint16_t, framebuffer_t::k_max_width> math_rhs_color{};
            std::array<uint16_t, framebuffer_t::k_max_width> output_color{};
            std::array<ppu_internal_layer_compositor_state_t, 4> backgrounds{};
            ppu_internal_layer_compositor_state_t objects{};

            [[nodiscard]] bool operator==(const ppu_compositor_state_t&) const noexcept = default;
        };

        struct ppu_pipeline_state_t
        {
            static constexpr uint16_t k_uninitialized_scanline{ 0xffffu };

            uint16_t initialized_scanline{ k_uninitialized_scanline };
            uint16_t next_object_evaluate_dot{ 0u };
            uint16_t next_pixel_dot{ 58u };
            uint16_t next_pixel_x{ 0u };
            uint16_t next_object_fetch_dot{ 1080u };
            uint16_t next_background_fetch_dot{ 0u };
            uint8_t next_object_fetch_index{ 0u };
            bool background_fetch_state_dirty{ false };
            bool use_cycle_background_pipeline{ false };
            bool background_begin_completed{ false };
            bool object_fetch_started{ false };
            bool object_fetch_completed{ false };

            [[nodiscard]] bool operator==(const ppu_pipeline_state_t&) const noexcept = default;
        };

        struct ppu_vram_state_t
        {
            uint8_t increment_size{ 1 };
            uint8_t mapping{ 0 };
            bool increment_on_high{ false };
            uint16_t address{ 0 };
            uint16_t read_latch{ 0 };

            [[nodiscard]] bool operator==(const ppu_vram_state_t&) const noexcept = default;
        };

        struct ppu_cgram_state_t
        {
            uint8_t address{ 0 };
            uint8_t latched_address{ 0 };
            bool write_high_pending{ false };
            bool read_high_pending{ false };
            uint8_t write_latch{ 0 };

            [[nodiscard]] bool operator==(const ppu_cgram_state_t&) const noexcept = default;
        };

        struct ppu_counter_latch_state_t
        {
            bool counters_latched{ false };
            uint16_t hcounter{ 0 };
            uint16_t vcounter{ 0 };
            bool hcounter_high_read{ false };
            bool vcounter_high_read{ false };

            [[nodiscard]] bool operator==(const ppu_counter_latch_state_t&) const noexcept = default;
        };

        framebuffer_t _composed_frame{};
        framebuffer_t _presented_frame{};
        framebuffer_t _presentation_composed_frame{};
        framebuffer_t _presentation_presented_frame{};
        bool _frame_high_geometry{ false };
        uint8_t _presentation_layer_mask{ ppu_presentation_options_t::k_all_layers_visible };
        std::array<uint8_t, 0x40> _registers{};
        std::array<uint16_t, 32 * 1024> _vram{};
        std::array<uint8_t, 544> _oam{};
        std::array<uint16_t, 256> _cgram{};
        video_timing_t _video_timing{ k_ntsc_video_timing };
        uint8_t _ppu1_version{ 1 };
        uint8_t _ppu2_version{ 3 };
        raster_counter_t _counter{};
        bool _timing_interlace{ false };
        bool _display_interlace{ false };
        bool _display_overscan{ false };
        uint64_t _frame_counter{ 0 };
        ppu_entropy_mode_t _entropy_mode{ ppu_entropy_mode_t::none };
        bool _entropy_seed_override_enabled{ false };
        uint32_t _entropy_seed{ 0 };
        uint32_t _entropy_sequence{ 0 };
        ppu_display_state_t _display{};
        ppu_display_write_history_t _display_write_history{};
        ppu_oam_state_t _oam_state{};
        ppu_bg_state_t _bg_state{};
        ppu_scroll_latch_state_t _scroll_latches{};
        ppu_mosaic_state_t _mosaic_state{};
        ppu_window_state_t _window_state{};
        std::array<ppu_background_layer_state_t, 4> _background_layer_state{};
        ppu_object_layer_state_t _object_layer_state{};
        ppu_color_math_state_t _color_math_state{};
        ppu_screen_state_t _screen_state{};
        ppu_compositor_state_t _compositor_state{};
        ppu_pipeline_state_t _pipeline_state{};
        ppu_vram_state_t _vram_state{};
        ppu_cgram_state_t _cgram_state{};
        std::array<ppu_cgram_write_trace_t, ppu_cgram_write_trace_capacity> _cgram_write_trace{};
        std::size_t _cgram_write_trace_count{ 0 };
        uint64_t _cgram_write_trace_start_frame{ 0 };
        bool _cgram_write_trace_enabled{ false };
        std::array<ppu_oam_write_trace_t, ppu_oam_write_trace_capacity> _oam_write_trace{};
        std::size_t _oam_write_trace_count{ 0 };
        uint64_t _oam_write_trace_start_frame{ 0 };
        bool _oam_write_trace_enabled{ false };
        ppu_counter_latch_state_t _counter_latch{};
        uint8_t _ppu1_mdr{ 0 };
        uint8_t _ppu2_mdr{ 0 };
        bool _frame_capture_enabled{ false };
        bool _completed_frame_queue_enabled{ false };
        std::deque<framebuffer_t> _completed_frames{};
    };

    struct ppu_t::causal_state_t
    {
        static constexpr uint32_t schema_version{ 1 };

        framebuffer_t composed_frame{};
        framebuffer_t presented_frame{};
        framebuffer_t presentation_composed_frame{};
        framebuffer_t presentation_presented_frame{};
        bool frame_high_geometry{ false };
        std::array<uint8_t, 0x40> registers{};
        std::array<uint16_t, 32 * 1024> vram{};
        std::array<uint8_t, 544> oam{};
        std::array<uint16_t, 256> cgram{};
        video_timing_t video_timing{ k_ntsc_video_timing };
        uint8_t ppu1_version{ 1 };
        uint8_t ppu2_version{ 3 };
        raster_counter_t counter{};
        bool timing_interlace{ false };
        bool display_interlace{ false };
        bool display_overscan{ false };
        uint64_t frame_counter{ 0 };
        ppu_entropy_mode_t entropy_mode{ ppu_entropy_mode_t::none };
        bool entropy_seed_override_enabled{ false };
        uint32_t entropy_seed{ 0 };
        uint32_t entropy_sequence{ 0 };
        ppu_display_state_t display{};
        ppu_display_write_history_t display_write_history{};
        ppu_oam_state_t oam_state{};
        ppu_bg_state_t bg_state{};
        ppu_scroll_latch_state_t scroll_latches{};
        ppu_mosaic_state_t mosaic_state{};
        ppu_window_state_t window_state{};
        std::array<ppu_background_layer_state_t, 4> background_layer_state{};
        ppu_object_layer_state_t object_layer_state{};
        ppu_color_math_state_t color_math_state{};
        ppu_screen_state_t screen_state{};
        ppu_compositor_state_t compositor_state{};
        ppu_pipeline_state_t pipeline_state{};
        ppu_vram_state_t vram_state{};
        ppu_cgram_state_t cgram_state{};
        ppu_counter_latch_state_t counter_latch{};
        bool external_latch_enabled{ false };
        uint8_t ppu1_mdr{ 0 };
        uint8_t ppu2_mdr{ 0 };

        [[nodiscard]] bool operator==(const causal_state_t&) const noexcept = default;
    };

    using ppu_causal_state_t = ppu_t::causal_state_t;
}
