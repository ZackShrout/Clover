//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TileGraphics.h"
#include "clover/analysis/TileMap.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/SnesDebugger.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_rom()
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0xeau });
        const uint8_t program[]{
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x05u, 0x21u,      // STA $2105: mode 1
            0xa9u, 0x20u,             // LDA #$20
            0x8du, 0x07u, 0x21u,      // STA $2107: BG1 map at word $2000
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x0bu, 0x21u,      // STA $210b: BG1 tiles at word $0000
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x2cu, 0x21u,      // STA $212c: enable BG1 on main screen
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x15u, 0x21u,      // STA $2115: increment after high
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x16u, 0x21u,      // STA $2116
            0x8du, 0x17u, 0x21u,      // STA $2117
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x18u, 0x21u,      // STA $2118: tile plane 0
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x19u, 0x21u,      // STA $2119
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x16u, 0x21u,      // STA $2116: map address low
            0xa9u, 0x20u,             // LDA #$20
            0x8du, 0x17u, 0x21u,      // STA $2117: map address high
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x18u, 0x21u,      // STA $2118: character 0
            0x8du, 0x19u, 0x21u,      // STA $2119: attributes 0
            0x80u, 0xfeu              // BRA *
        };
        for (size_t index{}; index < std::size(program); ++index)
            rom[index] = static_cast<std::byte>(program[index]);
        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x02u };
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint,
                           const std::string& error = {})
    {
        std::fprintf(
            stderr,
            "WorkbenchTileMapIntegrationTest failed at %s%s%s\n",
            checkpoint,
            error.empty() ? "" : ": ",
            error.c_str()
        );
        return 1;
    }
}

int main()
{
    using namespace clover;

    std::unique_ptr<frontend::emulator_core_t> emulator{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    const std::vector<std::byte> rom{ make_rom() };
    if (emulator == nullptr || !emulator->load_media(rom))
        return fail("load");
    emulator->power_on();
    frontend::debug_target_t* const target{ emulator->debug_target() };
    workbench::snes_debugger_t debugger{};
    std::string error{};
    if (target == nullptr || !debugger.initialize(*target, error))
        return fail("debugger", error);
    for (size_t step{}; step < 24u; ++step)
    {
        if (!debugger.step_instruction(error))
            return fail("execute_upload", error);
    }

    std::array<frontend::tile_layer_state_t, 4> layers{};
    if (emulator->inspect_tile_layers(layers) != 4u
        || !layers[0].active
        || layers[0].format != frontend::tile_layer_format_t::indexed_4bpp
        || layers[0].tile_map.value != 0x4000u
        || layers[0].tile_graphics.value != 0u
        || layers[0].width_tiles != 32u || layers[0].height_tiles != 32u
        || layers[0].horizontal_scroll != 0u
        || layers[0].vertical_scroll != 0u
        || layers[3].active
        || layers[3].format != frontend::tile_layer_format_t::inactive)
    {
        return fail("live_layer_snapshot");
    }
    const auto reader{
        [target](const analysis::address_t& address) -> std::optional<uint8_t>
        {
            if (address.address_space != "snes.vram")
                return std::nullopt;
            std::byte byte{};
            const frontend::memory_inspection_result_t result{
                target->inspect_memory(
                    {
                        frontend::snes_debug::k_vram_space,
                        address.address
                    },
                    std::span<std::byte>{ &byte, 1u }
                )
            };
            return result.status
                    == frontend::memory_inspection_status_t::complete
                ? std::optional<uint8_t>{ std::to_integer<uint8_t>(byte) }
                : std::nullopt;
        }
    };
    const analysis::decoded_tile_set_t tiles{
        analysis::decode_tiles(
            {
                .stable_id = "tiles",
                .name = "Tiles",
                .location = { "snes.vram", 0u },
                .tile_count = 1u,
                .format = analysis::tile_format_t::snes_4bpp
            },
            reader
        )
    };
    const analysis::decoded_tile_map_t map{
        analysis::decode_tile_map(
            {
                .stable_id = "map",
                .name = "Map",
                .location = { "snes.vram", 0x4000u },
                .screen_size = 0u,
                .tile_size = 8u,
                .tile_asset_id = "tiles",
                .palette_id = "palette"
            },
            reader
        )
    };
    if (!tiles.complete() || tiles.tiles.front().pixels[0] != 1u
        || !map.complete() || map.entries.front().character != 0u
        || map.entries.front().raw_value != 0u)
    {
        return fail("live_map_decode");
    }

    std::printf(
        "Workbench tile-map integration passed: PPU BG configuration and "
        "CPU uploads -> live layer snapshot -> decoded map and tile\n"
    );
    return 0;
}
