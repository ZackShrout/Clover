//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/SnesDebugger.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <memory>
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
            0x8du, 0x2cu, 0x21u,      // STA $212c: BG1 on main screen
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x15u, 0x21u,      // STA $2115: increment after high
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x16u, 0x21u,      // STA $2116: tile address low
            0x8du, 0x17u, 0x21u,      // STA $2117: tile address high
            0xa9u, 0x80u,             // LDA #$80
            0x8du, 0x18u, 0x21u,      // STA $2118: plane 0, row 0
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x19u, 0x21u,      // STA $2119
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x16u, 0x21u,      // STA $2116: map address low
            0xa9u, 0x20u,             // LDA #$20
            0x8du, 0x17u, 0x21u,      // STA $2117: map address high
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x18u, 0x21u,      // STA $2118: character 0
            0x8du, 0x19u, 0x21u,      // STA $2119: attributes 0
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x21u, 0x21u,      // STA $2121: CGRAM color 1
            0xa9u, 0x1fu,             // LDA #$1f
            0x8du, 0x22u, 0x21u,      // STA $2122: red low byte
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x22u, 0x21u,      // STA $2122: red high byte
            0xa9u, 0x00u,             // LDA #$00
            0x8du, 0x26u, 0x21u,      // STA $2126: window 1 left
            0xa9u, 0x7fu,             // LDA #$7f
            0x8du, 0x27u, 0x21u,      // STA $2127: window 1 right
            0xa9u, 0x02u,             // LDA #$02
            0x8du, 0x23u, 0x21u,      // STA $2123: BG1 uses window 1
            0xa9u, 0x01u,             // LDA #$01
            0x8du, 0x2eu, 0x21u,      // STA $212e: mask BG1 on main screen
            0xa9u, 0x0fu,             // LDA #$0f
            0x8du, 0x00u, 0x21u,      // STA $2100: unblank, full brightness
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
            "WorkbenchLayerFrameIntegrationTest failed at %s%s%s\n",
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
    if (target == nullptr || !debugger.initialize(*target, error)
        || !debugger.resume(error))
    {
        return fail("debugger", error);
    }

    frontend::video_plane_control_t* const planes{
        emulator->video_plane_control()
    };
    if (planes == nullptr)
        return fail("plane_control");

    frontend::video_plane_frame_view_t frame{};
    for (size_t attempt{}; attempt < 8u; ++attempt)
    {
        if (planes->inspect_video_plane_frame(0u, frame))
            break;
        if (debugger.pump_fast(50'000u, error) == 0u && !error.empty())
            return fail("fast_run", error);
    }
    if (frame.pixels == nullptr || frame.width != 256u
        || frame.height != 240u || frame.pitch_bytes < 256u * sizeof(uint32_t)
        || frame.format != frontend::pixel_format_t::argb8888
        || frame.frame_index == 0u)
    {
        return fail("completed_frame");
    }

    const auto* const pixels{ static_cast<const uint32_t*>(frame.pixels) };
    const size_t pitch{ frame.pitch_bytes / sizeof(uint32_t) };
    bool found_red_left{ false };
    bool found_red_right{ false };
    bool found_transparent_pixel{ false };
    for (size_t y{}; y < frame.height; ++y)
    {
        for (size_t x{}; x < frame.width; ++x)
        {
            const uint32_t pixel{ pixels[y * pitch + x] };
            found_transparent_pixel |= pixel == 0u;
            const bool red{ (pixel & 0xffff0000u) == 0xffff0000u
                && (pixel & 0x0000ffffu) == 0u };
            if (x <= 127u)
                found_red_left |= red;
            else
                found_red_right |= red;
        }
    }
    if (found_red_left || !found_red_right || !found_transparent_pixel)
        return fail("raw_bg_pixels");

    // Instruction-domain execution does not enter run_frame(), but Workbench
    // must still be able to publish the latest completed composite frame for
    // its live-output view.
    emulator->refresh_video_frame();
    const frontend::video_frame_view_t composite{ emulator->video_frame() };
    if (composite.pixels == nullptr || composite.width != 256u
        || composite.height != 240u
        || composite.pitch_bytes < 256u * sizeof(uint32_t)
        || composite.format != frontend::pixel_format_t::argb8888)
    {
        return fail("composite_frame");
    }
    const auto* const composite_pixels{
        static_cast<const uint32_t*>(composite.pixels)
    };
    const size_t composite_pitch{
        composite.pitch_bytes / sizeof(uint32_t)
    };
    bool found_composite_red{};
    for (size_t y{}; y < composite.height && !found_composite_red; ++y)
    {
        for (size_t x{ 128u }; x < composite.width; ++x)
        {
            const uint32_t pixel{
                composite_pixels[y * composite_pitch + x]
            };
            if ((pixel & 0xffff0000u) == 0xffff0000u
                && (pixel & 0x0000ffffu) == 0u)
            {
                found_composite_red = true;
                break;
            }
        }
    }
    if (!found_composite_red)
        return fail("debug_live_output");

    frontend::video_plane_frame_view_t invalid{};
    if (planes->inspect_video_plane_frame(4u, invalid)
        || invalid.pixels != nullptr)
    {
        return fail("invalid_plane");
    }

    std::printf(
        "Workbench layer-frame integration passed: optimized debug run -> "
        "completed raster-time BG and live composite frames\n"
    );
    return 0;
}
