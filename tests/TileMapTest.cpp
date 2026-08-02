//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/TileMap.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "TileMapTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::analysis;

    std::vector<uint8_t> bytes(8192u, 0u);
    const uint16_t raw{
        static_cast<uint16_t>(
            0x02a5u | (5u << 10u) | 0x2000u | 0x4000u | 0x8000u
        )
    };
    const uint64_t offset{ tile_map_entry_offset(3u, 33u, 34u) };
    bytes[static_cast<size_t>(offset)] = static_cast<uint8_t>(raw);
    bytes[static_cast<size_t>(offset + 1u)] = static_cast<uint8_t>(raw >> 8u);
    const tile_map_asset_t asset{
        .stable_id = "map",
        .name = "Map",
        .location = { "snes.cpu-bus", 0u },
        .screen_size = 3u,
        .tile_size = 8u,
        .tile_asset_id = "tiles",
        .palette_id = "palette"
    };
    const decoded_tile_map_t decoded{
        decode_tile_map(
            asset,
            [&bytes](const address_t& address) -> std::optional<uint8_t>
            {
                return address.address < bytes.size()
                    ? std::optional<uint8_t>{
                        bytes[static_cast<size_t>(address.address)]
                    }
                    : std::nullopt;
            }
        )
    };
    const auto& entry{
        decoded.entries[static_cast<size_t>(34u) * 64u + 33u]
    };
    if (!decoded.complete() || decoded.width != 64u || decoded.height != 64u
        || entry.character != 0x02a5u || entry.palette_group != 5u
        || !entry.priority || !entry.horizontal_flip || !entry.vertical_flip)
    {
        return fail("decode_attributes");
    }
    if (tile_map_entry_offset(1u, 32u, 0u) != 2048u
        || tile_map_entry_offset(2u, 0u, 32u) != 2048u
        || tile_map_entry_offset(3u, 0u, 32u) != 4096u)
    {
        return fail("screen_layout");
    }

    tile_map_asset_t invalid{ asset };
    invalid.location.address = 1u;
    if (validate_tile_map_asset(invalid).valid())
        return fail("alignment");
    invalid = asset;
    invalid.location = { "snes.vram", 65536u };
    if (validate_tile_map_asset(invalid).valid())
        return fail("vram_bounds");

    std::vector<uint8_t> wrapping_vram(65536u, 0u);
    wrapping_vram[65534u] = 0x55u;
    wrapping_vram[65535u] = 0x01u;
    tile_map_asset_t wrapping{ asset };
    wrapping.location = { "snes.vram", 65534u };
    wrapping.screen_size = 0u;
    const decoded_tile_map_t wrapped{
        decode_tile_map(
            wrapping,
            [&wrapping_vram](
                const address_t& address
            ) -> std::optional<uint8_t>
            {
                return wrapping_vram[
                    static_cast<size_t>(address.address)
                ];
            }
        )
    };
    if (!wrapped.complete() || wrapped.entries.front().character != 0x0155u)
        return fail("vram_wrap");

    std::printf(
        "Tile-map tests passed: SNES screen layout, entry attributes, "
        "validation, and VRAM wrapping\n"
    );
    return 0;
}
