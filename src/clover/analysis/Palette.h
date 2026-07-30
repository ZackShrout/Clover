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
    enum class palette_format_t : uint8_t
    {
        snes_bgr555
    };

    struct palette_asset_t
    {
        std::string stable_id{};
        std::string name{};
        address_t location{};
        uint16_t color_count{ 16 };
        palette_format_t format{ palette_format_t::snes_bgr555 };

        [[nodiscard]] bool operator==(
            const palette_asset_t&
        ) const noexcept = default;
    };

    struct palette_color_t
    {
        uint16_t index{ 0 };
        uint16_t raw_value{ 0 };
        uint8_t red5{ 0 };
        uint8_t green5{ 0 };
        uint8_t blue5{ 0 };
        uint8_t red8{ 0 };
        uint8_t green8{ 0 };
        uint8_t blue8{ 0 };

        [[nodiscard]] bool operator==(
            const palette_color_t&
        ) const noexcept = default;
    };

    enum class palette_conflict_kind_t : uint8_t
    {
        invalid_definition,
        misaligned_source,
        address_overflow,
        unavailable_byte,
        truncated_source
    };

    struct palette_conflict_t
    {
        palette_conflict_kind_t kind{
            palette_conflict_kind_t::invalid_definition
        };
        std::string asset_id{};
        std::optional<address_t> location{};
        std::string detail{};

        [[nodiscard]] bool operator==(
            const palette_conflict_t&
        ) const noexcept = default;
    };

    struct palette_validation_t
    {
        std::vector<palette_conflict_t> conflicts{};

        [[nodiscard]] bool valid() const noexcept
        {
            return conflicts.empty();
        }
    };

    using palette_byte_reader_t = std::function<
        std::optional<uint8_t>(const address_t&)
    >;

    struct decoded_palette_t
    {
        palette_asset_t asset{};
        std::vector<palette_color_t> colors{};
        std::vector<palette_conflict_t> conflicts{};

        [[nodiscard]] bool complete() const noexcept
        {
            return conflicts.empty()
                && colors.size() == asset.color_count;
        }
    };

    [[nodiscard]] palette_validation_t validate_palette_asset(
        const palette_asset_t& asset
    );

    [[nodiscard]] decoded_palette_t decode_palette(
        const palette_asset_t& asset,
        const palette_byte_reader_t& reader
    );
}
