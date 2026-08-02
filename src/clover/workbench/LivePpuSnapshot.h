//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/Palette.h"
#include "clover/analysis/TileGraphics.h"
#include "clover/analysis/TileMap.h"
#include "clover/frontend/EmulatorCore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace clover::workbench
{
    struct live_ppu_snapshot_t
    {
        frontend::tile_layer_state_t layer{};
        std::array<std::byte, 65536u> vram{};
        std::array<std::byte, 512u> cgram{};

        [[nodiscard]] std::optional<uint8_t> inspect_byte(
            const analysis::address_t& address
        ) const noexcept;
    };

    struct live_bg_assets_t
    {
        analysis::palette_asset_t palette{};
        analysis::tile_asset_t tiles{};
        analysis::tile_map_asset_t map{};
    };

    [[nodiscard]] bool capture_live_ppu_snapshot(
        frontend::emulator_core_t& core,
        frontend::debug_target_t& target,
        size_t layer_index,
        live_ppu_snapshot_t& destination,
        std::string& error
    );

    [[nodiscard]] std::optional<live_bg_assets_t> make_live_bg_assets(
        const frontend::tile_layer_state_t& layer
    );
}
