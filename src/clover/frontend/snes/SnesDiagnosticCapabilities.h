//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/DebugTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace clover::frontend::snes
{
    enum class tile_layer_format_t : uint8_t
    {
        indexed_2bpp,
        indexed_4bpp,
        indexed_8bpp,
        affine_mode7,
        inactive
    };

    struct tile_layer_state_t
    {
        uint32_t id{ 0u };
        std::string_view label{};
        bool active{ false };
        debug_address_t tile_map{};
        debug_address_t tile_graphics{};
        uint16_t width_tiles{ 0u };
        uint16_t height_tiles{ 0u };
        tile_layer_format_t format{ tile_layer_format_t::inactive };
        uint8_t screen_size{ 0u };
        uint8_t tile_size{ 8u };
        uint16_t palette_base{ 0u };
        uint16_t horizontal_scroll{ 0u };
        uint16_t vertical_scroll{ 0u };
    };

    struct tile_layer_diagnostics_t
    {
    public:
        virtual ~tile_layer_diagnostics_t() = default;
        [[nodiscard]] virtual size_t inspect_tile_layers(
            std::span<tile_layer_state_t> destination
        ) const noexcept = 0;
    };

    struct object_layer_state_t
    {
        bool active{ false };
        debug_address_t oam{};
        debug_address_t tile_graphics{};
        debug_address_t palette{};
        uint16_t tile_base_word_address{ 0u };
        uint8_t name_select{ 0u };
        uint8_t base_size{ 0u };
        uint8_t first_sprite{ 0u };
        bool interlace{ false };
        bool range_over{ false };
        bool time_over{ false };
    };

    struct object_layer_diagnostics_t
    {
    public:
        virtual ~object_layer_diagnostics_t() = default;
        [[nodiscard]] virtual bool inspect_object_layer(
            object_layer_state_t& destination
        ) const noexcept = 0;
    };

    enum class dma_transfer_kind_t : uint8_t
    {
        general,
        horizontal_blank
    };

    struct dma_transfer_record_t
    {
        uint64_t sequence{ 0 };
        uint64_t first_master_clock{ 0 };
        uint64_t last_master_clock{ 0 };
        uint64_t frame_index{ 0 };
        uint32_t initiator_address{ 0 };
        uint32_t first_a_bus_address{ 0 };
        uint32_t last_a_bus_address{ 0 };
        uint32_t byte_count{ 0 };
        uint16_t first_scanline{ 0 };
        uint16_t first_dot{ 0 };
        uint16_t last_scanline{ 0 };
        uint16_t last_dot{ 0 };
        uint8_t channel{ 0 };
        uint8_t channel_mask{ 0 };
        uint8_t control{ 0 };
        uint8_t b_bus_base{ 0 };
        uint8_t b_bus_offset_mask{ 0 };
        uint8_t first_value{ 0 };
        uint8_t last_value{ 0 };
        dma_transfer_kind_t kind{ dma_transfer_kind_t::general };
        bool direction_to_b_bus{ true };
        bool b_bus_access_valid{ true };
    };

    struct dma_transfer_inspection_result_t
    {
        size_t record_count{ 0 };
        uint64_t records_dropped{ 0 };
    };

    struct dma_transfer_diagnostics_t
    {
    public:
        virtual ~dma_transfer_diagnostics_t() = default;
        [[nodiscard]] virtual dma_transfer_inspection_result_t
            inspect_dma_transfers(
                std::span<dma_transfer_record_t> destination
            ) const noexcept = 0;
        virtual void clear_dma_transfers() noexcept = 0;
    };
}
