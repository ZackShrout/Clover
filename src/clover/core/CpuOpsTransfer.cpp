//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    namespace {
        [[nodiscard]] uint16_t apply_index_result(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint16_t result{ index_is_8bit(state) ? static_cast<uint16_t>(value & 0x00ffu) : value };
            set_zero_negative_flags(state, result, index_is_8bit(state));
            return result;
        }

        [[nodiscard]] uint16_t apply_accumulator_result(cpu_state_t& state, uint16_t value) noexcept
        {
            const uint16_t result{ accumulator_is_8bit(state)
                ? static_cast<uint16_t>(value & 0x00ffu)
                : value };
            set_zero_negative_flags(state, result, accumulator_is_8bit(state));
            return result;
        }
    } // anonymous namespace

    bool execute_transfer_opcode(uint8_t opcode,
                                 cpu_state_t& state,
                                 cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x88u:
            executor.idle_opcode_boundary(state);
            if (index_is_8bit(state))
            {
                state.y = static_cast<uint16_t>((state.y - 1u) & 0x00ffu);
                set_zero_negative_flags(state, state.y, true);
            }
            else
            {
                state.y = static_cast<uint16_t>(state.y - 1u);
                set_zero_negative_flags(state, state.y, false);
            }
            return true;
        case 0x8au:
            executor.idle_opcode_boundary(state);
            state.a = apply_accumulator_result(state, state.x);
            return true;
        case 0x9au:
            executor.idle_opcode_boundary(state);
            if (state.emulation_mode)
                state.sp = static_cast<uint16_t>(0x0100u | (state.x & 0x00ffu));
            else
                state.sp = state.x;
            return true;
        case 0x98u:
            executor.idle_opcode_boundary(state);
            state.a = apply_accumulator_result(state, state.y);
            return true;
        case 0x9bu:
            executor.idle_opcode_boundary(state);
            state.y = apply_index_result(state, state.x);
            return true;
        case 0xaau:
            executor.idle_opcode_boundary(state);
            state.x = apply_index_result(state, state.a);
            return true;
        case 0xa8u:
            executor.idle_opcode_boundary(state);
            state.y = apply_index_result(state, state.a);
            return true;
        case 0xbbu:
            executor.idle_opcode_boundary(state);
            state.x = apply_index_result(state, state.y);
            return true;
        case 0xbau:
            executor.idle_opcode_boundary(state);
            state.x = apply_index_result(state, state.sp);
            return true;
        case 0xc8u:
            executor.idle_opcode_boundary(state);
            if (index_is_8bit(state))
            {
                state.y = static_cast<uint16_t>((state.y + 1u) & 0x00ffu);
                set_zero_negative_flags(state, state.y, true);
            }
            else
            {
                state.y = static_cast<uint16_t>(state.y + 1u);
                set_zero_negative_flags(state, state.y, false);
            }
            return true;
        case 0xcau:
            executor.idle_opcode_boundary(state);
            if (index_is_8bit(state))
            {
                state.x = static_cast<uint16_t>((state.x - 1u) & 0x00ffu);
                set_zero_negative_flags(state, state.x, true);
            }
            else
            {
                state.x = static_cast<uint16_t>(state.x - 1u);
                set_zero_negative_flags(state, state.x, false);
            }
            return true;
        case 0xe8u:
            executor.idle_opcode_boundary(state);
            if (index_is_8bit(state))
            {
                state.x = static_cast<uint16_t>((state.x + 1u) & 0x00ffu);
                set_zero_negative_flags(state, state.x, true);
            }
            else
            {
                state.x = static_cast<uint16_t>(state.x + 1u);
                set_zero_negative_flags(state, state.x, false);
            }
            return true;
        case 0x1bu:
            executor.idle_opcode_boundary(state);
            state.sp = state.a;
            if (state.emulation_mode)
                state.sp = static_cast<uint16_t>(0x0100u | (state.sp & 0x00ffu));
            return true;
        case 0x3bu:
            executor.idle_opcode_boundary(state);
            state.a = state.sp;
            set_zero_negative_flags(state, state.a, false);
            return true;
        case 0x5bu:
            executor.idle_opcode_boundary(state);
            state.d = state.a;
            set_zero_negative_flags(state, state.d, false);
            return true;
        case 0x7bu:
            executor.idle_opcode_boundary(state);
            state.a = state.d;
            set_zero_negative_flags(state, state.a, false);
            return true;
        case 0xebu:
            executor.idle();
            executor.idle();
            state.a = static_cast<uint16_t>((state.a >> 8u) | (state.a << 8u));
            set_zero_negative_flags(state, state.a & 0x00ffu, true);
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
