//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/CpuInternal.h"

namespace clover::core
{
    namespace {
        void apply_block_move_index(cpu_state_t& state, uint16_t& index, int adjustment) noexcept
        {
            if (index_is_8bit(state))
            {
                const uint8_t narrowed{ static_cast<uint8_t>(index & 0x00ffu) };
                index = static_cast<uint16_t>(adjustment > 0
                    ? static_cast<uint8_t>(narrowed + 1u)
                    : static_cast<uint8_t>(narrowed - 1u));
                return;
            }

            index = static_cast<uint16_t>(adjustment > 0 ? index + 1u : index - 1u);
        }

        void block_move(cpu_state_t& state,
                        cpu_step_executor_t& executor,
                        int adjustment) noexcept
        {
            const uint8_t destination_bank{ executor.fetch_operand_u8(state) };
            const uint8_t source_bank{ executor.fetch_operand_u8(state) };
            const uint32_t source_address{
                (static_cast<uint32_t>(source_bank) << 16u) | state.x
            };
            const uint32_t destination_address{
                (static_cast<uint32_t>(destination_bank) << 16u) | state.y
            };

            state.db = destination_bank;

            const uint8_t value{ executor.read_u8(source_address) };
            executor.write_u8(destination_address, value);
            executor.idle();
            apply_block_move_index(state, state.x, adjustment);
            apply_block_move_index(state, state.y, adjustment);
            executor.idle();

            const uint16_t previous_count{ state.a };
            state.a = static_cast<uint16_t>(state.a - 1u);
            if (previous_count != 0)
                state.pc = static_cast<uint16_t>(state.pc - 3u);
        }

        void store_accumulator(cpu_state_t& state,
                               cpu_step_executor_t& executor,
                               uint8_t opcode) noexcept
        {
            if (accumulator_is_8bit(state))
            {
                const uint8_t value{ static_cast<uint8_t>(state.a & 0x00ffu) };
                switch (opcode)
                {
                case 0x85u:
                    executor.write_direct_u8(state, value);
                    return;
                case 0x83u:
                    executor.write_stack_relative_u8(state, value);
                    return;
                case 0x95u:
                    executor.write_direct_indexed_u8(state, state.x, value);
                    return;
                case 0x92u:
                    executor.write_direct_indirect_u8(state, value);
                    return;
                case 0x81u:
                    executor.write_direct_indexed_indirect_u8(state, value);
                    return;
                case 0x91u:
                    executor.write_direct_indirect_indexed_u8(state, value);
                    return;
                case 0x93u:
                    executor.write_stack_relative_indirect_indexed_u8(state, value);
                    return;
                case 0x87u:
                    executor.write_direct_indirect_long_u8(state, value);
                    return;
                case 0x97u:
                    executor.write_direct_indirect_long_indexed_u8(state, value);
                    return;
                case 0x8du:
                    executor.write_absolute_u8(state, value);
                    return;
                case 0x99u:
                    if (state.pb == 0x09u)
                    {
                        std::printf("CPU opcode 99 (8): PB:%02x PC:%04x A:%04x X:%04x Y:%04x DB:%02x P:%02x\n",
                                    state.pb,
                                    state.pc,
                                    state.a,
                                    state.x,
                                    state.y,
                                    state.db,
                                    state.p);
                    }
                    executor.write_absolute_indexed_u8(state, state.y, value);
                    return;
                case 0x8fu:
                    executor.write_long_u8(state, value);
                    return;
                case 0x9fu:
                    executor.write_long_indexed_u8(state, state.x, value);
                    return;
                default:
                    executor.write_absolute_indexed_u8(state, state.x, value);
                    return;
                }
            }

            switch (opcode)
            {
            case 0x85u:
                executor.write_direct_u16(state, state.a);
                return;
            case 0x83u:
                executor.write_stack_relative_u16(state, state.a);
                return;
            case 0x95u:
                executor.write_direct_indexed_u16(state, state.x, state.a);
                return;
            case 0x92u:
                executor.write_direct_indirect_u16(state, state.a);
                return;
            case 0x81u:
                executor.write_direct_indexed_indirect_u16(state, state.a);
                return;
            case 0x91u:
                executor.write_direct_indirect_indexed_u16(state, state.a);
                return;
            case 0x93u:
                executor.write_stack_relative_indirect_indexed_u16(state, state.a);
                return;
            case 0x87u:
                executor.write_direct_indirect_long_u16(state, state.a);
                return;
            case 0x97u:
                executor.write_direct_indirect_long_indexed_u16(state, state.a);
                return;
            case 0x8du:
                executor.write_absolute_u16(state, state.a);
                return;
            case 0x99u:
                if (state.pb == 0x09u)
                {
                    std::printf("CPU opcode 99 (16): PB:%02x PC:%04x A:%04x X:%04x Y:%04x DB:%02x P:%02x\n",
                                state.pb,
                                state.pc,
                                state.a,
                                state.x,
                                state.y,
                                state.db,
                                state.p);
                }
                executor.write_absolute_indexed_u16(state, state.y, state.a);
                return;
            case 0x8fu:
                executor.write_long_u16(state, state.a);
                return;
            case 0x9fu:
                executor.write_long_indexed_u16(state, state.x, state.a);
                return;
            default:
                executor.write_absolute_indexed_u16(state, state.x, state.a);
                return;
            }
        }

        void store_index(cpu_state_t& state,
                         cpu_step_executor_t& executor,
                         uint16_t value,
                         uint8_t opcode,
                         bool target_x) noexcept
        {
            const uint16_t index{ target_x ? state.y : state.x };

            if (index_is_8bit(state))
            {
                const uint8_t narrowed{ static_cast<uint8_t>(value & 0x00ffu) };
                switch (opcode)
                {
                case 0x84u:
                case 0x86u:
                    executor.write_direct_u8(state, narrowed);
                    return;
                case 0x94u:
                case 0x96u:
                    executor.write_direct_indexed_u8(state, index, narrowed);
                    return;
                default:
                    executor.write_absolute_u8(state, narrowed);
                    return;
                }
            }

            switch (opcode)
            {
            case 0x84u:
            case 0x86u:
                executor.write_direct_u16(state, value);
                return;
            case 0x94u:
            case 0x96u:
                executor.write_direct_indexed_u16(state, index, value);
                return;
            default:
                executor.write_absolute_u16(state, value);
                return;
            }
        }

        void store_zero(cpu_state_t& state,
                        cpu_step_executor_t& executor,
                        uint8_t opcode) noexcept
        {
            if (accumulator_is_8bit(state))
            {
                switch (opcode)
                {
                case 0x64u:
                    executor.write_direct_u8(state, 0);
                    return;
                case 0x74u:
                    executor.write_direct_indexed_u8(state, state.x, 0);
                    return;
                case 0x9cu:
                    executor.write_absolute_u8(state, 0);
                    return;
                default:
                    executor.write_absolute_indexed_u8(state, state.x, 0);
                    return;
                }
            }

            switch (opcode)
            {
            case 0x64u:
                executor.write_direct_u16(state, 0);
                return;
            case 0x74u:
                executor.write_direct_indexed_u16(state, state.x, 0);
                return;
            case 0x9cu:
                executor.write_absolute_u16(state, 0);
                return;
            default:
                executor.write_absolute_indexed_u16(state, state.x, 0);
                return;
            }
        }
    } // anonymous namespace

    bool execute_memory_opcode(uint8_t opcode,
                               cpu_state_t& state,
                               cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x83u:
        case 0x85u:
        case 0x81u:
        case 0x87u:
        case 0x93u:
        case 0x95u:
        case 0x91u:
        case 0x97u:
        case 0x92u:
        case 0x8du:
        case 0x8fu:
        case 0x99u:
        case 0x9du:
        case 0x9fu:
            store_accumulator(state, executor, opcode);
            return true;
        case 0x84u:
        case 0x94u:
        case 0x8cu:
            store_index(state, executor, state.y, opcode, false);
            return true;
        case 0x86u:
        case 0x96u:
        case 0x8eu:
            store_index(state, executor, state.x, opcode, true);
            return true;
        case 0x64u:
        case 0x74u:
        case 0x9cu:
        case 0x9eu:
            store_zero(state, executor, opcode);
            return true;
        case 0x44u:
            block_move(state, executor, -1);
            return true;
        case 0x54u:
            block_move(state, executor, 1);
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
