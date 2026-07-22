//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Scheduler.h"

#include "clover/core/snes/Apu.h"
#include "clover/core/snes/Bus.h"
#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/Dma.h"
#include "clover/core/snes/Interrupts.h"
#include "clover/core/snes/Ppu.h"

namespace
{
    void accumulate_ppu_step_result(clover::core::ppu_step_result_t& aggregate,
                                    const clover::core::ppu_step_result_t& step) noexcept
    {
        aggregate.timing = step.timing;
        aggregate.visible_scanlines = step.visible_scanlines;
        aggregate.interlace = step.interlace;
        aggregate.frame_complete = aggregate.frame_complete || step.frame_complete;
        aggregate.frames_completed += step.frames_completed;
        aggregate.entered_scanline = aggregate.entered_scanline || step.entered_scanline;
        aggregate.entered_frame_start =
            aggregate.entered_frame_start || step.entered_frame_start;
        aggregate.entered_hblank = aggregate.entered_hblank || step.entered_hblank;
        aggregate.entered_vblank = aggregate.entered_vblank || step.entered_vblank;
        aggregate.hdma_setup_triggered =
            aggregate.hdma_setup_triggered || step.hdma_setup_triggered;
        aggregate.hdma_transfer_triggered =
            aggregate.hdma_transfer_triggered || step.hdma_transfer_triggered;
        aggregate.nmi_requested = aggregate.nmi_requested || step.nmi_requested;
        aggregate.irq_requested = aggregate.irq_requested || step.irq_requested;
    }
}

namespace clover::core
{
    void scheduler_t::reset() noexcept
    {
        _master_clock = 0;
        _frame_index = 0;
    }

    hardware_step_result_t scheduler_t::step_hardware(cpu_t& cpu,
                                                      bus_t& bus,
                                                      ppu_t& ppu,
                                                      apu_t& apu,
                                                      dma_t& dma,
                                                      interrupt_controller_t& interrupts) noexcept
    {
        hardware_step_result_t result{};
        result.slot_owner = cpu.next_slot_owner(dma);
        if (result.slot_owner == hardware_slot_owner_t::dma)
        {
            const dma_step_result_t dma_step{ dma.step(bus, cpu.dma_phase()) };
            result.elapsed_master_clocks = dma_step.master_clocks;
            cpu.on_dma_step(dma, interrupts);
        }
        else
        {
            const cpu_step_result_t cpu_step{ cpu.step(bus, dma, interrupts) };
            result.elapsed_master_clocks = cpu_step.master_clocks;
            result.ppu = cpu_step.ppu;
        }

        if (result.slot_owner == hardware_slot_owner_t::dma)
        {
            result.elapsed_master_clocks = cpu.apply_system_timing(result.elapsed_master_clocks, ppu.video_timing());
            cpu.account_external_cpu_clocks(result.elapsed_master_clocks);
            const timing_snapshot_t starting_timing{ ppu.timing() };
            const master_clock_delta_t clocks_to_scanline{
                static_cast<master_clock_delta_t>(
                    ppu.current_scanline_clocks() - starting_timing.raster.dot
                )
            };
            if (clocks_to_scanline <= result.elapsed_master_clocks)
            {
                accumulate_ppu_step_result(
                    result.ppu,
                    bus.step_ppu_with_cpu_writes(clocks_to_scanline)
                );
                apu.step(clocks_to_scanline);
                apu.synchronize_cpu_thread();

                const master_clock_delta_t remaining{
                    static_cast<master_clock_delta_t>(
                        result.elapsed_master_clocks - clocks_to_scanline
                    )
                };
                if (remaining != 0)
                {
                    accumulate_ppu_step_result(
                        result.ppu,
                        bus.step_ppu_with_cpu_writes(remaining)
                    );
                    apu.step(remaining);
                }
            }
            else
            {
                result.ppu = bus.step_ppu_with_cpu_writes(result.elapsed_master_clocks);
                apu.step(result.elapsed_master_clocks);
            }
            cpu.on_ppu_step(result.elapsed_master_clocks,
                            ppu.video_timing(),
                            result.ppu,
                            dma,
                            interrupts);
        }

        _master_clock += result.elapsed_master_clocks;
        _frame_index += result.ppu.frames_completed;

        return result;
    }

    void scheduler_t::run_scanline(cpu_t& cpu,
                                   bus_t& bus,
                                   ppu_t& ppu,
                                   apu_t& apu,
                                   dma_t& dma,
                                   interrupt_controller_t& interrupts) noexcept
    {
        const timing_snapshot_t starting_timing{ ppu.timing() };
        while (true)
        {
            const hardware_step_result_t step{
                step_hardware(cpu, bus, ppu, apu, dma, interrupts)
            };

            if (step.ppu.frame_complete)
                return;

            if (step.ppu.timing.raster.scanline != starting_timing.raster.scanline)
                return;
        }
    }

    void scheduler_t::run_frame(cpu_t& cpu,
                                bus_t& bus,
                                ppu_t& ppu,
                                apu_t& apu,
                                dma_t& dma,
                                interrupt_controller_t& interrupts) noexcept
    {
        while (true)
        {
            const hardware_step_result_t step{
                step_hardware(cpu, bus, ppu, apu, dma, interrupts)
            };
            if (step.ppu.frame_complete)
                return;
        }
    }

    master_clock_count_t scheduler_t::master_clock() const noexcept
    {
        return _master_clock;
    }

    uint64_t scheduler_t::frame_index() const noexcept
    {
        return _frame_index;
    }
}
