//
// Created by Zack Shrout on 7/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Palette.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/snes/SnesDebugger.h"

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
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x21u, 0x21u,      // STA $2121
            0xa9u, 0x1fu,             // LDA #$1f
            0x8du, 0x22u, 0x21u,      // STA $2122 low
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x22u, 0x21u,      // STA $2122 high
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
            "WorkbenchPaletteIntegrationTest failed at %s%s%s\n",
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
    workbench::snes::snes_debugger_t debugger{};
    std::string error{};
    if (target == nullptr || !debugger.initialize(*target, error))
        return fail("debugger", error);
    for (size_t step{}; step < 6u; ++step)
    {
        if (!debugger.step_instruction(error))
            return fail("execute_palette_upload", error);
    }

    const analysis::palette_asset_t palette{
        .stable_id = "integration.cgram",
        .name = "Live CGRAM",
        .location = { "snes.cgram", 0u },
        .color_count = 1u
    };
    const auto reader{
        [target](const analysis::address_t& address) -> std::optional<uint8_t>
        {
            if (address.address_space != "snes.cgram")
                return std::nullopt;
            std::byte byte{};
            const frontend::memory_inspection_result_t result{
                target->inspect_memory(
                    {
                        frontend::snes_debug::k_cgram_space,
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
    const analysis::decoded_palette_t decoded{
        analysis::decode_palette(palette, reader)
    };
    if (!decoded.complete() || decoded.colors.size() != 1u
        || decoded.colors.front().raw_value != 0x001fu
        || decoded.colors.front().red8 != 255u
        || decoded.colors.front().green8 != 0u
        || decoded.colors.front().blue8 != 0u)
    {
        return fail("live_cgram_decode");
    }

    std::printf(
        "Workbench palette integration passed: CPU upload -> side-effect-free "
        "CGRAM -> BGR555 color\n"
    );
    return 0;
}
