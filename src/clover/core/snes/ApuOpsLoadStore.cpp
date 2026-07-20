//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"

namespace clover::core
{
    bool apu_t::execute_load_store_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x5du: // MOV X,A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.x = _registers.a;
            set_nz_flags(_registers.x);
            return true;

        case 0x8du: // MOV Y,#imm
            spc_consume_opcode_fetch();
            _registers.y = spc_fetch_u8();
            set_nz_flags(_registers.y);
            return true;

        case 0x8fu: // MOV dp,#imm
        {
            spc_consume_opcode_fetch();
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            (void)spc_load_direct(direct_address);
            spc_store_direct(direct_address, immediate);
            return true;
        }

        case 0xaeu: // POP A
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            _registers.a = spc_pull_stack();
            set_nz_flags(_registers.a);
            return true;

        case 0xafu: // MOV (X)+,A
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            spc_store_direct(_registers.x, _registers.a);
            ++_registers.x;
            return true;

        case 0xbfu: // MOV A,(X)+
            (void)spc_read_u8(_registers.pc);
            _registers.a = spc_load_direct(_registers.x++);
            spc_idle();
            set_nz_flags(_registers.a);
            return true;

        case 0xbau: // MOVW YA,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t low{ spc_load_direct(direct_address) };
            spc_idle();
            const uint8_t high{ spc_load_direct(static_cast<uint8_t>(direct_address + 1u)) };
            const uint16_t value{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
            _registers.a = static_cast<uint8_t>(value & 0x00ffu);
            _registers.y = static_cast<uint8_t>(value >> 8u);
            if (value == 0)
                _registers.psw |= k_psw_zero;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

            if ((_registers.y & 0x80u) != 0)
                _registers.psw |= k_psw_negative;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_negative);
            return true;
        }

        case 0xbdu: // MOV SP,X
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.sp = _registers.x;
            return true;

        case 0xc4u: // MOV dp,A
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            (void)spc_load_direct(direct_address);
            spc_store_direct(direct_address, _registers.a);
            return true;
        }

        case 0xc5u: // MOV abs,A
        {
            const uint16_t address{ spc_fetch_u16() };
            (void)spc_read_u8(address);
            spc_write_u8(address, _registers.a);
            return true;
        }

        case 0xc6u: // MOV (X),A
            spc_consume_opcode_fetch();
            // The indirect-X store performs a throwaway read from the next
            // opcode address before reading and writing the direct-page byte.
            // The IPL ROM uses this instruction in its 240-byte RAM-clear
            // loop, so omitting the cycle advances the boot signature by 480
            // SMP clocks.
            (void)spc_read_u8(_registers.pc);
            (void)spc_load_direct(_registers.x);
            spc_store_direct(_registers.x, _registers.a);
            return true;

        case 0xc7u: // MOV [dp+X],A
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            spc_idle();
            const uint8_t indexed_address{ static_cast<uint8_t>(zero_page_address + _registers.x) };
            const uint8_t low{ spc_load_direct(indexed_address) };
            const uint8_t high{ spc_load_direct(static_cast<uint8_t>(indexed_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
            (void)spc_read_u8(address);
            spc_write_u8(address, _registers.a);
            return true;
        }

        case 0xc9u: // MOV abs,X
        {
            const uint16_t address{ spc_fetch_u16() };
            (void)spc_read_u8(address);
            spc_write_u8(address, _registers.x);
            return true;
        }

        case 0xcbu: // MOV dp,Y
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            (void)spc_load_direct(direct_address);
            spc_store_direct(direct_address, _registers.y);
            return true;
        }

        case 0xccu: // MOV abs,Y
        {
            const uint16_t address{ spc_fetch_u16() };
            (void)spc_read_u8(address);
            spc_write_u8(address, _registers.y);
            return true;
        }

        case 0xcdu: // MOV X,#imm
            spc_consume_opcode_fetch();
            _registers.x = spc_fetch_u8();
            set_nz_flags(_registers.x);
            return true;

        case 0xd5u: // MOV abs+X,A
        {
            const uint16_t address{ spc_fetch_u16() };
            const uint16_t indexed_address{ static_cast<uint16_t>(address + _registers.x) };
            spc_idle();
            (void)spc_read_u8(indexed_address);
            spc_write_u8(indexed_address, _registers.a);
            return true;
        }

        case 0xd6u: // MOV abs+Y,A
        {
            const uint16_t address{ spc_fetch_u16() };
            const uint16_t indexed_address{ static_cast<uint16_t>(address + _registers.y) };
            spc_idle();
            (void)spc_read_u8(indexed_address);
            spc_write_u8(indexed_address, _registers.a);
            return true;
        }

        case 0xd8u: // MOV dp,X
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            (void)spc_load_direct(direct_address);
            spc_store_direct(direct_address, _registers.x);
            return true;
        }

        case 0xd4u: // MOV dp+X,A
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            const uint8_t indexed_address{ static_cast<uint8_t>(direct_address + _registers.x) };
            (void)spc_load_direct(indexed_address);
            spc_store_direct(indexed_address, _registers.a);
            return true;
        }

        case 0xd7u: // MOV [dp]+Y,A
        {
            spc_consume_opcode_fetch();
            const uint8_t zero_page_address{ spc_fetch_u8() };
            const uint8_t low{ spc_load_direct(zero_page_address) };
            const uint8_t high{ spc_load_direct(static_cast<uint8_t>(zero_page_address + 1u)) };
            spc_idle();
            const uint16_t address{
                static_cast<uint16_t>((static_cast<uint16_t>(high) << 8u) | low)
            };
            (void)spc_read_u8(static_cast<uint16_t>(address + _registers.y));
            spc_write_u8(static_cast<uint16_t>(address + _registers.y), _registers.a);
            return true;
        }

        case 0xd9u: // MOV dp+Y,X
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            const uint8_t indexed_address{ static_cast<uint8_t>(direct_address + _registers.y) };
            (void)spc_load_direct(indexed_address);
            spc_store_direct(indexed_address, _registers.x);
            return true;
        }

        case 0xdbu: // MOV dp+X,Y
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            const uint8_t indexed_address{ static_cast<uint8_t>(direct_address + _registers.x) };
            (void)spc_load_direct(indexed_address);
            spc_store_direct(indexed_address, _registers.y);
            return true;
        }

        case 0xdau: // MOVW dp,YA
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            (void)spc_load_direct(direct_address);
            spc_store_direct(direct_address, _registers.a);
            spc_store_direct(static_cast<uint8_t>(direct_address + 1u), _registers.y);
            return true;
        }

        case 0xddu: // MOV A,Y
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.a = _registers.y;
            set_nz_flags(_registers.a);
            return true;

        case 0xe4u: // MOV A,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            _registers.a = spc_load_direct(direct_address);
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xe5u: // MOV A,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            _registers.a = spc_read_u8(address);
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xe6u: // MOV A,(X)
            (void)spc_read_u8(_registers.pc);
            _registers.a = spc_load_direct(_registers.x);
            set_nz_flags(_registers.a);
            return true;

        case 0xf4u: // MOV A,dp+X
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            _registers.a = spc_load_direct(static_cast<uint8_t>(direct_address + _registers.x));
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xe7u: // MOV A,[dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            spc_idle();
            const uint8_t indexed_address{ static_cast<uint8_t>(zero_page_address + _registers.x) };
            const uint8_t low{ spc_load_direct(indexed_address) };
            const uint8_t high{ spc_load_direct(static_cast<uint8_t>(indexed_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
            _registers.a = spc_read_u8(address);
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xe8u: // MOV A,#imm
            spc_consume_opcode_fetch();
            _registers.a = spc_fetch_u8();
            set_nz_flags(_registers.a);
            return true;

        case 0xe9u: // MOV X,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            _registers.x = spc_read_u8(address);
            set_nz_flags(_registers.x);
            return true;
        }

        case 0xebu: // MOV Y,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            _registers.y = spc_load_direct(direct_address);
            set_nz_flags(_registers.y);
            return true;
        }

        case 0xecu: // MOV Y,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            _registers.y = spc_read_u8(address);
            set_nz_flags(_registers.y);
            return true;
        }

        case 0xf5u: // MOV A,abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            spc_idle();
            _registers.a = spc_read_u8(static_cast<uint16_t>(address + _registers.x));
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xf6u: // MOV A,abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            spc_idle();
            _registers.a = spc_read_u8(static_cast<uint16_t>(address + _registers.y));
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xf7u: // MOV A,[dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            const uint8_t low{ spc_load_direct(zero_page_address) };
            const uint8_t high{ spc_load_direct(static_cast<uint8_t>(zero_page_address + 1u)) };
            const uint16_t address{ static_cast<uint16_t>(low | (static_cast<uint16_t>(high) << 8u)) };
            spc_idle();
            _registers.a = spc_read_u8(static_cast<uint16_t>(address + _registers.y));
            set_nz_flags(_registers.a);
            return true;
        }

        case 0xf8u: // MOV X,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            _registers.x = spc_load_direct(direct_address);
            set_nz_flags(_registers.x);
            return true;
        }

        case 0xf9u: // MOV X,dp+Y
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            _registers.x = spc_load_direct(static_cast<uint8_t>(direct_address + _registers.y));
            set_nz_flags(_registers.x);
            return true;
        }

        case 0xfau: // MOV dp(1),dp(0)
        {
            spc_consume_opcode_fetch();
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t value{ spc_load_direct(source_address) };
            const uint8_t target_address{ spc_fetch_u8() };
            spc_store_direct(target_address, value);
            return true;
        }

        case 0xfbu: // MOV Y,dp+X
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            _registers.y = spc_load_direct(static_cast<uint8_t>(direct_address + _registers.x));
            set_nz_flags(_registers.y);
            return true;
        }

        case 0xfdu: // MOV Y,A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            _registers.y = _registers.a;
            set_nz_flags(_registers.y);
            return true;

        default:
            return false;
        }
    }
}
