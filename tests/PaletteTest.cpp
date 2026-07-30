//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/Palette.h"

#include <array>
#include <cstdint>
#include <cstdio>
#include <optional>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "PaletteTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::analysis;

    const palette_asset_t asset{
        .stable_id = "palette.test",
        .name = "Test palette",
        .location = { "snes.cgram", 0u },
        .color_count = 4u
    };
    const std::array<uint8_t, 8> bytes{
        0x1fu, 0x00u,
        0xe0u, 0x03u,
        0x00u, 0x7cu,
        0xffu, 0xffu
    };
    const auto reader{
        [&bytes](const address_t& address) -> std::optional<uint8_t>
        {
            if (address.address_space != "snes.cgram"
                || address.address >= bytes.size())
            {
                return std::nullopt;
            }
            return bytes[static_cast<size_t>(address.address)];
        }
    };
    const decoded_palette_t decoded{ decode_palette(asset, reader) };
    if (!decoded.complete() || decoded.colors.size() != 4u
        || decoded.colors[0].red8 != 255u
        || decoded.colors[0].green8 != 0u
        || decoded.colors[0].blue8 != 0u
        || decoded.colors[1].green8 != 255u
        || decoded.colors[2].blue8 != 255u
        || decoded.colors[3].raw_value != 0x7fffu
        || decoded.colors[3].red8 != 255u
        || decoded.colors[3].green8 != 255u
        || decoded.colors[3].blue8 != 255u)
    {
        return fail("bgr555_decode");
    }
    if (decode_palette(
            {
                .stable_id = "misaligned",
                .name = "Misaligned",
                .location = { "snes.cgram", 1u },
                .color_count = 1u
            },
            reader
        ).conflicts.front().kind != palette_conflict_kind_t::misaligned_source)
    {
        return fail("misalignment");
    }
    const decoded_palette_t truncated{
        decode_palette(
            {
                .stable_id = "truncated",
                .name = "Truncated",
                .location = { "snes.cgram", 0u },
                .color_count = 5u
            },
            reader
        )
    };
    if (truncated.complete() || truncated.colors.size() != 4u
        || truncated.conflicts.back().kind
            != palette_conflict_kind_t::truncated_source)
    {
        return fail("truncated_source");
    }
    if (validate_palette_asset({
            .stable_id = "outside-cgram",
            .name = "Outside",
            .location = { "snes.cgram", 510u },
            .color_count = 2u
        }).valid())
    {
        return fail("cgram_range");
    }

    std::printf(
        "Palette tests passed: SNES BGR555 decoding, alignment, truncation, "
        "and CGRAM bounds\n"
    );
    return 0;
}
