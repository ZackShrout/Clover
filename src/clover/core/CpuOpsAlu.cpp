//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/CpuInternal.h"

namespace clover::core
{
    namespace {
        [[nodiscard]] uint16_t fetch_accumulator_operand(cpu_state_t& state,
                                                         cpu_step_executor_t& executor) noexcept
        {
            if (accumulator_is_8bit(state))
                return executor.fetch_operand_u8(state);

            return executor.fetch_operand_u16(state);
        }

        [[nodiscard]] uint16_t fetch_index_operand(cpu_state_t& state,
                                                   cpu_step_executor_t& executor) noexcept
        {
            if (index_is_8bit(state))
                return executor.fetch_operand_u8(state);

            return executor.fetch_operand_u16(state);
        }

        [[nodiscard]] uint16_t read_accumulator_operand(cpu_state_t& state,
                                                        cpu_step_executor_t& executor,
                                                        uint8_t opcode,
                                                        uint8_t indexed_indirect_opcode,
                                                        uint8_t direct_indirect_opcode,
                                                        uint8_t direct_indirect_long_opcode,
                                                        uint8_t immediate_opcode,
                                                        uint8_t direct_opcode,
                                                        uint8_t direct_indexed_opcode,
                                                        uint8_t direct_indirect_indexed_opcode,
                                                        uint8_t direct_indirect_long_indexed_opcode,
                                                        uint8_t absolute_opcode,
                                                        uint8_t long_opcode,
                                                        uint8_t absolute_x_opcode,
                                                        uint8_t absolute_y_opcode,
                                                        uint8_t long_x_opcode) noexcept
        {
            if (opcode == indexed_indirect_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indexed_indirect_u8(state);
                return executor.read_direct_indexed_indirect_u16(state);
            }

            if (opcode == direct_indirect_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_u8(state);
                return executor.read_direct_indirect_u16(state);
            }

            if (opcode == direct_indirect_long_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_long_u8(state);
                return executor.read_direct_indirect_long_u16(state);
            }

            if (opcode == immediate_opcode)
                return fetch_accumulator_operand(state, executor);

            if (opcode == direct_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_u8(state);
                return executor.read_direct_u16(state);
            }

            if (opcode == static_cast<uint8_t>(direct_opcode - 0x02u))
            {
                if (accumulator_is_8bit(state))
                    return executor.read_stack_relative_u8(state);
                return executor.read_stack_relative_u16(state);
            }

            if (opcode == direct_indexed_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indexed_u8(state, state.x);
                return executor.read_direct_indexed_u16(state, state.x);
            }

            if (opcode == direct_indirect_indexed_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_indexed_u8(state);
                return executor.read_direct_indirect_indexed_u16(state);
            }

            if (opcode == static_cast<uint8_t>(direct_indirect_indexed_opcode + 0x02u))
            {
                if (accumulator_is_8bit(state))
                    return executor.read_stack_relative_indirect_indexed_u8(state);
                return executor.read_stack_relative_indirect_indexed_u16(state);
            }

            if (opcode == direct_indirect_long_indexed_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indirect_long_indexed_u8(state);
                return executor.read_direct_indirect_long_indexed_u16(state);
            }

            if (opcode == absolute_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_u8(state);
                return executor.read_absolute_u16(state);
            }

            if (opcode == long_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_long_u8(state);
                return executor.read_long_u16(state);
            }

            if (opcode == absolute_x_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_indexed_u8(state, state.x);
                return executor.read_absolute_indexed_u16(state, state.x);
            }

            if (opcode == absolute_y_opcode)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_indexed_u8(state, state.y);
                return executor.read_absolute_indexed_u16(state, state.y);
            }

            if (accumulator_is_8bit(state))
                return executor.read_long_indexed_u8(state, state.x);

            return executor.read_long_indexed_u16(state, state.x);
        }

        [[nodiscard]] uint16_t read_index_operand(cpu_state_t& state,
                                                  cpu_step_executor_t& executor,
                                                  uint8_t direct_opcode,
                                                  uint8_t absolute_opcode,
                                                  uint8_t immediate_opcode,
                                                  uint8_t opcode) noexcept
        {
            if (opcode == immediate_opcode)
                return fetch_index_operand(state, executor);

            if (opcode == direct_opcode)
            {
                if (index_is_8bit(state))
                    return executor.read_direct_u8(state);
                return executor.read_direct_u16(state);
            }

            if (index_is_8bit(state))
                return executor.read_absolute_u8(state);

            return executor.read_absolute_u16(state);
        }

        void store_accumulator_result(cpu_state_t& state, uint16_t value) noexcept
        {
            if (accumulator_is_8bit(state))
            {
                state.a = static_cast<uint16_t>((state.a & 0xff00u) | (value & 0x00ffu));
                return;
            }

            state.a = value;
        }
    } // anonymous namespace

    bool execute_alu_opcode(uint8_t opcode,
                            cpu_state_t& state,
                            cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x03u:
        case 0x01u:
        case 0x13u:
        case 0x12u:
        case 0x07u:
        case 0x05u:
        case 0x11u:
        case 0x15u:
        case 0x17u:
        case 0x0du:
        case 0x0fu:
        case 0x1du:
        case 0x19u:
        case 0x1fu:
        case 0x09u:
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            const uint16_t rhs{
                read_accumulator_operand(state, executor, opcode, 0x01u, 0x12u, 0x07u, 0x09u, 0x05u, 0x15u, 0x11u, 0x17u, 0x0du, 0x0fu, 0x1du, 0x19u, 0x1fu)
            };
            store_accumulator_result(state, logic_value(state, static_cast<uint16_t>(state.a | rhs), is_8bit));
            return true;
        }
        case 0x23u:
        case 0x21u:
        case 0x33u:
        case 0x32u:
        case 0x27u:
        case 0x25u:
        case 0x31u:
        case 0x35u:
        case 0x37u:
        case 0x2du:
        case 0x2fu:
        case 0x3du:
        case 0x39u:
        case 0x3fu:
        case 0x29u:
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            const uint16_t rhs{
                read_accumulator_operand(state, executor, opcode, 0x21u, 0x32u, 0x27u, 0x29u, 0x25u, 0x35u, 0x31u, 0x37u, 0x2du, 0x2fu, 0x3du, 0x39u, 0x3fu)
            };
            store_accumulator_result(state, logic_value(state, static_cast<uint16_t>(state.a & rhs), is_8bit));
            return true;
        }
        case 0x43u:
        case 0x41u:
        case 0x53u:
        case 0x52u:
        case 0x47u:
        case 0x45u:
        case 0x51u:
        case 0x55u:
        case 0x57u:
        case 0x4du:
        case 0x4fu:
        case 0x5du:
        case 0x59u:
        case 0x5fu:
        case 0x49u:
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            const uint16_t rhs{
                read_accumulator_operand(state, executor, opcode, 0x41u, 0x52u, 0x47u, 0x49u, 0x45u, 0x55u, 0x51u, 0x57u, 0x4du, 0x4fu, 0x5du, 0x59u, 0x5fu)
            };
            store_accumulator_result(state, logic_value(state, static_cast<uint16_t>(state.a ^ rhs), is_8bit));
            return true;
        }
        case 0x63u:
        case 0x61u:
        case 0x73u:
        case 0x72u:
        case 0x67u:
        case 0x65u:
        case 0x71u:
        case 0x75u:
        case 0x77u:
        case 0x6du:
        case 0x6fu:
        case 0x7du:
        case 0x79u:
        case 0x7fu:
        case 0x69u:
        {
            const uint16_t rhs{
                read_accumulator_operand(state, executor, opcode, 0x61u, 0x72u, 0x67u, 0x69u, 0x65u, 0x75u, 0x71u, 0x77u, 0x6du, 0x6fu, 0x7du, 0x79u, 0x7fu)
            };
            store_accumulator_result(state, adc_value(state, state.a, rhs));
            return true;
        }
        case 0xc3u:
        case 0xc1u:
        case 0xd3u:
        case 0xd2u:
        case 0xc7u:
        case 0xc5u:
        case 0xd1u:
        case 0xd5u:
        case 0xd7u:
        case 0xcdu:
        case 0xcfu:
        case 0xddu:
        case 0xd9u:
        case 0xdfu:
        case 0xc9u:
            compare_value(state,
                          state.a,
                          read_accumulator_operand(state, executor, opcode, 0xc1u, 0xd2u, 0xc7u, 0xc9u, 0xc5u, 0xd5u, 0xd1u, 0xd7u, 0xcdu, 0xcfu, 0xddu, 0xd9u, 0xdfu),
                          accumulator_is_8bit(state));
            return true;
        case 0xc4u:
        case 0xccu:
        case 0xc0u:
            compare_value(state,
                          state.y,
                          read_index_operand(state, executor, 0xc4u, 0xccu, 0xc0u, opcode),
                          index_is_8bit(state));
            return true;
        case 0xe4u:
        case 0xecu:
        case 0xe0u:
            compare_value(state,
                          state.x,
                          read_index_operand(state, executor, 0xe4u, 0xecu, 0xe0u, opcode),
                          index_is_8bit(state));
            return true;
        case 0xe3u:
        case 0xe1u:
        case 0xf3u:
        case 0xf2u:
        case 0xe7u:
        case 0xe5u:
        case 0xf1u:
        case 0xf5u:
        case 0xf7u:
        case 0xedu:
        case 0xefu:
        case 0xfdu:
        case 0xf9u:
        case 0xffu:
        case 0xe9u:
        {
            const uint16_t rhs{
                read_accumulator_operand(state, executor, opcode, 0xe1u, 0xf2u, 0xe7u, 0xe9u, 0xe5u, 0xf5u, 0xf1u, 0xf7u, 0xedu, 0xefu, 0xfdu, 0xf9u, 0xffu)
            };
            store_accumulator_result(state, sbc_value(state, state.a, rhs));
            return true;
        }
        default:
            return false;
        }
    }
} // namespace clover::core
