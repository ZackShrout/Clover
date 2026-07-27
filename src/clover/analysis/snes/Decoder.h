//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Instruction.h"

#include <array>
#include <cstdint>
#include <optional>

namespace clover::analysis::snes
{
    enum class bit_state_t : uint8_t
    {
        clear,
        set,
        unknown
    };

    struct cpu_decode_context_t
    {
        bit_state_t emulation{ bit_state_t::unknown };
        bit_state_t accumulator_width{ bit_state_t::unknown };
        bit_state_t index_width{ bit_state_t::unknown };
        std::optional<uint16_t> direct_page{};
        std::optional<uint8_t> data_bank{};
    };

    enum class byte_inspection_status_t : uint8_t
    {
        available,
        unavailable
    };

    struct inspected_byte_t
    {
        byte_inspection_status_t status{ byte_inspection_status_t::unavailable };
        uint8_t value{ 0 };
    };

    struct byte_source_t
    {
    public:
        virtual ~byte_source_t() = default;
        [[nodiscard]] virtual inspected_byte_t inspect(uint32_t cpu_address) const noexcept = 0;
    };

    enum class decode_status_t : uint8_t
    {
        complete,
        ambiguous_context,
        contradictory_context,
        unavailable
    };

    enum class decode_certainty_t : uint8_t
    {
        certain,
        context_dependent
    };

    struct decoded_instruction_t
    {
        uint32_t address{ 0 };
        uint8_t opcode{ 0 };
        instruction_id_t instruction{ instruction_id_t::nop };
        addressing_mode_t addressing_mode{ addressing_mode_t::implied };
        operand_width_rule_t operand_width_rule{ operand_width_rule_t::none };
        control_flow_kind_t control_flow{ control_flow_kind_t::linear };
        flag_effects_t flag_effects{};
        std::array<uint8_t, 4> bytes{};
        uint8_t byte_count{ 0 };
        uint8_t encoded_size{ 0 };
        uint8_t operand_size{ 0 };
        uint8_t minimum_size{ 0 };
        uint8_t maximum_size{ 0 };
        std::optional<uint32_t> operand_value{};
        std::optional<int32_t> relative_displacement{};
        std::optional<uint32_t> direct_target{};
        cpu_decode_context_t context{};
        decode_status_t status{ decode_status_t::unavailable };
        decode_certainty_t certainty{ decode_certainty_t::certain };
    };

    [[nodiscard]] uint32_t advance_program_address(uint32_t address,
                                                   uint16_t byte_count) noexcept;
    [[nodiscard]] decoded_instruction_t decode_instruction(
        const byte_source_t& source,
        uint32_t cpu_address,
        cpu_decode_context_t context
    ) noexcept;
    [[nodiscard]] std::string_view bit_state_name(bit_state_t state) noexcept;
    [[nodiscard]] std::string_view decode_status_name(decode_status_t status) noexcept;
}
