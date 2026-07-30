//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TileGraphics.h"

#include <limits>
#include <utility>

namespace clover::analysis
{
    uint32_t tile_bytes(tile_format_t format) noexcept
    {
        switch (format)
        {
        case tile_format_t::snes_2bpp:
            return 16u;
        case tile_format_t::snes_4bpp:
            return 32u;
        case tile_format_t::snes_8bpp:
            return 64u;
        }
        return 0u;
    }

    uint8_t tile_bits_per_pixel(tile_format_t format) noexcept
    {
        switch (format)
        {
        case tile_format_t::snes_2bpp:
            return 2u;
        case tile_format_t::snes_4bpp:
            return 4u;
        case tile_format_t::snes_8bpp:
            return 8u;
        }
        return 0u;
    }

    tile_validation_t validate_tile_asset(const tile_asset_t& asset)
    {
        tile_validation_t result{};
        const auto conflict = [&](tile_conflict_kind_t kind,
                                  std::string detail)
        {
            result.conflicts.push_back({
                .kind = kind,
                .asset_id = asset.stable_id,
                .location = asset.location,
                .detail = std::move(detail)
            });
        };
        const uint32_t bytes_per_tile{ tile_bytes(asset.format) };
        if (asset.stable_id.empty() || asset.name.empty()
            || asset.location.address_space.empty()
            || asset.tile_count == 0u || asset.tile_count > 4096u
            || bytes_per_tile == 0u)
        {
            conflict(
                tile_conflict_kind_t::invalid_definition,
                "Tile identity, name, location, count, or format is invalid"
            );
            return result;
        }
        if (asset.location.address % bytes_per_tile != 0u)
        {
            conflict(
                tile_conflict_kind_t::misaligned_source,
                "SNES planar tiles must begin on a whole-tile boundary"
            );
        }
        const uint64_t byte_count{
            static_cast<uint64_t>(asset.tile_count) * bytes_per_tile
        };
        if (asset.location.address
            > std::numeric_limits<uint64_t>::max() - byte_count)
        {
            conflict(
                tile_conflict_kind_t::address_overflow,
                "Tile source range overflows its address space"
            );
        }
        if (asset.location.address_space == "snes.vram"
            && (asset.location.address > 65536u
                || byte_count > 65536u - asset.location.address))
        {
            conflict(
                tile_conflict_kind_t::source_out_of_range,
                "Tile asset extends beyond the 64KB SNES VRAM space"
            );
        }
        const uint32_t maximum_colors{
            uint32_t{ 1 } << tile_bits_per_pixel(asset.format)
        };
        if (static_cast<uint32_t>(asset.palette_base) + maximum_colors > 256u)
        {
            conflict(
                tile_conflict_kind_t::invalid_definition,
                "Tile palette base and format exceed 256 color entries"
            );
        }
        return result;
    }

    decoded_tile_set_t decode_tiles(const tile_asset_t& asset,
                                    const tile_byte_reader_t& reader)
    {
        decoded_tile_set_t result{ .asset = asset };
        const tile_validation_t validation{ validate_tile_asset(asset) };
        result.conflicts = validation.conflicts;
        if (!validation.valid())
            return result;

        const uint8_t bits_per_pixel{
            tile_bits_per_pixel(asset.format)
        };
        const uint32_t bytes_per_tile{ tile_bytes(asset.format) };
        result.tiles.reserve(asset.tile_count);
        for (uint32_t tile_index{}; tile_index < asset.tile_count; ++tile_index)
        {
            std::array<uint8_t, 64> bytes{};
            bool available{ true };
            for (uint32_t byte_index{}; byte_index < bytes_per_tile; ++byte_index)
            {
                const address_t location{
                    asset.location.address_space,
                    asset.location.address
                        + static_cast<uint64_t>(tile_index) * bytes_per_tile
                        + byte_index
                };
                const std::optional<uint8_t> byte{ reader(location) };
                if (!byte.has_value())
                {
                    result.conflicts.push_back({
                        .kind = tile_index == 0u && byte_index == 0u
                            ? tile_conflict_kind_t::unavailable_byte
                            : tile_conflict_kind_t::truncated_source,
                        .asset_id = asset.stable_id,
                        .location = location,
                        .detail = tile_index == 0u && byte_index == 0u
                            ? "Tile source is unavailable"
                            : "Tile source ended before every tile was decoded"
                    });
                    available = false;
                    break;
                }
                bytes[byte_index] = *byte;
            }
            if (!available)
                break;

            decoded_tile_t tile{ .index = tile_index };
            for (uint8_t y{}; y < 8u; ++y)
            {
                for (uint8_t x{}; x < 8u; ++x)
                {
                    const uint8_t source_bit{
                        static_cast<uint8_t>(7u - x)
                    };
                    uint8_t color{};
                    for (uint8_t plane{}; plane < bits_per_pixel; ++plane)
                    {
                        const uint32_t plane_pair{
                            static_cast<uint32_t>(plane / 2u)
                        };
                        const uint32_t plane_byte{
                            plane_pair * 16u
                            + static_cast<uint32_t>(y) * 2u
                            + (plane & 1u)
                        };
                        color = static_cast<uint8_t>(
                            color
                            | (((bytes[plane_byte] >> source_bit) & 1u)
                                << plane)
                        );
                    }
                    tile.pixels[static_cast<size_t>(y) * 8u + x] = color;
                }
            }
            result.tiles.push_back(tile);
        }
        return result;
    }
}
