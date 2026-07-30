//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/ProgramModel.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clover::analysis
{
    enum class tile_format_t : uint8_t
    {
        snes_2bpp,
        snes_4bpp,
        snes_8bpp
    };

    struct tile_asset_t
    {
        std::string stable_id{};
        std::string name{};
        address_t location{};
        uint32_t tile_count{ 1u };
        tile_format_t format{ tile_format_t::snes_4bpp };
        std::string palette_id{};
        uint16_t palette_base{ 0u };

        [[nodiscard]] bool operator==(const tile_asset_t&) const noexcept = default;
    };

    struct decoded_tile_t
    {
        uint32_t index{ 0u };
        std::array<uint8_t, 64> pixels{};

        [[nodiscard]] bool operator==(
            const decoded_tile_t&
        ) const noexcept = default;
    };

    enum class tile_conflict_kind_t : uint8_t
    {
        invalid_definition,
        misaligned_source,
        address_overflow,
        unavailable_byte,
        truncated_source,
        source_out_of_range
    };

    struct tile_conflict_t
    {
        tile_conflict_kind_t kind{
            tile_conflict_kind_t::invalid_definition
        };
        std::string asset_id{};
        std::optional<address_t> location{};
        std::string detail{};

        [[nodiscard]] bool operator==(
            const tile_conflict_t&
        ) const noexcept = default;
    };

    struct tile_validation_t
    {
        std::vector<tile_conflict_t> conflicts{};

        [[nodiscard]] bool valid() const noexcept
        {
            return conflicts.empty();
        }
    };

    using tile_byte_reader_t = std::function<
        std::optional<uint8_t>(const address_t&)
    >;

    struct decoded_tile_set_t
    {
        tile_asset_t asset{};
        std::vector<decoded_tile_t> tiles{};
        std::vector<tile_conflict_t> conflicts{};

        [[nodiscard]] bool complete() const noexcept
        {
            return conflicts.empty() && tiles.size() == asset.tile_count;
        }
    };

    [[nodiscard]] uint32_t tile_bytes(tile_format_t format) noexcept;
    [[nodiscard]] uint8_t tile_bits_per_pixel(
        tile_format_t format
    ) noexcept;
    [[nodiscard]] tile_validation_t validate_tile_asset(
        const tile_asset_t& asset
    );
    [[nodiscard]] decoded_tile_set_t decode_tiles(
        const tile_asset_t& asset,
        const tile_byte_reader_t& reader
    );
}
