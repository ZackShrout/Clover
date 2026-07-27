//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//
//
// Independent W65C816 opcode reference used to catch production metadata
// drift. It follows the WDC W65C816S datasheet opcode matrix and addressing
// definitions (March 13, 2024, Tables 3-1 and 5-4). The compact
// mode/width/flow codes are decoded only by DecoderTest.
//

#pragma once

#include <array>
#include <string_view>

namespace clover::test
{
    struct golden_opcode_t
    {
        std::string_view mnemonic{};
        std::string_view mode{};
        char width{ '0' };
        char flow{ 'L' };
    };

#define G(mnemonic, mode, width) golden_opcode_t{ mnemonic, mode, width, 'L' }
#define F(mnemonic, mode, width, flow) golden_opcode_t{ mnemonic, mode, width, flow }

    // Flow: L=linear, C=conditional branch, B=unconditional branch,
    // A=call, J=jump, R=return, I=interrupt, W=wait, S=stop.
    // Width: 0=no operand, 1/2/3=fixed operand bytes, M/X=CPU width flag.
    inline constexpr std::array<golden_opcode_t, 256> k_wdc65816_golden{{
        F("BRK", "sig", '1', 'I'), G("ORA", "dix", '1'), F("COP", "sig", '1', 'I'), G("ORA", "sr", '1'),
        G("TSB", "d", '1'), G("ORA", "d", '1'), G("ASL", "d", '1'), G("ORA", "dil", '1'),
        G("PHP", "i", '0'), G("ORA", "imm", 'M'), G("ASL", "a", '0'), G("PHD", "i", '0'),
        G("TSB", "abs", '2'), G("ORA", "abs", '2'), G("ASL", "abs", '2'), G("ORA", "long", '3'),

        F("BPL", "r8", '1', 'C'), G("ORA", "diy", '1'), G("ORA", "di", '1'), G("ORA", "sriy", '1'),
        G("TRB", "d", '1'), G("ORA", "dx", '1'), G("ASL", "dx", '1'), G("ORA", "dily", '1'),
        G("CLC", "i", '0'), G("ORA", "absy", '2'), G("INC", "a", '0'), G("TCS", "i", '0'),
        G("TRB", "abs", '2'), G("ORA", "absx", '2'), G("ASL", "absx", '2'), G("ORA", "longx", '3'),

        F("JSR", "abs", '2', 'A'), G("AND", "dix", '1'), F("JSL", "long", '3', 'A'), G("AND", "sr", '1'),
        G("BIT", "d", '1'), G("AND", "d", '1'), G("ROL", "d", '1'), G("AND", "dil", '1'),
        G("PLP", "i", '0'), G("AND", "imm", 'M'), G("ROL", "a", '0'), G("PLD", "i", '0'),
        G("BIT", "abs", '2'), G("AND", "abs", '2'), G("ROL", "abs", '2'), G("AND", "long", '3'),

        F("BMI", "r8", '1', 'C'), G("AND", "diy", '1'), G("AND", "di", '1'), G("AND", "sriy", '1'),
        G("BIT", "dx", '1'), G("AND", "dx", '1'), G("ROL", "dx", '1'), G("AND", "dily", '1'),
        G("SEC", "i", '0'), G("AND", "absy", '2'), G("DEC", "a", '0'), G("TSC", "i", '0'),
        G("BIT", "absx", '2'), G("AND", "absx", '2'), G("ROL", "absx", '2'), G("AND", "longx", '3'),

        F("RTI", "i", '0', 'R'), G("EOR", "dix", '1'), G("WDM", "sig", '1'), G("EOR", "sr", '1'),
        G("MVP", "block", '2'), G("EOR", "d", '1'), G("LSR", "d", '1'), G("EOR", "dil", '1'),
        G("PHA", "i", '0'), G("EOR", "imm", 'M'), G("LSR", "a", '0'), G("PHK", "i", '0'),
        F("JMP", "abs", '2', 'J'), G("EOR", "abs", '2'), G("LSR", "abs", '2'), G("EOR", "long", '3'),

        F("BVC", "r8", '1', 'C'), G("EOR", "diy", '1'), G("EOR", "di", '1'), G("EOR", "sriy", '1'),
        G("MVN", "block", '2'), G("EOR", "dx", '1'), G("LSR", "dx", '1'), G("EOR", "dily", '1'),
        G("CLI", "i", '0'), G("EOR", "absy", '2'), G("PHY", "i", '0'), G("TCD", "i", '0'),
        F("JML", "long", '3', 'J'), G("EOR", "absx", '2'), G("LSR", "absx", '2'), G("EOR", "longx", '3'),

        F("RTS", "i", '0', 'R'), G("ADC", "dix", '1'), G("PER", "r16", '2'), G("ADC", "sr", '1'),
        G("STZ", "d", '1'), G("ADC", "d", '1'), G("ROR", "d", '1'), G("ADC", "dil", '1'),
        G("PLA", "i", '0'), G("ADC", "imm", 'M'), G("ROR", "a", '0'), F("RTL", "i", '0', 'R'),
        F("JMP", "absi", '2', 'J'), G("ADC", "abs", '2'), G("ROR", "abs", '2'), G("ADC", "long", '3'),

        F("BVS", "r8", '1', 'C'), G("ADC", "diy", '1'), G("ADC", "di", '1'), G("ADC", "sriy", '1'),
        G("STZ", "dx", '1'), G("ADC", "dx", '1'), G("ROR", "dx", '1'), G("ADC", "dily", '1'),
        G("SEI", "i", '0'), G("ADC", "absy", '2'), G("PLY", "i", '0'), G("TDC", "i", '0'),
        F("JMP", "absxi", '2', 'J'), G("ADC", "absx", '2'), G("ROR", "absx", '2'), G("ADC", "longx", '3'),

        F("BRA", "r8", '1', 'B'), G("STA", "dix", '1'), F("BRL", "r16", '2', 'B'), G("STA", "sr", '1'),
        G("STY", "d", '1'), G("STA", "d", '1'), G("STX", "d", '1'), G("STA", "dil", '1'),
        G("DEY", "i", '0'), G("BIT", "imm", 'M'), G("TXA", "i", '0'), G("PHB", "i", '0'),
        G("STY", "abs", '2'), G("STA", "abs", '2'), G("STX", "abs", '2'), G("STA", "long", '3'),

        F("BCC", "r8", '1', 'C'), G("STA", "diy", '1'), G("STA", "di", '1'), G("STA", "sriy", '1'),
        G("STY", "dx", '1'), G("STA", "dx", '1'), G("STX", "dy", '1'), G("STA", "dily", '1'),
        G("TYA", "i", '0'), G("STA", "absy", '2'), G("TXS", "i", '0'), G("TXY", "i", '0'),
        G("STZ", "abs", '2'), G("STA", "absx", '2'), G("STZ", "absx", '2'), G("STA", "longx", '3'),

        G("LDY", "imm", 'X'), G("LDA", "dix", '1'), G("LDX", "imm", 'X'), G("LDA", "sr", '1'),
        G("LDY", "d", '1'), G("LDA", "d", '1'), G("LDX", "d", '1'), G("LDA", "dil", '1'),
        G("TAY", "i", '0'), G("LDA", "imm", 'M'), G("TAX", "i", '0'), G("PLB", "i", '0'),
        G("LDY", "abs", '2'), G("LDA", "abs", '2'), G("LDX", "abs", '2'), G("LDA", "long", '3'),

        F("BCS", "r8", '1', 'C'), G("LDA", "diy", '1'), G("LDA", "di", '1'), G("LDA", "sriy", '1'),
        G("LDY", "dx", '1'), G("LDA", "dx", '1'), G("LDX", "dy", '1'), G("LDA", "dily", '1'),
        G("CLV", "i", '0'), G("LDA", "absy", '2'), G("TSX", "i", '0'), G("TYX", "i", '0'),
        G("LDY", "absx", '2'), G("LDA", "absx", '2'), G("LDX", "absy", '2'), G("LDA", "longx", '3'),

        G("CPY", "imm", 'X'), G("CMP", "dix", '1'), G("REP", "imm", '1'), G("CMP", "sr", '1'),
        G("CPY", "d", '1'), G("CMP", "d", '1'), G("DEC", "d", '1'), G("CMP", "dil", '1'),
        G("INY", "i", '0'), G("CMP", "imm", 'M'), G("DEX", "i", '0'), F("WAI", "i", '0', 'W'),
        G("CPY", "abs", '2'), G("CMP", "abs", '2'), G("DEC", "abs", '2'), G("CMP", "long", '3'),

        F("BNE", "r8", '1', 'C'), G("CMP", "diy", '1'), G("CMP", "di", '1'), G("CMP", "sriy", '1'),
        G("PEI", "di", '1'), G("CMP", "dx", '1'), G("DEC", "dx", '1'), G("CMP", "dily", '1'),
        G("CLD", "i", '0'), G("CMP", "absy", '2'), G("PHX", "i", '0'), F("STP", "i", '0', 'S'),
        F("JML", "absil", '2', 'J'), G("CMP", "absx", '2'), G("DEC", "absx", '2'), G("CMP", "longx", '3'),

        G("CPX", "imm", 'X'), G("SBC", "dix", '1'), G("SEP", "imm", '1'), G("SBC", "sr", '1'),
        G("CPX", "d", '1'), G("SBC", "d", '1'), G("INC", "d", '1'), G("SBC", "dil", '1'),
        G("INX", "i", '0'), G("SBC", "imm", 'M'), G("NOP", "i", '0'), G("XBA", "i", '0'),
        G("CPX", "abs", '2'), G("SBC", "abs", '2'), G("INC", "abs", '2'), G("SBC", "long", '3'),

        F("BEQ", "r8", '1', 'C'), G("SBC", "diy", '1'), G("SBC", "di", '1'), G("SBC", "sriy", '1'),
        G("PEA", "value", '2'), G("SBC", "dx", '1'), G("INC", "dx", '1'), G("SBC", "dily", '1'),
        G("SED", "i", '0'), G("SBC", "absy", '2'), G("PLX", "i", '0'), G("XCE", "i", '0'),
        F("JSR", "absxi", '2', 'A'), G("SBC", "absx", '2'), G("INC", "absx", '2'), G("SBC", "longx", '3')
    }};

#undef F
#undef G
}
