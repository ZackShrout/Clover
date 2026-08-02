//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/frontend/SnesCheckpoint.h"

#include <algorithm>
#include <new>

namespace clover::frontend
{
    namespace
    {
        [[nodiscard]] execution_boundary_t execution_boundary(
            core::cpu_step_boundary_t boundary
        ) noexcept
        {
            switch (boundary)
            {
            case core::cpu_step_boundary_t::instruction_retired:
                return execution_boundary_t::instruction;
            case core::cpu_step_boundary_t::reset_completed:
                return execution_boundary_t::reset;
            case core::cpu_step_boundary_t::interrupt_entered:
                return execution_boundary_t::interrupt;
            case core::cpu_step_boundary_t::waiting:
                return execution_boundary_t::waiting;
            case core::cpu_step_boundary_t::stopped:
                return execution_boundary_t::stopped;
            case core::cpu_step_boundary_t::none:
                return execution_boundary_t::none;
            }
            return execution_boundary_t::none;
        }
    }

    uint16_t snes_joypad_state(const gamepad_state_t& state) noexcept
    {
        uint16_t result{ 0 };
        const auto map = [&result, &state](gamepad_button_t button, uint8_t bit) noexcept
        {
            if (state.pressed(button))
                result |= static_cast<uint16_t>(1u << bit);
        };

        map(gamepad_button_t::face_south, 15u); // B
        map(gamepad_button_t::face_west, 14u);  // Y
        map(gamepad_button_t::back, 13u);
        map(gamepad_button_t::start, 12u);
        map(gamepad_button_t::dpad_up, 11u);
        map(gamepad_button_t::dpad_down, 10u);
        map(gamepad_button_t::dpad_left, 9u);
        map(gamepad_button_t::dpad_right, 8u);
        map(gamepad_button_t::face_east, 7u);   // A
        map(gamepad_button_t::face_north, 6u);  // X
        map(gamepad_button_t::left_shoulder, 5u);
        map(gamepad_button_t::right_shoulder, 4u);
        return result;
    }

    system_id_t snes_emulator_core_t::system() const noexcept
    {
        return system_id_t::snes;
    }

    bool snes_emulator_core_t::load_media(std::span<const std::byte> media) noexcept
    {
        if (!_console.load_cartridge(media))
            return false;

        _address_spaces[2].size_bytes = _console.canonical_media().size();
        return true;
    }

    void snes_emulator_core_t::power_on() noexcept
    {
        _console.power_on();
        _console.set_presentation_layer_mask(_visible_layer_mask);
#if defined(CLOVER_WORKBENCH_PPU_LAYER_CAPTURE)
        _console.set_raw_background_capture_enabled(true);
        // Workbench advances the machine through instruction-domain stepping,
        // so run_frame() does not bracket PPU composition for it.  Its
        // specialized core keeps composition enabled continuously; the normal
        // Player build contains neither this definition nor this branch.
        _console.set_frame_capture_enabled(true);
#endif
        _machine_running = true;
        _debug_paused = false;
    }

    void snes_emulator_core_t::reset() noexcept
    {
        _console.reset();
        _console.set_presentation_layer_mask(_visible_layer_mask);
#if defined(CLOVER_WORKBENCH_PPU_LAYER_CAPTURE)
        _console.set_raw_background_capture_enabled(true);
        _console.set_frame_capture_enabled(true);
#endif
    }

    void snes_emulator_core_t::set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept
    {
        if (port < 2u)
            _console.set_controller_state(static_cast<uint8_t>(port), snes_joypad_state(state));
    }

    void snes_emulator_core_t::run_frame() noexcept
    {
        if (_debug_paused)
            return;

        _console.run_frame();
        if (_visible_layer_mask != core::ppu_presentation_options_t::k_all_layers_visible)
        {
            _console.refresh_framebuffer({
                .visible_layer_mask = _visible_layer_mask
            });
        }
    }

    void snes_emulator_core_t::refresh_video_frame() noexcept
    {
        _console.refresh_framebuffer({
            .visible_layer_mask = _visible_layer_mask
        });
    }

    display_info_t snes_emulator_core_t::display_info() const noexcept
    {
        const core::video_timing_t& timing{ _console.video_timing() };
        const double refresh_hz{
            static_cast<double>(core::master_clock_frequency_hz(timing.standard))
                / static_cast<double>(timing.master_clocks_per_frame())
        };
        return {
            .framebuffer_width = core::framebuffer_t::k_max_width,
            .framebuffer_height = core::framebuffer_t::k_max_height,
            .pixel_aspect_ratio = timing.standard == core::video_standard_t::pal
                ? 55.f / 43.f
                : 8.f / 7.f,
            .nominal_refresh_hz = refresh_hz
        };
    }

    bool snes_emulator_core_t::set_hardware_configuration(
        core::snes_hardware_configuration_t configuration
    ) noexcept
    {
        return _console.set_hardware_configuration(configuration);
    }

    core::snes_hardware_identity_t snes_emulator_core_t::hardware_identity() const noexcept
    {
        return _console.hardware_identity();
    }

    video_frame_view_t snes_emulator_core_t::video_frame() const noexcept
    {
        return {
            .pixels = _console.framebuffer().data(),
            .width = _console.framebuffer().width(),
            .height = _console.framebuffer().height(),
            .pitch_bytes = _console.framebuffer().pitch_pixels() * sizeof(uint32_t),
            .format = pixel_format_t::argb8888
        };
    }

    audio_frame_view_t snes_emulator_core_t::audio_frame() const noexcept
    {
        return {
            .interleaved_samples = _console.audio_samples(),
            .sample_rate_hz = core::apu_t::k_audio_sample_rate_hz,
            .channels = 2u,
            .discontinuity = _console.audio_output_overflowed()
        };
    }

    std::span<const std::byte> snes_emulator_core_t::persistent_memory() const noexcept
    {
        return _console.persistent_memory();
    }

    bool snes_emulator_core_t::load_persistent_memory(std::span<const std::byte> data) noexcept
    {
        return _console.load_persistent_memory(data);
    }

    bool snes_emulator_core_t::persistent_memory_dirty() const noexcept
    {
        return _console.persistent_memory_dirty();
    }

    void snes_emulator_core_t::mark_persistent_memory_clean() noexcept
    {
        _console.mark_persistent_memory_clean();
    }

    video_plane_control_t* snes_emulator_core_t::video_plane_control() noexcept
    {
        return this;
    }

    debug_target_t* snes_emulator_core_t::debug_target() noexcept
    {
        return this;
    }

    std::span<const video_plane_descriptor_t> snes_emulator_core_t::video_planes() const noexcept
    {
        return _video_planes;
    }

    bool snes_emulator_core_t::set_video_plane_enabled(video_plane_id_t id, bool enabled) noexcept
    {
        if (id >= _video_planes.size())
            return false;
        const uint8_t bit{ static_cast<uint8_t>(1u << id) };
        _visible_layer_mask = enabled
            ? static_cast<uint8_t>(_visible_layer_mask | bit)
            : static_cast<uint8_t>(_visible_layer_mask & ~bit);
        _video_planes[id].enabled = enabled;
        _console.set_presentation_layer_mask(_visible_layer_mask);
        return true;
    }

    bool snes_emulator_core_t::inspect_video_plane_frame(
        video_plane_id_t id,
        video_plane_frame_view_t& destination
    ) const noexcept
    {
#if defined(CLOVER_WORKBENCH_PPU_LAYER_CAPTURE)
        const core::framebuffer_t* frame{};
        uint64_t frame_index{};
        if (id >= 4u
            || !_console.raw_background_frame(
                static_cast<uint8_t>(id), frame, frame_index
            )
            || frame == nullptr)
        {
            destination = {};
            return false;
        }
        destination = {
            .pixels = frame->data(),
            .width = frame->width(),
            .height = frame->height(),
            .pitch_bytes = frame->pitch_pixels() * sizeof(uint32_t),
            .format = pixel_format_t::argb8888,
            .frame_index = frame_index
        };
        return true;
#else
        static_cast<void>(id);
        destination = {};
        return false;
#endif
    }

#if defined(CLOVER_WORKBENCH_DIAGNOSTICS)
    size_t snes_emulator_core_t::inspect_tile_layers(
        std::span<snes::tile_layer_state_t> destination
    ) const noexcept
    {
        static constexpr std::array<std::string_view, 4> labels{
            "BG1", "BG2", "BG3", "BG4"
        };
        const core::ppu_render_state_snapshot_t snapshot{
            _console.ppu_render_state()
        };
        const size_t count{ std::min(destination.size(), labels.size()) };
        for (size_t index{}; index < count; ++index)
        {
            const core::ppu_background_render_state_t& background{
                snapshot.backgrounds[index]
            };
            snes::tile_layer_format_t format{ snes::tile_layer_format_t::inactive };
            switch (background.mode)
            {
            case core::ppu_background_render_state_t::mode_t::bpp2:
                format = snes::tile_layer_format_t::indexed_2bpp;
                break;
            case core::ppu_background_render_state_t::mode_t::bpp4:
                format = snes::tile_layer_format_t::indexed_4bpp;
                break;
            case core::ppu_background_render_state_t::mode_t::bpp8:
                format = snes::tile_layer_format_t::indexed_8bpp;
                break;
            case core::ppu_background_render_state_t::mode_t::mode7:
                format = snes::tile_layer_format_t::affine_mode7;
                break;
            case core::ppu_background_render_state_t::mode_t::inactive:
                format = snes::tile_layer_format_t::inactive;
                break;
            }
            destination[index] = {
                .id = static_cast<uint32_t>(index + 1u),
                .label = labels[index],
                .active = background.active
                    && (background.above_enabled
                        || background.below_enabled),
                .tile_map = {
                    snes_debug::k_vram_space,
                    static_cast<uint64_t>(background.screen_address) * 2u
                },
                .tile_graphics = {
                    snes_debug::k_vram_space,
                    static_cast<uint64_t>(background.tiledata_address) * 2u
                },
                .width_tiles = static_cast<uint16_t>(
                    32u << (background.screen_size & 0x01u)
                ),
                .height_tiles = static_cast<uint16_t>(
                    32u << ((background.screen_size >> 1u) & 0x01u)
                ),
                .format = format,
                .screen_size = background.screen_size,
                .tile_size = static_cast<uint8_t>(
                    background.large_tiles ? 16u : 8u
                ),
                .palette_base = static_cast<uint16_t>(
                    snapshot.bg_mode == 0u ? index * 32u : 0u
                ),
                .horizontal_scroll = background.hoffset,
                .vertical_scroll = background.voffset
            };
        }
        return count;
    }

    bool snes_emulator_core_t::inspect_object_layer(
        snes::object_layer_state_t& destination
    ) const noexcept
    {
        if (!_machine_running)
            return false;
        const core::ppu_object_render_state_t& objects{
            _console.ppu_render_state().objects
        };
        destination = {
            .active = objects.above_enabled || objects.below_enabled,
            .oam = { snes_debug::k_oam_space, 0u },
            .tile_graphics = {
                snes_debug::k_vram_space,
                static_cast<uint64_t>(objects.tiledata_address) * 2u
            },
            .palette = { snes_debug::k_cgram_space, 256u },
            .tile_base_word_address = objects.tiledata_address,
            .name_select = objects.nameselect,
            .base_size = objects.base_size,
            .first_sprite = objects.first_sprite,
            .interlace = objects.interlace,
            .range_over = objects.range_over,
            .time_over = objects.time_over
        };
        return true;
    }

    snes::dma_transfer_inspection_result_t snes_emulator_core_t::inspect_dma_transfers(
        std::span<snes::dma_transfer_record_t> destination
    ) const noexcept
    {
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
        std::array<core::dma_provenance_record_t, 512u> records{};
        const core::dma_provenance_snapshot_t snapshot{
            _console.copy_dma_provenance_records(records)
        };
        const size_t count{ std::min(destination.size(), snapshot.record_count) };
        const size_t source_start{ snapshot.record_count - count };
        for (size_t index{}; index < count; ++index)
        {
            const core::dma_provenance_record_t& source{
                records[source_start + index]
            };
            destination[index] = {
                .sequence = source.sequence,
                .first_master_clock = source.first_master_clock,
                .last_master_clock = source.last_master_clock,
                .frame_index = source.frame_index,
                .initiator_address = source.initiator_address,
                .first_a_bus_address = source.first_a_bus_address,
                .last_a_bus_address = source.last_a_bus_address,
                .byte_count = source.byte_count,
                .first_scanline = source.first_timing.raster.scanline,
                .first_dot = source.first_timing.raster.dot,
                .last_scanline = source.last_timing.raster.scanline,
                .last_dot = source.last_timing.raster.dot,
                .channel = source.channel,
                .channel_mask = source.channel_mask,
                .control = source.control,
                .b_bus_base = source.b_bus_base,
                .b_bus_offset_mask = source.b_bus_offset_mask,
                .first_value = source.first_value,
                .last_value = source.last_value,
                .kind = source.activity == core::dma_activity_t::general_dma
                    ? snes::dma_transfer_kind_t::general
                    : snes::dma_transfer_kind_t::horizontal_blank,
                .direction_to_b_bus = source.direction_to_b_bus,
                .b_bus_access_valid = source.b_bus_access_valid
            };
        }
        return {
            .record_count = count,
            .records_dropped = snapshot.records_dropped
        };
#else
        static_cast<void>(destination);
        return {};
#endif
    }

    void snes_emulator_core_t::clear_dma_transfers() noexcept
    {
#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
        _console.clear_dma_provenance_records();
#endif
    }
#endif

    std::span<const execution_domain_descriptor_t>
        snes_emulator_core_t::execution_domains() const noexcept
    {
        return _execution_domains;
    }

    std::span<const address_space_descriptor_t>
        snes_emulator_core_t::address_spaces() const noexcept
    {
        return _address_spaces;
    }

    memory_inspection_result_t snes_emulator_core_t::inspect_memory(
        debug_address_t address,
        std::span<std::byte> destination
    ) const noexcept
    {
        if (destination.empty())
        {
            return {
                .status = memory_inspection_status_t::complete
            };
        }

        const auto in_range = [address, destination](uint64_t size) noexcept
        {
            return address.value < size
                && destination.size() <= size - address.value;
        };

        if (address.space == snes_debug::k_wram_space)
        {
            if (!in_range(core::bus_t::k_wram_size))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            const std::span<const uint8_t> bytes{
                _console.wram_span(
                    static_cast<uint32_t>(address.value),
                    static_cast<uint32_t>(destination.size())
                )
            };
            for (size_t index{ 0 }; index < bytes.size(); ++index)
                destination[index] = static_cast<std::byte>(bytes[index]);
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_canonical_media_space)
        {
            const std::span<const std::byte> media{ _console.canonical_media() };
            if (!in_range(media.size()))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
                destination[index] = media[static_cast<size_t>(address.value) + index];
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_apu_ram_space)
        {
            if (!in_range(0x00010000u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
            {
                destination[index] = static_cast<std::byte>(
                    _console.apu_peek_ram(
                        static_cast<uint16_t>(address.value + index)
                    )
                );
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_cgram_space)
        {
            if (!in_range(512u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }
            const auto& cgram{ _console.ppu_cgram() };
            for (size_t index{}; index < destination.size(); ++index)
            {
                const uint64_t byte_address{ address.value + index };
                const uint16_t color{
                    cgram[static_cast<size_t>(byte_address / 2u)]
                };
                destination[index] = static_cast<std::byte>(
                    (byte_address & 1u) == 0u
                        ? color & 0x00ffu
                        : (color >> 8u) & 0x007fu
                );
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_vram_space)
        {
            if (!in_range(65536u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }
            const auto& vram{ _console.ppu_vram() };
            for (size_t index{}; index < destination.size(); ++index)
            {
                const uint64_t byte_address{ address.value + index };
                const uint16_t word{
                    vram[static_cast<size_t>(byte_address / 2u)]
                };
                destination[index] = static_cast<std::byte>(
                    (byte_address & 1u) == 0u
                        ? word & 0x00ffu
                        : word >> 8u
                );
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_oam_space)
        {
            if (!in_range(544u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }
            const auto& oam{ _console.ppu_oam() };
            for (size_t index{}; index < destination.size(); ++index)
            {
                destination[index] = static_cast<std::byte>(
                    oam[static_cast<size_t>(address.value) + index]
                );
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        if (address.space == snes_debug::k_cpu_bus_space)
        {
            if (!in_range(0x01000000u))
            {
                return {
                    .status = memory_inspection_status_t::out_of_range
                };
            }

            for (size_t index{ 0 }; index < destination.size(); ++index)
            {
                uint8_t value{};
                if (!_console.inspect_u8(
                    static_cast<uint32_t>(address.value + index),
                    value
                ))
                {
                    return {
                        .status = memory_inspection_status_t::unavailable,
                        .bytes_read = index
                    };
                }
                destination[index] = static_cast<std::byte>(value);
            }
            return {
                .status = memory_inspection_status_t::complete,
                .bytes_read = destination.size()
            };
        }

        return {
            .status = memory_inspection_status_t::invalid_address_space
        };
    }

    address_translation_result_t snes_emulator_core_t::translate_address(
        debug_address_t source,
        address_space_id_t destination_space
    ) const noexcept
    {
        const auto valid_space = [this](address_space_id_t id) noexcept
        {
            for (const address_space_descriptor_t& descriptor : _address_spaces)
            {
                if (descriptor.id == id)
                    return true;
            }
            return false;
        };
        if (!valid_space(source.space) || !valid_space(destination_space))
        {
            return {
                .status = address_translation_status_t::invalid_address_space
            };
        }

        if (source.space != snes_debug::k_cpu_bus_space
            || destination_space != snes_debug::k_canonical_media_space)
        {
            return {
                .status = address_translation_status_t::unsupported
            };
        }
        if (source.value >= 0x01000000u)
        {
            return {
                .status = address_translation_status_t::unmapped
            };
        }

        const core::cartridge_address_mapping_t mapping{
            _console.translate_cartridge_address(static_cast<uint32_t>(source.value))
        };
        if (mapping.kind != core::cartridge_address_kind_t::program_rom)
        {
            return {
                .status = address_translation_status_t::unmapped
            };
        }

        return {
            .status = address_translation_status_t::complete,
            .address = {
                .space = snes_debug::k_canonical_media_space,
                .value = mapping.storage_offset
            }
        };
    }

    std::span<const processor_register_descriptor_t>
        snes_emulator_core_t::processor_registers(
            execution_domain_id_t domain
        ) const noexcept
    {
        if (domain == snes_debug::k_main_cpu_domain)
            return _main_cpu_registers;
        return {};
    }

    processor_state_result_t snes_emulator_core_t::inspect_processor_state(
        execution_domain_id_t domain,
        std::span<processor_register_value_t> destination
    ) const noexcept
    {
        if (domain != snes_debug::k_main_cpu_domain)
        {
            return {
                .status = domain == snes_debug::k_audio_cpu_domain
                    ? processor_state_status_t::unsupported
                    : processor_state_status_t::invalid_domain,
                .domain = domain
            };
        }
        if (!_machine_running)
        {
            return {
                .status = processor_state_status_t::not_running,
                .domain = domain
            };
        }
        if (destination.size() < _main_cpu_registers.size())
        {
            return {
                .status = processor_state_status_t::insufficient_storage,
                .domain = domain
            };
        }

        const core::cpu_state_t& cpu{ _console.cpu_state() };
        const std::array<uint64_t, 10> values{
            cpu.pc,
            cpu.sp,
            cpu.a,
            cpu.x,
            cpu.y,
            cpu.d,
            cpu.p,
            cpu.db,
            cpu.pb,
            cpu.emulation_mode ? 1u : 0u
        };
        for (size_t index{ 0 }; index < values.size(); ++index)
            destination[index].value = values[index];
        return {
            .status = processor_state_status_t::complete,
            .domain = domain,
            .instruction_address = {
                .space = snes_debug::k_cpu_bus_space,
                .value = (static_cast<uint32_t>(cpu.pb) << 16u) | cpu.pc
            },
            .registers_written = values.size()
        };
    }

    execution_control_t* snes_emulator_core_t::execution_control() noexcept
    {
        return this;
    }

    execution_step_result_t snes_emulator_core_t::step_execution_domain(
        execution_domain_id_t domain
    ) noexcept
    {
        if (domain != snes_debug::k_main_cpu_domain)
        {
            const execution_step_status_t status{
                domain == snes_debug::k_audio_cpu_domain
                    ? execution_step_status_t::unsupported
                    : execution_step_status_t::invalid_domain
            };
            return {
                .status = status,
                .domain = domain
            };
        }

        if (!_machine_running)
        {
            return {
                .status = execution_step_status_t::not_running,
                .domain = domain
            };
        }
        if (!_debug_paused)
        {
            return {
                .status = execution_step_status_t::not_paused,
                .domain = domain
            };
        }

        const core::cpu_boundary_step_result_t result{ _console.step_cpu_boundary() };
        if (result.status != core::cpu_boundary_step_status_t::complete)
        {
            return {
                .status = execution_step_status_t::not_running,
                .domain = domain
            };
        }

        return {
            .status = execution_step_status_t::complete,
            .domain = domain,
            .boundary = execution_boundary(result.boundary),
            .machine_clocks_elapsed = result.elapsed_master_clocks
        };
    }

    execution_run_result_t snes_emulator_core_t::run_execution_domain(
        execution_domain_id_t domain,
        size_t instruction_budget,
        std::span<const execution_breakpoint_t> breakpoints,
        std::span<const execution_watchpoint_t> watchpoints
    ) noexcept
    {
        if (domain != snes_debug::k_main_cpu_domain)
        {
            return {
                .status = domain == snes_debug::k_audio_cpu_domain
                    ? execution_step_status_t::unsupported
                    : execution_step_status_t::invalid_domain,
                .domain = domain
            };
        }
        if (!_machine_running)
        {
            return {
                .status = execution_step_status_t::not_running,
                .domain = domain
            };
        }
        if (!_debug_paused)
        {
            return {
                .status = execution_step_status_t::not_paused,
                .domain = domain
            };
        }
        if (!watchpoints.empty() && !_observation_storage)
        {
            return {
                .status = execution_step_status_t::unsupported,
                .domain = domain
            };
        }

        const auto cpu_address = [this]() noexcept
        {
            const core::cpu_state_t& cpu{ _console.cpu_state() };
            return debug_address_t{
                .space = snes_debug::k_cpu_bus_space,
                .value = (static_cast<uint32_t>(cpu.pb) << 16u) | cpu.pc
            };
        };
        const auto restore_observations = [this]() noexcept
        {
            if (_observation_mask == 0u)
            {
                _console.set_observation_sink(nullptr);
                return;
            }
            core::snes_observation_mask_t core_mask{};
            if ((_observation_mask & k_observe_execution_boundary) != 0u)
                core_mask |= core::k_snes_observe_cpu_boundary;
            if ((_observation_mask & k_observe_memory_access) != 0u)
                core_mask |= core::k_snes_observe_cpu_memory_access;
            _observation_sink.configure(
                { _observation_storage.get(), k_observation_capacity },
                core_mask
            );
            _console.set_observation_sink(&_observation_sink);
        };

        _observation_sink.clear();
        if (watchpoints.empty())
        {
            _console.set_observation_sink(nullptr);
        }
        else
        {
            _observation_sink.configure(
                { _observation_storage.get(), k_observation_capacity },
                core::k_snes_observe_cpu_memory_access
            );
            _console.set_observation_sink(&_observation_sink);
        }

        execution_run_result_t result{
            .status = execution_step_status_t::complete,
            .domain = domain,
            .stop = execution_run_stop_t::budget_exhausted,
            .instruction_address = cpu_address()
        };
        for (; result.instructions_executed < instruction_budget;
             ++result.instructions_executed)
        {
            const debug_address_t before{ cpu_address() };
            for (size_t index{ 0 }; index < breakpoints.size(); ++index)
            {
                if (breakpoints[index].address.space == before.space
                    && breakpoints[index].address.value == before.value)
                {
                    result.stop = execution_run_stop_t::breakpoint;
                    result.trap_index = index;
                    result.instruction_address = before;
                    restore_observations();
                    return result;
                }
            }

            _observation_sink.clear();
            const core::cpu_boundary_step_result_t step{
                _console.step_cpu_boundary()
            };
            if (step.status != core::cpu_boundary_step_status_t::complete)
            {
                result.status = execution_step_status_t::not_running;
                result.instruction_address = cpu_address();
                restore_observations();
                return result;
            }
            result.machine_clocks_elapsed += step.elapsed_master_clocks;
            result.instruction_address = cpu_address();

            if (!watchpoints.empty())
            {
                for (const core::snes_observation_event_t& event
                     : _observation_sink.events())
                {
                    if (event.kind
                        != core::snes_observation_kind_t::cpu_memory_access)
                    {
                        continue;
                    }
                    const bool write{
                        event.cpu_memory_access.kind
                            == core::snes_memory_access_kind_t::write
                    };
                    for (size_t index{ 0 }; index < watchpoints.size(); ++index)
                    {
                        const execution_watchpoint_t& watchpoint{
                            watchpoints[index]
                        };
                        const uint64_t address{
                            event.cpu_memory_access.address
                        };
                        if (watchpoint.start.space
                                != snes_debug::k_cpu_bus_space
                            || (write ? !watchpoint.write : !watchpoint.read)
                            || address < watchpoint.start.value
                            || address - watchpoint.start.value
                                >= watchpoint.length)
                        {
                            continue;
                        }
                        ++result.instructions_executed;
                        result.stop = execution_run_stop_t::watchpoint;
                        result.trap_index = index;
                        result.access_address = {
                            .space = snes_debug::k_cpu_bus_space,
                            .value = address
                        };
                        result.access_instruction_address = {
                            .space = snes_debug::k_cpu_bus_space,
                            .value = event.cpu_memory_access.instruction_address
                        };
                        result.access_was_write = write;
                        result.access_value = event.cpu_memory_access.value;
                        restore_observations();
                        return result;
                    }
                }
            }
            if (step.boundary == core::cpu_step_boundary_t::waiting)
            {
                ++result.instructions_executed;
                result.stop = execution_run_stop_t::waiting;
                restore_observations();
                return result;
            }
            if (step.boundary == core::cpu_step_boundary_t::stopped)
            {
                ++result.instructions_executed;
                result.stop = execution_run_stop_t::stopped;
                restore_observations();
                return result;
            }
        }
        result.instruction_address = cpu_address();
        restore_observations();
        return result;
    }

    observation_control_t* snes_emulator_core_t::observation_control() noexcept
    {
        return this;
    }

    observation_mask_t snes_emulator_core_t::available_observations() const noexcept
    {
        return k_observe_execution_boundary | k_observe_memory_access;
    }

    observation_mask_t snes_emulator_core_t::observation_mask() const noexcept
    {
        return _observation_mask;
    }

    bool snes_emulator_core_t::set_observation_mask(observation_mask_t mask) noexcept
    {
        if ((mask & ~available_observations()) != 0u)
            return false;
        if (mask == _observation_mask)
            return true;

        if (mask == 0u)
        {
            _console.set_observation_sink(nullptr);
            _observation_sink.disable();
            _observation_storage.reset();
            _observation_mask = 0u;
            return true;
        }

        if (!_observation_storage)
        {
            _observation_storage.reset(
                new (std::nothrow) core::snes_observation_event_t[k_observation_capacity]
            );
            if (!_observation_storage)
                return false;
        }

        core::snes_observation_mask_t core_mask{};
        if ((mask & k_observe_execution_boundary) != 0u)
            core_mask |= core::k_snes_observe_cpu_boundary;
        if ((mask & k_observe_memory_access) != 0u)
            core_mask |= core::k_snes_observe_cpu_memory_access;
        _observation_sink.configure(
            { _observation_storage.get(), k_observation_capacity },
            core_mask
        );
        _console.set_observation_sink(&_observation_sink);
        _observation_mask = mask;
        return true;
    }

    observation_drain_result_t snes_emulator_core_t::drain_observations(
        std::span<observation_event_t> destination
    ) noexcept
    {
        const std::span<const core::snes_observation_event_t> source{
            _observation_sink.events()
        };
        const size_t count{ std::min(source.size(), destination.size()) };
        for (size_t index{ 0 }; index < count; ++index)
        {
            const core::snes_observation_event_t& event{ source[index] };
            observation_event_t translated{
                .kind = event.kind == core::snes_observation_kind_t::cpu_boundary
                    ? observation_kind_t::execution_boundary
                    : observation_kind_t::memory_access,
                .domain = snes_debug::k_main_cpu_domain,
                .machine_clock = event.master_clock,
                .frame_index = event.frame_index
            };
            if (event.kind == core::snes_observation_kind_t::cpu_boundary)
            {
                translated.execution_boundary = {
                    .boundary = execution_boundary(event.cpu_boundary.boundary),
                    .address_before = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = (static_cast<uint32_t>(
                            event.cpu_boundary.state_before.pb
                        ) << 16u) | event.cpu_boundary.state_before.pc
                    },
                    .address_after = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = (static_cast<uint32_t>(
                            event.cpu_boundary.state_after.pb
                        ) << 16u) | event.cpu_boundary.state_after.pc
                    }
                };
            }
            else
            {
                translated.memory_access = {
                    .kind = event.cpu_memory_access.kind
                            == core::snes_memory_access_kind_t::write
                        ? memory_access_kind_t::write
                        : memory_access_kind_t::read,
                    .address = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = event.cpu_memory_access.address
                    },
                    .value = event.cpu_memory_access.value,
                    .instruction_address = {
                        .space = snes_debug::k_cpu_bus_space,
                        .value = event.cpu_memory_access.instruction_address
                    }
                };
            }
            destination[index] = translated;
        }
        _observation_sink.discard(count);
        return {
            .events_written = count,
            .events_dropped = _observation_sink.take_dropped()
        };
    }

    void snes_emulator_core_t::clear_observations() noexcept
    {
        _observation_sink.clear();
    }

    debug_session_control_t* snes_emulator_core_t::debug_session_control() noexcept
    {
        return this;
    }

    debug_session_state_t snes_emulator_core_t::debug_session_state() const noexcept
    {
        if (!_machine_running)
            return debug_session_state_t::not_running;
        return _debug_paused
            ? debug_session_state_t::paused
            : debug_session_state_t::running;
    }

    debug_session_transition_result_t snes_emulator_core_t::pause_debug_session() noexcept
    {
        if (!_machine_running)
            return {};

        _debug_paused = true;
        return {
            .status = debug_session_transition_status_t::complete,
            .state = debug_session_state_t::paused
        };
    }

    debug_session_transition_result_t snes_emulator_core_t::resume_debug_session() noexcept
    {
        if (!_machine_running)
            return {};

        _debug_paused = false;
        return {
            .status = debug_session_transition_status_t::complete,
            .state = debug_session_state_t::running
        };
    }

    checkpoint_control_t* snes_emulator_core_t::checkpoint_control() noexcept
    {
        return this;
    }

    checkpoint_operation_result_t snes_emulator_core_t::capture_checkpoint(
        std::vector<std::byte>& checkpoint
    ) noexcept
    {
        if (!_machine_running)
            return { checkpoint_operation_status_t::not_running };
        if (!_debug_paused)
            return { checkpoint_operation_status_t::not_paused };

        const checkpoint_result_t result{
            capture_snes_checkpoint(_console, checkpoint)
        };
        if (result == checkpoint_result_t::success)
            return { checkpoint_operation_status_t::success };
        if (result == checkpoint_result_t::allocation_failed)
            return { checkpoint_operation_status_t::allocation_failed };
        return { checkpoint_operation_status_t::capture_failed };
    }

    checkpoint_operation_result_t snes_emulator_core_t::restore_checkpoint(
        std::span<const std::byte> checkpoint
    ) noexcept
    {
        if (!_machine_running)
            return { checkpoint_operation_status_t::not_running };
        if (!_debug_paused)
            return { checkpoint_operation_status_t::not_paused };

        const checkpoint_result_t result{
            restore_snes_checkpoint(_console, checkpoint)
        };
        switch (result)
        {
        case checkpoint_result_t::success:
            clear_observations();
            return { checkpoint_operation_status_t::success };
        case checkpoint_result_t::allocation_failed:
            return { checkpoint_operation_status_t::allocation_failed };
        case checkpoint_result_t::unsupported_format_version:
        case checkpoint_result_t::unsupported_system:
        case checkpoint_result_t::unsupported_core_state_version:
        case checkpoint_result_t::unsupported_subsystem_version:
        case checkpoint_result_t::media_mismatch:
        case checkpoint_result_t::hardware_mismatch:
            return { checkpoint_operation_status_t::incompatible_checkpoint };
        case checkpoint_result_t::core_restore_failed:
            return { checkpoint_operation_status_t::restore_failed };
        default:
            return { checkpoint_operation_status_t::invalid_checkpoint };
        }
    }
}
