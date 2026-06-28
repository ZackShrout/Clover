//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    bool execute_stack_opcode(uint8_t opcode,
                              cpu_state_t& state,
                              cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x0bu:
            executor.idle();
            executor.push_u16(state, state.d);
            return true;
        case 0x08u:
            executor.idle();
            executor.push_u8(state, state.p);
            return true;
        case 0x28u:
            executor.idle();
            executor.idle();
            state.p = executor.pull_u8(state);
            normalize_status_for_mode(state);
            return true;
        case 0x2bu:
            executor.idle();
            executor.idle();
            state.d = executor.pull_u16(state);
            set_zero_negative_flags(state, state.d, false);
            return true;
        case 0x4bu:
            executor.idle();
            executor.push_u8(state, state.pb);
            return true;
        case 0x48u:
            executor.idle();
            if (accumulator_is_8bit(state))
                executor.push_u8(state, static_cast<uint8_t>(state.a & 0x00ffu));
            else
                executor.push_u16(state, state.a);
            return true;
        case 0x68u:
            executor.idle();
            executor.idle();
            if (accumulator_is_8bit(state))
            {
                state.a = static_cast<uint16_t>((state.a & 0xff00u) | executor.pull_u8(state));
                set_zero_negative_flags(state, state.a, true);
            }
            else
            {
                state.a = executor.pull_u16(state);
                set_zero_negative_flags(state, state.a, false);
            }
            return true;
        case 0x5au:
            executor.idle();
            if (index_is_8bit(state))
                executor.push_u8(state, static_cast<uint8_t>(state.y & 0x00ffu));
            else
                executor.push_u16(state, state.y);
            return true;
        case 0xd4u:
        {
            const uint8_t offset{ executor.fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                executor.idle();
            const uint16_t address{ static_cast<uint16_t>(state.d + offset) };
            const uint8_t low{ executor.read_u8(address) };
            const uint8_t high{ executor.read_u8(static_cast<uint16_t>(address + 1u)) };
            executor.push_u16(state, static_cast<uint16_t>(low | (high << 8u)));
            return true;
        }
        case 0xf4u:
            executor.push_u16(state, executor.fetch_operand_u16(state));
            return true;
        case 0xfau:
            executor.idle();
            executor.idle();
            if (index_is_8bit(state))
            {
                state.x = executor.pull_u8(state);
                set_zero_negative_flags(state, state.x, true);
            }
            else
            {
                state.x = executor.pull_u16(state);
                set_zero_negative_flags(state, state.x, false);
            }
            return true;
        case 0x7au:
            executor.idle();
            executor.idle();
            if (index_is_8bit(state))
            {
                state.y = executor.pull_u8(state);
                set_zero_negative_flags(state, state.y, true);
            }
            else
            {
                state.y = executor.pull_u16(state);
                set_zero_negative_flags(state, state.y, false);
            }
            return true;
        case 0x62u:
        {
            const int16_t displacement{ static_cast<int16_t>(executor.fetch_operand_u16(state)) };
            executor.idle();
            executor.push_u16(state, static_cast<uint16_t>(state.pc + displacement));
            return true;
        }
        case 0xdau:
            executor.idle();
            if (index_is_8bit(state))
                executor.push_u8(state, static_cast<uint8_t>(state.x & 0x00ffu));
            else
                executor.push_u16(state, state.x);
            return true;
        case 0x8bu:
            executor.idle();
            executor.push_u8(state, state.db);
            return true;
        case 0xabu:
            executor.idle();
            executor.idle();
            state.db = executor.pull_u8(state);
            set_zero_negative_flags(state, state.db, true);
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
