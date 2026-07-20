//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Interrupts.h"

#include <limits>

namespace clover::core
{
    void interrupt_controller_t::reset() noexcept
    {
        _state = {};
        _nmi_transition_clock = 0;
        _irq_transition_clock = 0;
    }

    void interrupt_controller_t::assert_nmi_line() noexcept
    {
        if (!_state.nmi_line)
            _state.nmi_hold = true;

        _state.nmi_line = true;
    }

    void interrupt_controller_t::clear_nmi_line() noexcept
    {
        _state.nmi_line = false;
    }

    void interrupt_controller_t::force_nmi_transition() noexcept
    {
        _state.nmi_transition = true;
        _nmi_transition_clock = 0;
    }

    void interrupt_controller_t::assert_irq_line() noexcept
    {
        if (!_state.irq_line)
            _state.irq_hold = true;

        _state.irq_line = true;
    }

    void interrupt_controller_t::clear_irq_line() noexcept
    {
        _state.irq_line = false;
    }

    void interrupt_controller_t::clear_irq_status_line() noexcept
    {
        _state.irq_line = false;
        _state.irq_hold = false;
        _state.irq_transition = false;
    }

    void interrupt_controller_t::cancel_irq_delivery() noexcept
    {
        _state.irq_line = false;
        _state.irq_hold = false;
        _state.irq_transition = false;
        _state.irq_pending = false;
    }

    void interrupt_controller_t::force_irq_transition() noexcept
    {
        _state.irq_transition = true;
        _irq_transition_clock = 0;
    }

    void interrupt_controller_t::advance_to_observation_point(
        master_clock_count_t observation_clock) noexcept
    {
        if (_state.nmi_hold)
        {
            _state.nmi_hold = false;
            _state.nmi_transition = true;
            _nmi_transition_clock = observation_clock;
        }

        if (_state.irq_hold)
        {
            _state.irq_hold = false;
        }
        else if (_state.irq_line)
        {
            if (!_state.irq_transition)
                _irq_transition_clock = observation_clock;
            _state.irq_transition = true;
        }
    }

    void interrupt_controller_t::latch_from_lines() noexcept
    {
        if (_state.irq_lock)
            return;
    }

    void interrupt_controller_t::observe_opcode_edge(bool irq_disabled) noexcept
    {
        observe_opcode_edge(
            std::numeric_limits<master_clock_count_t>::max(),
            irq_disabled
        );
    }

    void interrupt_controller_t::observe_opcode_edge(
        master_clock_count_t observation_clock,
        bool irq_disabled) noexcept
    {
        if (_state.irq_lock)
            return;

        if (_state.nmi_transition && _nmi_transition_clock <= observation_clock)
        {
            _state.nmi_pending = true;
            _state.nmi_transition = false;
        }

        if (_state.irq_transition && _irq_transition_clock <= observation_clock)
        {
            if (!irq_disabled)
                _state.irq_pending = true;

            _state.irq_transition = false;
        }
    }

    interrupt_state_t interrupt_controller_t::sample() const noexcept
    {
        return _state;
    }

    bool interrupt_controller_t::consume_nmi() noexcept
    {
        const bool was_pending{ _state.nmi_pending };
        _state.nmi_pending = false;
        return was_pending;
    }

    bool interrupt_controller_t::consume_irq() noexcept
    {
        const bool was_pending{ _state.irq_pending };
        _state.irq_pending = false;
        return was_pending;
    }

    bool interrupt_controller_t::irq_lock() const noexcept
    {
        return _state.irq_lock;
    }

    void interrupt_controller_t::set_irq_lock() noexcept
    {
        _state.irq_lock = true;
    }

    void interrupt_controller_t::clear_irq_lock() noexcept
    {
        _state.irq_lock = false;
    }
}
