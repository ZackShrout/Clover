//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/Oam.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <optional>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "OamTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::analysis;

    std::array<uint8_t, 544> oam{};
    oam[0] = 0xf8u;
    oam[1] = 0x20u;
    oam[2] = 0x2fu;
    oam[3] = 0xdbu;
    oam[512] = 0x03u;

    const decoded_snes_oam_t decoded{
        decode_snes_oam(
            {
                .tile_base_word_address = 0x2000u,
                .name_select = 2u,
                .base_size = 6u
            },
            [&oam](uint16_t address) -> std::optional<uint8_t>
            {
                return oam[address];
            }
        )
    };
    const snes_oam_object_t& object{ decoded.objects[0] };
    if (!decoded.complete()
        || object.raw_x != 0x1f8u
        || object.screen_x != -8
        || object.y != 0x20u
        || object.character != 0x2fu
        || !object.name_table
        || object.palette != 5u
        || object.priority != 1u
        || !object.horizontal_flip
        || !object.vertical_flip
        || !object.large
        || object.width != 32u
        || object.height != 64u
        || !object.intersects_viewport)
    {
        return fail("entry_bits_and_size");
    }

    if (snes_oam_object_tile(object, 0u, 0u) != 0x52u
        || snes_oam_object_tile_word_address(
                decoded.configuration,
                object,
                0u,
                0u
            ) != 0x5520u)
    {
        return fail("flipped_tile_address");
    }
    snes_oam_object_t wrapped_object{};
    wrapped_object.character = 0xffu;
    wrapped_object.name_table = true;
    wrapped_object.width = 8u;
    wrapped_object.height = 8u;
    if (snes_oam_object_tile_word_address(
            {
                .tile_base_word_address = 0xe000u,
                .name_select = 3u
            },
            wrapped_object,
            0u,
            0u
        ) != 0x2ff0u)
    {
        return fail("vram_word_wrapping");
    }

    const decoded_snes_oam_t invalid{
        decode_snes_oam(
            { .base_size = 8u },
            [](uint16_t) -> std::optional<uint8_t> { return 0u; }
        )
    };
    if (invalid.conflicts.size() != 1u
        || invalid.conflicts[0].kind
            != snes_oam_conflict_kind_t::invalid_configuration)
    {
        return fail("invalid_configuration");
    }

    const decoded_snes_oam_t truncated{
        decode_snes_oam(
            {},
            [](uint16_t address) -> std::optional<uint8_t>
            {
                return address < 20u
                    ? std::optional<uint8_t>{ 0u }
                    : std::nullopt;
            }
        )
    };
    if (truncated.conflicts.size() != 1u
        || truncated.conflicts[0].kind
            != snes_oam_conflict_kind_t::truncated_source
        || truncated.conflicts[0].address != 20u)
    {
        return fail("truncated_source");
    }

    std::printf(
        "OAM tests passed: entry bits, size pairs, signed X, flips, "
        "tile addressing, and conflicts\n"
    );
    return 0;
}
