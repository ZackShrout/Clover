//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesPresentationModel.h"

#include "clover/analysis/snes/TileMap.h"
#include "clover/workbench/snes/SnesWorkbenchSupport.h"

#include <algorithm>

namespace clover::workbench::snes
{
    void snes_presentation_model_t::refresh_assets(
        project_t& project,
        active_tool_t& active_tool,
        std::string& error
    )
    {
        palettes = project.palettes(error);
        tile_assets = project.tile_assets(error);
        tile_maps = project.tile_maps(error);
        if (palettes.empty())
        {
            active_tool.close(k_palette_tool_id);
            palette_index = 0u;
            selected_color = 0u;
        }
        else
        {
            palette_index = std::min(palette_index, palettes.size() - 1u);
            selected_color = std::min<uint16_t>(
                selected_color,
                static_cast<uint16_t>(
                    palettes[palette_index].asset.color_count - 1u
                )
            );
        }
        if (tile_maps.empty())
        {
            active_tool.close(k_tile_map_tool_id);
            tile_map_index = 0u;
            selected_map_x = 0u;
            selected_map_y = 0u;
        }
        else
        {
            if (!pending_tile_map_id.empty())
            {
                const auto requested{ std::find_if(
                    tile_maps.begin(),
                    tile_maps.end(),
                    [this](const project_tile_map_t& map)
                    {
                        return map.asset.stable_id == pending_tile_map_id;
                    }
                ) };
                if (requested != tile_maps.end())
                {
                    tile_map_index = static_cast<size_t>(
                        std::distance(tile_maps.begin(), requested)
                    );
                    active_tool.activate(k_tile_map_tool_id);
                }
                pending_tile_map_id.clear();
            }
            tile_map_index = std::min(tile_map_index, tile_maps.size() - 1u);
            selected_map_x = std::min<uint16_t>(
                selected_map_x,
                analysis::tile_map_width(
                    tile_maps[tile_map_index].asset.screen_size
                ) - 1u
            );
            selected_map_y = std::min<uint16_t>(
                selected_map_y,
                analysis::tile_map_height(
                    tile_maps[tile_map_index].asset.screen_size
                ) - 1u
            );
        }
        if (tile_assets.empty())
        {
            active_tool.close(k_tile_graphics_tool_id);
            tile_asset_index = 0u;
            selected_tile = 0u;
        }
        else
        {
            tile_asset_index = std::min(
                tile_asset_index,
                tile_assets.size() - 1u
            );
            selected_tile = std::min<uint32_t>(
                selected_tile,
                tile_assets[tile_asset_index].asset.tile_count - 1u
            );
        }
    }

    void snes_presentation_model_t::refresh_dma(
        const frontend::snes::dma_transfer_diagnostics_t& diagnostics
    ) noexcept
    {
        const bool followed_latest{
            dma_inspection.record_count == 0u
                || selected_dma_transfer + 1u == dma_inspection.record_count
        };
        dma_inspection = diagnostics.inspect_dma_transfers(dma_transfers);
        if (dma_inspection.record_count == 0u)
            selected_dma_transfer = 0u;
        else if (followed_latest)
            selected_dma_transfer = dma_inspection.record_count - 1u;
        else
        {
            selected_dma_transfer = std::min(
                selected_dma_transfer,
                dma_inspection.record_count - 1u
            );
        }
    }

    bool snes_presentation_model_t::refresh_live_snapshot(
        const frontend::snes::tile_layer_diagnostics_t& diagnostics,
        frontend::debug_target_t& target,
        std::string& error
    )
    {
        if (!live_map_layer_index.has_value())
            return true;
        live_ppu_snapshot_t captured{};
        if (!capture_live_ppu_snapshot(
                diagnostics,
                target,
                *live_map_layer_index,
                captured,
                error
            ))
        {
            live_ppu_snapshot.reset();
            return false;
        }
        live_ppu_snapshot = std::move(captured);
        return true;
    }

    bool snes_presentation_model_t::capture_live_map(
        const frontend::snes::tile_layer_diagnostics_t& diagnostics,
        frontend::debug_target_t& target,
        size_t layer_index,
        project_t& project,
        std::string& error
    )
    {
        live_ppu_snapshot_t captured{};
        if (!capture_live_ppu_snapshot(
                diagnostics,
                target,
                layer_index,
                captured,
                error
            ))
        {
            return false;
        }
        const auto assets{ make_live_bg_assets(captured.layer) };
        if (!assets.has_value())
        {
            error = "Mode 7 maps require the later affine viewer";
            return false;
        }
        if (!project.set_palette(assets->palette, error)
            || !project.set_tile_asset(assets->tiles, error)
            || !project.set_tile_map(assets->map, error))
        {
            return false;
        }
        live_map_layer_index = layer_index;
        live_ppu_snapshot = std::move(captured);
        pending_tile_map_id = assets->map.stable_id;
        rendered_bg_view = false;
        return true;
    }

    bool snes_presentation_model_t::navigate(
        std::string_view active_tool_id,
        presentation_direction_t direction,
        bool fine_adjustment
    ) noexcept
    {
        if (active_tool_id == k_palette_tool_id && !palettes.empty())
        {
            const uint16_t count{ palettes[palette_index].asset.color_count };
            if (direction == presentation_direction_t::left
                && selected_color > 0u)
                --selected_color;
            else if (direction == presentation_direction_t::right
                     && selected_color + 1u < count)
                ++selected_color;
            else if (direction == presentation_direction_t::up
                     && selected_color >= 16u)
                selected_color = static_cast<uint16_t>(selected_color - 16u);
            else if (direction == presentation_direction_t::down
                     && selected_color + 16u < count)
                selected_color = static_cast<uint16_t>(selected_color + 16u);
            else
                return false;
            return true;
        }
        if (active_tool_id == k_tile_graphics_tool_id && !tile_assets.empty())
        {
            if (fine_adjustment)
            {
                uint8_t& coordinate{
                    direction == presentation_direction_t::left
                        || direction == presentation_direction_t::right
                        ? selected_pixel_x : selected_pixel_y
                };
                if ((direction == presentation_direction_t::left
                     || direction == presentation_direction_t::up)
                    && coordinate > 0u)
                    --coordinate;
                else if ((direction == presentation_direction_t::right
                          || direction == presentation_direction_t::down)
                         && coordinate < 7u)
                    ++coordinate;
                else
                    return false;
                return true;
            }
            const uint32_t count{
                tile_assets[tile_asset_index].asset.tile_count
            };
            if (direction == presentation_direction_t::left
                && selected_tile > 0u)
                --selected_tile;
            else if (direction == presentation_direction_t::right
                     && selected_tile + 1u < count)
                ++selected_tile;
            else if (direction == presentation_direction_t::up
                     && selected_tile >= 16u)
                selected_tile -= 16u;
            else if (direction == presentation_direction_t::down
                     && selected_tile + 16u < count)
                selected_tile += 16u;
            else
                return false;
            return true;
        }
        if (active_tool_id == k_tile_map_tool_id && !tile_maps.empty())
        {
            const auto& asset{ tile_maps[tile_map_index].asset };
            const uint16_t width{ analysis::tile_map_width(asset.screen_size) };
            const uint16_t height{ analysis::tile_map_height(asset.screen_size) };
            if (direction == presentation_direction_t::left
                && selected_map_x > 0u)
                --selected_map_x;
            else if (direction == presentation_direction_t::right
                     && selected_map_x + 1u < width)
                ++selected_map_x;
            else if (direction == presentation_direction_t::up
                     && selected_map_y > 0u)
                --selected_map_y;
            else if (direction == presentation_direction_t::down
                     && selected_map_y + 1u < height)
                ++selected_map_y;
            else
                return false;
            return true;
        }
        if (active_tool_id == k_object_tool_id)
        {
            if (direction == presentation_direction_t::left
                && selected_object > 0u)
                --selected_object;
            else if (direction == presentation_direction_t::right
                     && selected_object < 127u)
                ++selected_object;
            else if (direction == presentation_direction_t::up
                     && selected_object >= 16u)
                selected_object = static_cast<uint8_t>(selected_object - 16u);
            else if (direction == presentation_direction_t::down
                     && selected_object < 112u)
                selected_object = static_cast<uint8_t>(selected_object + 16u);
            else
                return false;
            return true;
        }
        if (active_tool_id == k_dma_tool_id)
        {
            if (direction == presentation_direction_t::up
                && selected_dma_transfer > 0u)
                --selected_dma_transfer;
            else if (direction == presentation_direction_t::down
                     && selected_dma_transfer + 1u < dma_inspection.record_count)
                ++selected_dma_transfer;
            else
                return false;
            return true;
        }
        return false;
    }
}
