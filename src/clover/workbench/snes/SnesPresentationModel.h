//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"
#include "clover/workbench/snes/LivePpuSnapshot.h"
#include "clover/workbench/Project.h"
#include "clover/workbench/ToolRegistry.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clover::workbench::snes
{
    enum class presentation_direction_t : uint8_t
    {
        left,
        right,
        up,
        down
    };

    struct snes_presentation_model_t
    {
        std::vector<project_palette_t> palettes{};
        std::vector<project_tile_asset_t> tile_assets{};
        std::vector<project_tile_map_t> tile_maps{};
        size_t palette_index{ 0u };
        uint16_t selected_color{ 0u };
        size_t tile_asset_index{ 0u };
        uint32_t selected_tile{ 0u };
        uint8_t selected_pixel_x{ 0u };
        uint8_t selected_pixel_y{ 0u };
        bool rendered_bg_view{ false };
        size_t tile_map_index{ 0u };
        uint16_t selected_map_x{ 0u };
        uint16_t selected_map_y{ 0u };
        std::string pending_tile_map_id{};
        std::optional<size_t> live_map_layer_index{};
        std::optional<live_ppu_snapshot_t> live_ppu_snapshot{};
        bool tile_map_full_view{ false };
        uint8_t selected_object{ 0u };
        std::array<frontend::snes::dma_transfer_record_t, 512u> dma_transfers{};
        frontend::snes::dma_transfer_inspection_result_t dma_inspection{};
        size_t selected_dma_transfer{ 0u };

        void refresh_assets(
            project_t& project,
            active_tool_t& active_tool,
            std::string& error
        );
        void refresh_dma(
            const frontend::snes::dma_transfer_diagnostics_t& diagnostics
        ) noexcept;
        [[nodiscard]] bool refresh_live_snapshot(
            const frontend::snes::tile_layer_diagnostics_t& diagnostics,
            frontend::debug_target_t& target,
            std::string& error
        );
        [[nodiscard]] bool capture_live_map(
            const frontend::snes::tile_layer_diagnostics_t& diagnostics,
            frontend::debug_target_t& target,
            size_t layer_index,
            project_t& project,
            std::string& error
        );
        [[nodiscard]] bool navigate(
            std::string_view active_tool_id,
            presentation_direction_t direction,
            bool fine_adjustment = false
        ) noexcept;
    };
}
