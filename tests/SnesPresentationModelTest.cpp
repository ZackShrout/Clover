//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesPresentationModel.h"
#include "clover/workbench/snes/SnesWorkbenchSupport.h"

#include <cstdio>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "SnesPresentationModelTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::workbench;
    using namespace clover::workbench::snes;

    snes_presentation_model_t model{};
    model.palettes.push_back({
        .asset = { .color_count = 32u }
    });
    model.tile_assets.push_back({
        .asset = { .tile_count = 64u }
    });
    model.tile_maps.push_back({
        .asset = {
            .screen_size = 0u
        }
    });

    if (!model.navigate(
            k_palette_tool_id,
            presentation_direction_t::down
        )
        || model.selected_color != 16u
        || !model.navigate(
            k_palette_tool_id,
            presentation_direction_t::right
        )
        || model.selected_color != 17u)
        return fail("palette_navigation");

    if (!model.navigate(
            k_tile_graphics_tool_id,
            presentation_direction_t::down
        )
        || model.selected_tile != 16u
        || !model.navigate(
            k_tile_graphics_tool_id,
            presentation_direction_t::right,
            true
        )
        || model.selected_pixel_x != 1u)
        return fail("tile_navigation");

    if (!model.navigate(
            k_tile_map_tool_id,
            presentation_direction_t::right
        )
        || !model.navigate(
            k_tile_map_tool_id,
            presentation_direction_t::down
        )
        || model.selected_map_x != 1u
        || model.selected_map_y != 1u)
        return fail("map_navigation");

    if (!model.navigate(k_object_tool_id, presentation_direction_t::down)
        || model.selected_object != 16u)
        return fail("object_navigation");

    model.dma_inspection.record_count = 2u;
    if (!model.navigate(k_dma_tool_id, presentation_direction_t::down)
        || model.selected_dma_transfer != 1u
        || model.navigate(k_dma_tool_id, presentation_direction_t::down))
        return fail("dma_navigation_bounds");

    return 0;
}
