//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Apu.h"

namespace clover::core
{
    bool apu_t::execute_control_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x00u: // NOP
            step_spc_cycles(2);
            return true;

        case 0x0du: // PUSH P
            push_stack(_registers.psw);
            step_spc_cycles(4);
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
            push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            const uint16_t address_vector{ static_cast<uint16_t>(0xffdeu - (static_cast<uint16_t>(vector) << 1u)) };
            _registers.pc = read_u16(address_vector);
            step_spc_cycles(8);
            return true;
        }

        case 0x1du: // DEC X
            --_registers.x;
            set_nz_flags(_registers.x);
            step_spc_cycles(2);
            return true;

        case 0x1cu: // ASL A
            arithmetic_shift_left_accumulator();
            step_spc_cycles(2);
            return true;

        case 0x20u: // CLRP
            _registers.psw &= static_cast<uint8_t>(~k_psw_direct_page);
            step_spc_cycles(2);
            return true;

        case 0x2du: // PUSH A
            push_stack(_registers.a);
            step_spc_cycles(4);
            return true;

        case 0x3cu: // ROL A
            rotate_left_accumulator();
            step_spc_cycles(2);
            return true;

        case 0x3du: // INX
            ++_registers.x;
            set_nz_flags(_registers.x);
            step_spc_cycles(2);
            return true;

        case 0x3fu: // JSR abs
        {
            const uint16_t address{ fetch_u16() };
            push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            _registers.pc = address;
            step_spc_cycles(8);
            return true;
        }

        case 0x4fu: // CALL $ffxx
        {
            const uint8_t low{ fetch_u8() };
            push_stack(static_cast<uint8_t>(_registers.pc >> 8u));
            push_stack(static_cast<uint8_t>(_registers.pc & 0x00ffu));
            _registers.pc = static_cast<uint16_t>(0xff00u | low);
            step_spc_cycles(6);
            return true;
        }

        case 0x4du: // PUSH X
            push_stack(_registers.x);
            step_spc_cycles(4);
            return true;

        case 0x5fu: // JMP abs
            _registers.pc = fetch_u16();
            step_spc_cycles(3);
            return true;

        case 0x5cu: // LSR A
            logical_shift_right_accumulator();
            step_spc_cycles(2);
            return true;

        case 0x60u: // CLC
            _registers.psw &= static_cast<uint8_t>(~k_psw_carry);
            step_spc_cycles(2);
            return true;

        case 0x7cu: // ROR A
            rotate_right_accumulator();
            step_spc_cycles(2);
            return true;

        case 0x80u: // SEC
            _registers.psw |= k_psw_carry;
            step_spc_cycles(2);
            return true;

        case 0x6du: // PUSH Y
            push_stack(_registers.y);
            step_spc_cycles(4);
            return true;

        case 0x6fu: // RTS
        {
            const uint16_t address{
                static_cast<uint16_t>(pull_stack() | (static_cast<uint16_t>(pull_stack()) << 8u))
            };
            _registers.pc = address;
            step_spc_cycles(5);
            return true;
        }

        case 0x7du: // MOV A,X
            _registers.a = _registers.x;
            set_nz_flags(_registers.a);
            step_spc_cycles(2);
            return true;

        case 0x8eu: // POP P
            _registers.psw = pull_stack();
            step_spc_cycles(4);
            return true;

        case 0x9du: // MOV X,SP
            _registers.x = _registers.sp;
            set_nz_flags(_registers.x);
            step_spc_cycles(2);
            return true;

        case 0xceu: // POP X
            _registers.x = pull_stack();
            set_nz_flags(_registers.x);
            step_spc_cycles(4);
            return true;

        case 0xdcu: // DEC Y
            --_registers.y;
            set_nz_flags(_registers.y);
            step_spc_cycles(2);
            return true;

        case 0xe0u: // CLRV
            _registers.psw &= static_cast<uint8_t>(~(k_psw_half_carry | k_psw_overflow));
            step_spc_cycles(2);
            return true;

        case 0xeeu: // POP Y
            _registers.y = pull_stack();
            set_nz_flags(_registers.y);
            step_spc_cycles(4);
            return true;

        case 0xedu: // CMC
            _registers.psw ^= k_psw_carry;
            step_spc_cycles(2);
            return true;

        default:
            return false;
        }
    }
}
