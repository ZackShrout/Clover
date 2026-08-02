//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Oam.h"
#include "clover/analysis/snes/Palette.h"
#include "clover/analysis/snes/TileGraphics.h"
#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/snes/SnesDebugger.h"

#include <array>
#include <cstddef>
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
            0xa9u, 0x00u, 0x8du, 0x01u, 0x21u, // OBJSEL: base 0, 8/16
            0xa9u, 0x10u, 0x8du, 0x2cu, 0x21u, // enable OBJ main screen
            0xa9u, 0x00u, 0x8du, 0x02u, 0x21u, // OAM address low
            0x8du, 0x03u, 0x21u,               // OAM address high
            0xa9u, 0x20u, 0x8du, 0x04u, 0x21u, // x = 32
            0xa9u, 0x28u, 0x8du, 0x04u, 0x21u, // y = 40
            0xa9u, 0x00u, 0x8du, 0x04u, 0x21u, // character 0
            0xa9u, 0x06u, 0x8du, 0x04u, 0x21u, // palette 3
            0xa9u, 0x80u, 0x8du, 0x15u, 0x21u, // VRAM increment high
            0xa9u, 0x00u, 0x8du, 0x16u, 0x21u,
            0x8du, 0x17u, 0x21u,
            0xa9u, 0x80u, 0x8du, 0x18u, 0x21u, // tile pixel 0 = 1
            0xa9u, 0x00u, 0x8du, 0x19u, 0x21u,
            0xa9u, 0xb1u, 0x8du, 0x21u, 0x21u, // OBJ palette 3 color 1
            0xa9u, 0x1fu, 0x8du, 0x22u, 0x21u,
            0xa9u, 0x00u, 0x8du, 0x22u, 0x21u,
            0x80u, 0xfeu
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
            "WorkbenchOamIntegrationTest failed at %s%s%s\n",
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
    const auto* const object_diagnostics{
        dynamic_cast<const frontend::snes::object_layer_diagnostics_t*>(
            emulator.get()
        )
    };
    workbench::snes::snes_debugger_t debugger{};
    std::string error{};
    if (target == nullptr || object_diagnostics == nullptr
        || !debugger.initialize(*target, error))
        return fail("debugger", error);
    for (size_t step{}; step < 40u; ++step)
    {
        if (!debugger.step_instruction(error))
            return fail("execute_upload", error);
    }

    frontend::snes::object_layer_state_t layer{};
    if (!object_diagnostics->inspect_object_layer(layer)
        || !layer.active
        || layer.tile_base_word_address != 0u
        || layer.base_size != 0u)
    {
        return fail("live_object_layer");
    }
    std::array<std::byte, 544> raw_oam{};
    if (target->inspect_memory(layer.oam, raw_oam).status
            != frontend::memory_inspection_status_t::complete)
    {
        return fail("read_oam");
    }
    const analysis::decoded_snes_oam_t oam{
        analysis::decode_snes_oam(
            {
                .tile_base_word_address = layer.tile_base_word_address,
                .name_select = layer.name_select,
                .base_size = layer.base_size,
                .interlace = layer.interlace
            },
            [&raw_oam](uint16_t address) -> std::optional<uint8_t>
            {
                return std::to_integer<uint8_t>(raw_oam[address]);
            }
        )
    };
    const analysis::snes_oam_object_t& object{ oam.objects[0] };
    if (!oam.complete() || object.screen_x != 32 || object.y != 40u
        || object.character != 0u || object.palette != 3u
        || object.width != 8u || object.height != 8u
        || !object.intersects_viewport)
    {
        return fail("decode_live_object");
    }

    const auto reader{
        [target](const analysis::address_t& address) -> std::optional<uint8_t>
        {
            const frontend::address_space_id_t space{
                address.address_space == "snes.vram"
                    ? frontend::snes_debug::k_vram_space
                    : frontend::snes_debug::k_cgram_space
            };
            std::byte byte{};
            const auto result{ target->inspect_memory(
                { space, address.address },
                std::span<std::byte>{ &byte, 1u }
            ) };
            return result.status
                    == frontend::memory_inspection_status_t::complete
                ? std::optional<uint8_t>{ std::to_integer<uint8_t>(byte) }
                : std::nullopt;
        }
    };
    const analysis::decoded_tile_set_t tile{
        analysis::decode_tiles(
            {
                .stable_id = "obj-tile",
                .name = "OBJ tile",
                .location = { "snes.vram", 0u },
                .tile_count = 1u,
                .format = analysis::tile_format_t::snes_4bpp
            },
            reader
        )
    };
    const analysis::decoded_palette_t palette{
        analysis::decode_palette(
            {
                .stable_id = "obj-palette",
                .name = "OBJ palette",
                .location = { "snes.cgram", 0u },
                .color_count = 256u
            },
            reader
        )
    };
    if (!tile.complete() || tile.tiles[0].pixels[0] != 1u
        || !palette.complete() || palette.colors[177u].raw_value != 0x001fu)
    {
        return fail("decode_live_object_graphics");
    }

    std::printf(
        "Workbench OAM integration passed: CPU PPU uploads -> read-only "
        "OAM/VRAM/CGRAM -> decoded live OBJ\n"
    );
    return 0;
}
