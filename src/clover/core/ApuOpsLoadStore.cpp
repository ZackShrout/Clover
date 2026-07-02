//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Apu.h"

namespace clover::core
{
    bool apu_t::execute_load_store_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x5du: // MOV X,A
            _registers.x = _registers.a;
            set_nz_flags(_registers.x);
            step_spc_cycles(2);
            return true;

        case 0x8du: // MOV Y,#imm
            _registers.y = fetch_u8();
            set_nz_flags(_registers.y);
            step_spc_cycles(2);
            return true;

        case 0x8fu: // MOV dp,#imm
        {
            const uint8_t immediate{ fetch_u8() };
            const uint8_t direct_address{ fetch_u8() };
            write_direct(direct_address, immediate);
            step_spc_cycles(5);
            return true;
        }

        case 0xaeu: // POP A
            _registers.a = pull_stack();
            set_nz_flags(_registers.a);
            step_spc_cycles(4);
            return true;

        case 0xafu: // MOV (X)+,A
            write_u8(_registers.x, _registers.a);
            ++_registers.x;
            step_spc_cycles(4);
            return true;

        case 0xbfu: // MOV A,(X)+
            _registers.a = read_x_indirect_increment();
            step_spc_cycles(4);
            return true;

        case 0xbau: // MOVW YA,dp
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t value{ read_direct_u16(direct_address) };
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
            step_spc_cycles(4);
            return true;
        }

        case 0xbdu: // MOV SP,X
            _registers.sp = _registers.x;
            step_spc_cycles(2);
            return true;

        case 0xc4u: // MOV dp,A
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(direct_address, _registers.a);
            step_spc_cycles(4);
            return true;
        }

        case 0xc5u: // MOV abs,A
        {
            const uint16_t address{ fetch_u16() };
            write_u8(address, _registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xc6u: // MOV (X),A
            write_x_indirect(_registers.a);
            step_spc_cycles(4);
            return true;

        case 0xc7u: // MOV [dp+X],A
        {
            const uint8_t zero_page_address{ fetch_u8() };
            write_indexed_indirect(zero_page_address, _registers.a);
            step_spc_cycles(6);
            return true;
        }

        case 0xc9u: // MOV abs,X
        {
            const uint16_t address{ fetch_u16() };
            write_u8(address, _registers.x);
            step_spc_cycles(5);
            return true;
        }

        case 0xcbu: // MOV dp,Y
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(direct_address, _registers.y);
            step_spc_cycles(4);
            return true;
        }

        case 0xccu: // MOV abs,Y
        {
            const uint16_t address{ fetch_u16() };
            write_u8(address, _registers.y);
            step_spc_cycles(5);
            return true;
        }

        case 0xcdu: // MOV X,#imm
            _registers.x = fetch_u8();
            set_nz_flags(_registers.x);
            step_spc_cycles(2);
            return true;

        case 0xd5u: // MOV abs+X,A
        {
            const uint16_t address{ fetch_u16() };
            const uint16_t indexed_address{ static_cast<uint16_t>(address + _registers.x) };
            write_u8(indexed_address, _registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xd6u: // MOV abs+Y,A
        {
            const uint16_t address{ fetch_u16() };
            const uint16_t indexed_address{ static_cast<uint16_t>(address + _registers.y) };
            write_u8(indexed_address, _registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xd8u: // MOV dp,X
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(direct_address, _registers.x);
            step_spc_cycles(4);
            return true;
        }

        case 0xd4u: // MOV dp+X,A
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(static_cast<uint8_t>(direct_address + _registers.x), _registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xd7u: // MOV [dp]+Y,A
        {
            const uint8_t zero_page_address{ fetch_u8() };
            write_indirect_y(zero_page_address, _registers.a);
            step_spc_cycles(6);
            return true;
        }

        case 0xd9u: // MOV dp+Y,X
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(static_cast<uint8_t>(direct_address + _registers.y), _registers.x);
            step_spc_cycles(5);
            return true;
        }

        case 0xdbu: // MOV dp+X,Y
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct(static_cast<uint8_t>(direct_address + _registers.x), _registers.y);
            step_spc_cycles(5);
            return true;
        }

        case 0xdau: // MOVW dp,YA
        {
            const uint8_t direct_address{ fetch_u8() };
            write_direct_u16(direct_address,
                             static_cast<uint16_t>(_registers.a | (static_cast<uint16_t>(_registers.y) << 8u)));
            step_spc_cycles(5);
            return true;
        }

        case 0xddu: // MOV A,Y
            _registers.a = _registers.y;
            set_nz_flags(_registers.a);
            step_spc_cycles(2);
            return true;

        case 0xe4u: // MOV A,dp
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(direct_address) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.a = phased_io_read
                ? read_direct_phased(direct_address, 1, 1)
                : read_direct(direct_address);
            set_nz_flags(_registers.a);
            if (!phased_io_read)
                step_spc_cycles(3);
            return true;
        }

        case 0xe5u: // MOV A,abs
        {
            const uint16_t address{ fetch_u16() };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.a = phased_io_read
                ? read_u8_phased(address, 2, 1)
                : read_u8(address);
            set_nz_flags(_registers.a);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xe6u: // MOV A,(X)
            _registers.a = read_x_indirect();
            set_nz_flags(_registers.a);
            step_spc_cycles(3);
            return true;

        case 0xf4u: // MOV A,dp+X
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.a = phased_io_read
                ? read_direct_indexed_phased(direct_address, _registers.x, 2, 1)
                : read_direct_indexed(direct_address, _registers.x);
            set_nz_flags(_registers.a);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xe7u: // MOV A,[dp+X]
        {
            const uint8_t zero_page_address{ fetch_u8() };
            _registers.a = read_indexed_indirect(zero_page_address);
            set_nz_flags(_registers.a);
            step_spc_cycles(6);
            return true;
        }

        case 0xe8u: // MOV A,#imm
            _registers.a = fetch_u8();
            set_nz_flags(_registers.a);
            step_spc_cycles(2);
            return true;

        case 0xe9u: // MOV X,abs
        {
            const uint16_t address{ fetch_u16() };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.x = phased_io_read
                ? read_u8_phased(address, 2, 1)
                : read_u8(address);
            set_nz_flags(_registers.x);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xebu: // MOV Y,dp
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(direct_address) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.y = phased_io_read
                ? read_direct_phased(direct_address, 1, 1)
                : read_direct(direct_address);
            set_nz_flags(_registers.y);
            if (!phased_io_read)
                step_spc_cycles(3);
            return true;
        }

        case 0xecu: // MOV Y,abs
        {
            const uint16_t address{ fetch_u16() };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.y = phased_io_read
                ? read_u8_phased(address, 2, 1)
                : read_u8(address);
            set_nz_flags(_registers.y);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xf5u: // MOV A,abs+X
        {
            const uint16_t address{ fetch_u16() };
            _registers.a = read_u8(static_cast<uint16_t>(address + _registers.x));
            set_nz_flags(_registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xf6u: // MOV A,abs+Y
        {
            const uint16_t address{ fetch_u16() };
            _registers.a = read_u8(static_cast<uint16_t>(address + _registers.y));
            set_nz_flags(_registers.a);
            step_spc_cycles(5);
            return true;
        }

        case 0xf7u: // MOV A,[dp]+Y
        {
            const uint8_t zero_page_address{ fetch_u8() };
            _registers.a = read_indirect_y(zero_page_address);
            set_nz_flags(_registers.a);
            step_spc_cycles(6);
            return true;
        }

        case 0xf8u: // MOV X,dp
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(direct_address) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.x = phased_io_read
                ? read_direct_phased(direct_address, 1, 1)
                : read_direct(direct_address);
            set_nz_flags(_registers.x);
            if (!phased_io_read)
                step_spc_cycles(3);
            return true;
        }

        case 0xf9u: // MOV X,dp+Y
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(static_cast<uint8_t>(direct_address + _registers.y)) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.x = phased_io_read
                ? read_direct_indexed_phased(direct_address, _registers.y, 2, 1)
                : read_direct_indexed(direct_address, _registers.y);
            set_nz_flags(_registers.x);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xfau: // MOV dp(1),dp(0)
        {
            const uint8_t source_address{ fetch_u8() };
            const uint8_t target_address{ fetch_u8() };
            write_direct(target_address, read_direct(source_address));
            step_spc_cycles(5);
            return true;
        }

        case 0xfbu: // MOV Y,dp+X
        {
            const uint8_t direct_address{ fetch_u8() };
            const uint16_t address{ direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)) };
            const bool phased_io_read{ is_cpu_port_address(address) };
            _registers.y = phased_io_read
                ? read_direct_indexed_phased(direct_address, _registers.x, 2, 1)
                : read_direct_indexed(direct_address, _registers.x);
            set_nz_flags(_registers.y);
            if (!phased_io_read)
                step_spc_cycles(4);
            return true;
        }

        case 0xfdu: // MOV Y,A
            _registers.y = _registers.a;
            set_nz_flags(_registers.y);
            step_spc_cycles(2);
            return true;

        default:
            return false;
        }
    }
}
