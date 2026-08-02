//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/TileGraphics.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <vector>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "TileGraphicsTest failed at %s\n", checkpoint);
        return 1;
    }

    [[nodiscard]] bool decode_pattern(clover::analysis::tile_format_t format)
    {
        using namespace clover::analysis;
        std::vector<uint8_t> bytes(tile_bytes(format), 0u);
        const uint8_t bpp{ tile_bits_per_pixel(format) };
        for (uint8_t plane{}; plane < bpp; ++plane)
        {
            const uint32_t byte_index{
                static_cast<uint32_t>(plane / 2u) * 16u + (plane & 1u)
            };
            if ((0xa5u & (1u << plane)) != 0u)
                bytes[byte_index] = 0x80u;
        }
        const tile_asset_t asset{
            .stable_id = "pattern",
            .name = "Pattern",
            .location = { "snes.vram", 0u },
            .tile_count = 1u,
            .format = format
        };
        const decoded_tile_set_t decoded{
            decode_tiles(
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
        const uint8_t mask{
            static_cast<uint8_t>((uint16_t{ 1 } << bpp) - 1u)
        };
        return decoded.complete()
            && decoded.tiles.front().pixels[0] == (0xa5u & mask)
            && decoded.tiles.front().pixels[1] == 0u;
    }
}

int main()
{
    using namespace clover::analysis;
    if (!decode_pattern(tile_format_t::snes_2bpp))
        return fail("decode_2bpp");
    if (!decode_pattern(tile_format_t::snes_4bpp))
        return fail("decode_4bpp");
    if (!decode_pattern(tile_format_t::snes_8bpp))
        return fail("decode_8bpp");

    const tile_asset_t misaligned{
        .stable_id = "misaligned",
        .name = "Misaligned",
        .location = { "snes.vram", 1u },
        .tile_count = 1u,
        .format = tile_format_t::snes_4bpp
    };
    if (validate_tile_asset(misaligned).valid())
        return fail("alignment");
    const tile_asset_t outside{
        .stable_id = "outside",
        .name = "Outside",
        .location = { "snes.vram", 65504u },
        .tile_count = 2u,
        .format = tile_format_t::snes_4bpp
    };
    if (validate_tile_asset(outside).valid())
        return fail("vram_bounds");
    const tile_asset_t bad_palette{
        .stable_id = "palette",
        .name = "Palette overflow",
        .location = { "snes.vram", 0u },
        .tile_count = 1u,
        .format = tile_format_t::snes_8bpp,
        .palette_base = 1u
    };
    if (validate_tile_asset(bad_palette).valid())
        return fail("palette_bounds");

    std::printf(
        "Tile graphics tests passed: SNES 2bpp, 4bpp, and 8bpp planar "
        "decoding, alignment, palette, and VRAM bounds\n"
    );
    return 0;
}
