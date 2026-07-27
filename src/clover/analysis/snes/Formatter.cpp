//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Formatter.h"

#include "clover/analysis/snes/HardwareSymbols.h"

#include <iomanip>
#include <sstream>

namespace
{
    using namespace clover::analysis::snes;

    [[nodiscard]] std::string hex_value(uint32_t value, uint8_t digits)
    {
        std::ostringstream output{};
        output << '$' << std::uppercase << std::hex << std::setfill('0')
               << std::setw(digits) << value;
        return output.str();
    }

    [[nodiscard]] std::string long_address(uint32_t value)
    {
        return hex_value((value >> 16u) & 0xffu, 2)
            + ":"
            + hex_value(value & 0xffffu, 4).substr(1);
    }

    [[nodiscard]] std::string absolute_operand(
        uint32_t value,
        const decoded_instruction_t& instruction,
        const formatting_options_t& options
    )
    {
        if (options.use_hardware_symbols && instruction.context.data_bank.has_value())
        {
            const uint32_t address{
                (static_cast<uint32_t>(*instruction.context.data_bank) << 16u)
                | (value & 0xffffu)
            };
            const std::optional<hardware_symbol_t> symbol{ hardware_symbol(address) };
            if (symbol.has_value())
                return symbol->name;
        }
        return hex_value(value & 0xffffu, 4);
    }

    [[nodiscard]] std::string known_operand(
        const decoded_instruction_t& instruction,
        const formatting_options_t& options
    )
    {
        const uint32_t value{ instruction.operand_value.value_or(0) };
        switch (instruction.addressing_mode)
        {
        case addressing_mode_t::implied:
            return {};
        case addressing_mode_t::accumulator:
            return "A";
        case addressing_mode_t::signature:
            return "#" + hex_value(value, 2);
        case addressing_mode_t::immediate:
            return "#" + hex_value(value, static_cast<uint8_t>(instruction.operand_size * 2u));
        case addressing_mode_t::direct:
            return hex_value(value, 2);
        case addressing_mode_t::direct_x:
            return hex_value(value, 2) + ",X";
        case addressing_mode_t::direct_y:
            return hex_value(value, 2) + ",Y";
        case addressing_mode_t::direct_indirect:
            return "(" + hex_value(value, 2) + ")";
        case addressing_mode_t::direct_indexed_indirect:
            return "(" + hex_value(value, 2) + ",X)";
        case addressing_mode_t::direct_indirect_indexed:
            return "(" + hex_value(value, 2) + "),Y";
        case addressing_mode_t::direct_indirect_long:
            return "[" + hex_value(value, 2) + "]";
        case addressing_mode_t::direct_indirect_long_indexed:
            return "[" + hex_value(value, 2) + "],Y";
        case addressing_mode_t::stack_relative:
            return hex_value(value, 2) + ",S";
        case addressing_mode_t::stack_relative_indirect_indexed:
            return "(" + hex_value(value, 2) + ",S),Y";
        case addressing_mode_t::absolute:
            return absolute_operand(value, instruction, options);
        case addressing_mode_t::absolute_value:
            return hex_value(value, 4);
        case addressing_mode_t::absolute_x:
            return absolute_operand(value, instruction, options) + ",X";
        case addressing_mode_t::absolute_y:
            return absolute_operand(value, instruction, options) + ",Y";
        case addressing_mode_t::absolute_long:
            return long_address(value);
        case addressing_mode_t::absolute_long_x:
            return long_address(value) + ",X";
        case addressing_mode_t::absolute_indirect:
            return "(" + hex_value(value, 4) + ")";
        case addressing_mode_t::absolute_indexed_indirect:
            return "(" + hex_value(value, 4) + ",X)";
        case addressing_mode_t::absolute_indirect_long:
            return "[" + hex_value(value, 4) + "]";
        case addressing_mode_t::relative8:
        case addressing_mode_t::relative16:
            return instruction.direct_target.has_value()
                ? long_address(*instruction.direct_target)
                : hex_value(value, static_cast<uint8_t>(instruction.operand_size * 2u));
        case addressing_mode_t::block_move:
            return hex_value(value & 0xffu, 2) + "," + hex_value((value >> 8u) & 0xffu, 2);
        }
        return {};
    }

    [[nodiscard]] std::string ambiguous_operand(
        const decoded_instruction_t& instruction,
        const formatting_options_t& options
    )
    {
        const uint8_t low{ instruction.bytes[1] };
        const uint16_t wide{
            static_cast<uint16_t>(
                low | (static_cast<uint16_t>(instruction.bytes[2]) << 8u)
            )
        };
        std::string result{
            "#" + hex_value(low, 2) + "|#" + hex_value(wide, 4)
        };
        if (options.include_context_on_ambiguity)
        {
            const char width_flag{
                instruction.operand_width_rule == operand_width_rule_t::accumulator
                    ? 'M'
                    : 'X'
            };
            result += " {E=";
            result += bit_state_name(instruction.context.emulation);
            result += ",";
            result += width_flag;
            result += "=";
            result += bit_state_name(
                width_flag == 'M'
                    ? instruction.context.accumulator_width
                    : instruction.context.index_width
            );
            result += "}";
        }
        return result;
    }

    void append_json_string(std::ostringstream& output, std::string_view value)
    {
        output << '"';
        for (const char character : value)
        {
            if (character == '"' || character == '\\')
                output << '\\';
            output << character;
        }
        output << '"';
    }
}

namespace clover::analysis::snes
{
    std::string format_instruction(
        const decoded_instruction_t& instruction,
        formatting_options_t options
    )
    {
        if (instruction.byte_count == 0)
            return "<unavailable>";

        std::string output{ instruction_mnemonic(instruction.instruction) };
        std::string operand{};
        if (instruction.status == decode_status_t::ambiguous_context)
            operand = ambiguous_operand(instruction, options);
        else if (instruction.status == decode_status_t::complete)
            operand = known_operand(instruction, options);
        else if (instruction.status == decode_status_t::contradictory_context)
            operand = "<contradictory-context>";
        else
            operand = "<unavailable>";

        if (!operand.empty())
            output += " " + operand;
        return output;
    }

    std::string format_instruction_json(const decoded_instruction_t& instruction)
    {
        std::ostringstream output{};
        output << '{';
        output << "\"address\":" << instruction.address << ',';
        output << "\"opcode\":" << static_cast<uint32_t>(instruction.opcode) << ',';
        output << "\"mnemonic\":";
        append_json_string(output, instruction_mnemonic(instruction.instruction));
        output << ",\"addressing_mode\":";
        append_json_string(output, addressing_mode_name(instruction.addressing_mode));
        output << ",\"operand_width_rule\":";
        append_json_string(output, operand_width_rule_name(instruction.operand_width_rule));
        output << ",\"control_flow\":";
        append_json_string(output, control_flow_name(instruction.control_flow));
        output << ",\"status\":";
        append_json_string(output, decode_status_name(instruction.status));
        output << ",\"encoded_size\":" << static_cast<uint32_t>(instruction.encoded_size);
        output << ",\"minimum_size\":" << static_cast<uint32_t>(instruction.minimum_size);
        output << ",\"maximum_size\":" << static_cast<uint32_t>(instruction.maximum_size);
        output << ",\"bytes\":[";
        for (uint8_t index{ 0 }; index < instruction.byte_count; ++index)
        {
            if (index != 0)
                output << ',';
            output << static_cast<uint32_t>(instruction.bytes[index]);
        }
        output << "],\"context\":{\"e\":";
        append_json_string(output, bit_state_name(instruction.context.emulation));
        output << ",\"m\":";
        append_json_string(output, bit_state_name(instruction.context.accumulator_width));
        output << ",\"x\":";
        append_json_string(output, bit_state_name(instruction.context.index_width));
        output << ",\"direct_page\":";
        if (instruction.context.direct_page.has_value())
            output << *instruction.context.direct_page;
        else
            output << "null";
        output << ",\"data_bank\":";
        if (instruction.context.data_bank.has_value())
            output << static_cast<uint32_t>(*instruction.context.data_bank);
        else
            output << "null";
        output << '}';
        output << ",\"direct_target\":";
        if (instruction.direct_target.has_value())
            output << *instruction.direct_target;
        else
            output << "null";
        output << ",\"text\":";
        append_json_string(output, format_instruction(instruction));
        output << '}';
        return output.str();
    }
}
