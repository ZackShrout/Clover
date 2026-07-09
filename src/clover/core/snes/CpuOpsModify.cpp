//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/CpuInternal.h"

namespace clover::core
{
    namespace {
        template<typename op_t>
        void modify_accumulator(cpu_state_t& state,
                                cpu_step_executor_t& executor,
                                op_t&& operation) noexcept
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            executor.retire_irq_sensitive_internal_operation(state);
            const uint16_t result{ operation(state, state.a, is_8bit) };
            if (is_8bit)
                state.a = static_cast<uint16_t>((state.a & 0xff00u) | (result & 0x00ffu));
            else
                state.a = result;
        }

        template<typename op_t>
        void modify_direct(cpu_state_t& state,
                           cpu_step_executor_t& executor,
                           bool is_8bit,
                           op_t&& operation) noexcept
        {
            const uint8_t offset{ executor.fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                executor.idle();
            const uint16_t address{ effective_direct_address(state, offset) };
            if (is_8bit)
            {
                const uint8_t value{ executor.read_u8(address) };
                executor.idle();
                executor.write_u8(address, static_cast<uint8_t>(operation(state, value, true)));
                return;
            }

            const uint8_t low{ executor.read_u8(address) };
            const uint8_t high{ executor.read_u8(static_cast<uint16_t>(address + 1u)) };
            executor.idle();
            const uint16_t result{ operation(state, static_cast<uint16_t>(low | (high << 8u)), false) };
            executor.write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(result >> 8u));
            executor.write_u8(address, static_cast<uint8_t>(result & 0x00ffu));
        }

        template<typename op_t>
        void modify_direct_indexed(cpu_state_t& state,
                                   cpu_step_executor_t& executor,
                                   bool is_8bit,
                                   op_t&& operation) noexcept
        {
            const uint8_t offset{ executor.fetch_operand_u8(state) };
            if ((state.d & 0x00ffu) != 0)
                executor.idle();
            executor.idle();
            const uint16_t address{ effective_direct_indexed_address(state, offset, state.x) };
            if (is_8bit)
            {
                const uint8_t value{ executor.read_u8(address) };
                executor.idle();
                executor.write_u8(address, static_cast<uint8_t>(operation(state, value, true)));
                return;
            }

            const uint8_t low{ executor.read_u8(address) };
            const uint8_t high{ executor.read_u8(static_cast<uint16_t>(address + 1u)) };
            executor.idle();
            const uint16_t result{ operation(state, static_cast<uint16_t>(low | (high << 8u)), false) };
            executor.write_u8(static_cast<uint16_t>(address + 1u), static_cast<uint8_t>(result >> 8u));
            executor.write_u8(address, static_cast<uint8_t>(result & 0x00ffu));
        }

        template<typename op_t>
        void modify_absolute(cpu_state_t& state,
                             cpu_step_executor_t& executor,
                             bool is_8bit,
                             op_t&& operation) noexcept
        {
            const uint16_t address{ effective_absolute_address(executor.fetch_operand_u16(state)) };
            const uint32_t data_base{ data_address(state, address) };
            if (is_8bit)
            {
                const uint8_t value{ executor.read_u8(data_base) };
                executor.idle();
                executor.write_u8(data_base, static_cast<uint8_t>(operation(state, value, true)));
                return;
            }

            const uint8_t low{ executor.read_u8(data_base) };
            const uint8_t high{ executor.read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            executor.idle();
            const uint16_t result{ operation(state, static_cast<uint16_t>(low | (high << 8u)), false) };
            executor.write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                              static_cast<uint8_t>(result >> 8u));
            executor.write_u8(data_base, static_cast<uint8_t>(result & 0x00ffu));
        }

        template<typename op_t>
        void modify_absolute_indexed(cpu_state_t& state,
                                     cpu_step_executor_t& executor,
                                     bool is_8bit,
                                     op_t&& operation) noexcept
        {
            const uint16_t base_address{ effective_absolute_address(executor.fetch_operand_u16(state)) };
            executor.idle();
            const uint16_t address{ static_cast<uint16_t>(base_address + state.x) };
            const uint32_t data_base{ data_address(state, address) };
            if (is_8bit)
            {
                const uint8_t value{ executor.read_u8(data_base) };
                executor.idle();
                executor.write_u8(data_base, static_cast<uint8_t>(operation(state, value, true)));
                return;
            }

            const uint8_t low{ executor.read_u8(data_base) };
            const uint8_t high{ executor.read_u8(data_address(state, static_cast<uint16_t>(address + 1u))) };
            executor.idle();
            const uint16_t result{ operation(state, static_cast<uint16_t>(low | (high << 8u)), false) };
            executor.write_u8(data_address(state, static_cast<uint16_t>(address + 1u)),
                              static_cast<uint8_t>(result >> 8u));
            executor.write_u8(data_base, static_cast<uint8_t>(result & 0x00ffu));
        }

        template<typename op_t>
        void read_modify_write(cpu_state_t& state,
                               cpu_step_executor_t& executor,
                               uint8_t opcode,
                               op_t&& operation) noexcept
        {
            const bool is_8bit{ accumulator_is_8bit(state) };
            switch (opcode)
            {
            case 0x06u:
            case 0x26u:
            case 0x46u:
            case 0x66u:
            case 0xc6u:
            case 0xe6u:
            case 0x04u:
            case 0x14u:
                modify_direct(state, executor, is_8bit, operation);
                return;
            case 0x16u:
            case 0x36u:
            case 0x56u:
            case 0x76u:
            case 0xd6u:
            case 0xf6u:
                modify_direct_indexed(state, executor, is_8bit, operation);
                return;
            case 0x0eu:
            case 0x2eu:
            case 0x4eu:
            case 0x6eu:
            case 0xceu:
            case 0xeeu:
            case 0x0cu:
            case 0x1cu:
                modify_absolute(state, executor, is_8bit, operation);
                return;
            default:
                modify_absolute_indexed(state, executor, is_8bit, operation);
                return;
            }
        }

        [[nodiscard]] uint16_t read_bit_operand(cpu_state_t& state,
                                                cpu_step_executor_t& executor,
                                                uint8_t opcode) noexcept
        {
            if (opcode == 0x89u)
            {
                if (accumulator_is_8bit(state))
                    return executor.fetch_operand_u8(state);
                return executor.fetch_operand_u16(state);
            }

            if (opcode == 0x24u)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_u8(state);
                return executor.read_direct_u16(state);
            }

            if (opcode == 0x2cu)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_absolute_u8(state);
                return executor.read_absolute_u16(state);
            }

            if (opcode == 0x34u)
            {
                if (accumulator_is_8bit(state))
                    return executor.read_direct_indexed_u8(state, state.x);
                return executor.read_direct_indexed_u16(state, state.x);
            }

            if (accumulator_is_8bit(state))
                return executor.read_absolute_indexed_u8(state, state.x);
            return executor.read_absolute_indexed_u16(state, state.x);
        }
    } // anonymous namespace

    bool execute_modify_opcode(uint8_t opcode,
                               cpu_state_t& state,
                               cpu_step_executor_t& executor) noexcept
    {
        switch (opcode)
        {
        case 0x0au:
            modify_accumulator(state, executor, asl_value);
            return true;
        case 0x2au:
            modify_accumulator(state, executor, rol_value);
            return true;
        case 0x4au:
            modify_accumulator(state, executor, lsr_value);
            return true;
        case 0x6au:
            modify_accumulator(state, executor, ror_value);
            return true;
        case 0x1au:
            modify_accumulator(state, executor, inc_value);
            return true;
        case 0x3au:
            modify_accumulator(state, executor, dec_value);
            return true;
        case 0x06u:
        case 0x0eu:
        case 0x16u:
        case 0x1eu:
            read_modify_write(state, executor, opcode, asl_value);
            return true;
        case 0x26u:
        case 0x2eu:
        case 0x36u:
        case 0x3eu:
            read_modify_write(state, executor, opcode, rol_value);
            return true;
        case 0x46u:
        case 0x4eu:
        case 0x56u:
        case 0x5eu:
            read_modify_write(state, executor, opcode, lsr_value);
            return true;
        case 0x66u:
        case 0x6eu:
        case 0x76u:
        case 0x7eu:
            read_modify_write(state, executor, opcode, ror_value);
            return true;
        case 0xc6u:
        case 0xceu:
        case 0xd6u:
        case 0xdeu:
            read_modify_write(state, executor, opcode, dec_value);
            return true;
        case 0xe6u:
        case 0xeeu:
        case 0xf6u:
        case 0xfeu:
            read_modify_write(state, executor, opcode, inc_value);
            return true;
        case 0x24u:
        case 0x2cu:
        case 0x34u:
        case 0x3cu:
        case 0x89u:
        {
            bit_test(state,
                     state.a,
                     read_bit_operand(state, executor, opcode),
                     accumulator_is_8bit(state),
                     opcode != 0x89u);
            return true;
        }
        case 0x04u:
        case 0x0cu:
            read_modify_write(
                state,
                executor,
                opcode,
                [](cpu_state_t& target_state, uint16_t value, bool is_8bit) noexcept
                {
                    bit_test(target_state, target_state.a, value, is_8bit, false);
                    return static_cast<uint16_t>((value | target_state.a) & mask_for_width(is_8bit));
                });
            return true;
        case 0x14u:
        case 0x1cu:
            read_modify_write(
                state,
                executor,
                opcode,
                [](cpu_state_t& target_state, uint16_t value, bool is_8bit) noexcept
                {
                    bit_test(target_state, target_state.a, value, is_8bit, false);
                    return static_cast<uint16_t>((value & ~target_state.a) & mask_for_width(is_8bit));
                });
            return true;
        default:
            return false;
        }
    }
} // namespace clover::core
