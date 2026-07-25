//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"

namespace clover::core
{
    struct apu_t;
    struct bus_t;
    struct cpu_t;
    struct dma_t;
    struct interrupt_controller_t;
    struct ppu_t;
    struct snes_observation_sink_t;

    struct scheduler_causal_state_t
    {
        static constexpr uint32_t schema_version{ 1 };

        master_clock_count_t master_clock{ 0 };
        uint64_t frame_index{ 0 };

        [[nodiscard]] bool operator==(const scheduler_causal_state_t&) const noexcept = default;
    };

    struct scheduler_t
    {
    public:
        void reset() noexcept;
        void set_observation_sink(snes_observation_sink_t* sink) noexcept;
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
        [[nodiscard]] scheduler_causal_state_t capture_causal_state() const noexcept;
        void restore_causal_state(const scheduler_causal_state_t& state) noexcept;

    private:
        master_clock_count_t _master_clock{ 0 };
        uint64_t _frame_index{ 0 };
        snes_observation_sink_t* _observation_sink{ nullptr };
    };
}
