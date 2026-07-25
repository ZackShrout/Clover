//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesCheckpoint.h"

#include "clover/frontend/MediaIdentity.h"

#include <array>
#include <bit>
#include <limits>
#include <new>
#include <type_traits>

namespace clover::frontend
{
    namespace
    {
        constexpr std::array<std::byte, 8> k_magic{
            std::byte{ 'C' }, std::byte{ 'L' }, std::byte{ 'V' }, std::byte{ 'R' },
            std::byte{ 'C' }, std::byte{ 'K' }, std::byte{ 'P' }, std::byte{ 'T' }
        };
        constexpr uint16_t k_format_version{ 1u };
        constexpr uint32_t k_core_state_version{ 1u };
        constexpr uint32_t k_system_snes{ 1u };
        constexpr uint16_t k_header_size{ 128u };
        constexpr uint64_t k_max_cartridge_ram_bytes{ 1024u * 1024u };
        constexpr uint64_t k_max_enhancement_state_bytes{ 1024u * 1024u };

        template <typename>
        inline constexpr bool k_unsupported_serialized_type{ false };

        class writer_t
        {
        public:
            explicit writer_t(std::vector<std::byte>& bytes) : _bytes{ bytes } {}

            template <typename T>
            void value(const T& value)
            {
                if constexpr (std::is_same_v<T, bool>)
                {
                    integer<uint8_t>(value ? 1u : 0u);
                }
                else if constexpr (std::is_enum_v<T>)
                {
                    integer<std::underlying_type_t<T>>(static_cast<std::underlying_type_t<T>>(value));
                }
                else if constexpr (std::is_integral_v<T>)
                {
                    integer<std::make_unsigned_t<T>>(std::bit_cast<std::make_unsigned_t<T>>(value));
                }
                else if constexpr (requires { value.size(); value.begin(); value.end(); })
                {
                    for (const auto& item : value)
                        this->value(item);
                }
                else if constexpr (std::is_array_v<T>)
                {
                    for (const auto& item : value)
                        this->value(item);
                }
                else
                {
                    static_assert(k_unsupported_serialized_type<T>,
                                  "checkpoint field requires an explicit codec");
                }
            }

            void bytes(std::span<const std::byte> bytes)
            {
                _bytes.insert(_bytes.end(), bytes.begin(), bytes.end());
            }

            [[nodiscard]] constexpr bool valid() const noexcept { return true; }

        private:
            template <typename T>
            void integer(T value)
            {
                for (size_t index{ 0 }; index < sizeof(T); ++index)
                {
                    _bytes.push_back(static_cast<std::byte>(
                        static_cast<uint64_t>(value) >> (index * 8u)
                    ));
                }
            }

            std::vector<std::byte>& _bytes;
        };

        class reader_t
        {
        public:
            explicit reader_t(std::span<const std::byte> bytes) : _bytes{ bytes } {}

            template <typename T>
            void value(T& value)
            {
                if (!_valid)
                    return;

                if constexpr (std::is_same_v<T, bool>)
                {
                    uint8_t encoded{};
                    integer(encoded);
                    if (encoded > 1u)
                        _valid = false;
                    else
                        value = encoded != 0u;
                }
                else if constexpr (std::is_enum_v<T>)
                {
                    std::underlying_type_t<T> encoded{};
                    integer(encoded);
                    value = static_cast<T>(encoded);
                }
                else if constexpr (std::is_integral_v<T>)
                {
                    integer(value);
                }
                else if constexpr (requires { value.size(); value.begin(); value.end(); })
                {
                    for (auto& item : value)
                        this->value(item);
                }
                else if constexpr (std::is_array_v<T>)
                {
                    for (auto& item : value)
                        this->value(item);
                }
                else
                {
                    static_assert(k_unsupported_serialized_type<T>,
                                  "checkpoint field requires an explicit codec");
                }
            }

            void bytes(std::span<std::byte> destination)
            {
                if (!_valid || destination.size() > remaining())
                {
                    _valid = false;
                    return;
                }
                std::copy_n(_bytes.begin() + static_cast<std::ptrdiff_t>(_offset),
                            destination.size(),
                            destination.begin());
                _offset += destination.size();
            }

            [[nodiscard]] bool valid() const noexcept { return _valid; }
            [[nodiscard]] size_t remaining() const noexcept { return _bytes.size() - _offset; }
            void invalidate() noexcept { _valid = false; }

        private:
            template <typename T>
            void integer(T& value)
            {
                using unsigned_t = std::make_unsigned_t<T>;
                if (sizeof(T) > remaining())
                {
                    _valid = false;
                    return;
                }
                unsigned_t encoded{};
                for (size_t index{ 0 }; index < sizeof(T); ++index)
                {
                    encoded |= static_cast<unsigned_t>(
                        static_cast<uint8_t>(_bytes[_offset + index])
                    ) << (index * 8u);
                }
                _offset += sizeof(T);
                value = std::bit_cast<T>(encoded);
            }

            std::span<const std::byte> _bytes;
            size_t _offset{ 0u };
            bool _valid{ true };
        };

        template <typename archive_t, typename value_t>
        void scalar(archive_t& archive, value_t& value)
        {
            archive.value(value);
        }

        template <typename archive_t, typename value_t>
        void fields(archive_t& archive, value_t& value)
        {
            archive.value(value);
        }

        template <typename archive_t>
        void timing_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.master_clock);
            scalar(archive, value.raster.scanline);
            scalar(archive, value.raster.dot);
            scalar(archive, value.in_hblank);
            scalar(archive, value.in_vblank);
        }

        template <typename archive_t>
        void video_timing_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.standard);
            scalar(archive, value.master_clocks_per_scanline);
            scalar(archive, value.scanlines_per_frame);
            scalar(archive, value.visible_scanlines);
            scalar(archive, value.overscan_visible_scanlines);
            scalar(archive, value.short_scanline);
            scalar(archive, value.short_scanline_clocks);
            scalar(archive, value.hblank_start_dot);
            scalar(archive, value.hdma_trigger_dot);
        }

        template <typename archive_t>
        void raster_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.master_clock);
            scalar(archive, value.scanline);
            scalar(archive, value.dot);
            scalar(archive, value.odd_field);
        }

        template <typename archive_t>
        void framebuffer_fields(archive_t& archive, auto& value)
        {
            uint32_t width{ value.width() };
            uint32_t height{ value.height() };
            uint32_t pitch{ value.pitch_pixels() };
            scalar(archive, width);
            scalar(archive, height);
            scalar(archive, pitch);
            if constexpr (std::is_same_v<std::remove_cvref_t<archive_t>, reader_t>)
            {
                if (!archive.valid()
                    || width == 0u
                    || width > core::framebuffer_t::k_max_width
                    || height == 0u
                    || height > core::framebuffer_t::k_max_height
                    || pitch < width
                    || pitch > core::framebuffer_t::k_max_width)
                {
                    archive.invalidate();
                    return;
                }
                value.set_geometry(width, height, pitch);
            }
            for (size_t index{ 0 }; index < core::framebuffer_t::k_max_pixel_count; ++index)
                scalar(archive, value.data()[index]);
        }

        template <typename archive_t>
        void pixel_candidate_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.priority);
            scalar(archive, value.palette);
            scalar(archive, value.palette_group);
            scalar(archive, value.color_math_enabled);
            scalar(archive, value.source);
        }

        template <typename archive_t>
        void background_tile_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.screen_x);
            scalar(archive, value.source_x);
            scalar(archive, value.source_y);
            scalar(archive, value.tilemap_address);
            scalar(archive, value.tilemap_entry);
            scalar(archive, value.tiledata_address);
            scalar(archive, value.vram_address);
            scalar(archive, value.character);
            scalar(archive, value.fine_x);
            scalar(archive, value.fine_y);
            scalar(archive, value.palette_base);
            scalar(archive, value.palette_group);
            scalar(archive, value.priority);
            scalar(archive, value.row_pair_count);
            scalar(archive, value.row_data);
            scalar(archive, value.hmirror);
            scalar(archive, value.vmirror);
        }

        template <typename archive_t>
        void object_tile_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.object_index);
            scalar(archive, value.x);
            scalar(archive, value.tile_x);
            scalar(archive, value.source_y);
            scalar(archive, value.fine_y);
            scalar(archive, value.tiledata_address);
            scalar(archive, value.vram_address);
            scalar(archive, value.palette_base);
            scalar(archive, value.priority);
            scalar(archive, value.row_pair_count);
            scalar(archive, value.row_data);
            scalar(archive, value.data);
            scalar(archive, value.hflip);
        }

        template <typename archive_t>
        void decoded_object_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.x);
            scalar(archive, value.y);
            scalar(archive, value.character);
            scalar(archive, value.nameselect);
            scalar(archive, value.vflip);
            scalar(archive, value.hflip);
            scalar(archive, value.priority);
            scalar(archive, value.palette);
            scalar(archive, value.size_select);
            scalar(archive, value.width);
            scalar(archive, value.height);
        }

        template <typename archive_t>
        void display_write_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.frame_index);
            scalar(archive, value.scanline);
            scalar(archive, value.dot);
            scalar(archive, value.value);
        }

        template <typename archive_t>
        void ppu_display_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.disabled);
            scalar(archive, value.brightness);
        }

        template <typename archive_t>
        void ppu_display_history_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.count);
            for (auto& entry : value.entries)
                display_write_fields(archive, entry);
        }

        template <typename archive_t>
        void ppu_oam_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.base_address);
            scalar(archive, value.address);
            scalar(archive, value.latched_address);
            scalar(archive, value.priority);
            scalar(archive, value.write_latch);
        }

        template <typename archive_t>
        void ppu_bg_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.mode);
            scalar(archive, value.bg3_priority);
            scalar(archive, value.large_tiles);
            scalar(archive, value.render_mode);
            scalar(archive, value.active);
            scalar(archive, value.tiledata_address);
            scalar(archive, value.screen_address);
            scalar(archive, value.screen_size);
            scalar(archive, value.priority);
            scalar(archive, value.hoffset);
            scalar(archive, value.voffset);
            scalar(archive, value.above_enabled);
            scalar(archive, value.below_enabled);
            scalar(archive, value.window_above_enabled);
            scalar(archive, value.window_below_enabled);
            scalar(archive, value.window_mask);
        }

        template <typename archive_t>
        void ppu_scroll_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.ppu1);
            scalar(archive, value.ppu2);
            scalar(archive, value.mode7);
            scalar(archive, value.mode7_hoffset);
            scalar(archive, value.mode7_voffset);
        }

        template <typename archive_t>
        void ppu_mosaic_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.enabled);
            scalar(archive, value.size);
            scalar(archive, value.vcounter);
        }

        template <typename archive_t>
        void ppu_window_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.one_invert);
            scalar(archive, value.one_enable);
            scalar(archive, value.two_invert);
            scalar(archive, value.two_enable);
            scalar(archive, value.one_left);
            scalar(archive, value.one_right);
            scalar(archive, value.two_left);
            scalar(archive, value.two_right);
            scalar(archive, value.object_mask);
            scalar(archive, value.color_mask);
            scalar(archive, value.color_mask_above);
            scalar(archive, value.color_mask_below);
        }

        template <typename archive_t>
        void evaluated_item_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.valid);
            scalar(archive, value.index);
        }

        template <typename archive_t>
        void fetched_object_tile_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.valid);
            object_tile_fields(archive, value.candidate);
        }

        template <typename archive_t>
        void ppu_object_layer_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.base_size);
            scalar(archive, value.nameselect);
            scalar(archive, value.tiledata_address);
            scalar(archive, value.first_sprite);
            scalar(archive, value.priority);
            scalar(archive, value.interlace);
            scalar(archive, value.range_over);
            scalar(archive, value.time_over);
            scalar(archive, value.above_enabled);
            scalar(archive, value.below_enabled);
            scalar(archive, value.window_above_enabled);
            scalar(archive, value.window_below_enabled);
            scalar(archive, value.evaluation_scanline);
            scalar(archive, value.pipeline_x);
            scalar(archive, value.rendered_scanline);
            scalar(archive, value.fetched_scanline);
            scalar(archive, value.active_buffer);
            scalar(archive, value.evaluation_first_sprite);
            scalar(archive, value.evaluation_count);
            scalar(archive, value.evaluation_progress);
            scalar(archive, value.evaluation_indices);
            for (auto& buffer : value.items)
                for (auto& item : buffer)
                    evaluated_item_fields(archive, item);
            scalar(archive, value.tile_count);
            for (auto& tile : value.tiles)
                object_tile_fields(archive, tile);
            for (auto& buffer : value.tile_buffers)
                for (auto& tile : buffer)
                    fetched_object_tile_fields(archive, tile);
            scalar(archive, value.render_tile_count);
            for (auto& tile : value.render_tiles)
                object_tile_fields(archive, tile);
            scalar(archive, value.fetched_tile_count);
            for (auto& tile : value.fetched_tiles)
                object_tile_fields(archive, tile);
            for (auto& object : value.objects)
                decoded_object_fields(archive, object);
        }

        template <typename archive_t>
        void ppu_background_layer_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.evaluation_scanline);
            scalar(archive, value.tile_count);
            scalar(archive, value.offset_hoffset);
            scalar(archive, value.offset_voffset);
            for (auto& sample : value.samples)
                pixel_candidate_fields(archive, sample);
            for (auto& tile : value.tiles)
                background_tile_fields(archive, tile);
            for (auto& tile : value.render_tiles)
                background_tile_fields(archive, tile);
            scalar(archive, value.rendering_index);
            scalar(archive, value.pixel_counter);
            scalar(archive, value.mosaic_hcounter);
            pixel_candidate_fields(archive, value.mosaic_pixel);
            for (auto& tile : value.cycle_tiles)
                background_tile_fields(archive, tile);
            scalar(archive, value.cycle_rendering_index);
            scalar(archive, value.cycle_pixel_counter);
            scalar(archive, value.cycle_offset_hoffset);
            scalar(archive, value.cycle_offset_voffset);
            scalar(archive, value.cycle_mosaic_hcounter);
            pixel_candidate_fields(archive, value.cycle_mosaic_pixel);
            pixel_candidate_fields(archive, value.cycle_below_pixel);
        }

        template <typename archive_t>
        void ppu_color_math_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.direct_color);
            scalar(archive, value.blend_mode);
            scalar(archive, value.color_halve);
            scalar(archive, value.color_mode_subtract);
            scalar(archive, value.bg_color_enable);
            scalar(archive, value.obj_color_enable);
            scalar(archive, value.backdrop_color_enable);
            scalar(archive, value.fixed_red);
            scalar(archive, value.fixed_green);
            scalar(archive, value.fixed_blue);
        }

        template <typename archive_t>
        void ppu_screen_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.hires);
            scalar(archive, value.pseudo_hires);
            scalar(archive, value.overscan);
            scalar(archive, value.interlace);
            scalar(archive, value.mode7_extbg);
            scalar(archive, value.mode7_a);
            scalar(archive, value.mode7_b);
            scalar(archive, value.mode7_c);
            scalar(archive, value.mode7_d);
            scalar(archive, value.mode7_x);
            scalar(archive, value.mode7_y);
            scalar(archive, value.mode7_repeat);
            scalar(archive, value.mode7_hflip);
            scalar(archive, value.mode7_vflip);
        }

        template <typename archive_t>
        void ppu_internal_compositor_fields(archive_t& archive, auto& value)
        {
            pixel_candidate_fields(archive, value.above);
            pixel_candidate_fields(archive, value.below);
            for (auto& sample : value.above_samples)
                pixel_candidate_fields(archive, sample);
            for (auto& sample : value.below_samples)
                pixel_candidate_fields(archive, sample);
        }

        template <typename archive_t>
        void ppu_compositor_fields(archive_t& archive, auto& value)
        {
            pixel_candidate_fields(archive, value.above);
            pixel_candidate_fields(archive, value.below);
            for (auto& sample : value.above_samples)
                pixel_candidate_fields(archive, sample);
            for (auto& sample : value.below_samples)
                pixel_candidate_fields(archive, sample);
            scalar(archive, value.color_enable_above);
            scalar(archive, value.color_enable_below);
            scalar(archive, value.math_enable);
            scalar(archive, value.math_uses_subscreen);
            scalar(archive, value.math_uses_fixed_color);
            scalar(archive, value.color_halve_active);
            scalar(archive, value.above_transparent);
            scalar(archive, value.below_transparent);
            scalar(archive, value.above_color);
            scalar(archive, value.below_color);
            scalar(archive, value.math_rhs_color);
            scalar(archive, value.output_color);
            for (auto& layer : value.backgrounds)
                ppu_internal_compositor_fields(archive, layer);
            ppu_internal_compositor_fields(archive, value.objects);
        }

        template <typename archive_t>
        void ppu_pipeline_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.initialized_scanline);
            scalar(archive, value.next_object_evaluate_dot);
            scalar(archive, value.next_pixel_dot);
            scalar(archive, value.next_pixel_x);
            scalar(archive, value.next_object_fetch_dot);
            scalar(archive, value.next_background_fetch_dot);
            scalar(archive, value.next_object_fetch_index);
            scalar(archive, value.background_fetch_state_dirty);
            scalar(archive, value.use_cycle_background_pipeline);
            scalar(archive, value.background_begin_completed);
            scalar(archive, value.object_fetch_started);
            scalar(archive, value.object_fetch_completed);
        }

        template <typename archive_t>
        void ppu_vram_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.increment_size);
            scalar(archive, value.mapping);
            scalar(archive, value.increment_on_high);
            scalar(archive, value.address);
            scalar(archive, value.read_latch);
        }

        template <typename archive_t>
        void ppu_cgram_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.address);
            scalar(archive, value.latched_address);
            scalar(archive, value.write_high_pending);
            scalar(archive, value.read_high_pending);
            scalar(archive, value.write_latch);
        }

        template <typename archive_t>
        void ppu_counter_latch_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.counters_latched);
            scalar(archive, value.hcounter);
            scalar(archive, value.vcounter);
            scalar(archive, value.hcounter_high_read);
            scalar(archive, value.vcounter_high_read);
        }

        template <typename archive_t>
        void ppu_fields(archive_t& archive, auto& value)
        {
            framebuffer_fields(archive, value.composed_frame);
            framebuffer_fields(archive, value.presented_frame);
            framebuffer_fields(archive, value.presentation_composed_frame);
            framebuffer_fields(archive, value.presentation_presented_frame);
            scalar(archive, value.frame_high_geometry);
            scalar(archive, value.registers);
            scalar(archive, value.vram);
            scalar(archive, value.oam);
            scalar(archive, value.cgram);
            video_timing_fields(archive, value.video_timing);
            scalar(archive, value.ppu1_version);
            scalar(archive, value.ppu2_version);
            raster_fields(archive, value.counter);
            scalar(archive, value.timing_interlace);
            scalar(archive, value.display_interlace);
            scalar(archive, value.display_overscan);
            scalar(archive, value.frame_counter);
            scalar(archive, value.entropy_mode);
            scalar(archive, value.entropy_seed_override_enabled);
            scalar(archive, value.entropy_seed);
            scalar(archive, value.entropy_sequence);
            ppu_display_fields(archive, value.display);
            ppu_display_history_fields(archive, value.display_write_history);
            ppu_oam_fields(archive, value.oam_state);
            ppu_bg_fields(archive, value.bg_state);
            ppu_scroll_fields(archive, value.scroll_latches);
            ppu_mosaic_fields(archive, value.mosaic_state);
            ppu_window_fields(archive, value.window_state);
            for (auto& layer : value.background_layer_state)
                ppu_background_layer_fields(archive, layer);
            ppu_object_layer_fields(archive, value.object_layer_state);
            ppu_color_math_fields(archive, value.color_math_state);
            ppu_screen_fields(archive, value.screen_state);
            ppu_compositor_fields(archive, value.compositor_state);
            ppu_pipeline_fields(archive, value.pipeline_state);
            ppu_vram_fields(archive, value.vram_state);
            ppu_cgram_fields(archive, value.cgram_state);
            ppu_counter_latch_fields(archive, value.counter_latch);
            scalar(archive, value.external_latch_enabled);
            scalar(archive, value.ppu1_mdr);
            scalar(archive, value.ppu2_mdr);
        }

        template <typename archive_t>
        void apu_register_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.pc);
            scalar(archive, value.a);
            scalar(archive, value.x);
            scalar(archive, value.y);
            scalar(archive, value.sp);
            scalar(archive, value.psw);
        }

        template <typename archive_t>
        void apu_io_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.timers_disable);
            scalar(archive, value.ram_writable);
            scalar(archive, value.ram_disable);
            scalar(archive, value.timers_enable);
            scalar(archive, value.external_wait_states);
            scalar(archive, value.internal_wait_states);
            scalar(archive, value.dsp_address);
            scalar(archive, value.aux4);
            scalar(archive, value.aux5);
        }

        template <typename archive_t>
        void apu_timer_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.stage0);
            scalar(archive, value.stage1);
            scalar(archive, value.stage2);
            scalar(archive, value.stage3);
            scalar(archive, value.line);
            scalar(archive, value.enable);
            scalar(archive, value.target);
        }

        template <typename archive_t>
        void apu_access_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.kind);
            scalar(archive, value.address);
            scalar(archive, value.value);
            scalar(archive, value.awaiting_cpu_sync);
        }

        template <typename archive_t>
        void apu_instruction_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.active);
            scalar(archive, value.abort_requested);
            apu_register_fields(archive, value.start_registers);
            scalar(archive, value.start_current_opcode_pc);
            scalar(archive, value.start_last_opcode);
            for (auto& access : value.accesses)
                apu_access_fields(archive, access);
            scalar(archive, value.access_count);
            scalar(archive, value.replay_cursor);
        }

        template <typename archive_t>
        void dsp_output_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.primary_sample_count);
            scalar(archive, value.emergency_sample_count);
            scalar(archive, value.primary_output_enabled);
            scalar(archive, value.overflowed);
            scalar(archive, value.emergency_samples);
        }

        template <typename archive_t>
        void apu_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.master_clock);
            scalar(archive, value.smp_clock_credit);
            scalar(archive, value.master_clock_frequency_hz);
            apu_register_fields(archive, value.registers);
            scalar(archive, value.ipl_rom_enabled);
            scalar(archive, value.halted);
            scalar(archive, value.waiting);
            scalar(archive, value.stopped);
            scalar(archive, value.current_opcode_pc);
            scalar(archive, value.last_opcode);
            apu_io_fields(archive, value.io);
            scalar(archive, value.dsp_state);
            scalar(archive, value.dsp_clock_remainder);
            scalar(archive, value.dsp_initialized);
            scalar(archive, value.audio_output.samples);
            dsp_output_fields(archive, value.audio_output.dsp_output);
            apu_timer_fields(archive, value.timer0);
            apu_timer_fields(archive, value.timer1);
            apu_timer_fields(archive, value.timer2);
            scalar(archive, value.apu_to_cpu_ports);
            scalar(archive, value.cpu_to_apu_ports);
            apu_instruction_fields(archive, value.instruction_context);
            scalar(archive, value.smp_suspended_for_cpu);
            scalar(archive, value.cpu_io_window_target_clocks);
            scalar(archive, value.cpu_io_window_consumed_master_numerator);
            scalar(archive, value.ram);
        }

        template <typename archive_t>
        void cpu_register_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.pc);
            scalar(archive, value.sp);
            scalar(archive, value.a);
            scalar(archive, value.x);
            scalar(archive, value.y);
            scalar(archive, value.d);
            scalar(archive, value.p);
            scalar(archive, value.db);
            scalar(archive, value.pb);
            scalar(archive, value.emulation_mode);
        }

        template <typename archive_t>
        void cpu_io_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.auto_joypad_poll);
            scalar(archive, value.hirq_enabled);
            scalar(archive, value.virq_enabled);
            scalar(archive, value.nmi_enabled);
            scalar(archive, value.irq_enabled);
            scalar(archive, value.nmi_flag);
            scalar(archive, value.irq_flag);
            scalar(archive, value.in_hblank);
            scalar(archive, value.in_vblank);
            scalar(archive, value.fast_rom_enabled);
            scalar(archive, value.controller_port_1_latch);
            scalar(archive, value.controller_port_1_shift_count);
            scalar(archive, value.controller_port_2_shift_count);
            scalar(archive, value.nmi_hold_clocks);
            scalar(archive, value.irq_hold_clocks);
            scalar(archive, value.pio);
            scalar(archive, value.multiply_a);
            scalar(archive, value.multiply_b);
            scalar(archive, value.dividend);
            scalar(archive, value.divisor);
            scalar(archive, value.quotient);
            scalar(archive, value.multiply_or_remainder);
            scalar(archive, value.auto_joypad_busy_clocks);
            scalar(archive, value.auto_joypad_latched_1);
            scalar(archive, value.auto_joypad_latched_2);
            scalar(archive, value.joy1);
            scalar(archive, value.joy2);
            scalar(archive, value.joy3);
            scalar(archive, value.joy4);
            scalar(archive, value.htime);
            scalar(archive, value.vtime);
            scalar(archive, value.wram_address);
        }

        template <typename archive_t>
        void cpu_fields(archive_t& archive, auto& value)
        {
            cpu_register_fields(archive, value.registers);
            cpu_io_fields(archive, value.io);
            scalar(archive, value.master_clock);
            scalar(archive, value.dma_counter);
            raster_fields(archive, value.counter);
            scalar(archive, value.interrupt_poll_phase);
            timing_fields(archive, value.last_timing);
            timing_fields(archive, value.last_irq_timing);
            timing_fields(archive, value.last_irq_gate_timing);
            scalar(archive, value.irq_condition_valid);
            scalar(archive, value.nmi_poll_valid);
            scalar(archive, value.dma_active);
            scalar(archive, value.reset_pending);
            scalar(archive, value.waiting);
            scalar(archive, value.wait_wake_idle_pending);
            scalar(archive, value.stopped);
            scalar(archive, value.visible_scanlines);
            video_timing_fields(archive, value.video_timing);
            scalar(archive, value.cpu_version);
            scalar(archive, value.interlace);
            scalar(archive, value.dram_refresh_dot);
            scalar(archive, value.dram_refresh_pending);
            scalar(archive, value.hdma_setup_dot);
            scalar(archive, value.hdma_setup_pending);
            scalar(archive, value.multiply_counter);
            scalar(archive, value.divide_counter);
            scalar(archive, value.math_shift);
            scalar(archive, value.controller_state);
        }

        template <typename archive_t>
        void dma_channel_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.dma_enabled);
            scalar(archive, value.hdma_enabled);
            scalar(archive, value.hdma_active);
            scalar(archive, value.hdma_completed);
            scalar(archive, value.hdma_do_transfer);
            scalar(archive, value.control);
            scalar(archive, value.target_address);
            scalar(archive, value.source_address);
            scalar(archive, value.source_bank);
            scalar(archive, value.indirect_bank);
            scalar(archive, value.indirect_address);
            scalar(archive, value.hdma_table_address);
            scalar(archive, value.line_counter);
            scalar(archive, value.unused);
            scalar(archive, value.transfer_units);
            scalar(archive, value.transfer_size);
        }

        template <typename archive_t>
        void dma_fields(archive_t& archive, auto& value)
        {
            for (auto& channel : value.channels)
                dma_channel_fields(archive, channel);
            scalar(archive, value.pending_general_dma_mask);
            scalar(archive, value.pending_hdma_setup_mask);
            scalar(archive, value.pending_hdma_transfer_mask);
            scalar(archive, value.activity);
            scalar(archive, value.active_channel_index);
            scalar(archive, value.substep);
            scalar(archive, value.alignment_pending);
            scalar(archive, value.general_dma_batch_started);
            scalar(archive, value.cpu_bus_cycle_clocks);
            scalar(archive, value.dma_counter);
            scalar(archive, value.general_dma_units_remaining);
            scalar(archive, value.general_dma_transfer_index);
            scalar(archive, value.hdma_transfer_index);
            scalar(archive, value.hdma_reload_pending);
            scalar(archive, value.general_dma_suspended);
            scalar(archive, value.suspended_general_dma_channel_index);
            scalar(archive, value.suspended_general_dma_substep);
            scalar(archive, value.suspended_general_dma_alignment_pending);
            scalar(archive, value.suspended_general_dma_batch_started);
            scalar(archive, value.suspended_general_dma_units_remaining);
            scalar(archive, value.suspended_general_dma_transfer_index);
        }

        template <typename archive_t>
        void interrupt_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.state.nmi_line);
            scalar(archive, value.state.nmi_hold);
            scalar(archive, value.state.nmi_transition);
            scalar(archive, value.state.irq_line);
            scalar(archive, value.state.irq_hold);
            scalar(archive, value.state.irq_transition);
            scalar(archive, value.state.nmi_pending);
            scalar(archive, value.state.irq_pending);
            scalar(archive, value.state.irq_lock);
            scalar(archive, value.cpu_irq_line);
            scalar(archive, value.cartridge_irq_line);
            scalar(archive, value.nmi_transition_clock);
            scalar(archive, value.irq_transition_clock);
        }

        template <typename archive_t>
        void bus_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.wram);
            scalar(archive, value.entropy_mode);
            scalar(archive, value.entropy_seed_override_enabled);
            scalar(archive, value.entropy_seed);
            scalar(archive, value.entropy_sequence);
            scalar(archive, value.open_bus);
            for (auto& write : value.pending_cpu_writes)
            {
                scalar(archive, write.address);
                scalar(archive, write.value);
            }
            scalar(archive, value.pending_cpu_write_count);
            for (auto& write : value.pending_ppu_writes)
            {
                scalar(archive, write.address);
                scalar(archive, write.value);
                scalar(archive, write.apply_after_clocks);
            }
            scalar(archive, value.pending_ppu_write_count);
            for (auto& write : value.pending_apu_writes)
            {
                scalar(archive, write.address);
                scalar(archive, write.value);
                scalar(archive, write.apply_after_clocks);
            }
            scalar(archive, value.pending_apu_write_count);
            scalar(archive, value.apu_progressed_cpu_clocks);
        }

        template <typename archive_t>
        bool cartridge_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.bootstrap_program_rom);

            uint64_t ram_size{ static_cast<uint64_t>(value.ram_data.size()) };
            scalar(archive, ram_size);
            if constexpr (std::is_same_v<std::remove_cvref_t<archive_t>, reader_t>)
            {
                if (!archive.valid()
                    || ram_size > k_max_cartridge_ram_bytes
                    || ram_size > archive.remaining())
                {
                    return false;
                }
                value.ram_data.resize(static_cast<size_t>(ram_size));
            }
            scalar(archive, value.ram_data);

            uint64_t enhancement_size{
                static_cast<uint64_t>(value.enhancement_state.size())
            };
            scalar(archive, enhancement_size);
            if constexpr (std::is_same_v<std::remove_cvref_t<archive_t>, reader_t>)
            {
                if (!archive.valid()
                    || enhancement_size > k_max_enhancement_state_bytes
                    || enhancement_size > archive.remaining())
                {
                    return false;
                }
                value.enhancement_state.resize(
                    static_cast<size_t>(enhancement_size)
                );
            }
            scalar(archive, value.enhancement_state);

            uint64_t media_size{ static_cast<uint64_t>(value.canonical_media_size) };
            scalar(archive, media_size);
            if constexpr (std::is_same_v<std::remove_cvref_t<archive_t>, reader_t>)
            {
                if (media_size > std::numeric_limits<size_t>::max())
                    return false;
                value.canonical_media_size = static_cast<size_t>(media_size);
            }
            scalar(archive, value.header.mapping_mode);
            scalar(archive, value.header.raw_map_mode);
            scalar(archive, value.header.raw_cartridge_type);
            scalar(archive, value.header.raw_ram_size);
            scalar(archive, value.header.destination_code);
            scalar(archive, value.header.reset_vector);
            scalar(archive, value.mapping_mode);
            scalar(archive, value.hardware);
            scalar(archive, value.loaded);
            scalar(archive, value.ram_persistent);
            scalar(archive, value.ram_dirty);
            return archive.valid();
        }

        template <typename archive_t>
        bool console_fields(archive_t& archive, auto& value)
        {
            scalar(archive, value.powered_on);
            scalar(archive, value.hardware_configuration.model);
            scalar(archive, value.hardware_configuration.region);
            scalar(archive, value.resolved_video_standard);
            scalar(archive, value.scheduler.master_clock);
            scalar(archive, value.scheduler.frame_index);
            bus_fields(archive, value.bus);
            if (!cartridge_fields(archive, value.cartridge))
                return false;
            cpu_fields(archive, value.cpu);
            dma_fields(archive, value.dma);
            interrupt_fields(archive, value.interrupts);
            ppu_fields(archive, value.ppu);
            apu_fields(archive, value.apu);
            return archive.valid();
        }

        [[nodiscard]] uint32_t crc32(std::span<const std::byte> data) noexcept
        {
            uint32_t crc{ 0xffff'ffffu };
            for (const std::byte byte : data)
            {
                crc ^= static_cast<uint8_t>(byte);
                for (uint8_t bit{ 0 }; bit < 8u; ++bit)
                    crc = (crc >> 1u) ^ (0xedb8'8320u & (0u - (crc & 1u)));
            }
            return ~crc;
        }

        template <typename archive_t>
        void schema_fields(archive_t& archive,
                           uint32_t& console,
                           uint32_t& scheduler,
                           uint32_t& bus,
                           uint32_t& cartridge,
                           uint32_t& cpu,
                           uint32_t& dma,
                           uint32_t& interrupts,
                           uint32_t& ppu,
                           uint32_t& apu)
        {
            scalar(archive, console);
            scalar(archive, scheduler);
            scalar(archive, bus);
            scalar(archive, cartridge);
            scalar(archive, cpu);
            scalar(archive, dma);
            scalar(archive, interrupts);
            scalar(archive, ppu);
            scalar(archive, apu);
        }

        struct envelope_metadata_t
        {
            uint16_t format_version{ k_format_version };
            uint16_t header_size{ k_header_size };
            uint32_t system{ k_system_snes };
            uint32_t core_state_version{ k_core_state_version };
            uint64_t media_length{ 0 };
            media_digest_t media_hash{};
            uint8_t hardware_model{ 0 };
            uint8_t requested_region{ 0 };
            uint8_t resolved_video_standard{ 0 };
            uint8_t mapper{ 0 };
            uint8_t cartridge_hardware{ 0 };
            uint8_t reserved0{ 0 };
            uint16_t reserved1{ 0 };
            uint64_t mutable_memory_size{ 0 };
            uint32_t console_schema{ core::console_causal_state_t::schema_version };
            uint32_t scheduler_schema{ core::scheduler_causal_state_t::schema_version };
            uint32_t bus_schema{ core::bus_causal_state_t::schema_version };
            uint32_t cartridge_schema{ core::cartridge_causal_state_t::schema_version };
            uint32_t cpu_schema{ core::cpu_causal_state_t::schema_version };
            uint32_t dma_schema{ core::dma_causal_state_t::schema_version };
            uint32_t interrupts_schema{
                core::interrupt_controller_causal_state_t::schema_version
            };
            uint32_t ppu_schema{ core::ppu_causal_state_t::schema_version };
            uint32_t apu_schema{ core::apu_causal_state_t::schema_version };
            uint64_t payload_length{ 0 };
            uint32_t payload_checksum{ 0 };
            uint32_t reserved2{ 0 };
        };

        template <typename archive_t>
        void metadata_fields(archive_t& archive, envelope_metadata_t& metadata)
        {
            scalar(archive, metadata.format_version);
            scalar(archive, metadata.header_size);
            scalar(archive, metadata.system);
            scalar(archive, metadata.core_state_version);
            scalar(archive, metadata.media_length);
            scalar(archive, metadata.media_hash);
            scalar(archive, metadata.hardware_model);
            scalar(archive, metadata.requested_region);
            scalar(archive, metadata.resolved_video_standard);
            scalar(archive, metadata.mapper);
            scalar(archive, metadata.cartridge_hardware);
            scalar(archive, metadata.reserved0);
            scalar(archive, metadata.reserved1);
            scalar(archive, metadata.mutable_memory_size);
            schema_fields(archive,
                          metadata.console_schema,
                          metadata.scheduler_schema,
                          metadata.bus_schema,
                          metadata.cartridge_schema,
                          metadata.cpu_schema,
                          metadata.dma_schema,
                          metadata.interrupts_schema,
                          metadata.ppu_schema,
                          metadata.apu_schema);
            scalar(archive, metadata.payload_length);
            scalar(archive, metadata.payload_checksum);
            scalar(archive, metadata.reserved2);
        }

        [[nodiscard]] bool schemas_supported(const envelope_metadata_t& metadata) noexcept
        {
            return metadata.console_schema == core::console_causal_state_t::schema_version
                && metadata.scheduler_schema == core::scheduler_causal_state_t::schema_version
                && metadata.bus_schema == core::bus_causal_state_t::schema_version
                && metadata.cartridge_schema == core::cartridge_causal_state_t::schema_version
                && metadata.cpu_schema == core::cpu_causal_state_t::schema_version
                && metadata.dma_schema == core::dma_causal_state_t::schema_version
                && metadata.interrupts_schema
                    == core::interrupt_controller_causal_state_t::schema_version
                && metadata.ppu_schema == core::ppu_causal_state_t::schema_version
                && metadata.apu_schema == core::apu_causal_state_t::schema_version;
        }

        [[nodiscard]] checkpoint_result_t map_capture_result(
            core::console_checkpoint_result_t result
        ) noexcept
        {
            return result == core::console_checkpoint_result_t::allocation_failed
                ? checkpoint_result_t::allocation_failed
                : checkpoint_result_t::capture_failed;
        }
    }

    checkpoint_result_t capture_snes_checkpoint(
        core::console_t& console,
        std::vector<std::byte>& checkpoint
    ) noexcept
    {
        try
        {
            auto state{ std::make_unique<core::console_causal_state_t>() };
            const core::console_checkpoint_result_t captured{
                console.capture_causal_state(*state)
            };
            if (captured != core::console_checkpoint_result_t::success)
                return map_capture_result(captured);

            std::vector<std::byte> payload{};
            payload.reserve(5u * 1024u * 1024u);
            writer_t payload_writer{ payload };
            if (!console_fields(payload_writer, *state))
                return checkpoint_result_t::capture_failed;

            const std::span<const std::byte> media{ console.canonical_media() };
            envelope_metadata_t metadata{};
            metadata.media_length = media.size();
            metadata.media_hash = media_sha256(media);
            metadata.hardware_model = static_cast<uint8_t>(
                state->hardware_configuration.model
            );
            metadata.requested_region = static_cast<uint8_t>(
                state->hardware_configuration.region
            );
            metadata.resolved_video_standard = static_cast<uint8_t>(
                state->resolved_video_standard
            );
            metadata.mapper = static_cast<uint8_t>(state->cartridge.mapping_mode);
            metadata.cartridge_hardware = static_cast<uint8_t>(state->cartridge.hardware);
            metadata.mutable_memory_size = state->cartridge.ram_data.size();
            metadata.payload_length = payload.size();
            metadata.payload_checksum = crc32(payload);

            std::vector<std::byte> encoded{};
            encoded.reserve(k_header_size + payload.size());
            encoded.insert(encoded.end(), k_magic.begin(), k_magic.end());
            writer_t header_writer{ encoded };
            metadata_fields(header_writer, metadata);
            if (encoded.size() != k_header_size)
                return checkpoint_result_t::capture_failed;
            encoded.insert(encoded.end(), payload.begin(), payload.end());
            checkpoint = std::move(encoded);
            return checkpoint_result_t::success;
        }
        catch (...)
        {
            return checkpoint_result_t::allocation_failed;
        }
    }

    checkpoint_result_t restore_snes_checkpoint(
        core::console_t& console,
        std::span<const std::byte> checkpoint,
        checkpoint_decode_limits_t limits
    ) noexcept
    {
        if (checkpoint.size() < k_magic.size())
            return checkpoint_result_t::truncated;
        if (!std::equal(k_magic.begin(), k_magic.end(), checkpoint.begin()))
            return checkpoint_result_t::invalid_magic;
        if (checkpoint.size() < k_header_size)
            return checkpoint_result_t::truncated;

        envelope_metadata_t metadata{};
        reader_t header_reader{ checkpoint.subspan(k_magic.size(), k_header_size - k_magic.size()) };
        metadata_fields(header_reader, metadata);
        if (!header_reader.valid() || header_reader.remaining() != 0u)
            return checkpoint_result_t::truncated;
        if (metadata.format_version != k_format_version
            || metadata.header_size != k_header_size)
        {
            return checkpoint_result_t::unsupported_format_version;
        }
        if (metadata.system != k_system_snes)
            return checkpoint_result_t::unsupported_system;
        if (metadata.core_state_version != k_core_state_version)
            return checkpoint_result_t::unsupported_core_state_version;
        if (!schemas_supported(metadata))
            return checkpoint_result_t::unsupported_subsystem_version;
        if (metadata.payload_length > limits.max_payload_bytes
            || metadata.payload_length > std::numeric_limits<size_t>::max())
        {
            return checkpoint_result_t::payload_too_large;
        }
        if (metadata.payload_length > checkpoint.size() - k_header_size)
            return checkpoint_result_t::truncated;
        if (metadata.payload_length < checkpoint.size() - k_header_size)
            return checkpoint_result_t::trailing_data;

        const std::span<const std::byte> payload{ checkpoint.subspan(k_header_size) };
        if (crc32(payload) != metadata.payload_checksum)
            return checkpoint_result_t::checksum_mismatch;

        const std::span<const std::byte> media{ console.canonical_media() };
        if (metadata.media_length != media.size()
            || metadata.media_hash != media_sha256(media))
        {
            return checkpoint_result_t::media_mismatch;
        }

        try
        {
            auto state{ std::make_unique<core::console_causal_state_t>() };
            reader_t payload_reader{ payload };
            if (!console_fields(payload_reader, *state)
                || !payload_reader.valid()
                || payload_reader.remaining() != 0u)
            {
                return checkpoint_result_t::malformed_payload;
            }

            if (metadata.hardware_model
                    != static_cast<uint8_t>(state->hardware_configuration.model)
                || metadata.requested_region
                    != static_cast<uint8_t>(state->hardware_configuration.region)
                || metadata.resolved_video_standard
                    != static_cast<uint8_t>(state->resolved_video_standard)
                || metadata.mapper != static_cast<uint8_t>(state->cartridge.mapping_mode)
                || metadata.cartridge_hardware
                    != static_cast<uint8_t>(state->cartridge.hardware)
                || metadata.mutable_memory_size != state->cartridge.ram_data.size())
            {
                return checkpoint_result_t::hardware_mismatch;
            }

            const core::console_checkpoint_result_t restored{
                console.restore_causal_state(*state)
            };
            return restored == core::console_checkpoint_result_t::success
                ? checkpoint_result_t::success
                : checkpoint_result_t::core_restore_failed;
        }
        catch (...)
        {
            return checkpoint_result_t::allocation_failed;
        }
    }
}
