//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/snes/SnesToolRenderer.h"

#include <algorithm>
#include <iomanip>
#include <sstream>
#include <string>

namespace
{
    [[nodiscard]] std::string formatted_address(uint32_t address)
    {
        std::ostringstream output{};
        output << '$' << std::uppercase << std::hex << std::setfill('0')
               << std::setw(2) << ((address >> 16u) & 0xffu)
               << ':' << std::setw(4) << (address & 0xffffu);
        return output.str();
    }

    [[nodiscard]] std::string formatted_b_bus_targets(
        uint8_t base,
        uint8_t offset_mask
    )
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0');
        bool first{ true };
        for (uint8_t offset{}; offset < 4u; ++offset)
        {
            if ((offset_mask & static_cast<uint8_t>(1u << offset)) == 0u)
                continue;
            if (!first)
                output << '/';
            output << "$21" << std::setw(2)
                   << static_cast<unsigned>(base + offset);
            first = false;
        }
        if (first)
            output << "$21" << std::setw(2) << static_cast<unsigned>(base);
        return output.str();
    }
}

namespace clover::platform::sdl::snes
{
    void render_dma_history(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        std::span<const frontend::snes::dma_transfer_record_t> transfers,
        const frontend::snes::dma_transfer_inspection_result_t& inspection,
        size_t selected_transfer,
        const text_renderer_t& draw_text
    )
    {
        draw_text(
            bounds.x + 10.f,
            48.f,
            "DMA / HDMA TRANSFERS  " + std::to_string(inspection.record_count)
                + (inspection.records_dropped == 0u
                    ? std::string{}
                    : "  (" + std::to_string(inspection.records_dropped)
                        + " older dropped)")
        );
        if (inspection.record_count == 0u)
        {
            draw_text(
                bounds.x + 10.f,
                76.f,
                "No DMA transfers captured yet; run the ROM"
            );
            return;
        }

        selected_transfer = std::min(
            selected_transfer,
            inspection.record_count - 1u
        );
        const size_t visible_rows{ std::max<size_t>(
            1u,
            static_cast<size_t>((bounds.h - 70.f) / 18.f)
        ) };
        const size_t first_row{
            selected_transfer >= visible_rows
                ? selected_transfer - visible_rows + 1u : 0u
        };
        const size_t last_row{ std::min(
            inspection.record_count,
            first_row + visible_rows
        ) };
        float row_y{ 74.f };
        for (size_t index{ first_row }; index < last_row; ++index)
        {
            const auto& transfer{ transfers[index] };
            if (index == selected_transfer)
            {
                static_cast<void>(SDL_SetRenderDrawColor(
                    renderer, 50u, 84u, 128u, 255u
                ));
                const SDL_FRect highlight{
                    bounds.x + 6.f, row_y - 2.f, bounds.w - 12.f, 18.f
                };
                static_cast<void>(SDL_RenderFillRect(renderer, &highlight));
            }
            std::ostringstream row{};
            row << (index == selected_transfer ? "> " : "  ")
                << '#' << transfer.sequence << ' '
                << (transfer.kind == frontend::snes::dma_transfer_kind_t::general
                    ? "MDMA" : "HDMA")
                << " C" << static_cast<unsigned>(transfer.channel)
                << ' ' << std::dec << transfer.byte_count << " B  "
                << formatted_address(transfer.first_a_bus_address);
            if (transfer.last_a_bus_address != transfer.first_a_bus_address)
                row << '-' << formatted_address(transfer.last_a_bus_address);
            row << (transfer.direction_to_b_bus ? " -> " : " <- ")
                << formatted_b_bus_targets(
                    transfer.b_bus_base,
                    transfer.b_bus_offset_mask
                )
                << "  F" << std::dec << transfer.frame_index
                << ' ' << transfer.first_scanline << ':' << transfer.first_dot;
            draw_text(bounds.x + 10.f, row_y, row.str());
            row_y += 18.f;
        }
    }

    void render_palette(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_palette_t& palette,
        uint16_t selected_color,
        const text_renderer_t& draw_text
    )
    {
        draw_text(bounds.x + 10.f, 48.f, "PALETTE  " + palette.asset.name);
        if (palette.colors.empty())
        {
            draw_text(
                bounds.x + 10.f,
                76.f,
                palette.conflicts.empty()
                    ? "Palette has no colors" : palette.conflicts.front().detail
            );
            return;
        }
        const float swatch_size{ std::max(
            8.f,
            std::min(28.f, (bounds.w - 52.f) / 16.f - 4.f)
        ) };
        const float grid_width{ 16.f * swatch_size + 15.f * 4.f };
        const float grid_x{
            bounds.x + std::max(12.f, (bounds.w - grid_width) / 2.f)
        };
        for (const auto& color : palette.colors)
        {
            const SDL_FRect swatch{
                grid_x + static_cast<float>(color.index % 16u)
                    * (swatch_size + 4.f),
                78.f + static_cast<float>(color.index / 16u)
                    * (swatch_size + 4.f),
                swatch_size,
                swatch_size
            };
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer, color.red8, color.green8, color.blue8, 255u
            ));
            static_cast<void>(SDL_RenderFillRect(renderer, &swatch));
            const bool selected{ color.index == selected_color };
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer,
                selected ? 255u : 72u,
                selected ? 255u : 82u,
                selected ? 255u : 98u,
                255u
            ));
            static_cast<void>(SDL_RenderRect(renderer, &swatch));
        }
    }

    void render_tiles(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_tile_set_t& tiles,
        const analysis::decoded_palette_t* palette,
        uint32_t selected_tile,
        const text_renderer_t& draw_text
    )
    {
        draw_text(bounds.x + 10.f, 48.f, "TILES  " + tiles.asset.name);
        const float pixel_size{ std::max(
            1.f,
            std::min(4.f, (bounds.h - 76.f) / (16.f * 8.f + 15.f * 4.f))
        ) };
        const float tile_size{ pixel_size * 8.f };
        const float grid_width{ tile_size * 16.f + 4.f * 15.f };
        const float grid_x{
            bounds.x + std::max(10.f, (bounds.w - grid_width) / 2.f)
        };
        const uint16_t maximum{ static_cast<uint16_t>(
            (1u << analysis::tile_bits_per_pixel(tiles.asset.format)) - 1u
        ) };
        for (const auto& tile : tiles.tiles)
        {
            const float tile_x{ grid_x + static_cast<float>(tile.index % 16u)
                * (tile_size + 4.f) };
            const float tile_y{ 76.f + static_cast<float>(tile.index / 16u)
                * (tile_size + 4.f) };
            for (uint8_t y{}; y < 8u; ++y)
            {
                for (uint8_t x{}; x < 8u; ++x)
                {
                    const uint8_t pixel{ tile.pixels[
                        static_cast<size_t>(y) * 8u + x
                    ] };
                    uint8_t red{ static_cast<uint8_t>(
                        static_cast<uint16_t>(pixel) * 255u / maximum
                    ) };
                    uint8_t green{ red };
                    uint8_t blue{ red };
                    const size_t palette_color{
                        static_cast<size_t>(tiles.asset.palette_base) + pixel
                    };
                    if (palette != nullptr
                        && palette_color < palette->colors.size())
                    {
                        const auto& color{ palette->colors[palette_color] };
                        red = color.red8;
                        green = color.green8;
                        blue = color.blue8;
                    }
                    static_cast<void>(SDL_SetRenderDrawColor(
                        renderer, red, green, blue, 255u
                    ));
                    const SDL_FRect pixel_rect{
                        tile_x + static_cast<float>(x) * pixel_size,
                        tile_y + static_cast<float>(y) * pixel_size,
                        pixel_size,
                        pixel_size
                    };
                    static_cast<void>(SDL_RenderFillRect(renderer, &pixel_rect));
                }
            }
            const SDL_FRect tile_rect{ tile_x, tile_y, tile_size, tile_size };
            const bool selected{ tile.index == selected_tile };
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer,
                selected ? 255u : 60u,
                selected ? 255u : 70u,
                selected ? 255u : 86u,
                255u
            ));
            static_cast<void>(SDL_RenderRect(renderer, &tile_rect));
        }
        if (tiles.tiles.empty())
        {
            draw_text(
                bounds.x + 10.f,
                76.f,
                tiles.conflicts.empty()
                    ? "Tile asset has no tiles" : tiles.conflicts.front().detail
            );
        }
    }

    void render_oam(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_snes_oam_t& oam,
        const analysis::decoded_palette_t& palette,
        uint8_t selected_object,
        const object_tile_reader_t& read_tile,
        const text_renderer_t& draw_text
    )
    {
        draw_text(bounds.x + 10.f, 48.f, "OBJECTS  Live OAM");
        if (!oam.complete())
        {
            draw_text(
                bounds.x + 10.f,
                76.f,
                oam.conflicts.empty()
                    ? "Live OAM is unavailable" : oam.conflicts.front().detail
            );
            return;
        }

        selected_object = std::min<uint8_t>(selected_object, 127u);
        const float cell_size{ std::max(
            14.f,
            std::min(30.f, (bounds.w - 60.f) / 16.f)
        ) };
        const float grid_width{ cell_size * 16.f };
        const float grid_x{ bounds.x + (bounds.w - grid_width) / 2.f };
        const float grid_y{ 78.f };
        for (const analysis::snes_oam_object_t& object : oam.objects)
        {
            const uint8_t palette_index{ static_cast<uint8_t>(
                128u + object.palette * 16u + 1u
            ) };
            uint8_t red{ 44u };
            uint8_t green{ 48u };
            uint8_t blue{ 58u };
            if (object.intersects_viewport
                && palette_index < palette.colors.size())
            {
                const auto& color{ palette.colors[palette_index] };
                red = color.red8;
                green = color.green8;
                blue = color.blue8;
            }
            const SDL_FRect cell{
                grid_x + (object.index % 16u) * cell_size,
                grid_y + (object.index / 16u) * cell_size,
                cell_size - 2.f,
                cell_size - 2.f
            };
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer, red, green, blue, 255u
            ));
            static_cast<void>(SDL_RenderFillRect(renderer, &cell));
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer,
                object.index == selected_object ? 255u : 75u,
                object.index == selected_object ? 255u : 84u,
                object.index == selected_object ? 255u : 102u,
                255u
            ));
            static_cast<void>(SDL_RenderRect(renderer, &cell));
        }

        const analysis::snes_oam_object_t& object{
            oam.objects[selected_object]
        };
        const float preview_top{ grid_y + cell_size * 8.f + 28.f };
        const float preview_available{
            std::max(64.f, bounds.h - preview_top - 18.f)
        };
        const float pixel_size{ std::max(
            1.f,
            std::min(
                8.f,
                std::min(
                    (bounds.w - 40.f) / object.width,
                    preview_available / object.height
                )
            )
        ) };
        const float preview_width{ object.width * pixel_size };
        const float preview_height{ object.height * pixel_size };
        const float preview_x{
            bounds.x + (bounds.w - preview_width) / 2.f
        };
        const float preview_y{
            preview_top + (preview_available - preview_height) / 2.f
        };
        for (uint8_t y{}; y < object.height; ++y)
        {
            for (uint8_t x{}; x < object.width; ++x)
            {
                const bool checker{ (((x / 4u) + (y / 4u)) & 1u) != 0u };
                static_cast<void>(SDL_SetRenderDrawColor(
                    renderer,
                    checker ? 31u : 24u,
                    checker ? 36u : 29u,
                    checker ? 46u : 38u,
                    255u
                ));
                const SDL_FRect pixel_rect{
                    preview_x + x * pixel_size,
                    preview_y + y * pixel_size,
                    pixel_size,
                    pixel_size
                };
                static_cast<void>(SDL_RenderFillRect(renderer, &pixel_rect));
            }
        }

        for (uint8_t y{}; y < object.height; ++y)
        {
            for (uint8_t x{}; x < object.width; ++x)
            {
                const uint16_t word_address{
                    analysis::snes_oam_object_tile_word_address(
                        oam.configuration, object, x, y
                    )
                };
                const std::optional<analysis::decoded_tile_t> tile{
                    read_tile(word_address)
                };
                if (!tile.has_value())
                    continue;
                const std::array<uint8_t, 2> source_pixel{
                    analysis::snes_oam_object_source_pixel(object, x, y)
                };
                const uint8_t color_index{ tile->pixels[
                    static_cast<size_t>(source_pixel[1] & 7u) * 8u
                        + (source_pixel[0] & 7u)
                ] };
                if (color_index == 0u)
                    continue;
                const size_t palette_index{
                    static_cast<size_t>(128u)
                        + object.palette * 16u + color_index
                };
                if (palette_index >= palette.colors.size())
                    continue;
                const auto& color{ palette.colors[palette_index] };
                static_cast<void>(SDL_SetRenderDrawColor(
                    renderer, color.red8, color.green8, color.blue8, 255u
                ));
                const SDL_FRect pixel_rect{
                    preview_x + x * pixel_size,
                    preview_y + y * pixel_size,
                    pixel_size,
                    pixel_size
                };
                static_cast<void>(SDL_RenderFillRect(renderer, &pixel_rect));
            }
        }
    }

    void render_tile_map(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_tile_map_t& map,
        const analysis::decoded_tile_set_t* tiles,
        const analysis::decoded_palette_t* palette,
        const tile_map_render_options_t& options,
        const text_renderer_t& draw_text
    )
    {
        draw_text(
            bounds.x + 10.f,
            48.f,
            (options.live_snapshot ? "RAW BG SNAPSHOT  " : "TILE MAP  ")
                + map.asset.name
        );
        if (map.entries.empty() || tiles == nullptr || palette == nullptr)
        {
            std::string detail{ "Tile-map links are unavailable" };
            if (!map.conflicts.empty())
                detail = map.conflicts.front().detail;
            else if (tiles != nullptr && !tiles->conflicts.empty())
                detail = tiles->conflicts.front().detail;
            else if (palette != nullptr && !palette->conflicts.empty())
                detail = palette->conflicts.front().detail;
            draw_text(bounds.x + 10.f, 76.f, detail);
            return;
        }

        const uint8_t tile_size{ map.asset.tile_size };
        const uint16_t map_width{
            static_cast<uint16_t>(map.width * tile_size)
        };
        const uint16_t map_height{
            static_cast<uint16_t>(map.height * tile_size)
        };
        const bool show_viewport{ options.live_snapshot && !options.full_map };
        const uint16_t view_width{
            static_cast<uint16_t>(show_viewport ? 256u : map_width)
        };
        const uint16_t view_height{
            static_cast<uint16_t>(show_viewport ? 224u : map_height)
        };
        const uint16_t origin_x{ static_cast<uint16_t>(show_viewport
            ? options.horizontal_scroll % map_width : 0u) };
        const uint16_t origin_y{ static_cast<uint16_t>(show_viewport
            ? options.vertical_scroll % map_height : 0u) };
        const float pixel_size{ std::max(
            0.5f,
            std::min(
                4.f,
                std::min(
                    (bounds.w - 24.f) / view_width,
                    (bounds.h - 84.f) / view_height
                )
            )
        ) };
        const float rendered_width{ view_width * pixel_size };
        const float rendered_height{ view_height * pixel_size };
        const float map_x{ bounds.x + std::max(
            12.f, (bounds.w - rendered_width) / 2.f
        ) };
        const float map_y{ 76.f + std::max(
            0.f, (bounds.h - 76.f - rendered_height) / 2.f
        ) };
        const uint8_t bpp{ analysis::tile_bits_per_pixel(tiles->asset.format) };
        const uint16_t colors_per_group{ static_cast<uint16_t>(1u << bpp) };
        for (uint16_t y{}; y < view_height; ++y)
        {
            const uint16_t source_pixel_y{
                static_cast<uint16_t>((origin_y + y) % map_height)
            };
            const uint16_t entry_y{
                static_cast<uint16_t>(source_pixel_y / tile_size)
            };
            for (uint16_t x{}; x < view_width; ++x)
            {
                const uint16_t source_pixel_x{
                    static_cast<uint16_t>((origin_x + x) % map_width)
                };
                const uint16_t entry_x{
                    static_cast<uint16_t>(source_pixel_x / tile_size)
                };
                const analysis::decoded_tile_map_entry_t& entry{
                    map.entries[static_cast<size_t>(entry_y) * map.width
                        + entry_x]
                };
                const uint8_t local_x{
                    static_cast<uint8_t>(source_pixel_x % tile_size)
                };
                const uint8_t local_y{
                    static_cast<uint8_t>(source_pixel_y % tile_size)
                };
                const uint8_t tile_x{ entry.horizontal_flip
                    ? static_cast<uint8_t>(tile_size - 1u - local_x) : local_x };
                const uint8_t tile_y{ entry.vertical_flip
                    ? static_cast<uint8_t>(tile_size - 1u - local_y) : local_y };
                const uint16_t character{ static_cast<uint16_t>(
                    entry.character + tile_x / 8u
                        + static_cast<uint16_t>(tile_y / 8u) * 16u
                ) };
                const bool checker{ (((x >> 3u) ^ (y >> 3u)) & 1u) != 0u };
                uint8_t red{ static_cast<uint8_t>(checker ? 24u : 15u) };
                uint8_t green{ static_cast<uint8_t>(checker ? 29u : 19u) };
                uint8_t blue{ static_cast<uint8_t>(checker ? 38u : 27u) };
                if (character < tiles->tiles.size())
                {
                    const uint8_t pixel{ tiles->tiles[character].pixels[
                        static_cast<size_t>(tile_y & 7u) * 8u
                            + (tile_x & 7u)
                    ] };
                    const uint16_t palette_index{ static_cast<uint16_t>(
                        map.asset.palette_base
                            + (bpp == 8u ? 0u
                                : entry.palette_group * colors_per_group)
                            + pixel
                    ) };
                    if (pixel != 0u && palette_index < palette->colors.size())
                    {
                        const auto& color{ palette->colors[palette_index] };
                        red = color.red8;
                        green = color.green8;
                        blue = color.blue8;
                    }
                }
                static_cast<void>(SDL_SetRenderDrawColor(
                    renderer, red, green, blue, 255u
                ));
                const SDL_FRect pixel_rect{
                    map_x + x * pixel_size,
                    map_y + y * pixel_size,
                    pixel_size,
                    pixel_size
                };
                static_cast<void>(SDL_RenderFillRect(renderer, &pixel_rect));
            }
        }

        const uint16_t selected_source_x{
            static_cast<uint16_t>(options.selected_x * tile_size)
        };
        const uint16_t selected_source_y{
            static_cast<uint16_t>(options.selected_y * tile_size)
        };
        const uint16_t selected_view_x{ static_cast<uint16_t>(
            (selected_source_x + map_width - origin_x) % map_width
        ) };
        const uint16_t selected_view_y{ static_cast<uint16_t>(
            (selected_source_y + map_height - origin_y) % map_height
        ) };
        if (selected_view_x < view_width && selected_view_y < view_height)
        {
            static_cast<void>(SDL_SetRenderDrawColor(
                renderer, 255u, 255u, 255u, 255u
            ));
            const SDL_FRect selected_rect{
                map_x + selected_view_x * pixel_size,
                map_y + selected_view_y * pixel_size,
                tile_size * pixel_size,
                tile_size * pixel_size
            };
            static_cast<void>(SDL_RenderRect(renderer, &selected_rect));
        }
        if (show_viewport)
        {
            draw_text(
                bounds.x + 10.f,
                64.f,
                "VIEWPORT  SCROLL " + std::to_string(origin_x) + ","
                    + std::to_string(origin_y) + "  (F: full map)"
            );
        }
        else if (options.live_snapshot)
        {
            draw_text(
                bounds.x + 10.f,
                64.f,
                "FULL MAP  (F: current viewport)"
            );
        }
    }
}
