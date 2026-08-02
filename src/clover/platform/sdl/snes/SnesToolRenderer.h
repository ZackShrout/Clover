//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Palette.h"
#include "clover/analysis/snes/Oam.h"
#include "clover/analysis/snes/TileGraphics.h"
#include "clover/analysis/snes/TileMap.h"
#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"

#include <SDL3/SDL.h>

#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace clover::platform::sdl::snes
{
    using text_renderer_t = std::function<void(float, float, std::string_view)>;

    void render_dma_history(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        std::span<const frontend::snes::dma_transfer_record_t> transfers,
        const frontend::snes::dma_transfer_inspection_result_t& inspection,
        size_t selected_transfer,
        const text_renderer_t& draw_text
    );

    void render_palette(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_palette_t& palette,
        uint16_t selected_color,
        const text_renderer_t& draw_text
    );

    void render_tiles(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_tile_set_t& tiles,
        const analysis::decoded_palette_t* palette,
        uint32_t selected_tile,
        const text_renderer_t& draw_text
    );

    using object_tile_reader_t = std::function<
        std::optional<analysis::decoded_tile_t>(uint16_t word_address)
    >;

    void render_oam(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_snes_oam_t& oam,
        const analysis::decoded_palette_t& palette,
        uint8_t selected_object,
        const object_tile_reader_t& read_tile,
        const text_renderer_t& draw_text
    );

    struct tile_map_render_options_t
    {
        bool live_snapshot{ false };
        bool full_map{ false };
        uint16_t horizontal_scroll{ 0u };
        uint16_t vertical_scroll{ 0u };
        uint16_t selected_x{ 0u };
        uint16_t selected_y{ 0u };
    };

    void render_tile_map(
        SDL_Renderer* renderer,
        const SDL_FRect& bounds,
        const analysis::decoded_tile_map_t& map,
        const analysis::decoded_tile_set_t* tiles,
        const analysis::decoded_palette_t* palette,
        const tile_map_render_options_t& options,
        const text_renderer_t& draw_text
    );
}
