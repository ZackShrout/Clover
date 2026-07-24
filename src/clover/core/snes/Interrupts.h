//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"

namespace clover::core
{
    struct interrupt_state_t
    {
        bool nmi_line{ false };
        bool nmi_hold{ false };
        bool nmi_transition{ false };
        bool irq_line{ false };
        bool irq_hold{ false };
        bool irq_transition{ false };
        bool nmi_pending{ false };
        bool irq_pending{ false };
        bool irq_lock{ false };
    };

    struct interrupt_controller_t
    {
    public:
        void reset() noexcept;
        void assert_nmi_line() noexcept;
        void clear_nmi_line() noexcept;
        void force_nmi_transition() noexcept;
        void assert_irq_line() noexcept;
        void clear_irq_line() noexcept;
        void set_cartridge_irq_line(bool asserted) noexcept;
        void clear_irq_status_line() noexcept;
        void cancel_irq_delivery() noexcept;
        void force_irq_transition() noexcept;
        void advance_to_observation_point(master_clock_count_t observation_clock) noexcept;
        void latch_from_lines() noexcept;
        void observe_opcode_edge(bool irq_disabled) noexcept;
        void observe_opcode_edge(master_clock_count_t observation_clock,
                                 bool irq_disabled) noexcept;
        [[nodiscard]] interrupt_state_t sample() const noexcept;
        bool consume_nmi() noexcept;
        bool consume_irq() noexcept;
        [[nodiscard]] bool irq_lock() const noexcept;
        void set_irq_lock() noexcept;
        void clear_irq_lock() noexcept;

    private:
        void refresh_irq_line() noexcept;
        interrupt_state_t _state{};
        bool _cpu_irq_line{ false };
        bool _cartridge_irq_line{ false };
        // The 65816 samples transitions on its last-cycle edge. Preserve the
        // poll clock so a transition raised during that final cycle waits for
        // the following instruction.
        master_clock_count_t _nmi_transition_clock{ 0 };
        master_clock_count_t _irq_transition_clock{ 0 };
    };
}
