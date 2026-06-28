//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    namespace {
        [[nodiscard]] bool branch_taken(uint8_t opcode, const cpu_state_t& state) noexcept
        {
            switch (opcode)
            {
            case 0x10u:
                return (state.p & k_status_negative) == 0;
            case 0x30u:
                return (state.p & k_status_negative) != 0;
            case 0x50u:
                return (state.p & k_status_overflow) == 0;
            case 0x70u:
                return (state.p & k_status_overflow) != 0;
            case 0x90u:
                return (state.p & k_status_carry) == 0;
            case 0xb0u:
                return (state.p & k_status_carry) != 0;
            case 0xd0u:
                return (state.p & k_status_zero) == 0;
            case 0xf0u:
                return (state.p & k_status_zero) != 0;
            default:
                return false;
            }
        }

        void execute_conditional_branch(cpu_state_t& state,
                                        cpu_step_executor_t& executor,
                                        bool take_branch) noexcept
        {
            const int8_t displacement{ static_cast<int8_t>(executor.fetch_operand_u8(state)) };
            if (!take_branch)
                return;

            const uint16_t target{ static_cast<uint16_t>(state.pc + displacement) };
            if (state.emulation_mode && (state.pc & 0xff00u) != (target & 0xff00u))
                executor.idle();

            executor.idle();
            state.pc = target;
        }
    } // anonymous namespace

    bool execute_branch_opcode(uint8_t opcode,
                               cpu_state_t& state,
                               cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x10u:
        case 0x30u:
        case 0x50u:
        case 0x70u:
        case 0x90u:
        case 0xb0u:
        case 0xd0u:
        case 0xf0u:
            execute_conditional_branch(state, executor, branch_taken(opcode, state));
            return true;
        case 0x80u:
        {
            const int8_t displacement{ static_cast<int8_t>(executor.fetch_operand_u8(state)) };
            executor.idle();
            state.pc = static_cast<uint16_t>(state.pc + displacement);
            return true;
        }
        case 0x82u:
        {
            const int16_t displacement{ static_cast<int16_t>(executor.fetch_operand_u16(state)) };
            executor.idle();
            state.pc = static_cast<uint16_t>(state.pc + displacement);
            return true;
        }
        default:
            return false;
        }
    }
} // namespace clover::core
