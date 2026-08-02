//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Oam.h"

#include <algorithm>

namespace clover::analysis
{
    namespace
    {
        constexpr std::array<uint8_t, 8> k_small_width{
            8u, 8u, 8u, 16u, 16u, 32u, 16u, 16u
        };
        constexpr std::array<uint8_t, 8> k_small_height{
            8u, 8u, 8u, 16u, 16u, 32u, 32u, 32u
        };
        constexpr std::array<uint8_t, 8> k_large_width{
            16u, 32u, 64u, 32u, 64u, 64u, 32u, 32u
        };
        constexpr std::array<uint8_t, 8> k_large_height{
            16u, 32u, 64u, 32u, 64u, 64u, 64u, 32u
        };

        [[nodiscard]] bool intersects_viewport(
            int16_t x,
            uint8_t y,
            uint8_t width,
            uint8_t height
        ) noexcept
        {
            const bool horizontal{ x < 256 && x + width > 0 };
            const bool vertical{
                y < 224u || static_cast<uint16_t>(y) + height > 256u
            };
            return horizontal && vertical;
        }

        [[nodiscard]] uint8_t source_y(
            const snes_oam_object_t& object,
            uint8_t pixel_y
        ) noexcept
        {
            if (!object.vertical_flip)
                return pixel_y;
            if (object.width == object.height)
                return static_cast<uint8_t>(object.height - 1u - pixel_y);
            if (pixel_y < object.width)
                return static_cast<uint8_t>(object.width - 1u - pixel_y);
            return static_cast<uint8_t>(
                object.width + (object.width - 1u)
                    - (pixel_y - object.width)
            );
        }
    }

    decoded_snes_oam_t decode_snes_oam(
        snes_oam_configuration_t configuration,
        const snes_oam_byte_reader_t& reader
    )
    {
        decoded_snes_oam_t result{ .configuration = configuration };
        if (configuration.base_size >= 8u
            || configuration.name_select >= 4u
            || (configuration.tile_base_word_address & 0x1fffu) != 0u)
        {
            result.conflicts.push_back({
                .kind = snes_oam_conflict_kind_t::invalid_configuration,
                .detail = "OBJ tile base, name select, or size selection is invalid"
            });
            return result;
        }

        std::array<uint8_t, 544> bytes{};
        for (uint16_t address{}; address < bytes.size(); ++address)
        {
            const std::optional<uint8_t> byte{ reader(address) };
            if (!byte.has_value())
            {
                result.conflicts.push_back({
                    .kind = address == 0u
                        ? snes_oam_conflict_kind_t::unavailable_byte
                        : snes_oam_conflict_kind_t::truncated_source,
                    .address = address,
                    .detail = address == 0u
                        ? "OAM source is unavailable"
                        : "OAM source ended before all 544 bytes were decoded"
                });
                return result;
            }
            bytes[address] = *byte;
        }

        for (uint16_t index{}; index < result.objects.size(); ++index)
        {
            const uint16_t base{ static_cast<uint16_t>(index * 4u) };
            const uint8_t high{
                bytes[512u + index / 4u]
            };
            const uint8_t shift{ static_cast<uint8_t>((index & 3u) * 2u) };
            const bool x_high{ ((high >> shift) & 1u) != 0u };
            const bool large{ ((high >> (shift + 1u)) & 1u) != 0u };
            const uint8_t attributes{ bytes[base + 3u] };
            const uint16_t raw_x{
                static_cast<uint16_t>(bytes[base] | (x_high ? 0x100u : 0u))
            };
            const int16_t screen_x{
                raw_x < 256u
                    ? static_cast<int16_t>(raw_x)
                    : static_cast<int16_t>(
                        static_cast<int16_t>(raw_x) - 512
                    )
            };
            uint8_t height{
                large ? k_large_height[configuration.base_size]
                      : k_small_height[configuration.base_size]
            };
            if (!large && configuration.interlace
                && configuration.base_size >= 6u)
            {
                height = 16u;
            }
            const uint8_t width{
                large ? k_large_width[configuration.base_size]
                      : k_small_width[configuration.base_size]
            };
            result.objects[index] = {
                .index = static_cast<uint8_t>(index),
                .raw_x = raw_x,
                .screen_x = screen_x,
                .y = bytes[base + 1u],
                .character = bytes[base + 2u],
                .name_table = (attributes & 0x01u) != 0u,
                .palette = static_cast<uint8_t>((attributes >> 1u) & 7u),
                .priority = static_cast<uint8_t>((attributes >> 4u) & 3u),
                .horizontal_flip = (attributes & 0x40u) != 0u,
                .vertical_flip = (attributes & 0x80u) != 0u,
                .large = large,
                .width = width,
                .height = height,
                .intersects_viewport = intersects_viewport(
                    screen_x,
                    bytes[base + 1u],
                    width,
                    height
                )
            };
        }
        return result;
    }

    uint16_t snes_oam_object_tile(const snes_oam_object_t& object,
                                  uint8_t pixel_x,
                                  uint8_t pixel_y) noexcept
    {
        const std::array<uint8_t, 2> source{
            snes_oam_object_source_pixel(object, pixel_x, pixel_y)
        };
        const uint16_t character_x{
            static_cast<uint16_t>(object.character & 0x0fu)
        };
        const uint16_t character_y{
            static_cast<uint16_t>(object.character >> 4u)
        };
        return static_cast<uint16_t>(
            (((character_y + source[1] / 8u) & 0x0fu) << 4u)
                | ((character_x + source[0] / 8u) & 0x0fu)
        );
    }

    std::array<uint8_t, 2> snes_oam_object_source_pixel(
        const snes_oam_object_t& object,
        uint8_t pixel_x,
        uint8_t pixel_y
    ) noexcept
    {
        return {
            object.horizontal_flip
                ? static_cast<uint8_t>(object.width - 1u - pixel_x)
                : pixel_x,
            source_y(object, pixel_y)
        };
    }

    uint16_t snes_oam_object_tile_word_address(
        const snes_oam_configuration_t& configuration,
        const snes_oam_object_t& object,
        uint8_t pixel_x,
        uint8_t pixel_y
    ) noexcept
    {
        uint16_t base{ configuration.tile_base_word_address };
        if (object.name_table)
        {
            base = static_cast<uint16_t>(
                base + ((1u + configuration.name_select) << 12u)
            );
        }
        return static_cast<uint16_t>(
            (base + (snes_oam_object_tile(object, pixel_x, pixel_y) << 4u))
                & 0x7fffu
        );
    }
}
