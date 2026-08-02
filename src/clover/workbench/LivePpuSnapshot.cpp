//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/LivePpuSnapshot.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace clover::workbench
{
    namespace
    {
        [[nodiscard]] bool same_layer(
            const frontend::tile_layer_state_t& left,
            const frontend::tile_layer_state_t& right
        ) noexcept
        {
            return left.id == right.id
                && left.label == right.label
                && left.active == right.active
                && left.tile_map.space == right.tile_map.space
                && left.tile_map.value == right.tile_map.value
                && left.tile_graphics.space == right.tile_graphics.space
                && left.tile_graphics.value == right.tile_graphics.value
                && left.width_tiles == right.width_tiles
                && left.height_tiles == right.height_tiles
                && left.format == right.format
                && left.screen_size == right.screen_size
                && left.tile_size == right.tile_size
                && left.palette_base == right.palette_base
                && left.horizontal_scroll == right.horizontal_scroll
                && left.vertical_scroll == right.vertical_scroll;
        }

        [[nodiscard]] const frontend::address_space_descriptor_t* find_space(
            frontend::debug_target_t& target,
            std::string_view stable_id
        ) noexcept
        {
            const std::span<const frontend::address_space_descriptor_t> spaces{
                target.address_spaces()
            };
            const auto found{ std::find_if(
                spaces.begin(), spaces.end(),
                [stable_id](const auto& space)
                {
                    return space.stable_id == stable_id;
                }
            ) };
            return found == spaces.end() ? nullptr : &*found;
        }

        [[nodiscard]] bool inspect_space(
            frontend::debug_target_t& target,
            const frontend::address_space_descriptor_t& space,
            std::span<std::byte> destination
        ) noexcept
        {
            const frontend::memory_inspection_result_t result{
                target.inspect_memory(
                    { space.id, 0u }, destination
                )
            };
            return result.status
                    == frontend::memory_inspection_status_t::complete
                && result.bytes_read == destination.size();
        }
    }

    std::optional<uint8_t> live_ppu_snapshot_t::inspect_byte(
        const analysis::address_t& address
    ) const noexcept
    {
        if (address.address_space == "snes.vram"
            && address.address < vram.size())
        {
            return std::to_integer<uint8_t>(vram[address.address]);
        }
        if (address.address_space == "snes.cgram"
            && address.address < cgram.size())
        {
            return std::to_integer<uint8_t>(cgram[address.address]);
        }
        return std::nullopt;
    }

    bool capture_live_ppu_snapshot(
        frontend::emulator_core_t& core,
        frontend::debug_target_t& target,
        size_t layer_index,
        live_ppu_snapshot_t& destination,
        std::string& error
    )
    {
        std::array<frontend::tile_layer_state_t, 4u> before{};
        const size_t before_count{ core.inspect_tile_layers(before) };
        if (layer_index >= before_count || !before[layer_index].active)
        {
            error = "Selected BG layer is inactive";
            return false;
        }
        const auto* const vram_space{ find_space(target, "snes.vram") };
        const auto* const cgram_space{ find_space(target, "snes.cgram") };
        if (vram_space == nullptr || cgram_space == nullptr)
        {
            error = "Live PPU memory spaces are unavailable";
            return false;
        }

        live_ppu_snapshot_t captured{};
        captured.layer = before[layer_index];
        if (!inspect_space(target, *vram_space, captured.vram)
            || !inspect_space(target, *cgram_space, captured.cgram))
        {
            error = "Unable to capture a complete live PPU snapshot";
            return false;
        }

        // A future asynchronous core must not silently pair memory with PPU
        // registers from a different instant. Reject the capture if metadata
        // changed while the memory copies were being made.
        std::array<frontend::tile_layer_state_t, 4u> after{};
        const size_t after_count{ core.inspect_tile_layers(after) };
        if (layer_index >= after_count
            || !same_layer(captured.layer, after[layer_index]))
        {
            error = "Live PPU state changed during snapshot capture";
            return false;
        }

        destination = std::move(captured);
        error.clear();
        return true;
    }

    std::optional<live_bg_assets_t> make_live_bg_assets(
        const frontend::tile_layer_state_t& layer
    )
    {
        std::optional<analysis::tile_format_t> format{};
        switch (layer.format)
        {
        case frontend::tile_layer_format_t::indexed_2bpp:
            format = analysis::tile_format_t::snes_2bpp;
            break;
        case frontend::tile_layer_format_t::indexed_4bpp:
            format = analysis::tile_format_t::snes_4bpp;
            break;
        case frontend::tile_layer_format_t::indexed_8bpp:
            format = analysis::tile_format_t::snes_8bpp;
            break;
        case frontend::tile_layer_format_t::affine_mode7:
        case frontend::tile_layer_format_t::inactive:
            return std::nullopt;
        }
        if (layer.tile_map.value >= 65536u
            || layer.tile_graphics.value >= 65536u)
        {
            return std::nullopt;
        }

        const std::string layer_name{ layer.label };
        const std::string palette_id{ "palette@snes.cgram:000" };
        const std::string tile_id{ "tiles@live-" + layer_name };
        const uint32_t bytes_per_tile{ analysis::tile_bytes(*format) };
        const uint32_t tile_count{ std::min<uint32_t>(
            1024u,
            static_cast<uint32_t>(
                (65536u - layer.tile_graphics.value) / bytes_per_tile
            )
        ) };
        return live_bg_assets_t{
            .palette = {
                .stable_id = palette_id,
                .name = "Live CGRAM",
                .location = { "snes.cgram", 0u },
                .color_count = 256u
            },
            .tiles = {
                .stable_id = tile_id,
                .name = "Live " + layer_name + " tiles",
                .location = { "snes.vram", layer.tile_graphics.value },
                .tile_count = tile_count,
                .format = *format,
                .palette_id = palette_id,
                .palette_base = layer.palette_base
            },
            .map = {
                .stable_id = "tilemap@live-" + layer_name,
                .name = "Live " + layer_name + " map",
                .location = { "snes.vram", layer.tile_map.value },
                .screen_size = layer.screen_size,
                .tile_size = layer.tile_size,
                .tile_asset_id = tile_id,
                .palette_id = palette_id,
                .palette_base = layer.palette_base
            }
        };
    }
}
