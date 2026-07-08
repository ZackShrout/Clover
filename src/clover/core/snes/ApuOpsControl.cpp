//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"

namespace clover::core
{
    bool apu_t::execute_control_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x00u: // NOP
            (void)spc_read_u8(_registers.pc);
            return true;

        case 0x0du: // PUSH P
            spc_consume_opcode_fetch();
            spc_push_stack(_registers.psw);
            spc_idle();
            return true;

        case 0x01u:
        case 0x11u:
        case 0x21u:
        case 0x31u:
        case 0x41u:
        case 0x51u:
        case 0x61u:
        case 0x71u:
        case 0x81u:
        case 0x91u:
        case 0xa1u:
        case 0xb1u:
        case 0xc1u:
        case 0xd1u:
        case 0xe1u:
        case 0xf1u:
        {
            const uint8_t vector{ static_cast<uint8_t>(opcode >> 4u) };
            spc_consume_opcode_fetch();
            spc_idle();
            spc_push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            spc_push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            spc_idle();
            const uint16_t address_vector{ static_cast<uint16_t>(0xffdeu - (static_cast<uint16_t>(vector) << 1u)) };
            const uint8_t low{ spc_read_u8(address_vector) };
            const uint8_t high{ spc_read_u8(static_cast<uint16_t>(address_vector + 1u)) };
            _registers.pc = static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u));
            return true;
        }

        case 0x1du: // DEC X
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            --_registers.x;
            set_nz_flags(_registers.x);
            return true;

        case 0x1cu: // ASL A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            arithmetic_shift_left_accumulator();
            return true;

        case 0x20u: // CLRP
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.psw &= static_cast<uint8_t>(~k_psw_direct_page);
            return true;

        case 0x2du: // PUSH A
            spc_consume_opcode_fetch();
            spc_push_stack(_registers.a);
            spc_idle();
            return true;

        case 0x3cu: // ROL A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            rotate_left_accumulator();
            return true;

        case 0x3du: // INX
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            ++_registers.x;
            set_nz_flags(_registers.x);
            return true;

        case 0x3fu: // JSR abs
        {
            const uint16_t address{ spc_fetch_u16() };
            spc_idle();
            spc_push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            spc_push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            spc_idle();
            spc_idle();
            _registers.pc = address;
            return true;
        }

        case 0x4fu: // CALL $ffxx
        {
            const uint8_t low{ spc_fetch_u8() };
            spc_idle();
            spc_push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            spc_push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            spc_idle();
            _registers.pc = static_cast<uint16_t>(0xff00u | low);
            return true;
        }

        case 0x4du: // PUSH X
            spc_consume_opcode_fetch();
            spc_push_stack(_registers.x);
            spc_idle();
            return true;

        case 0x5fu: // JMP abs
            _registers.pc = spc_fetch_u16();
            return true;

        case 0x5cu: // LSR A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            logical_shift_right_accumulator();
            return true;

        case 0x60u: // CLC
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
            return true;

        case 0x7cu: // ROR A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            rotate_right_accumulator();
            return true;

        case 0x80u: // SEC
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.psw |= k_psw_carry;
            return true;

        case 0x6du: // PUSH Y
            spc_consume_opcode_fetch();
            spc_push_stack(_registers.y);
            spc_idle();
            return true;

        case 0x6fu: // RTS
        {
            spc_idle();
            const uint16_t address{
                static_cast<uint16_t>(spc_pull_stack() | (static_cast<uint16_t>(spc_pull_stack()) << 8u))
            };
            _registers.pc = address;
            return true;
        }

        case 0x7du: // MOV A,X
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.a = _registers.x;
            set_nz_flags(_registers.a);
            return true;

        case 0x8eu: // POP P
            spc_idle();
            _registers.psw = spc_pull_stack();
            return true;

        case 0x9du: // MOV X,SP
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.x = _registers.sp;
            set_nz_flags(_registers.x);
            return true;

        case 0xceu: // POP X
            spc_idle();
            _registers.x = spc_pull_stack();
            set_nz_flags(_registers.x);
            return true;

        case 0xdcu: // DEC Y
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            --_registers.y;
            set_nz_flags(_registers.y);
            return true;

        case 0xe0u: // CLRV
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.psw &= static_cast<uint8_t>(~(k_psw_half_carry | k_psw_overflow));
            return true;

        case 0xc0u: // DI / clear interrupt enable
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            _registers.psw &= static_cast<uint8_t>(~0x04u);
            return true;

        case 0xeeu: // POP Y
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            _registers.y = spc_pull_stack();
            set_nz_flags(_registers.y);
            return true;

        case 0xedu: // CMC
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.psw ^= k_psw_carry;
            return true;

        default:
            return false;
        }
    }
}
