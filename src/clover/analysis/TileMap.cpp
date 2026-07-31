//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TileMap.h"

#include <limits>
#include <utility>

namespace clover::analysis
{
    uint16_t tile_map_width(uint8_t screen_size) noexcept
    {
        return static_cast<uint16_t>(32u << (screen_size & 0x01u));
    }

    uint16_t tile_map_height(uint8_t screen_size) noexcept
    {
        return static_cast<uint16_t>(
            32u << ((screen_size >> 1u) & 0x01u)
        );
    }

    uint64_t tile_map_entry_offset(uint8_t screen_size,
                                   uint16_t x,
                                   uint16_t y) noexcept
    {
        uint64_t entry{
            static_cast<uint64_t>(x & 31u)
                + static_cast<uint64_t>(y & 31u) * 32u
        };
        if (x >= 32u)
            entry += 1024u;
        if (y >= 32u)
            entry += static_cast<uint64_t>(1024u)
                << (screen_size & 0x01u);
        return entry * 2u;
    }

    tile_map_validation_t validate_tile_map_asset(
        const tile_map_asset_t& asset
    )
    {
        tile_map_validation_t result{};
        const auto conflict = [&](tile_map_conflict_kind_t kind,
                                  std::string detail)
        {
            result.conflicts.push_back({
                .kind = kind,
                .asset_id = asset.stable_id,
                .location = asset.location,
                .detail = std::move(detail)
            });
        };
        if (asset.stable_id.empty() || asset.name.empty()
            || asset.location.address_space.empty()
            || asset.screen_size > 3u
            || (asset.tile_size != 8u && asset.tile_size != 16u)
            || asset.format != tile_map_format_t::snes_background
            || asset.tile_asset_id.empty() || asset.palette_id.empty()
            || asset.palette_base > 255u)
        {
            conflict(
                tile_map_conflict_kind_t::invalid_definition,
                "Tile-map identity, geometry, format, or asset links are invalid"
            );
            return result;
        }
        if ((asset.location.address & 1u) != 0u)
        {
            conflict(
                tile_map_conflict_kind_t::misaligned_source,
                "SNES tile maps must begin on a 16-bit entry boundary"
            );
        }
        const uint64_t byte_count{
            static_cast<uint64_t>(tile_map_width(asset.screen_size))
                * tile_map_height(asset.screen_size) * 2u
        };
        if (asset.location.address_space == "snes.vram")
        {
            if (asset.location.address >= 65536u)
            {
                conflict(
                    tile_map_conflict_kind_t::source_out_of_range,
                    "Tile-map source begins outside the 64KB SNES VRAM space"
                );
            }
        }
        else if (asset.location.address
                 > std::numeric_limits<uint64_t>::max() - byte_count)
        {
            conflict(
                tile_map_conflict_kind_t::address_overflow,
                "Tile-map source range overflows its address space"
            );
        }
        return result;
    }

    decoded_tile_map_t decode_tile_map(
        const tile_map_asset_t& asset,
        const tile_map_byte_reader_t& reader
    )
    {
        decoded_tile_map_t result{
            .asset = asset,
            .width = tile_map_width(asset.screen_size),
            .height = tile_map_height(asset.screen_size)
        };
        const tile_map_validation_t validation{
            validate_tile_map_asset(asset)
        };
        result.conflicts = validation.conflicts;
        if (!validation.valid())
            return result;

        result.entries.reserve(
            static_cast<size_t>(result.width) * result.height
        );
        for (uint16_t y{}; y < result.height; ++y)
        {
            for (uint16_t x{}; x < result.width; ++x)
            {
                const uint64_t offset{
                    tile_map_entry_offset(asset.screen_size, x, y)
                };
                const uint64_t low_address{
                    asset.location.address_space == "snes.vram"
                        ? (asset.location.address + offset) & 0xffffu
                        : asset.location.address + offset
                };
                const uint64_t high_address{
                    asset.location.address_space == "snes.vram"
                        ? (low_address + 1u) & 0xffffu
                        : low_address + 1u
                };
                const std::optional<uint8_t> low{
                    reader({
                        asset.location.address_space,
                        low_address
                    })
                };
                const std::optional<uint8_t> high{
                    reader({
                        asset.location.address_space,
                        high_address
                    })
                };
                if (!low.has_value() || !high.has_value())
                {
                    result.conflicts.push_back({
                        .kind = result.entries.empty()
                            ? tile_map_conflict_kind_t::unavailable_byte
                            : tile_map_conflict_kind_t::truncated_source,
                        .asset_id = asset.stable_id,
                        .location = address_t{
                            asset.location.address_space,
                            !low.has_value() ? low_address : high_address
                        },
                        .detail = result.entries.empty()
                            ? "Tile-map source is unavailable"
                            : "Tile-map source ended before every entry was decoded"
                    });
                    return result;
                }
                const uint16_t raw{
                    static_cast<uint16_t>(
                        *low | (static_cast<uint16_t>(*high) << 8u)
                    )
                };
                result.entries.push_back({
                    .x = x,
                    .y = y,
                    .raw_value = raw,
                    .character = static_cast<uint16_t>(raw & 0x03ffu),
                    .palette_group = static_cast<uint8_t>(
                        (raw >> 10u) & 0x07u
                    ),
                    .priority = (raw & 0x2000u) != 0u,
                    .horizontal_flip = (raw & 0x4000u) != 0u,
                    .vertical_flip = (raw & 0x8000u) != 0u
                });
            }
        }
        return result;
    }
}
