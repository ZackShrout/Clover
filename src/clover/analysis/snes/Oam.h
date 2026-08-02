//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace clover::analysis
{
    struct snes_oam_configuration_t
    {
        uint16_t tile_base_word_address{ 0u };
        uint8_t name_select{ 0u };
        uint8_t base_size{ 0u };
        bool interlace{ false };
    };

    struct snes_oam_object_t
    {
        uint8_t index{ 0u };
        uint16_t raw_x{ 0u };
        int16_t screen_x{ 0 };
        uint8_t y{ 0u };
        uint8_t character{ 0u };
        bool name_table{ false };
        uint8_t palette{ 0u };
        uint8_t priority{ 0u };
        bool horizontal_flip{ false };
        bool vertical_flip{ false };
        bool large{ false };
        uint8_t width{ 0u };
        uint8_t height{ 0u };
        bool intersects_viewport{ false };
    };

    enum class snes_oam_conflict_kind_t : uint8_t
    {
        invalid_configuration,
        unavailable_byte,
        truncated_source
    };

    struct snes_oam_conflict_t
    {
        snes_oam_conflict_kind_t kind{
            snes_oam_conflict_kind_t::invalid_configuration
        };
        uint16_t address{ 0u };
        std::string detail{};
    };

    using snes_oam_byte_reader_t = std::function<
        std::optional<uint8_t>(uint16_t)
    >;

    struct decoded_snes_oam_t
    {
        snes_oam_configuration_t configuration{};
        std::array<snes_oam_object_t, 128> objects{};
        std::vector<snes_oam_conflict_t> conflicts{};

        [[nodiscard]] bool complete() const noexcept
        {
            return conflicts.empty();
        }
    };

    [[nodiscard]] decoded_snes_oam_t decode_snes_oam(
        snes_oam_configuration_t configuration,
        const snes_oam_byte_reader_t& reader
    );
    [[nodiscard]] uint16_t snes_oam_object_tile(
        const snes_oam_object_t& object,
        uint8_t pixel_x,
        uint8_t pixel_y
    ) noexcept;
    [[nodiscard]] std::array<uint8_t, 2> snes_oam_object_source_pixel(
        const snes_oam_object_t& object,
        uint8_t pixel_x,
        uint8_t pixel_y
    ) noexcept;
    [[nodiscard]] uint16_t snes_oam_object_tile_word_address(
        const snes_oam_configuration_t& configuration,
        const snes_oam_object_t& object,
        uint8_t pixel_x,
        uint8_t pixel_y
    ) noexcept;
}
