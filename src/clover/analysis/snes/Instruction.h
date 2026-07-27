//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace clover::analysis::snes
{
    enum class instruction_id_t : uint8_t
    {
        adc,
        and_,
        asl,
        bcc,
        bcs,
        beq,
        bit,
        bmi,
        bne,
        bpl,
        bra,
        brk,
        brl,
        bvc,
        bvs,
        clc,
        cld,
        cli,
        clv,
        cmp,
        cop,
        cpx,
        cpy,
        dec,
        dex,
        dey,
        eor,
        inc,
        inx,
        iny,
        jml,
        jmp,
        jsl,
        jsr,
        lda,
        ldx,
        ldy,
        lsr,
        mvn,
        mvp,
        nop,
        ora,
        pea,
        pei,
        per,
        pha,
        phb,
        phd,
        phk,
        php,
        phx,
        phy,
        pla,
        plb,
        pld,
        plp,
        plx,
        ply,
        rep,
        rol,
        ror,
        rti,
        rtl,
        rts,
        sbc,
        sec,
        sed,
        sei,
        sep,
        sta,
        stp,
        stx,
        sty,
        stz,
        tax,
        tay,
        tcd,
        tcs,
        tdc,
        trb,
        tsb,
        tsc,
        tsx,
        txa,
        txs,
        txy,
        tya,
        tyx,
        wai,
        wdm,
        xba,
        xce
    };

    enum class addressing_mode_t : uint8_t
    {
        implied,
        accumulator,
        signature,
        immediate,
        direct,
        direct_x,
        direct_y,
        direct_indirect,
        direct_indexed_indirect,
        direct_indirect_indexed,
        direct_indirect_long,
        direct_indirect_long_indexed,
        stack_relative,
        stack_relative_indirect_indexed,
        absolute,
        absolute_value,
        absolute_x,
        absolute_y,
        absolute_long,
        absolute_long_x,
        absolute_indirect,
        absolute_indexed_indirect,
        absolute_indirect_long,
        relative8,
        relative16,
        block_move
    };

    enum class operand_width_rule_t : uint8_t
    {
        none,
        fixed8,
        fixed16,
        fixed24,
        accumulator,
        index
    };

    enum class control_flow_kind_t : uint8_t
    {
        linear,
        conditional_branch,
        unconditional_branch,
        call,
        jump,
        return_,
        interrupt,
        wait,
        stop
    };

    enum class status_flag_t : uint16_t
    {
        carry = 1u << 0u,
        zero = 1u << 1u,
        irq_disable = 1u << 2u,
        decimal = 1u << 3u,
        index_width = 1u << 4u,
        accumulator_width = 1u << 5u,
        overflow = 1u << 6u,
        negative = 1u << 7u,
        emulation = 1u << 8u
    };

    [[nodiscard]] constexpr uint16_t flag_mask(status_flag_t flag) noexcept
    {
        return static_cast<uint16_t>(flag);
    }

    struct flag_effects_t
    {
        uint16_t read_mask{ 0 };
        uint16_t written_mask{ 0 };
        uint16_t set_mask{ 0 };
        uint16_t clear_mask{ 0 };
        bool operand_selected{ false };
        bool exchange_carry_emulation{ false };
    };

    struct opcode_descriptor_t
    {
        instruction_id_t instruction{ instruction_id_t::nop };
        addressing_mode_t addressing_mode{ addressing_mode_t::implied };
        operand_width_rule_t operand_width{ operand_width_rule_t::none };
        control_flow_kind_t control_flow{ control_flow_kind_t::linear };
        flag_effects_t flag_effects{};
    };

    [[nodiscard]] const std::array<opcode_descriptor_t, 256>& opcode_table() noexcept;
    [[nodiscard]] const opcode_descriptor_t& opcode_descriptor(uint8_t opcode) noexcept;
    [[nodiscard]] std::string_view instruction_mnemonic(instruction_id_t instruction) noexcept;
    [[nodiscard]] std::string_view addressing_mode_name(addressing_mode_t mode) noexcept;
    [[nodiscard]] std::string_view operand_width_rule_name(operand_width_rule_t rule) noexcept;
    [[nodiscard]] std::string_view control_flow_name(control_flow_kind_t kind) noexcept;
}
