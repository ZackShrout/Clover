//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    bool execute_system_opcode(uint8_t opcode,
                               cpu_t& cpu,
                               cpu_state_t& state,
                               cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x00u:
            enter_interrupt_handler(state, executor, brk_vector(state), true, false);
            return true;
        case 0x02u:
            enter_interrupt_handler(state, executor, cop_vector(state), true, false);
            return true;
        case 0xeau:
            executor.retire_opcode_boundary(state);
            return true;
        case 0x18u:
            executor.retire_opcode_boundary(state);
            state.p &= static_cast<uint8_t>(~k_status_carry);
            return true;
        case 0x38u:
            executor.retire_opcode_boundary(state);
            state.p |= k_status_carry;
            return true;
        case 0xd8u:
            executor.retire_opcode_boundary(state);
            state.p &= static_cast<uint8_t>(~k_status_decimal);
            return true;
        case 0xc2u:
        {
            const uint8_t mask{ executor.fetch_operand_u8(state) };
            executor.idle();
            state.p &= static_cast<uint8_t>(~mask);
            normalize_status_for_mode(state);
            return true;
        }
        case 0xe2u:
        {
            const uint8_t mask{ executor.fetch_operand_u8(state) };
            executor.idle();
            state.p |= mask;
            normalize_status_for_mode(state);
            return true;
        }
        case 0x58u:
            executor.retire_opcode_boundary(state);
            state.p &= static_cast<uint8_t>(~k_status_irq_disable);
            // CLI defers newly-visible IRQ delivery until the following opcode
            // boundary; the next CPU step clears this lock before executing.
            executor.set_irq_lock();
            return true;
        case 0x78u:
            executor.retire_opcode_boundary(state);
            state.p |= k_status_irq_disable;
            return true;
        case 0xb8u:
            executor.retire_opcode_boundary(state);
            state.p &= static_cast<uint8_t>(~k_status_overflow);
            return true;
        case 0xf8u:
            executor.retire_opcode_boundary(state);
            state.p |= k_status_decimal;
            return true;
        case 0xfbu:
        {
            executor.retire_opcode_boundary(state);
            const bool carry{ (state.p & k_status_carry) != 0 };
            const bool previous_emulation_mode{ state.emulation_mode };
            if (previous_emulation_mode)
                state.p |= k_status_carry;
            else
                state.p &= static_cast<uint8_t>(~k_status_carry);

            if (carry)
                enter_emulation_mode(state);
            else
                state.emulation_mode = false;
            return true;
        }
        case 0x40u:
            return_from_interrupt(state, executor);
            return true;
        case 0x42u:
            static_cast<void>(executor.fetch_operand_u8(state));
            executor.retire_instruction();
            return true;
        case 0xcbu:
            executor.idle();
            cpu.set_waiting(true);
            executor.retire_instruction();
            return true;
        case 0xdbu:
            executor.idle();
            cpu.set_stopped(true);
            executor.retire_instruction();
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
