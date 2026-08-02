//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Palette.h"

#include <limits>
#include <utility>

namespace
{
    [[nodiscard]] constexpr uint8_t expand_5_to_8(uint8_t value) noexcept
    {
        return static_cast<uint8_t>((value << 3u) | (value >> 2u));
    }
}

namespace clover::analysis
{
    palette_validation_t validate_palette_asset(const palette_asset_t& asset)
    {
        palette_validation_t result{};
        const auto conflict = [&](palette_conflict_kind_t kind,
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
            || asset.color_count == 0u || asset.color_count > 256u
            || asset.format != palette_format_t::snes_bgr555)
        {
            conflict(
                palette_conflict_kind_t::invalid_definition,
                "Palette identity, name, location, or color count is invalid"
            );
        }
        if ((asset.location.address & 1u) != 0u)
        {
            conflict(
                palette_conflict_kind_t::misaligned_source,
                "SNES BGR555 palette data must begin on a two-byte boundary"
            );
        }
        const uint64_t byte_count{
            static_cast<uint64_t>(asset.color_count) * 2u
        };
        if (asset.location.address
            > std::numeric_limits<uint64_t>::max() - byte_count)
        {
            conflict(
                palette_conflict_kind_t::address_overflow,
                "Palette source range overflows its address space"
            );
        }
        if (asset.location.address_space == "snes.cgram"
            && (asset.location.address > 512u
                || byte_count > 512u - asset.location.address))
        {
            conflict(
                palette_conflict_kind_t::truncated_source,
                "Palette extends beyond the 512-byte SNES CGRAM space"
            );
        }
        return result;
    }

    decoded_palette_t decode_palette(const palette_asset_t& asset,
                                      const palette_byte_reader_t& reader)
    {
        decoded_palette_t result{ .asset = asset };
        const palette_validation_t validation{
            validate_palette_asset(asset)
        };
        result.conflicts = validation.conflicts;
        if (!validation.valid())
            return result;

        result.colors.reserve(asset.color_count);
        for (uint16_t index{}; index < asset.color_count; ++index)
        {
            const address_t low_address{
                asset.location.address_space,
                asset.location.address + static_cast<uint64_t>(index) * 2u
            };
            const address_t high_address{
                low_address.address_space,
                low_address.address + 1u
            };
            const std::optional<uint8_t> low{ reader(low_address) };
            const std::optional<uint8_t> high{ reader(high_address) };
            if (!low.has_value() || !high.has_value())
            {
                result.conflicts.push_back({
                    .kind = (!low.has_value() && index == 0u)
                        ? palette_conflict_kind_t::unavailable_byte
                        : palette_conflict_kind_t::truncated_source,
                    .asset_id = asset.stable_id,
                    .location = !low.has_value() ? low_address : high_address,
                    .detail = (!low.has_value() && index == 0u)
                        ? "Palette source is unavailable"
                        : "Palette source ended before every color was decoded"
                });
                break;
            }
            const uint16_t raw{
                static_cast<uint16_t>(
                    (
                        static_cast<uint16_t>(*low)
                        | (static_cast<uint16_t>(*high) << 8u)
                    ) & 0x7fffu
                )
            };
            const uint8_t red5{ static_cast<uint8_t>(raw & 0x1fu) };
            const uint8_t green5{
                static_cast<uint8_t>((raw >> 5u) & 0x1fu)
            };
            const uint8_t blue5{
                static_cast<uint8_t>((raw >> 10u) & 0x1fu)
            };
            result.colors.push_back({
                .index = index,
                .raw_value = raw,
                .red5 = red5,
                .green5 = green5,
                .blue5 = blue5,
                .red8 = expand_5_to_8(red5),
                .green8 = expand_5_to_8(green5),
                .blue8 = expand_5_to_8(blue5)
            });
        }
        return result;
    }
}
