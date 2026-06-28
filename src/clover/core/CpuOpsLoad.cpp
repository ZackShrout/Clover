//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    namespace {
        [[nodiscard]] uint16_t read_accumulator_value(cpu_state_t& state,
                                                      cpu_step_executor_t& executor,
                                                      uint8_t opcode) noexcept
        {
            switch (opcode)
            {
            case 0xa9u:
                if (accumulator_is_8bit(state))
                    return executor.fetch_operand_u8(state);
                return executor.fetch_operand_u16(state);
            case 0xa5u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_u8(state);
                return executor.read_direct_u16(state);
            case 0xa3u:
                if (accumulator_is_8bit(state))
                    return executor.read_stack_relative_u8(state);
                return executor.read_stack_relative_u16(state);
            case 0xb5u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indexed_u8(state, state.x);
                return executor.read_direct_indexed_u16(state, state.x);
            case 0xadu:
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_u8(state);
                return executor.read_absolute_u16(state);
            case 0xbdu:
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_indexed_u8(state, state.x);
                return executor.read_absolute_indexed_u16(state, state.x);
            case 0xb9u:
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_indexed_u8(state, state.y);
                return executor.read_absolute_indexed_u16(state, state.y);
            case 0xb2u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_u8(state);
                return executor.read_direct_indirect_u16(state);
            case 0xa1u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indexed_indirect_u8(state);
                return executor.read_direct_indexed_indirect_u16(state);
            case 0xb1u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_indexed_u8(state);
                return executor.read_direct_indirect_indexed_u16(state);
            case 0xb3u:
                if (accumulator_is_8bit(state))
                    return executor.read_stack_relative_indirect_indexed_u8(state);
                return executor.read_stack_relative_indirect_indexed_u16(state);
            case 0xa7u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_long_u8(state);
                return executor.read_direct_indirect_long_u16(state);
            case 0xb7u:
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_long_indexed_u8(state);
                return executor.read_direct_indirect_long_indexed_u16(state);
            case 0xafu:
                if (accumulator_is_8bit(state))
                    return executor.read_long_u8(state);
                return executor.read_long_u16(state);
            case 0xbfu:
                if (accumulator_is_8bit(state))
                    return executor.read_long_indexed_u8(state, state.x);
                return executor.read_long_indexed_u16(state, state.x);
            default:
                return 0;
            }
        }

        [[nodiscard]] uint16_t read_index_value(cpu_state_t& state,
                                                cpu_step_executor_t& executor,
                                                uint8_t opcode,
                                                bool target_x) noexcept
        {
            const uint16_t index{ target_x ? state.y : state.x };

            switch (opcode)
            {
            case 0xa2u:
            case 0xa0u:
                if (index_is_8bit(state))
                    return executor.fetch_operand_u8(state);
                return executor.fetch_operand_u16(state);
            case 0xa6u:
            case 0xa4u:
                if (index_is_8bit(state))
                    return executor.read_direct_u8(state);
                return executor.read_direct_u16(state);
            case 0xb6u:
            case 0xb4u:
                if (index_is_8bit(state))
                    return executor.read_direct_indexed_u8(state, index);
                return executor.read_direct_indexed_u16(state, index);
            case 0xaeu:
            case 0xacu:
                if (index_is_8bit(state))
                    return executor.read_absolute_u8(state);
                return executor.read_absolute_u16(state);
            case 0xbeu:
            case 0xbcu:
                if (index_is_8bit(state))
                    return executor.read_absolute_indexed_u8(state, index);
                return executor.read_absolute_indexed_u16(state, index);
            default:
                return 0;
            }
        }

        void load_accumulator(cpu_state_t& state,
                              cpu_step_executor_t& executor,
                              uint8_t opcode) noexcept
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            const uint16_t value{ read_accumulator_value(state, executor, opcode) };

            if (is_8bit)
            {
                state.a = static_cast<uint16_t>((state.a & 0xff00u) | (value & 0x00ffu));
                set_zero_negative_flags(state, state.a, true);
                return;
            }

            state.a = value;
            set_zero_negative_flags(state, state.a, false);
        }

        void load_index(uint16_t& target,
                        cpu_state_t& state,
                        cpu_step_executor_t& executor,
                        uint8_t opcode,
                        bool target_x) noexcept
        {
            const bool is_8bit{ index_is_8bit(state) };
            target = read_index_value(state, executor, opcode, target_x);
            set_zero_negative_flags(state, target, is_8bit);
        }
    } // anonymous namespace

    bool execute_load_opcode(uint8_t opcode,
                             cpu_state_t& state,
                             cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0xa9u:
        case 0xa5u:
        case 0xa3u:
        case 0xa1u:
        case 0xa7u:
        case 0xb5u:
        case 0xb1u:
        case 0xb3u:
        case 0xb7u:
        case 0xadu:
        case 0xafu:
        case 0xbdu:
        case 0xb9u:
        case 0xbfu:
        case 0xb2u:
            load_accumulator(state, executor, opcode);
            return true;
        case 0xa2u:
        case 0xa6u:
        case 0xb6u:
        case 0xaeu:
        case 0xbeu:
            load_index(state.x, state, executor, opcode, true);
            return true;
        case 0xa0u:
        case 0xa4u:
        case 0xb4u:
        case 0xacu:
        case 0xbcu:
            load_index(state.y, state, executor, opcode, false);
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
