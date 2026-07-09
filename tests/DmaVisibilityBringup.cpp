//
// Created by Zack Shrout on 7/9/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
    [[nodiscard]] const char* slot_owner_name(clover::core::hardware_slot_owner_t owner) noexcept
    {
        return owner == clover::core::hardware_slot_owner_t::dma ? "dma" : "cpu";
    }

    [[nodiscard]] const char* dma_activity_name(clover::core::dma_activity_t activity) noexcept
    {
        switch (activity)
        {
        case clover::core::dma_activity_t::idle:
            return "idle";
        case clover::core::dma_activity_t::general_dma:
            return "general";
        case clover::core::dma_activity_t::hdma_setup:
            return "hdma_setup";
        case clover::core::dma_activity_t::hdma_transfer:
            return "hdma_transfer";
        default:
            return "unknown";
        }
    }

    void seed_dma_visibility_case(clover::core::console_t& console) noexcept
    {
        console.power_on();
        console.write_u8(0x7e1234u, 0x4au);
        console.write_u8(0x7e1235u, 0x7cu);
        console.write_u8(0x7e2000u, 0x81u);
        console.write_u8(0x7e2001u, 0x00u);
        console.write_u8(0x7e2002u, 0x21u);
        console.write_u8(0x7e2100u, 0x99u);

        console.write_u8(0x004200u, 0x90u);
        console.write_u8(0x004207u, 0x10u);
        console.write_u8(0x004208u, 0x00u);

        console.write_u8(0x004300u, 0x40u);
        console.write_u8(0x004301u, 0x19u);
        console.write_u8(0x004302u, 0x00u);
        console.write_u8(0x004303u, 0x20u);
        console.write_u8(0x004304u, 0x7eu);
        console.write_u8(0x004307u, 0x7eu);
        console.write_u8(0x00420cu, 0x01u);

        console.write_u8(0x004310u, 0x01u);
        console.write_u8(0x004311u, 0x18u);
        console.write_u8(0x004312u, 0x34u);
        console.write_u8(0x004313u, 0x12u);
        console.write_u8(0x004314u, 0x7eu);
        console.write_u8(0x004315u, 0x02u);
        console.write_u8(0x004316u, 0x00u);
        console.write_u8(0x004317u, 0x40u);
        console.write_u8(0x004318u, 0x78u);
        console.write_u8(0x004319u, 0x56u);
        console.write_u8(0x00431au, 0x81u);
        console.write_u8(0x00431bu, 0xa5u);
        console.write_u8(0x00420bu, 0x02u);
    }

    void print_snapshot(uint64_t step_index,
                        const clover::core::console_t& console,
                        const clover::core::hardware_step_result_t& step,
                        clover::core::dma_activity_t before_activity,
                        bool before_general_pending,
                        bool before_hdma_pending) noexcept
    {
        const auto snapshot{ console.capture_timing_snapshot() };
        const auto& cpu{ console.cpu_state() };

        std::printf(
            "step=%llu slot=%s scanline=%u dot=%u pc=%02x:%04x "
            "before_dma=%s after_dma=%s before_general=%u after_general=%u "
            "before_hdma=%u after_hdma=%u entered_hblank=%u hdma_setup=%u hdma_transfer=%u\n",
            static_cast<unsigned long long>(step_index),
            slot_owner_name(step.slot_owner),
            snapshot.ppu_timing.raster.scanline,
            snapshot.ppu_timing.raster.dot,
            cpu.pb,
            cpu.pc,
            dma_activity_name(before_activity),
            dma_activity_name(console.dma_activity()),
            before_general_pending ? 1u : 0u,
            console.general_dma_pending() ? 1u : 0u,
            before_hdma_pending ? 1u : 0u,
            console.hdma_pending() ? 1u : 0u,
            step.ppu.entered_hblank ? 1u : 0u,
            step.ppu.hdma_setup_triggered ? 1u : 0u,
            step.ppu.hdma_transfer_triggered ? 1u : 0u
        );
    }
}

int main(int argc, char** argv)
{
    int max_steps{ 64 };
    if (argc >= 2)
    {
        max_steps = std::atoi(argv[1]);
        if (max_steps <= 0)
            max_steps = 64;
    }

    clover::core::console_t console{};
    seed_dma_visibility_case(console);

    std::printf("clover dma visibility microcase\n");
    for (int step_index{ 0 }; step_index < max_steps; ++step_index)
    {
        const clover::core::dma_activity_t before_activity{ console.dma_activity() };
        const bool before_general_pending{ console.general_dma_pending() };
        const bool before_hdma_pending{ console.hdma_pending() };
        const clover::core::hardware_step_result_t step{ console.step_hardware() };

        print_snapshot(static_cast<uint64_t>(step_index),
                       console,
                       step,
                       before_activity,
                       before_general_pending,
                       before_hdma_pending);

        const auto timing{ console.timing() };
        if (!console.general_dma_pending()
            && !console.hdma_pending()
            && timing.raster.scanline > 0)
        {
            break;
        }
    }

    std::printf("result: final_dma=%s final_general=%u final_hdma=%u scanline=%u dot=%u\n",
                dma_activity_name(console.dma_activity()),
                console.general_dma_pending() ? 1u : 0u,
                console.hdma_pending() ? 1u : 0u,
                console.timing().raster.scanline,
                console.timing().raster.dot);
    return 0;
}
