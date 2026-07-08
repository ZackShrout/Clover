//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Scheduler.h"

#include "clover/core/Apu.h"
#include "clover/core/Bus.h"
#include "clover/core/Cpu.h"
#include "clover/core/Dma.h"
#include "clover/core/Interrupts.h"
#include "clover/core/Ppu.h"

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
        }

        result.elapsed_master_clocks = cpu.apply_system_timing(result.elapsed_master_clocks, ppu.video_timing());

        _master_clock += result.elapsed_master_clocks;

        if (result.slot_owner == hardware_slot_owner_t::cpu)
            result.ppu = bus.step_ppu_with_cpu_writes(result.elapsed_master_clocks);
        else
            result.ppu = ppu.step(result.elapsed_master_clocks);

        if (result.slot_owner == hardware_slot_owner_t::cpu)
            bus.step_apu_with_cpu_writes(result.elapsed_master_clocks);
        else
            apu.step(result.elapsed_master_clocks);
        cpu.on_ppu_step(result.elapsed_master_clocks,
                        ppu.video_timing(),
                        result.ppu,
                        dma,
                        interrupts);
        if (result.ppu.frame_complete)
            ++_frame_index;

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
