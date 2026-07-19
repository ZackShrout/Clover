//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"

namespace clover::core
{
    bool apu_t::execute_branch_bit_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x03u:
        case 0x13u:
        case 0x23u:
        case 0x33u:
        case 0x43u:
        case 0x53u:
        case 0x63u:
        case 0x73u:
        case 0x83u:
        case 0x93u:
        case 0xa3u:
        case 0xb3u:
        case 0xc3u:
        case 0xd3u:
        case 0xe3u:
        case 0xf3u:
        {
            const uint8_t bit_index{ static_cast<uint8_t>(opcode >> 4u) };
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            branch_relative_if_direct_bit(direct_address,
                                          static_cast<uint8_t>(1u << (bit_index & 0x07u)),
                                          (opcode & 0x10u) == 0);
            return true;
        }

        case 0x10u: // BPL rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_negative) == 0);
            return true;

        case 0x1fu: // JMP [abs+X]
        {
            spc_consume_opcode_fetch();
            const uint16_t base{ spc_fetch_u16() };
            spc_idle();
            const uint16_t address{ static_cast<uint16_t>(base + _registers.x) };
            const uint8_t low{ spc_read_u8(address) };
            const uint8_t high{ spc_read_u8(static_cast<uint16_t>(address + 1u)) };
            _registers.pc = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u));
            return true;
        }

        case 0x2fu: // BRA rel
            spc_consume_opcode_fetch();
            branch_relative_if(true);
            return true;

        case 0x2eu: // CBNE dp,rel
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            branch_relative_if_accumulator_not_equal_direct(direct_address);
            return true;
        }

        case 0x30u: // BMI rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_negative) != 0);
            return true;

        case 0x50u: // BVC rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_overflow) == 0);
            return true;

        case 0x70u: // BVS rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_overflow) != 0);
            return true;

        case 0x6eu: // DBNZ dp,rel
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            decrement_direct_and_branch_if_not_zero(direct_address);
            return true;
        }

        case 0x90u: // BCC rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_carry) == 0);
            return true;

        case 0x0au: // OR1 C,abs.bit
        case 0x2au: // OR1 C,/abs.bit
        case 0x4au: // AND1 C,abs.bit
        case 0x6au: // AND1 C,/abs.bit
        case 0x8au: // EOR1 C,abs.bit
        case 0xaau: // MOV1 C,abs.bit
        case 0xcau: // MOV1 abs.bit,C
        case 0xeau: // NOT1 abs.bit
        {
            const uint16_t encoded_address{ spc_fetch_u16() };
            const uint16_t address{ static_cast<uint16_t>(encoded_address & 0x1fffu) };
            const uint8_t bit_index{ static_cast<uint8_t>(encoded_address >> 13u) };
            const uint8_t bit_mask{ static_cast<uint8_t>(1u << bit_index) };
            uint8_t value{ spc_read_u8(address) };
            const bool bit_set{ (value & static_cast<uint8_t>(1u << bit_index)) != 0 };
            const bool carry_set{ (_registers.psw & k_psw_carry) != 0 };

            switch (opcode >> 5u)
            {
            case 0u:
                spc_idle();
                if (carry_set || bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 1u:
                spc_idle();
                if (carry_set || !bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 2u:
                if (carry_set && bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 3u:
                if (carry_set && !bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 4u:
                spc_idle();
                if (carry_set != bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 5u:
                if (bit_set)
                    _registers.psw |= k_psw_carry;
                else
                    _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
                break;
            case 6u:
                spc_idle();
                value = carry_set
                    ? static_cast<uint8_t>(value | bit_mask)
                    : static_cast<uint8_t>(value & static_cast<uint8_t>(~bit_mask));
                spc_write_u8(address, value);
                break;
            case 7u:
                value ^= bit_mask;
                spc_write_u8(address, value);
                break;
            default:
                break;
            }
            return true;
        }

        case 0xb0u: // BCS rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_carry) != 0);
            return true;

        case 0x02u:
        case 0x12u:
        case 0x22u:
        case 0x32u:
        case 0x42u:
        case 0x52u:
        case 0x62u:
        case 0x72u:
        case 0x82u:
        case 0x92u:
        case 0xa2u:
        case 0xb2u:
        case 0xc2u:
        case 0xd2u:
        case 0xe2u:
        case 0xf2u:
        {
            spc_consume_opcode_fetch();
            const uint8_t bit_index{ static_cast<uint8_t>(opcode >> 4u) };
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t bit_mask{ static_cast<uint8_t>(1u << (bit_index & 0x07u)) };
            uint8_t value{ spc_load_direct(direct_address) };
            value = (opcode & 0x10u) == 0
                ? static_cast<uint8_t>(value | bit_mask)
                : static_cast<uint8_t>(value & static_cast<uint8_t>(~bit_mask));
            spc_store_direct(direct_address, value);
            return true;
        }

        case 0xd0u: // BNE rel
            spc_consume_opcode_fetch();
            branch_relative_if((_registers.psw & k_psw_zero) == 0);
            return true;

        case 0xdeu: // CBNE dp+X,rel
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            branch_relative_if_accumulator_not_equal_direct_indexed(direct_address, _registers.x);
            return true;
        }

        case 0xf0u: // BEQ rel
        {
            spc_consume_opcode_fetch();
            const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
            if ((_registers.psw & k_psw_zero) == 0)
                return true;

            spc_idle();
            spc_idle();
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            return true;
        }

        case 0xfeu: // DBNZ Y,rel
        {
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            const int8_t displacement{ static_cast<int8_t>(spc_fetch_u8()) };
            if (--_registers.y == 0)
                return true;

            spc_idle();
            spc_idle();
            _registers.pc = static_cast<uint16_t>(_registers.pc + displacement);
            return true;
        }

        default:
            return false;
        }
    }
}
