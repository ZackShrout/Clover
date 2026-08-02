//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesWorkbenchSupport.h"

#include "clover/workbench/Project.h"
#include "clover/workbench/SnesDebugger.h"

namespace clover::workbench::snes
{
    frontend::system_id_t snes_workbench_support_t::system() const noexcept
    {
        return frontend::system_id_t::snes;
    }

    std::string_view snes_workbench_support_t::stable_id() const noexcept
    {
        return "snes";
    }

    std::string_view snes_workbench_support_t::display_name() const noexcept
    {
        return "Super Nintendo Entertainment System";
    }

    std::unique_ptr<frontend::emulator_core_t>
        snes_workbench_support_t::create_core() const noexcept
    {
        return frontend::create_emulator_core(system());
    }

    std::unique_ptr<debugger_t>
        snes_workbench_support_t::create_debugger() const
    {
        return std::make_unique<snes_debugger_t>();
    }

    bool snes_workbench_support_t::prepare_project(
        project_t& project,
        const std::filesystem::path& projects_root,
        std::span<const std::byte> media,
        std::string& error
    ) const
    {
        return project.open(projects_root, system(), media, error)
            && project.import_snes_hardware_symbols(error);
    }

    bool snes_workbench_support_t::refresh_hardware_symbols(
        project_t& project,
        std::string& error
    ) const
    {
        return project.import_snes_hardware_symbols(error);
    }

    void snes_workbench_support_t::register_tools(
        tool_registry_t& registry
    ) const
    {
        const auto add_tool = [&registry](
            std::string_view id,
            std::string_view title,
            std::string_view owner
        )
        {
            static_cast<void>(registry.register_tool({
                .id = std::string{ id },
                .title = std::string{ title },
                .owner = std::string{ owner }
            }));
        };
        add_tool(k_disassembly_tool_id, "Disassembly", "workbench");
        add_tool(k_output_tool_id, "Live Game Output", "workbench");
        add_tool(k_palette_tool_id, "Palette / CGRAM", stable_id());
        add_tool(k_tile_graphics_tool_id, "Tile Graphics / VRAM", stable_id());
        add_tool(k_tile_map_tool_id, "Background Tile Map", stable_id());
        add_tool(k_object_tool_id, "Objects / OAM", stable_id());
        add_tool(k_dma_tool_id, "DMA / HDMA Transfers", stable_id());

        const auto add_toggle = [&registry](
            std::string_view id,
            std::string_view label,
            std::string_view tool,
            std::string_view owner
        )
        {
            static_cast<void>(registry.register_command({
                .id = std::string{ id },
                .label = std::string{ label },
                .owner = std::string{ owner },
                .activation = command_activation_t::toggle_tool,
                .tool = std::string{ tool }
            }));
        };
        add_toggle("workbench.output.toggle", "Toggle live output",
                   k_output_tool_id, "workbench");
        add_toggle("snes.palette.toggle", "Toggle palette inspector",
                   k_palette_tool_id, stable_id());
        add_toggle("snes.tiles.toggle", "Toggle tile inspector",
                   k_tile_graphics_tool_id, stable_id());
        add_toggle("snes.tile-map.toggle", "Toggle tile-map inspector",
                   k_tile_map_tool_id, stable_id());
        add_toggle("snes.objects.toggle", "Toggle OAM inspector",
                   k_object_tool_id, stable_id());
        add_toggle("snes.dma.toggle", "Toggle DMA inspector",
                   k_dma_tool_id, stable_id());
    }
}
