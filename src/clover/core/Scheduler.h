//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/Timing.h"

namespace clover::core
{
    struct apu_t;
    struct bus_t;
    struct cpu_t;
    struct dma_t;
    struct interrupt_controller_t;
    struct ppu_t;

    struct scheduler_t
    {
    public:
        void reset() noexcept;
        [[nodiscard]] hardware_step_result_t step_hardware(cpu_t& cpu,
                                                           bus_t& bus,
                                                           ppu_t& ppu,
                                                           apu_t& apu,
                                                           dma_t& dma,
                                                           interrupt_controller_t& interrupts) noexcept;
        void run_scanline(cpu_t& cpu,
                          bus_t& bus,
                          ppu_t& ppu,
                          apu_t& apu,
                          dma_t& dma,
                          interrupt_controller_t& interrupts) noexcept;
        void run_frame(cpu_t& cpu,
                       bus_t& bus,
                       ppu_t& ppu,
                       apu_t& apu,
                       dma_t& dma,
                       interrupt_controller_t& interrupts) noexcept;
        [[nodiscard]] master_clock_count_t master_clock() const noexcept;
        [[nodiscard]] uint64_t frame_index() const noexcept;

    private:
        master_clock_count_t _master_clock{ 0 };
        uint64_t _frame_index{ 0 };
    };
}
