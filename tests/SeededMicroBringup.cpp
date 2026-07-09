//
// Created by Zack Shrout on 7/8/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace
{
    void seed_hirq_cli_case(clover::core::console_t& console)
    {
        console.power_on();
        console.write_u8(0x000000u, 0x58u);
        for (uint32_t address{ 0x000001u }; address <= 0x00000au; ++address)
            console.write_u8(address, 0xeau);
        console.write_u8(0x001234u, 0xeau);
        console.write_u8(0x00fffeu, 0x34u);
        console.write_u8(0x00ffffu, 0x12u);
        console.write_u8(0x004207u, 0x10u);
        console.write_u8(0x004208u, 0x00u);
        console.write_u8(0x004200u, 0x10u);
    }

    void print_step(uint64_t step_index, const clover::core::console_t& console)
    {
        const auto& cpu{ console.cpu_state() };
        const auto snapshot{ console.capture_timing_snapshot() };
        const auto& interrupts{ console.interrupts() };
        std::printf("step=%llu scanline=%u dot=%u poll_phase=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x irq_line=%u irq_pending=%u irq_lock=%u\n",
                    static_cast<unsigned long long>(step_index),
                    snapshot.ppu_timing.raster.scanline,
                    snapshot.ppu_timing.raster.dot,
                    console.cpu_interrupt_poll_phase_for_testing(),
                    cpu.pb,
                    cpu.pc,
                    cpu.a,
                    cpu.x,
                    cpu.y,
                    cpu.sp,
                    cpu.p,
                    interrupts.irq_line ? 1u : 0u,
                    interrupts.irq_pending ? 1u : 0u,
                    interrupts.irq_lock ? 1u : 0u);
    }
}

int main(int argc, char** argv)
{
    int max_steps{ 32 };
    int initial_poll_phase{ 0 };
    if (argc >= 2)
    {
        max_steps = std::atoi(argv[1]);
        if (max_steps <= 0)
            max_steps = 32;
    }
    if (argc >= 3)
    {
        initial_poll_phase = std::atoi(argv[2]);
        if (initial_poll_phase < 0)
            initial_poll_phase = 0;
    }

    clover::core::console_t console{};
    seed_hirq_cli_case(console);
    console.set_cpu_interrupt_poll_phase_for_testing(static_cast<clover::core::master_clock_delta_t>(initial_poll_phase));

    std::printf("clover seeded microcase: hirq_cli poll_phase=%d\n", initial_poll_phase);
    print_step(0, console);

    bool entered_irq{ false };
    for (int step_index{ 1 }; step_index <= max_steps; ++step_index)
    {
        static_cast<void>(console.step_hardware());
        print_step(static_cast<uint64_t>(step_index), console);
        if (console.cpu_state().pc == 0x1234u)
        {
            entered_irq = true;
            break;
        }
    }

    std::printf("result: entered_irq=%u final_pc=%04x final_sp=%04x stacked_p=%02x\n",
                entered_irq ? 1u : 0u,
                console.cpu_state().pc,
                console.cpu_state().sp,
                console.read_u8(0x0001fdu));
    return 0;
}
