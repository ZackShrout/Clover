//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"

namespace clover::core
{
    bool apu_t::execute_alu_opcode(uint8_t opcode) noexcept
    {
        switch (opcode)
        {
        case 0x04u: // OR A,dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            or_accumulator(spc_load_direct(direct_address));
            return true;
        }

        case 0x05u: // OR A,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            or_accumulator(spc_read_u8(address));
            return true;
        }

        case 0x06u: // OR A,(X)
            (void)spc_read_u8(_registers.pc);
            or_accumulator(spc_load_direct(_registers.x));
            return true;

        case 0x07u: // OR A,[dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            or_accumulator(spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0x08u: // OR A,#imm
            or_accumulator(spc_fetch_u8());
            return true;

        case 0x09u: // OR dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t target_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(target_address) | spc_load_direct(source_address)) };
            spc_store_direct(target_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x0bu: // ASL dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            arithmetic_shift_left_memory(direct_page_address(direct_address));
            return true;
        }

        case 0x0cu: // ASL abs
        {
            const uint16_t address{ spc_fetch_u16() };
            arithmetic_shift_left_memory(address);
            return true;
        }

        case 0x14u: // OR A,dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            or_accumulator(spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0x15u: // OR A,abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            or_accumulator(spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0x16u: // OR A,abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            or_accumulator(spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0x17u: // OR A,[dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            or_accumulator(spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0x18u: // OR dp,#imm
        {
            spc_consume_opcode_fetch();
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) | immediate) };
            spc_store_direct(direct_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x19u: // OR (X)=(Y)
            or_indirect_x_with_indirect_y();
            return true;

        case 0x1au: // DECW dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            uint16_t value{ static_cast<uint16_t>(spc_load_direct(direct_address) - 1u) };
            spc_store_direct(direct_address, static_cast<uint8_t>(value >> 0u));
            value = static_cast<uint16_t>(value + (static_cast<uint16_t>(spc_load_direct(static_cast<uint8_t>(direct_address + 1u))) << 8u));
            spc_store_direct(static_cast<uint8_t>(direct_address + 1u), static_cast<uint8_t>(value >> 8u));
            if (value == 0)
                _registers.psw |= k_psw_zero;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

            if ((value & 0x8000u) != 0)
                _registers.psw |= k_psw_negative;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_negative);
            return true;
        }

        case 0x1bu: // ASL dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            arithmetic_shift_left_memory(direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)));
            return true;
        }

        case 0x2bu: // ROL dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            rotate_left_memory(direct_page_address(direct_address));
            return true;
        }

        case 0x2cu: // ROL abs
        {
            const uint16_t address{ spc_fetch_u16() };
            rotate_left_memory(address);
            return true;
        }

        case 0x3bu: // ROL dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            rotate_left_memory(direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)));
            return true;
        }

        case 0x24u: // AND A,dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            and_accumulator(spc_load_direct(direct_address));
            return true;
        }

        case 0x25u: // AND A,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            and_accumulator(spc_read_u8(address));
            return true;
        }

        case 0x26u: // AND A,(X)
            (void)spc_read_u8(_registers.pc);
            and_accumulator(spc_load_direct(_registers.x));
            return true;

        case 0x27u: // AND A,[dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            and_accumulator(spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0x28u: // AND A,#imm
            and_accumulator(spc_fetch_u8());
            return true;

        case 0x29u: // AND dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t target_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(target_address) & spc_load_direct(source_address)) };
            spc_store_direct(target_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x38u: // AND dp,#imm
        {
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            and_direct_immediate(immediate, direct_address);
            return true;
        }

        case 0x39u: // AND (X)=(Y)
            and_indirect_x_with_indirect_y();
            return true;

        case 0x3au: // INCW dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            uint16_t value{ static_cast<uint16_t>(spc_load_direct(direct_address) + 1u) };
            spc_store_direct(direct_address, static_cast<uint8_t>(value >> 0u));
            value = static_cast<uint16_t>(value + (static_cast<uint16_t>(spc_load_direct(static_cast<uint8_t>(direct_address + 1u))) << 8u));
            spc_store_direct(static_cast<uint8_t>(direct_address + 1u), static_cast<uint8_t>(value >> 8u));
            if (value == 0)
                _registers.psw |= k_psw_zero;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_zero);

            if ((value & 0x8000u) != 0)
                _registers.psw |= k_psw_negative;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_negative);
            return true;
        }

        case 0x45u: // EOR abs
        {
            const uint16_t address{ spc_fetch_u16() };
            xor_accumulator(spc_read_u8(address));
            return true;
        }

        case 0x44u: // EOR dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            xor_accumulator(spc_load_direct(direct_address));
            return true;
        }

        case 0x46u: // EOR (X)
            (void)spc_read_u8(_registers.pc);
            xor_accumulator(spc_load_direct(_registers.x));
            return true;

        case 0x47u: // EOR [dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            xor_accumulator(spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0x48u: // EOR A,#imm
            xor_accumulator(spc_fetch_u8());
            return true;

        case 0x49u: // EOR dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t target_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(target_address) ^ spc_load_direct(source_address)) };
            spc_store_direct(target_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x4eu: // TCLR1 abs
        {
            const uint16_t address{ spc_fetch_u16() };
            test_and_modify_bits_absolute(address, false);
            return true;
        }

        case 0x58u: // EOR dp,#imm
        {
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            xor_direct_immediate(immediate, direct_address);
            return true;
        }

        case 0x59u: // EOR (X)=(Y)
            xor_indirect_x_with_indirect_y();
            return true;

        case 0x64u: // CMP A,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            set_compare_flags(_registers.a, spc_load_direct(direct_address));
            return true;
        }

        case 0x65u: // CMP A,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            set_compare_flags(_registers.a, spc_read_u8(address));
            return true;
        }

        case 0x66u: // CMP A,(X)
            (void)spc_read_u8(_registers.pc);
            set_compare_flags(_registers.a, spc_load_direct(_registers.x));
            return true;

        case 0x67u: // CMP A,[dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            set_compare_flags(_registers.a, spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0x68u: // CMP A,#imm
            spc_consume_opcode_fetch();
            set_compare_flags(_registers.a, spc_fetch_u8());
            return true;

        case 0x69u: // CMP dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t target_address{ spc_fetch_u8() };
            set_compare_flags(spc_load_direct(target_address), spc_load_direct(source_address));
            spc_idle();
            return true;
        }

        case 0x74u: // CMP A,dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            set_compare_flags(_registers.a, spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0x75u: // CMP A,abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            set_compare_flags(_registers.a, spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0x76u: // CMP A,abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            set_compare_flags(_registers.a, spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0x77u: // CMP A,[dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            set_compare_flags(_registers.a, spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0x78u: // CMP dp,#imm
        {
            spc_consume_opcode_fetch();
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            set_compare_flags(spc_load_direct(direct_address), immediate);
            return true;
        }

        case 0x79u: // CMP (X),(Y)
            (void)spc_read_u8(_registers.pc);
            set_compare_flags(spc_load_direct(_registers.x), spc_load_direct(_registers.y));
            spc_idle();
            return true;

        case 0x7au: // ADDW YA,dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint16_t ya{ static_cast<uint16_t>(_registers.a | (static_cast<uint16_t>(_registers.y) << 8u)) };
            uint16_t value{ spc_load_direct(direct_address) };
            spc_idle();
            value = static_cast<uint16_t>(value | (static_cast<uint16_t>(spc_load_direct(static_cast<uint8_t>(direct_address + 1u))) << 8u));
            const uint16_t result{ add_word(ya, value) };
            _registers.a = static_cast<uint8_t>(result & 0x00ffu);
            _registers.y = static_cast<uint8_t>(result >> 8u);
            return true;
        }

        case 0x34u: // AND A,dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            and_accumulator(spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0x35u: // AND A,abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            and_accumulator(spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0x36u: // AND A,abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            and_accumulator(spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0x37u: // AND A,[dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            and_accumulator(spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0x7eu: // CMP Y,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            set_compare_flags(_registers.y, spc_load_direct(direct_address));
            return true;
        }

        case 0x84u: // ADC dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            add_with_carry(spc_load_direct(direct_address));
            return true;
        }

        case 0x85u: // ADC abs
        {
            const uint16_t address{ spc_fetch_u16() };
            add_with_carry(spc_read_u8(address));
            return true;
        }

        case 0x86u: // ADC (X)
            (void)spc_read_u8(_registers.pc);
            add_with_carry(spc_load_direct(_registers.x));
            return true;

        case 0x87u: // ADC [dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            add_with_carry(spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0x88u: // ADC #imm
            add_with_carry(spc_fetch_u8());
            return true;

        case 0x89u: // ADC dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t rhs{ spc_load_direct(source_address) };
            const uint8_t target_address{ spc_fetch_u8() };
            const uint8_t original_a{ _registers.a };
            _registers.a = spc_load_direct(target_address);
            add_with_carry(rhs);
            spc_store_direct(target_address, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0x94u: // ADC dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            add_with_carry(spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0x95u: // ADC abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            add_with_carry(spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0x96u: // ADC abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            add_with_carry(spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0x97u: // ADC [dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            add_with_carry(spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0x98u: // ADC dp,#imm
        {
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t original_a{ _registers.a };
            _registers.a = spc_load_direct(direct_address);
            add_with_carry(immediate);
            spc_store_direct(direct_address, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0x9au: // SUBW YA,dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint16_t ya{ static_cast<uint16_t>(_registers.a | (static_cast<uint16_t>(_registers.y) << 8u)) };
            uint16_t value{ spc_load_direct(direct_address) };
            spc_idle();
            value = static_cast<uint16_t>(value | (static_cast<uint16_t>(spc_load_direct(static_cast<uint8_t>(direct_address + 1u))) << 8u));
            const uint16_t result{ subtract_word(ya, value) };
            _registers.a = static_cast<uint8_t>(result & 0x00ffu);
            _registers.y = static_cast<uint8_t>(result >> 8u);
            return true;
        }

        case 0x99u: // ADC (X)=(Y)
        {
            const uint8_t original_a{ _registers.a };
            (void)spc_read_u8(_registers.pc);
            const uint8_t rhs{ spc_load_direct(_registers.y) };
            _registers.a = spc_load_direct(_registers.x);
            add_with_carry(rhs);
            spc_store_direct(_registers.x, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0x9bu: // DEC dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            const uint16_t address{ direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)) };
            const uint8_t value{ static_cast<uint8_t>(spc_read_u8(address) - 1u) };
            spc_write_u8(address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x8bu: // DEC dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) - 1u) };
            spc_store_direct(direct_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x8cu: // DEC abs
        {
            const uint16_t address{ spc_fetch_u16() };
            const uint8_t value{ static_cast<uint8_t>(spc_read_u8(address) - 1u) };
            spc_write_u8(address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x9fu: // XCN A
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            spc_idle();
            spc_idle();
            exchange_accumulator_nibbles();
            return true;

        case 0x9eu: // DIV YA,X
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            divide_ya_by_x();
            return true;

        case 0xa4u: // SBC dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            subtract_with_carry(spc_load_direct(direct_address));
            return true;
        }

        case 0xa5u: // SBC abs
        {
            const uint16_t address{ spc_fetch_u16() };
            subtract_with_carry(spc_read_u8(address));
            return true;
        }

        case 0xa6u: // SBC (X)
            (void)spc_read_u8(_registers.pc);
            subtract_with_carry(spc_load_direct(_registers.x));
            return true;

        case 0xa7u: // SBC [dp+X]
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            subtract_with_carry(spc_read_indexed_indirect(zero_page_address, _registers.x));
            return true;
        }

        case 0xa8u: // SBC #imm
            subtract_with_carry(spc_fetch_u8());
            return true;

        case 0xa9u: // SBC dp(1),dp(0)
        {
            const uint8_t source_address{ spc_fetch_u8() };
            const uint8_t rhs{ spc_load_direct(source_address) };
            const uint8_t target_address{ spc_fetch_u8() };
            const uint8_t original_a{ _registers.a };
            _registers.a = spc_load_direct(target_address);
            subtract_with_carry(rhs);
            spc_store_direct(target_address, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0xb4u: // SBC dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            subtract_with_carry(spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0xb5u: // SBC abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            subtract_with_carry(spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0xb6u: // SBC abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            subtract_with_carry(spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0xb7u: // SBC [dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            subtract_with_carry(spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0xb8u: // SBC dp,#imm
        {
            const uint8_t immediate{ spc_fetch_u8() };
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t original_a{ _registers.a };
            _registers.a = spc_load_direct(direct_address);
            subtract_with_carry(immediate);
            spc_store_direct(direct_address, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0xb9u: // SBC (X)=(Y)
        {
            const uint8_t original_a{ _registers.a };
            (void)spc_read_u8(_registers.pc);
            const uint8_t rhs{ spc_load_direct(_registers.y) };
            _registers.a = spc_load_direct(_registers.x);
            subtract_with_carry(rhs);
            spc_store_direct(_registers.x, _registers.a);
            _registers.a = original_a;
            return true;
        }

        case 0x9cu: // DEC A
            (void)spc_read_u8(_registers.pc);
            --_registers.a;
            set_nz_flags(_registers.a);
            return true;

        case 0xacu: // INC abs
        {
            const uint16_t address{ spc_fetch_u16() };
            const uint8_t value{ static_cast<uint8_t>(spc_read_u8(address) + 1u) };
            spc_write_u8(address, value);
            set_nz_flags(value);
            return true;
        }

        case 0xadu: // CMP Y,#imm
            spc_consume_opcode_fetch();
            set_compare_flags(_registers.y, spc_fetch_u8());
            return true;

        case 0xabu: // INC dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            const uint8_t value{ static_cast<uint8_t>(spc_load_direct(direct_address) + 1u) };
            spc_store_direct(direct_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0xbbu: // INC dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            const uint16_t address{ direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)) };
            const uint8_t value{ static_cast<uint8_t>(spc_read_u8(address) + 1u) };
            spc_write_u8(address, value);
            set_nz_flags(value);
            return true;
        }

        case 0xbcu: // INC A
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            ++_registers.a;
            set_nz_flags(_registers.a);
            return true;

        case 0x4bu: // LSR dp
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            logical_shift_right_memory(direct_page_address(direct_address));
            return true;
        }

        case 0x4cu: // LSR abs
        {
            const uint16_t address{ spc_fetch_u16() };
            logical_shift_right_memory(address);
            return true;
        }

        case 0x5bu: // LSR dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            logical_shift_right_memory(direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)));
            return true;
        }

        case 0x54u: // EOR dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            xor_accumulator(spc_load_direct_indexed(direct_address, _registers.x));
            return true;
        }

        case 0x55u: // EOR abs+X
        {
            const uint16_t address{ spc_fetch_u16() };
            xor_accumulator(spc_read_abs_indexed(address, _registers.x));
            return true;
        }

        case 0x56u: // EOR abs+Y
        {
            const uint16_t address{ spc_fetch_u16() };
            xor_accumulator(spc_read_abs_indexed(address, _registers.y));
            return true;
        }

        case 0x57u: // EOR [dp]+Y
        {
            const uint8_t zero_page_address{ spc_fetch_u8() };
            xor_accumulator(spc_read_indirect_indexed(zero_page_address, _registers.y));
            return true;
        }

        case 0x6bu: // ROR dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            uint8_t value{ spc_load_direct(direct_address) };
            const uint8_t carry_in{ static_cast<uint8_t>((_registers.psw & k_psw_carry) != 0 ? 0x80u : 0u) };
            if ((value & 0x01u) != 0)
                _registers.psw |= k_psw_carry;
            else
                _registers.psw &= static_cast<uint8_t>(~k_psw_carry);

            value = static_cast<uint8_t>(carry_in | (value >> 1u));
            spc_store_direct(direct_address, value);
            set_nz_flags(value);
            return true;
        }

        case 0x6cu: // ROR abs
        {
            const uint16_t address{ spc_fetch_u16() };
            rotate_right_memory(address);
            return true;
        }

        case 0x7bu: // ROR dp+X
        {
            const uint8_t direct_address{ spc_fetch_u8() };
            spc_idle();
            rotate_right_memory(direct_page_address(static_cast<uint8_t>(direct_address + _registers.x)));
            return true;
        }

        case 0x0eu: // TSET1 abs
        {
            const uint16_t address{ spc_fetch_u16() };
            test_and_modify_bits_absolute(address, true);
            return true;
        }

        case 0xc8u: // CMP X,#imm
            spc_consume_opcode_fetch();
            set_compare_flags(_registers.x, spc_fetch_u8());
            return true;

        case 0xcfu: // MUL YA
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            spc_idle();
            multiply_ya();
            return true;

        case 0xfcu: // INC Y
            spc_consume_opcode_fetch();
            (void)spc_read_u8(_registers.pc);
            ++_registers.y;
            set_nz_flags(_registers.y);
            return true;

        case 0x1eu: // CMP X,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            set_compare_flags(_registers.x, spc_read_u8(address));
            return true;
        }

        case 0x3eu: // CMP X,dp
        {
            spc_consume_opcode_fetch();
            const uint8_t direct_address{ spc_fetch_u8() };
            set_compare_flags(_registers.x, spc_load_direct(direct_address));
            return true;
        }

        case 0x5eu: // CMP Y,abs
        {
            const uint16_t address{ spc_fetch_u16() };
            set_compare_flags(_registers.y, spc_read_u8(address));
            return true;
        }

        default:
            return false;
        }
    }
}
