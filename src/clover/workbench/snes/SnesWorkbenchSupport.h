//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/workbench/WorkbenchTargetSupport.h"

namespace clover::workbench::snes
{
    inline constexpr std::string_view k_output_tool_id{ "workbench.output" };
    inline constexpr std::string_view k_palette_tool_id{ "snes.palette" };
    inline constexpr std::string_view k_tile_graphics_tool_id{
        "snes.tile-graphics"
    };
    inline constexpr std::string_view k_tile_map_tool_id{ "snes.tile-map" };
    inline constexpr std::string_view k_object_tool_id{ "snes.objects" };
    inline constexpr std::string_view k_dma_tool_id{ "snes.dma" };

    class snes_workbench_support_t final : public workbench_target_support_t
    {
    public:
        [[nodiscard]] frontend::system_id_t system() const noexcept override;
        [[nodiscard]] std::string_view stable_id() const noexcept override;
        [[nodiscard]] std::string_view display_name() const noexcept override;
        [[nodiscard]] std::unique_ptr<frontend::emulator_core_t>
            create_core() const noexcept override;
        [[nodiscard]] std::unique_ptr<debugger_t>
            create_debugger() const override;
        [[nodiscard]] bool prepare_project(
            project_t& project,
            const std::filesystem::path& projects_root,
            std::span<const std::byte> media,
            std::string& error
        ) const override;
        [[nodiscard]] bool refresh_hardware_symbols(
            project_t& project,
            std::string& error
        ) const override;
        void register_tools(tool_registry_t& registry) const override;
    };
}
