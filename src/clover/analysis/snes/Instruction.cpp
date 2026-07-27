//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Instruction.h"

namespace
{
    using namespace clover::analysis::snes;

    constexpr uint16_t k_c{ flag_mask(status_flag_t::carry) };
    constexpr uint16_t k_z{ flag_mask(status_flag_t::zero) };
    constexpr uint16_t k_i{ flag_mask(status_flag_t::irq_disable) };
    constexpr uint16_t k_d{ flag_mask(status_flag_t::decimal) };
    constexpr uint16_t k_x{ flag_mask(status_flag_t::index_width) };
    constexpr uint16_t k_m{ flag_mask(status_flag_t::accumulator_width) };
    constexpr uint16_t k_v{ flag_mask(status_flag_t::overflow) };
    constexpr uint16_t k_n{ flag_mask(status_flag_t::negative) };
    constexpr uint16_t k_e{ flag_mask(status_flag_t::emulation) };
    constexpr uint16_t k_nz{ static_cast<uint16_t>(k_n | k_z) };
    constexpr uint16_t k_nzc{ static_cast<uint16_t>(k_n | k_z | k_c) };
    constexpr uint16_t k_nzvc{ static_cast<uint16_t>(k_n | k_z | k_v | k_c) };
    constexpr uint16_t k_all_status{
        static_cast<uint16_t>(k_c | k_z | k_i | k_d | k_x | k_m | k_v | k_n)
    };

    [[nodiscard]] constexpr flag_effects_t flag_effects(
        instruction_id_t instruction,
        addressing_mode_t mode
    ) noexcept
    {
        switch (instruction)
        {
        case instruction_id_t::adc:
        case instruction_id_t::sbc:
            return { static_cast<uint16_t>(k_c | k_d), k_nzvc };
        case instruction_id_t::and_:
        case instruction_id_t::eor:
        case instruction_id_t::ora:
        case instruction_id_t::lda:
        case instruction_id_t::ldx:
        case instruction_id_t::ldy:
        case instruction_id_t::pla:
        case instruction_id_t::plb:
        case instruction_id_t::pld:
        case instruction_id_t::plx:
        case instruction_id_t::ply:
        case instruction_id_t::tax:
        case instruction_id_t::tay:
        case instruction_id_t::tcd:
        case instruction_id_t::tdc:
        case instruction_id_t::tsc:
        case instruction_id_t::tsx:
        case instruction_id_t::txa:
        case instruction_id_t::txy:
        case instruction_id_t::tya:
        case instruction_id_t::tyx:
        case instruction_id_t::xba:
            return { 0, k_nz };
        case instruction_id_t::asl:
        case instruction_id_t::lsr:
        case instruction_id_t::rol:
        case instruction_id_t::ror:
            return { k_c, k_nzc };
        case instruction_id_t::bit:
            return {
                0,
                mode == addressing_mode_t::immediate
                    ? k_z
                    : static_cast<uint16_t>(k_n | k_v | k_z)
            };
        case instruction_id_t::cmp:
        case instruction_id_t::cpx:
        case instruction_id_t::cpy:
            return { 0, k_nzc };
        case instruction_id_t::dec:
        case instruction_id_t::dex:
        case instruction_id_t::dey:
        case instruction_id_t::inc:
        case instruction_id_t::inx:
        case instruction_id_t::iny:
            return { 0, k_nz };
        case instruction_id_t::trb:
        case instruction_id_t::tsb:
            return { 0, k_z };
        case instruction_id_t::clc:
            return { 0, k_c, 0, k_c };
        case instruction_id_t::sec:
            return { 0, k_c, k_c };
        case instruction_id_t::cld:
            return { 0, k_d, 0, k_d };
        case instruction_id_t::sed:
            return { 0, k_d, k_d };
        case instruction_id_t::cli:
            return { 0, k_i, 0, k_i };
        case instruction_id_t::sei:
            return { 0, k_i, k_i };
        case instruction_id_t::clv:
            return { 0, k_v, 0, k_v };
        case instruction_id_t::plp:
        case instruction_id_t::rti:
            return { 0, k_all_status };
        case instruction_id_t::rep:
            return { 0, k_all_status, 0, 0, true };
        case instruction_id_t::sep:
            return { 0, k_all_status, 0, 0, true };
        case instruction_id_t::brk:
        case instruction_id_t::cop:
            return { 0, static_cast<uint16_t>(k_i | k_d), k_i, k_d };
        case instruction_id_t::xce:
            return {
                static_cast<uint16_t>(k_c | k_e),
                static_cast<uint16_t>(k_c | k_e),
                0,
                0,
                false,
                true
            };
        default:
            return {};
        }
    }

    [[nodiscard]] constexpr opcode_descriptor_t descriptor(
        instruction_id_t instruction,
        addressing_mode_t mode,
        operand_width_rule_t width,
        control_flow_kind_t control_flow = control_flow_kind_t::linear
    ) noexcept
    {
        return {
            instruction,
            mode,
            width,
            control_flow,
            flag_effects(instruction, mode)
        };
    }

    using I = instruction_id_t;
    using A = addressing_mode_t;
    using W = operand_width_rule_t;
    using F = control_flow_kind_t;

#define D(i, a, w) descriptor(I::i, A::a, W::w)
#define C(i, a, w, f) descriptor(I::i, A::a, W::w, F::f)

    constexpr std::array<opcode_descriptor_t, 256> k_opcode_table{
        C(brk, signature, fixed8, interrupt), D(ora, direct_indexed_indirect, fixed8),
        C(cop, signature, fixed8, interrupt), D(ora, stack_relative, fixed8),
        D(tsb, direct, fixed8), D(ora, direct, fixed8), D(asl, direct, fixed8),
        D(ora, direct_indirect_long, fixed8), D(php, implied, none),
        D(ora, immediate, accumulator), D(asl, accumulator, none), D(phd, implied, none),
        D(tsb, absolute, fixed16), D(ora, absolute, fixed16), D(asl, absolute, fixed16),
        D(ora, absolute_long, fixed24),

        C(bpl, relative8, fixed8, conditional_branch),
        D(ora, direct_indirect_indexed, fixed8), D(ora, direct_indirect, fixed8),
        D(ora, stack_relative_indirect_indexed, fixed8), D(trb, direct, fixed8),
        D(ora, direct_x, fixed8), D(asl, direct_x, fixed8),
        D(ora, direct_indirect_long_indexed, fixed8), D(clc, implied, none),
        D(ora, absolute_y, fixed16), D(inc, accumulator, none), D(tcs, implied, none),
        D(trb, absolute, fixed16), D(ora, absolute_x, fixed16),
        D(asl, absolute_x, fixed16), D(ora, absolute_long_x, fixed24),

        C(jsr, absolute, fixed16, call), D(and_, direct_indexed_indirect, fixed8),
        C(jsl, absolute_long, fixed24, call), D(and_, stack_relative, fixed8),
        D(bit, direct, fixed8), D(and_, direct, fixed8), D(rol, direct, fixed8),
        D(and_, direct_indirect_long, fixed8), D(plp, implied, none),
        D(and_, immediate, accumulator), D(rol, accumulator, none), D(pld, implied, none),
        D(bit, absolute, fixed16), D(and_, absolute, fixed16), D(rol, absolute, fixed16),
        D(and_, absolute_long, fixed24),

        C(bmi, relative8, fixed8, conditional_branch),
        D(and_, direct_indirect_indexed, fixed8), D(and_, direct_indirect, fixed8),
        D(and_, stack_relative_indirect_indexed, fixed8), D(bit, direct_x, fixed8),
        D(and_, direct_x, fixed8), D(rol, direct_x, fixed8),
        D(and_, direct_indirect_long_indexed, fixed8), D(sec, implied, none),
        D(and_, absolute_y, fixed16), D(dec, accumulator, none), D(tsc, implied, none),
        D(bit, absolute_x, fixed16), D(and_, absolute_x, fixed16),
        D(rol, absolute_x, fixed16), D(and_, absolute_long_x, fixed24),

        C(rti, implied, none, return_), D(eor, direct_indexed_indirect, fixed8),
        D(wdm, signature, fixed8), D(eor, stack_relative, fixed8),
        D(mvp, block_move, fixed16), D(eor, direct, fixed8), D(lsr, direct, fixed8),
        D(eor, direct_indirect_long, fixed8), D(pha, implied, none),
        D(eor, immediate, accumulator), D(lsr, accumulator, none), D(phk, implied, none),
        C(jmp, absolute, fixed16, jump), D(eor, absolute, fixed16),
        D(lsr, absolute, fixed16), D(eor, absolute_long, fixed24),

        C(bvc, relative8, fixed8, conditional_branch),
        D(eor, direct_indirect_indexed, fixed8), D(eor, direct_indirect, fixed8),
        D(eor, stack_relative_indirect_indexed, fixed8), D(mvn, block_move, fixed16),
        D(eor, direct_x, fixed8), D(lsr, direct_x, fixed8),
        D(eor, direct_indirect_long_indexed, fixed8), D(cli, implied, none),
        D(eor, absolute_y, fixed16), D(phy, implied, none), D(tcd, implied, none),
        C(jml, absolute_long, fixed24, jump), D(eor, absolute_x, fixed16),
        D(lsr, absolute_x, fixed16), D(eor, absolute_long_x, fixed24),

        C(rts, implied, none, return_), D(adc, direct_indexed_indirect, fixed8),
        D(per, relative16, fixed16), D(adc, stack_relative, fixed8),
        D(stz, direct, fixed8), D(adc, direct, fixed8), D(ror, direct, fixed8),
        D(adc, direct_indirect_long, fixed8), D(pla, implied, none),
        D(adc, immediate, accumulator), D(ror, accumulator, none),
        C(rtl, implied, none, return_), C(jmp, absolute_indirect, fixed16, jump),
        D(adc, absolute, fixed16), D(ror, absolute, fixed16), D(adc, absolute_long, fixed24),

        C(bvs, relative8, fixed8, conditional_branch),
        D(adc, direct_indirect_indexed, fixed8), D(adc, direct_indirect, fixed8),
        D(adc, stack_relative_indirect_indexed, fixed8), D(stz, direct_x, fixed8),
        D(adc, direct_x, fixed8), D(ror, direct_x, fixed8),
        D(adc, direct_indirect_long_indexed, fixed8), D(sei, implied, none),
        D(adc, absolute_y, fixed16), D(ply, implied, none), D(tdc, implied, none),
        C(jmp, absolute_indexed_indirect, fixed16, jump), D(adc, absolute_x, fixed16),
        D(ror, absolute_x, fixed16), D(adc, absolute_long_x, fixed24),

        C(bra, relative8, fixed8, unconditional_branch),
        D(sta, direct_indexed_indirect, fixed8), C(brl, relative16, fixed16, unconditional_branch),
        D(sta, stack_relative, fixed8), D(sty, direct, fixed8), D(sta, direct, fixed8),
        D(stx, direct, fixed8), D(sta, direct_indirect_long, fixed8), D(dey, implied, none),
        D(bit, immediate, accumulator), D(txa, implied, none), D(phb, implied, none),
        D(sty, absolute, fixed16), D(sta, absolute, fixed16), D(stx, absolute, fixed16),
        D(sta, absolute_long, fixed24),

        C(bcc, relative8, fixed8, conditional_branch),
        D(sta, direct_indirect_indexed, fixed8), D(sta, direct_indirect, fixed8),
        D(sta, stack_relative_indirect_indexed, fixed8), D(sty, direct_x, fixed8),
        D(sta, direct_x, fixed8), D(stx, direct_y, fixed8),
        D(sta, direct_indirect_long_indexed, fixed8), D(tya, implied, none),
        D(sta, absolute_y, fixed16), D(txs, implied, none), D(txy, implied, none),
        D(stz, absolute, fixed16), D(sta, absolute_x, fixed16), D(stz, absolute_x, fixed16),
        D(sta, absolute_long_x, fixed24),

        D(ldy, immediate, index), D(lda, direct_indexed_indirect, fixed8),
        D(ldx, immediate, index), D(lda, stack_relative, fixed8), D(ldy, direct, fixed8),
        D(lda, direct, fixed8), D(ldx, direct, fixed8), D(lda, direct_indirect_long, fixed8),
        D(tay, implied, none), D(lda, immediate, accumulator), D(tax, implied, none),
        D(plb, implied, none), D(ldy, absolute, fixed16), D(lda, absolute, fixed16),
        D(ldx, absolute, fixed16), D(lda, absolute_long, fixed24),

        C(bcs, relative8, fixed8, conditional_branch),
        D(lda, direct_indirect_indexed, fixed8), D(lda, direct_indirect, fixed8),
        D(lda, stack_relative_indirect_indexed, fixed8), D(ldy, direct_x, fixed8),
        D(lda, direct_x, fixed8), D(ldx, direct_y, fixed8),
        D(lda, direct_indirect_long_indexed, fixed8), D(clv, implied, none),
        D(lda, absolute_y, fixed16), D(tsx, implied, none), D(tyx, implied, none),
        D(ldy, absolute_x, fixed16), D(lda, absolute_x, fixed16),
        D(ldx, absolute_y, fixed16), D(lda, absolute_long_x, fixed24),

        D(cpy, immediate, index), D(cmp, direct_indexed_indirect, fixed8),
        D(rep, immediate, fixed8), D(cmp, stack_relative, fixed8), D(cpy, direct, fixed8),
        D(cmp, direct, fixed8), D(dec, direct, fixed8), D(cmp, direct_indirect_long, fixed8),
        D(iny, implied, none), D(cmp, immediate, accumulator), D(dex, implied, none),
        C(wai, implied, none, wait), D(cpy, absolute, fixed16), D(cmp, absolute, fixed16),
        D(dec, absolute, fixed16), D(cmp, absolute_long, fixed24),

        C(bne, relative8, fixed8, conditional_branch),
        D(cmp, direct_indirect_indexed, fixed8), D(cmp, direct_indirect, fixed8),
        D(cmp, stack_relative_indirect_indexed, fixed8), D(pei, direct_indirect, fixed8),
        D(cmp, direct_x, fixed8), D(dec, direct_x, fixed8),
        D(cmp, direct_indirect_long_indexed, fixed8), D(cld, implied, none),
        D(cmp, absolute_y, fixed16), D(phx, implied, none), C(stp, implied, none, stop),
        C(jml, absolute_indirect_long, fixed16, jump), D(cmp, absolute_x, fixed16),
        D(dec, absolute_x, fixed16), D(cmp, absolute_long_x, fixed24),

        D(cpx, immediate, index), D(sbc, direct_indexed_indirect, fixed8),
        D(sep, immediate, fixed8), D(sbc, stack_relative, fixed8), D(cpx, direct, fixed8),
        D(sbc, direct, fixed8), D(inc, direct, fixed8), D(sbc, direct_indirect_long, fixed8),
        D(inx, implied, none), D(sbc, immediate, accumulator), D(nop, implied, none),
        D(xba, implied, none), D(cpx, absolute, fixed16), D(sbc, absolute, fixed16),
        D(inc, absolute, fixed16), D(sbc, absolute_long, fixed24),

        C(beq, relative8, fixed8, conditional_branch),
        D(sbc, direct_indirect_indexed, fixed8), D(sbc, direct_indirect, fixed8),
        D(sbc, stack_relative_indirect_indexed, fixed8), D(pea, absolute_value, fixed16),
        D(sbc, direct_x, fixed8), D(inc, direct_x, fixed8),
        D(sbc, direct_indirect_long_indexed, fixed8), D(sed, implied, none),
        D(sbc, absolute_y, fixed16), D(plx, implied, none), D(xce, implied, none),
        C(jsr, absolute_indexed_indirect, fixed16, call), D(sbc, absolute_x, fixed16),
        D(inc, absolute_x, fixed16), D(sbc, absolute_long_x, fixed24)
    };

#undef C
#undef D
}

namespace clover::analysis::snes
{
    const std::array<opcode_descriptor_t, 256>& opcode_table() noexcept
    {
        return k_opcode_table;
    }

    const opcode_descriptor_t& opcode_descriptor(uint8_t opcode) noexcept
    {
        return k_opcode_table[opcode];
    }

    std::string_view instruction_mnemonic(instruction_id_t instruction) noexcept
    {
        switch (instruction)
        {
#define MNEMONIC(id, text) case instruction_id_t::id: return text
        MNEMONIC(adc, "ADC");
        MNEMONIC(and_, "AND");
        MNEMONIC(asl, "ASL");
        MNEMONIC(bcc, "BCC");
        MNEMONIC(bcs, "BCS");
        MNEMONIC(beq, "BEQ");
        MNEMONIC(bit, "BIT");
        MNEMONIC(bmi, "BMI");
        MNEMONIC(bne, "BNE");
        MNEMONIC(bpl, "BPL");
        MNEMONIC(bra, "BRA");
        MNEMONIC(brk, "BRK");
        MNEMONIC(brl, "BRL");
        MNEMONIC(bvc, "BVC");
        MNEMONIC(bvs, "BVS");
        MNEMONIC(clc, "CLC");
        MNEMONIC(cld, "CLD");
        MNEMONIC(cli, "CLI");
        MNEMONIC(clv, "CLV");
        MNEMONIC(cmp, "CMP");
        MNEMONIC(cop, "COP");
        MNEMONIC(cpx, "CPX");
        MNEMONIC(cpy, "CPY");
        MNEMONIC(dec, "DEC");
        MNEMONIC(dex, "DEX");
        MNEMONIC(dey, "DEY");
        MNEMONIC(eor, "EOR");
        MNEMONIC(inc, "INC");
        MNEMONIC(inx, "INX");
        MNEMONIC(iny, "INY");
        MNEMONIC(jml, "JML");
        MNEMONIC(jmp, "JMP");
        MNEMONIC(jsl, "JSL");
        MNEMONIC(jsr, "JSR");
        MNEMONIC(lda, "LDA");
        MNEMONIC(ldx, "LDX");
        MNEMONIC(ldy, "LDY");
        MNEMONIC(lsr, "LSR");
        MNEMONIC(mvn, "MVN");
        MNEMONIC(mvp, "MVP");
        MNEMONIC(nop, "NOP");
        MNEMONIC(ora, "ORA");
        MNEMONIC(pea, "PEA");
        MNEMONIC(pei, "PEI");
        MNEMONIC(per, "PER");
        MNEMONIC(pha, "PHA");
        MNEMONIC(phb, "PHB");
        MNEMONIC(phd, "PHD");
        MNEMONIC(phk, "PHK");
        MNEMONIC(php, "PHP");
        MNEMONIC(phx, "PHX");
        MNEMONIC(phy, "PHY");
        MNEMONIC(pla, "PLA");
        MNEMONIC(plb, "PLB");
        MNEMONIC(pld, "PLD");
        MNEMONIC(plp, "PLP");
        MNEMONIC(plx, "PLX");
        MNEMONIC(ply, "PLY");
        MNEMONIC(rep, "REP");
        MNEMONIC(rol, "ROL");
        MNEMONIC(ror, "ROR");
        MNEMONIC(rti, "RTI");
        MNEMONIC(rtl, "RTL");
        MNEMONIC(rts, "RTS");
        MNEMONIC(sbc, "SBC");
        MNEMONIC(sec, "SEC");
        MNEMONIC(sed, "SED");
        MNEMONIC(sei, "SEI");
        MNEMONIC(sep, "SEP");
        MNEMONIC(sta, "STA");
        MNEMONIC(stp, "STP");
        MNEMONIC(stx, "STX");
        MNEMONIC(sty, "STY");
        MNEMONIC(stz, "STZ");
        MNEMONIC(tax, "TAX");
        MNEMONIC(tay, "TAY");
        MNEMONIC(tcd, "TCD");
        MNEMONIC(tcs, "TCS");
        MNEMONIC(tdc, "TDC");
        MNEMONIC(trb, "TRB");
        MNEMONIC(tsb, "TSB");
        MNEMONIC(tsc, "TSC");
        MNEMONIC(tsx, "TSX");
        MNEMONIC(txa, "TXA");
        MNEMONIC(txs, "TXS");
        MNEMONIC(txy, "TXY");
        MNEMONIC(tya, "TYA");
        MNEMONIC(tyx, "TYX");
        MNEMONIC(wai, "WAI");
        MNEMONIC(wdm, "WDM");
        MNEMONIC(xba, "XBA");
        MNEMONIC(xce, "XCE");
#undef MNEMONIC
        }
        return "???";
    }

    std::string_view addressing_mode_name(addressing_mode_t mode) noexcept
    {
        switch (mode)
        {
#define MODE(id) case addressing_mode_t::id: return #id
        MODE(implied);
        MODE(accumulator);
        MODE(signature);
        MODE(immediate);
        MODE(direct);
        MODE(direct_x);
        MODE(direct_y);
        MODE(direct_indirect);
        MODE(direct_indexed_indirect);
        MODE(direct_indirect_indexed);
        MODE(direct_indirect_long);
        MODE(direct_indirect_long_indexed);
        MODE(stack_relative);
        MODE(stack_relative_indirect_indexed);
        MODE(absolute);
        MODE(absolute_value);
        MODE(absolute_x);
        MODE(absolute_y);
        MODE(absolute_long);
        MODE(absolute_long_x);
        MODE(absolute_indirect);
        MODE(absolute_indexed_indirect);
        MODE(absolute_indirect_long);
        MODE(relative8);
        MODE(relative16);
        MODE(block_move);
#undef MODE
        }
        return "unknown";
    }

    std::string_view operand_width_rule_name(operand_width_rule_t rule) noexcept
    {
        switch (rule)
        {
        case operand_width_rule_t::none: return "none";
        case operand_width_rule_t::fixed8: return "fixed8";
        case operand_width_rule_t::fixed16: return "fixed16";
        case operand_width_rule_t::fixed24: return "fixed24";
        case operand_width_rule_t::accumulator: return "accumulator";
        case operand_width_rule_t::index: return "index";
        }
        return "unknown";
    }

    std::string_view control_flow_name(control_flow_kind_t kind) noexcept
    {
        switch (kind)
        {
        case control_flow_kind_t::linear: return "linear";
        case control_flow_kind_t::conditional_branch: return "conditional_branch";
        case control_flow_kind_t::unconditional_branch: return "unconditional_branch";
        case control_flow_kind_t::call: return "call";
        case control_flow_kind_t::jump: return "jump";
        case control_flow_kind_t::return_: return "return";
        case control_flow_kind_t::interrupt: return "interrupt";
        case control_flow_kind_t::wait: return "wait";
        case control_flow_kind_t::stop: return "stop";
        }
        return "unknown";
    }
}
