//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/ProgramModel.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clover::analysis
{
    enum class tile_map_format_t : uint8_t
    {
        snes_background
    };

    struct tile_map_asset_t
    {
        std::string stable_id{};
        std::string name{};
        address_t location{};
        uint8_t screen_size{ 0u };
        uint8_t tile_size{ 8u };
        tile_map_format_t format{ tile_map_format_t::snes_background };
        std::string tile_asset_id{};
        std::string palette_id{};
        uint16_t palette_base{ 0u };

        [[nodiscard]] bool operator==(
            const tile_map_asset_t&
        ) const noexcept = default;
    };

    struct decoded_tile_map_entry_t
    {
        uint16_t x{ 0u };
        uint16_t y{ 0u };
        uint16_t raw_value{ 0u };
        uint16_t character{ 0u };
        uint8_t palette_group{ 0u };
        bool priority{ false };
        bool horizontal_flip{ false };
        bool vertical_flip{ false };

        [[nodiscard]] bool operator==(
            const decoded_tile_map_entry_t&
        ) const noexcept = default;
    };

    enum class tile_map_conflict_kind_t : uint8_t
    {
        invalid_definition,
        misaligned_source,
        address_overflow,
        unavailable_byte,
        truncated_source,
        source_out_of_range
    };

    struct tile_map_conflict_t
    {
        tile_map_conflict_kind_t kind{
            tile_map_conflict_kind_t::invalid_definition
        };
        std::string asset_id{};
        std::optional<address_t> location{};
        std::string detail{};

        [[nodiscard]] bool operator==(
            const tile_map_conflict_t&
        ) const noexcept = default;
    };

    struct tile_map_validation_t
    {
        std::vector<tile_map_conflict_t> conflicts{};

        [[nodiscard]] bool valid() const noexcept
        {
            return conflicts.empty();
        }
    };

    using tile_map_byte_reader_t = std::function<
        std::optional<uint8_t>(const address_t&)
    >;

    struct decoded_tile_map_t
    {
        tile_map_asset_t asset{};
        uint16_t width{ 0u };
        uint16_t height{ 0u };
        std::vector<decoded_tile_map_entry_t> entries{};
        std::vector<tile_map_conflict_t> conflicts{};

        [[nodiscard]] bool complete() const noexcept
        {
            return conflicts.empty()
                && entries.size()
                    == static_cast<size_t>(width) * height;
        }
    };

    [[nodiscard]] uint16_t tile_map_width(uint8_t screen_size) noexcept;
    [[nodiscard]] uint16_t tile_map_height(uint8_t screen_size) noexcept;
    [[nodiscard]] uint64_t tile_map_entry_offset(
        uint8_t screen_size,
        uint16_t x,
        uint16_t y
    ) noexcept;
    [[nodiscard]] tile_map_validation_t validate_tile_map_asset(
        const tile_map_asset_t& asset
    );
    [[nodiscard]] decoded_tile_map_t decode_tile_map(
        const tile_map_asset_t& asset,
        const tile_map_byte_reader_t& reader
    );
}
