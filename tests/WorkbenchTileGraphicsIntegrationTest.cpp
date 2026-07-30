//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/TileGraphics.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/SnesDebugger.h"

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
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x15u, 0x21u,      // STA $2115: increment after high
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x16u, 0x21u,      // STA $2116: VRAM address low
            0x8du, 0x17u, 0x21u,      // STA $2117: VRAM address high
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x18u, 0x21u,      // STA $2118: plane 0, pixel 0
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x19u, 0x21u,      // STA $2119: plane 1
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
            "WorkbenchTileGraphicsIntegrationTest failed at %s%s%s\n",
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
    for (size_t step{}; step < 9u; ++step)
    {
        if (!debugger.step_instruction(error))
            return fail("execute_vram_upload", error);
    }

    const analysis::tile_asset_t asset{
        .stable_id = "integration.vram",
        .name = "Live VRAM",
        .location = { "snes.vram", 0u },
        .tile_count = 1u,
        .format = analysis::tile_format_t::snes_2bpp
    };
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
    const analysis::decoded_tile_set_t decoded{
        analysis::decode_tiles(asset, reader)
    };
    if (!decoded.complete() || decoded.tiles.size() != 1u
        || decoded.tiles.front().pixels[0] != 1u
        || decoded.tiles.front().pixels[1] != 0u)
    {
        return fail("live_vram_decode");
    }

    std::printf(
        "Workbench tile integration passed: CPU upload -> side-effect-free "
        "VRAM -> SNES planar pixels\n"
    );
    return 0;
}
