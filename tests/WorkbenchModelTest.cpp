//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/ToolRegistry.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace
{
    void require(bool condition, std::string_view message)
    {
        if (condition)
            return;
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

int main()
{
    using namespace clover::workbench;

    tool_registry_t registry{};
    require(registry.register_tool({
        .id = "workbench.disassembly",
        .title = "Disassembly",
        .owner = "workbench"
    }), "register generic tool");
    require(registry.register_tool({
        .id = "snes.palette",
        .title = "Palette",
        .owner = "snes"
    }), "register system tool");
    require(!registry.register_tool({
        .id = "snes.palette",
        .title = "Duplicate",
        .owner = "snes"
    }), "reject duplicate tool ID");
    require(registry.register_command({
        .id = "snes.palette.toggle",
        .label = "Toggle palette",
        .owner = "snes",
        .activation = command_activation_t::toggle_tool,
        .tool = "snes.palette"
    }), "register command for known tool");
    require(!registry.register_command({
        .id = "genesis.vdp.toggle",
        .label = "Toggle VDP",
        .owner = "genesis",
        .activation = command_activation_t::toggle_tool,
        .tool = "genesis.vdp"
    }), "reject command for unknown tool");
    require(registry.tool("snes.palette") != nullptr,
            "look up registered tool");
    require(registry.command("snes.palette.toggle") != nullptr,
            "look up registered command");

    active_tool_t active{};
    tool_active_flag_t palette{ active, "snes.palette" };
    tool_active_flag_t output{ active, "workbench.output" };
    require(active.active() == k_disassembly_tool_id,
            "disassembly is the default tool");
    require(registry.activate_command("snes.palette.toggle", active),
            "activate registered toggle command");
    require(palette && !output, "activate one tool");
    output = true;
    require(output && !palette, "activation is mutually exclusive");
    palette = false;
    require(output, "closing an inactive tool preserves active selection");
    output = false;
    require(active.active() == k_disassembly_tool_id,
            "closing active tool returns to fallback");
    active.toggle("snes.palette");
    require(palette, "toggle opens tool");
    active.toggle("snes.palette");
    require(active.active() == k_disassembly_tool_id, "toggle closes tool");
    require(!registry.activate_command("missing.command", active),
            "unknown command is not activated");

    std::cout << "Workbench model tests passed\n";
    return 0;
}
