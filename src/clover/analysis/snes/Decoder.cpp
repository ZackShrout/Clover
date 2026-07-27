//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Decoder.h"

namespace
{
    using namespace clover::analysis::snes;

    struct size_range_t
    {
        uint8_t minimum{ 0 };
        uint8_t maximum{ 0 };
    };

    [[nodiscard]] size_range_t operand_size(
        operand_width_rule_t rule,
        const cpu_decode_context_t& context
    ) noexcept
    {
        switch (rule)
        {
        case operand_width_rule_t::none:
            return { 0, 0 };
        case operand_width_rule_t::fixed8:
            return { 1, 1 };
        case operand_width_rule_t::fixed16:
            return { 2, 2 };
        case operand_width_rule_t::fixed24:
            return { 3, 3 };
        case operand_width_rule_t::accumulator:
        case operand_width_rule_t::index:
            break;
        }

        const bit_state_t width_flag{
            rule == operand_width_rule_t::accumulator
                ? context.accumulator_width
                : context.index_width
        };
        if (context.emulation == bit_state_t::set || width_flag == bit_state_t::set)
            return { 1, 1 };
        if (context.emulation == bit_state_t::clear && width_flag == bit_state_t::clear)
            return { 2, 2 };
        return { 1, 2 };
    }

    [[nodiscard]] uint32_t little_endian_operand(
        const std::array<uint8_t, 4>& bytes,
        uint8_t operand_bytes
    ) noexcept
    {
        uint32_t value{ 0 };
        for (uint8_t index{ 0 }; index < operand_bytes; ++index)
        {
            value |= static_cast<uint32_t>(bytes[static_cast<size_t>(index) + 1u])
                << (index * 8u);
        }
        return value;
    }

    [[nodiscard]] int32_t sign_extend8(uint8_t value) noexcept
    {
        return static_cast<int32_t>(static_cast<int8_t>(value));
    }

    [[nodiscard]] int32_t sign_extend16(uint16_t value) noexcept
    {
        return static_cast<int32_t>(static_cast<int16_t>(value));
    }

    [[nodiscard]] uint32_t relative_target(
        uint32_t address,
        uint8_t encoded_size,
        int32_t displacement
    ) noexcept
    {
        const uint32_t bank{ address & 0x00ff0000u };
        const uint16_t next_pc{
            static_cast<uint16_t>(
                static_cast<uint16_t>(address)
                + encoded_size
            )
        };
        return bank | static_cast<uint16_t>(next_pc + displacement);
    }

    [[nodiscard]] std::optional<uint32_t> direct_target(
        const decoded_instruction_t& decoded
    ) noexcept
    {
        if (!decoded.operand_value.has_value() || decoded.encoded_size == 0)
            return std::nullopt;

        if (decoded.addressing_mode == addressing_mode_t::relative8
            || decoded.addressing_mode == addressing_mode_t::relative16)
        {
            return relative_target(
                decoded.address,
                decoded.encoded_size,
                decoded.relative_displacement.value_or(0)
            );
        }

        if (decoded.control_flow != control_flow_kind_t::call
            && decoded.control_flow != control_flow_kind_t::jump)
        {
            return std::nullopt;
        }

        if (decoded.addressing_mode == addressing_mode_t::absolute_long)
            return *decoded.operand_value & 0x00ffffffu;

        if (decoded.addressing_mode == addressing_mode_t::absolute)
        {
            return (decoded.address & 0x00ff0000u)
                | (*decoded.operand_value & 0x0000ffffu);
        }

        return std::nullopt;
    }
}

namespace clover::analysis::snes
{
    uint32_t advance_program_address(uint32_t address, uint16_t byte_count) noexcept
    {
        const uint32_t bank{ address & 0x00ff0000u };
        const uint16_t pc{
            static_cast<uint16_t>(
                static_cast<uint16_t>(address)
                + byte_count
            )
        };
        return bank | pc;
    }

    decoded_instruction_t decode_instruction(
        const byte_source_t& source,
        uint32_t cpu_address,
        cpu_decode_context_t context
    ) noexcept
    {
        decoded_instruction_t decoded{};
        decoded.address = cpu_address & 0x00ffffffu;
        decoded.context = context;

        const inspected_byte_t opcode_byte{ source.inspect(decoded.address) };
        if (opcode_byte.status != byte_inspection_status_t::available)
            return decoded;

        decoded.opcode = opcode_byte.value;
        decoded.bytes[0] = opcode_byte.value;
        decoded.byte_count = 1;

        const opcode_descriptor_t& descriptor{ opcode_descriptor(decoded.opcode) };
        decoded.instruction = descriptor.instruction;
        decoded.addressing_mode = descriptor.addressing_mode;
        decoded.operand_width_rule = descriptor.operand_width;
        decoded.control_flow = descriptor.control_flow;
        decoded.flag_effects = descriptor.flag_effects;

        const size_range_t operands{ operand_size(descriptor.operand_width, context) };
        decoded.minimum_size = static_cast<uint8_t>(operands.minimum + 1u);
        decoded.maximum_size = static_cast<uint8_t>(operands.maximum + 1u);
        if (context.emulation == bit_state_t::set
            && (context.accumulator_width == bit_state_t::clear
                || context.index_width == bit_state_t::clear))
        {
            decoded.status = decode_status_t::contradictory_context;
            decoded.certainty = decode_certainty_t::context_dependent;
            return decoded;
        }

        for (uint8_t index{ 1 }; index < decoded.maximum_size; ++index)
        {
            const inspected_byte_t byte{
                source.inspect(advance_program_address(decoded.address, index))
            };
            if (byte.status != byte_inspection_status_t::available)
            {
                decoded.status = decode_status_t::unavailable;
                return decoded;
            }
            decoded.bytes[index] = byte.value;
            ++decoded.byte_count;
        }

        if (decoded.minimum_size != decoded.maximum_size)
        {
            decoded.status = decode_status_t::ambiguous_context;
            decoded.certainty = decode_certainty_t::context_dependent;
            return decoded;
        }

        decoded.encoded_size = decoded.minimum_size;
        decoded.operand_size = operands.minimum;
        decoded.status = decode_status_t::complete;
        if (decoded.operand_size != 0)
            decoded.operand_value = little_endian_operand(decoded.bytes, decoded.operand_size);

        if (decoded.addressing_mode == addressing_mode_t::relative8)
        {
            decoded.relative_displacement = sign_extend8(
                static_cast<uint8_t>(decoded.operand_value.value_or(0))
            );
        }
        else if (decoded.addressing_mode == addressing_mode_t::relative16)
        {
            decoded.relative_displacement = sign_extend16(
                static_cast<uint16_t>(decoded.operand_value.value_or(0))
            );
        }
        decoded.direct_target = direct_target(decoded);
        return decoded;
    }

    std::string_view bit_state_name(bit_state_t state) noexcept
    {
        switch (state)
        {
        case bit_state_t::clear: return "0";
        case bit_state_t::set: return "1";
        case bit_state_t::unknown: return "?";
        }
        return "?";
    }

    std::string_view decode_status_name(decode_status_t status) noexcept
    {
        switch (status)
        {
        case decode_status_t::complete: return "complete";
        case decode_status_t::ambiguous_context: return "ambiguous_context";
        case decode_status_t::contradictory_context: return "contradictory_context";
        case decode_status_t::unavailable: return "unavailable";
        }
        return "unavailable";
    }
}
