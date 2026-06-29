//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    namespace {
        [[nodiscard]] uint16_t read_indirect_pointer_u16(cpu_step_executor_t& executor,
                                                         uint16_t pointer) noexcept
        {
            const uint8_t low{ executor.read_u8(pointer) };
            const uint8_t high{ executor.read_u8(static_cast<uint16_t>(pointer + 1u)) };
            return static_cast<uint16_t>(low | (high << 8u));
        }

        [[nodiscard]] uint32_t read_indirect_pointer_u24(cpu_step_executor_t& executor,
                                                         uint16_t pointer) noexcept
        {
            const uint8_t low{ executor.read_u8(pointer) };
            const uint8_t high{ executor.read_u8(static_cast<uint16_t>(pointer + 1u)) };
            const uint8_t bank{ executor.read_u8(static_cast<uint16_t>(pointer + 2u)) };
            return (static_cast<uint32_t>(bank) << 16u) | low | (high << 8u);
        }
    } // anonymous namespace

    bool execute_jump_opcode(uint8_t opcode,
                             cpu_state_t& state,
                             cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x20u:
        {
            const uint16_t target{ executor.fetch_operand_u16(state) };
            executor.idle();
            executor.push_u16(state, static_cast<uint16_t>(state.pc - 1u));
            state.pc = target;
            executor.retire_instruction();
            return true;
        }
        case 0x22u:
        {
            const uint16_t target_low_word{ executor.fetch_operand_u16(state) };
            executor.push_u8(state, state.pb);
            executor.idle();
            const uint8_t target_bank{ executor.fetch_operand_u8(state) };
            executor.push_u16(state, static_cast<uint16_t>(state.pc - 1u));
            state.pb = target_bank;
            state.pc = target_low_word;
            executor.retire_instruction();
            return true;
        }
        case 0x4cu:
            state.pc = executor.fetch_operand_u16(state);
            executor.retire_instruction();
            return true;
        case 0x5cu:
        {
            const uint32_t target{ executor.fetch_operand_u24(state) };
            state.pb = static_cast<uint8_t>(target >> 16u);
            state.pc = static_cast<uint16_t>(target & 0x00ffffu);
            executor.retire_instruction();
            return true;
        }
        case 0x60u:
            executor.idle();
            executor.idle();
            state.pc = static_cast<uint16_t>(executor.pull_u16(state) + 1u);
            executor.idle();
            executor.retire_instruction();
            return true;
        case 0x6bu:
        {
            executor.idle();
            executor.idle();
            state.pc = static_cast<uint16_t>(executor.pull_u16(state) + 1u);
            state.pb = executor.pull_u8(state);
            executor.retire_instruction();
            return true;
        }
        case 0x6cu:
            state.pc = read_indirect_pointer_u16(executor, executor.fetch_operand_u16(state));
            executor.retire_instruction();
            return true;
        case 0x7cu:
        {
            const uint16_t pointer_base{ executor.fetch_operand_u16(state) };
            executor.idle();
            const uint32_t pointer_address{
                (static_cast<uint32_t>(state.pb) << 16u)
                | static_cast<uint16_t>(pointer_base + state.x)
            };
            const uint8_t low{ executor.read_u8(pointer_address) };
            const uint8_t high{ executor.read_u8((pointer_address & 0xff0000u)
                | static_cast<uint16_t>(pointer_base + state.x + 1u)) };
            state.pc = static_cast<uint16_t>(low | (high << 8u));
            executor.retire_instruction();
            return true;
        }
        case 0xdcu:
        {
            const uint32_t target{ read_indirect_pointer_u24(executor, executor.fetch_operand_u16(state)) };
            state.pb = static_cast<uint8_t>(target >> 16u);
            state.pc = static_cast<uint16_t>(target & 0x00ffffu);
            executor.retire_instruction();
            return true;
        }
        case 0xfcu:
        {
            const uint16_t pointer_base{ executor.fetch_operand_u16(state) };
            executor.idle();
            executor.push_u16(state, static_cast<uint16_t>(state.pc - 1u));
            const uint32_t pointer_address{
                (static_cast<uint32_t>(state.pb) << 16u)
                | static_cast<uint16_t>(pointer_base + state.x)
            };
            const uint8_t low{ executor.read_u8(pointer_address) };
            const uint8_t high{ executor.read_u8((pointer_address & 0xff0000u)
                | static_cast<uint16_t>(pointer_base + state.x + 1u)) };
            state.pc = static_cast<uint16_t>(low | (high << 8u));
            executor.retire_instruction();
            return true;
        }
        default:
            return false;
        }
    }
} // namespace clover::core
