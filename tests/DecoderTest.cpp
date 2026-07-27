//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Formatter.h"
#include "clover/analysis/snes/HardwareSymbols.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/core/snes/Console.h"

#include "Wdc65816Golden.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace
{
    using namespace clover::analysis::snes;

    [[nodiscard]] int fail(const char* message)
    {
        std::fprintf(stderr, "Decoder test failed: %s\n", message);
        return 1;
    }

    [[nodiscard]] int fail_opcode(const char* message, uint16_t opcode)
    {
        std::fprintf(
            stderr,
            "Decoder test failed: %s at opcode $%02x\n",
            message,
            opcode
        );
        return 1;
    }

    [[nodiscard]] std::optional<addressing_mode_t> golden_mode(std::string_view mode)
    {
        if (mode == "i") return addressing_mode_t::implied;
        if (mode == "a") return addressing_mode_t::accumulator;
        if (mode == "sig") return addressing_mode_t::signature;
        if (mode == "imm") return addressing_mode_t::immediate;
        if (mode == "d") return addressing_mode_t::direct;
        if (mode == "dx") return addressing_mode_t::direct_x;
        if (mode == "dy") return addressing_mode_t::direct_y;
        if (mode == "di") return addressing_mode_t::direct_indirect;
        if (mode == "dix") return addressing_mode_t::direct_indexed_indirect;
        if (mode == "diy") return addressing_mode_t::direct_indirect_indexed;
        if (mode == "dil") return addressing_mode_t::direct_indirect_long;
        if (mode == "dily") return addressing_mode_t::direct_indirect_long_indexed;
        if (mode == "sr") return addressing_mode_t::stack_relative;
        if (mode == "sriy") return addressing_mode_t::stack_relative_indirect_indexed;
        if (mode == "abs") return addressing_mode_t::absolute;
        if (mode == "value") return addressing_mode_t::absolute_value;
        if (mode == "absx") return addressing_mode_t::absolute_x;
        if (mode == "absy") return addressing_mode_t::absolute_y;
        if (mode == "long") return addressing_mode_t::absolute_long;
        if (mode == "longx") return addressing_mode_t::absolute_long_x;
        if (mode == "absi") return addressing_mode_t::absolute_indirect;
        if (mode == "absxi") return addressing_mode_t::absolute_indexed_indirect;
        if (mode == "absil") return addressing_mode_t::absolute_indirect_long;
        if (mode == "r8") return addressing_mode_t::relative8;
        if (mode == "r16") return addressing_mode_t::relative16;
        if (mode == "block") return addressing_mode_t::block_move;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<operand_width_rule_t> golden_width(char width)
    {
        switch (width)
        {
        case '0': return operand_width_rule_t::none;
        case '1': return operand_width_rule_t::fixed8;
        case '2': return operand_width_rule_t::fixed16;
        case '3': return operand_width_rule_t::fixed24;
        case 'M': return operand_width_rule_t::accumulator;
        case 'X': return operand_width_rule_t::index;
        default: return std::nullopt;
        }
    }

    [[nodiscard]] std::optional<control_flow_kind_t> golden_flow(char flow)
    {
        switch (flow)
        {
        case 'L': return control_flow_kind_t::linear;
        case 'C': return control_flow_kind_t::conditional_branch;
        case 'B': return control_flow_kind_t::unconditional_branch;
        case 'A': return control_flow_kind_t::call;
        case 'J': return control_flow_kind_t::jump;
        case 'R': return control_flow_kind_t::return_;
        case 'I': return control_flow_kind_t::interrupt;
        case 'W': return control_flow_kind_t::wait;
        case 'S': return control_flow_kind_t::stop;
        default: return std::nullopt;
        }
    }

    [[nodiscard]] uint8_t golden_size(
        char width,
        bool wide_accumulator,
        bool wide_index
    )
    {
        switch (width)
        {
        case '0': return 1;
        case '1': return 2;
        case '2': return 3;
        case '3': return 4;
        case 'M':
            return static_cast<uint8_t>(wide_accumulator ? 3 : 2);
        case 'X':
            return static_cast<uint8_t>(wide_index ? 3 : 2);
        default:
            return 0;
        }
    }

    struct fake_debug_target_t final : clover::frontend::debug_target_t
    {
        std::array<std::byte, 4> bytes{
            std::byte{ 0xa9u },
            std::byte{ 0x34u },
            std::byte{ 0x12u },
            std::byte{ 0xeau }
        };
        mutable size_t translation_count{ 0 };
        mutable size_t canonical_inspection_count{ 0 };

        [[nodiscard]] std::span<const clover::frontend::execution_domain_descriptor_t>
            execution_domains() const noexcept override
        {
            return {};
        }

        [[nodiscard]] std::span<const clover::frontend::address_space_descriptor_t>
            address_spaces() const noexcept override
        {
            return {};
        }

        [[nodiscard]] clover::frontend::memory_inspection_result_t inspect_memory(
            clover::frontend::debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept override
        {
            if (address.space != 2u || destination.size() != 1 || address.value >= bytes.size())
            {
                return {
                    .status = clover::frontend::memory_inspection_status_t::unavailable
                };
            }
            ++canonical_inspection_count;
            destination[0] = bytes[static_cast<size_t>(address.value)];
            return {
                .status = clover::frontend::memory_inspection_status_t::complete,
                .bytes_read = 1
            };
        }

        [[nodiscard]] clover::frontend::address_translation_result_t translate_address(
            clover::frontend::debug_address_t source,
            clover::frontend::address_space_id_t destination_space
        ) const noexcept override
        {
            ++translation_count;
            if (source.space != 1u || destination_space != 2u
                || source.value < 0x008000u || source.value >= 0x008004u)
            {
                return {
                    .status = clover::frontend::address_translation_status_t::unmapped
                };
            }
            return {
                .status = clover::frontend::address_translation_status_t::complete,
                .address = {
                    .space = 2u,
                    .value = source.value - 0x008000u
                }
            };
        }
    };
}

int main()
{
    using namespace clover::analysis::snes;

    const std::array<std::byte, 4> bytes{
        std::byte{ 0 },
        std::byte{ 0x34u },
        std::byte{ 0x12u },
        std::byte{ 0x56u }
    };

    for (uint16_t opcode{ 0 }; opcode <= 0xffu; ++opcode)
    {
        std::array<std::byte, 4> instruction_bytes{ bytes };
        instruction_bytes[0] = static_cast<std::byte>(opcode);
        const span_byte_source_t source{ instruction_bytes, 0x808000u };

        const opcode_descriptor_t& descriptor{
            opcode_descriptor(static_cast<uint8_t>(opcode))
        };
        const clover::test::golden_opcode_t& golden{
            clover::test::k_wdc65816_golden[opcode]
        };
        const std::optional<addressing_mode_t> expected_mode{
            golden_mode(golden.mode)
        };
        const std::optional<operand_width_rule_t> expected_width{
            golden_width(golden.width)
        };
        const std::optional<control_flow_kind_t> expected_flow{
            golden_flow(golden.flow)
        };
        if (instruction_mnemonic(descriptor.instruction) == "???"
            || addressing_mode_name(descriptor.addressing_mode) == "unknown"
            || operand_width_rule_name(descriptor.operand_width) == "unknown"
            || control_flow_name(descriptor.control_flow) == "unknown")
        {
            return fail("incomplete opcode metadata");
        }
        if (!expected_mode.has_value()
            || !expected_width.has_value()
            || !expected_flow.has_value()
            || instruction_mnemonic(descriptor.instruction) != golden.mnemonic
            || descriptor.addressing_mode != *expected_mode
            || descriptor.operand_width != *expected_width
            || descriptor.control_flow != *expected_flow)
        {
            return fail_opcode("independent golden metadata mismatch", opcode);
        }

        const decoded_instruction_t emulation{
            decode_instruction(
                source,
                0x808000u,
                { bit_state_t::set, bit_state_t::set, bit_state_t::set }
            )
        };
        if (emulation.status != decode_status_t::complete
            || emulation.opcode != opcode
            || emulation.encoded_size != golden_size(golden.width, false, false))
        {
            return fail_opcode("emulation-mode golden decode", opcode);
        }

        const decoded_instruction_t native8{
            decode_instruction(
                source,
                0x808000u,
                { bit_state_t::clear, bit_state_t::set, bit_state_t::set }
            )
        };
        if (native8.status != decode_status_t::complete
            || native8.encoded_size != golden_size(golden.width, false, false))
        {
            return fail_opcode("native 8-bit golden decode", opcode);
        }

        const decoded_instruction_t native16{
            decode_instruction(
                source,
                0x808000u,
                { bit_state_t::clear, bit_state_t::clear, bit_state_t::clear }
            )
        };
        if (native16.status != decode_status_t::complete
            || native16.encoded_size != golden_size(golden.width, true, true))
        {
            return fail_opcode("native 16-bit golden decode", opcode);
        }
    }

    {
        std::unique_ptr<clover::core::console_t> console{
            std::make_unique<clover::core::console_t>()
        };
        for (uint16_t opcode{ 0 }; opcode <= 0xffu; ++opcode)
        {
            console->power_on();
            console->write_u8(0x000000u, static_cast<uint8_t>(opcode));
            console->write_u8(0x000001u, 0x00u);
            console->write_u8(0x000002u, 0x00u);
            console->write_u8(0x000003u, 0x00u);

            const uint64_t placeholders_before{
                console->cpu_placeholder_opcode_count()
            };
            static_cast<void>(console->step_hardware());
            if (console->cpu_placeholder_opcode_count() != placeholders_before)
                return fail_opcode("real executor used placeholder dispatch", opcode);

            const clover::test::golden_opcode_t& golden{
                clover::test::k_wdc65816_golden[opcode]
            };
            if (golden.flow == 'L'
                && console->cpu_state().pc != golden_size(golden.width, false, false))
            {
                return fail_opcode("executor operand consumption drift", opcode);
            }
        }
    }

    {
        const opcode_descriptor_t& lda_immediate{ opcode_descriptor(0xa9u) };
        const opcode_descriptor_t& ldx_immediate{ opcode_descriptor(0xa2u) };
        const opcode_descriptor_t& jml_indirect{ opcode_descriptor(0xdcu) };
        const opcode_descriptor_t& jsr_indexed_indirect{ opcode_descriptor(0xfcu) };
        if (lda_immediate.instruction != instruction_id_t::lda
            || lda_immediate.addressing_mode != addressing_mode_t::immediate
            || lda_immediate.operand_width != operand_width_rule_t::accumulator
            || ldx_immediate.instruction != instruction_id_t::ldx
            || ldx_immediate.operand_width != operand_width_rule_t::index
            || jml_indirect.instruction != instruction_id_t::jml
            || jml_indirect.addressing_mode != addressing_mode_t::absolute_indirect_long
            || jml_indirect.control_flow != control_flow_kind_t::jump
            || jsr_indexed_indirect.instruction != instruction_id_t::jsr
            || jsr_indexed_indirect.addressing_mode
                != addressing_mode_t::absolute_indexed_indirect
            || jsr_indexed_indirect.control_flow != control_flow_kind_t::call)
        {
            return fail("representative opcode metadata");
        }

        const opcode_descriptor_t& bit_immediate{ opcode_descriptor(0x89u) };
        const opcode_descriptor_t& bit_absolute{ opcode_descriptor(0x2cu) };
        const opcode_descriptor_t& rep{ opcode_descriptor(0xc2u) };
        const opcode_descriptor_t& xce{ opcode_descriptor(0xfbu) };
        const uint16_t negative{ flag_mask(status_flag_t::negative) };
        const uint16_t overflow{ flag_mask(status_flag_t::overflow) };
        const uint16_t zero{ flag_mask(status_flag_t::zero) };
        if (bit_immediate.flag_effects.written_mask != zero
            || (bit_absolute.flag_effects.written_mask & negative) == 0
            || (bit_absolute.flag_effects.written_mask & overflow) == 0
            || !rep.flag_effects.operand_selected
            || !xce.flag_effects.exchange_carry_emulation)
        {
            return fail("architectural flag metadata");
        }
    }

    {
        const std::array<std::byte, 3> immediate{
            std::byte{ 0xa9u }, std::byte{ 0x34u }, std::byte{ 0x12u }
        };
        const span_byte_source_t source{ immediate, 0x008000u };
        const decoded_instruction_t ambiguous{
            decode_instruction(source, 0x008000u, {})
        };
        if (ambiguous.status != decode_status_t::ambiguous_context
            || ambiguous.minimum_size != 2
            || ambiguous.maximum_size != 3
            || ambiguous.encoded_size != 0
            || format_instruction(ambiguous).find("E=?") == std::string::npos)
        {
            return fail("accumulator-width ambiguity");
        }
    }

    {
        const std::array<std::byte, 1> nop{ std::byte{ 0xeau } };
        const span_byte_source_t source{ nop, 0x008000u };
        const decoded_instruction_t contradictory{
            decode_instruction(
                source,
                0x008000u,
                {
                    bit_state_t::set,
                    bit_state_t::clear,
                    bit_state_t::set
                }
            )
        };
        if (contradictory.status != decode_status_t::contradictory_context
            || format_instruction(contradictory).find("contradictory")
                == std::string::npos)
        {
            return fail("emulation-mode context contradiction");
        }
    }

    {
        const std::array<std::byte, 2> branch{
            std::byte{ 0x80u }, std::byte{ 0xfcu }
        };
        const span_byte_source_t source{ branch, 0x80fffeu };
        const decoded_instruction_t decoded{
            decode_instruction(
                source,
                0x80fffeu,
                { bit_state_t::set, bit_state_t::set, bit_state_t::set }
            )
        };
        if (decoded.direct_target != 0x80fffcu
            || decoded.relative_displacement != -4
            || advance_program_address(0x80ffffu, 1) != 0x800000u)
        {
            return fail("bank-local relative target and PC wrap");
        }
    }

    {
        const std::array<std::byte, 3> store{
            std::byte{ 0x8du }, std::byte{ 0x00u }, std::byte{ 0x21u }
        };
        const span_byte_source_t source{ store, 0x808000u };
        const decoded_instruction_t decoded{
            decode_instruction(
                source,
                0x808000u,
                {
                    .emulation = bit_state_t::set,
                    .accumulator_width = bit_state_t::set,
                    .index_width = bit_state_t::set,
                    .data_bank = 0u
                }
            )
        };
        if (format_instruction(decoded) != "STA INIDISP"
            || !hardware_symbol(0x4300u).has_value()
            || hardware_symbol(0x4300u)->name != "DMAP0"
            || hardware_symbol(0x7e2100u).has_value())
        {
            return fail("hardware symbols");
        }
        const std::string json{ format_instruction_json(decoded) };
        if (json.find("\"mnemonic\":\"STA\"") == std::string::npos
            || json.find("\"addressing_mode\":\"absolute\"") == std::string::npos)
        {
            return fail("machine-readable output");
        }
    }

    {
        const std::array<std::byte, 3> pea{
            std::byte{ 0xf4u }, std::byte{ 0x00u }, std::byte{ 0x21u }
        };
        const span_byte_source_t source{ pea, 0x808000u };
        const decoded_instruction_t decoded{
            decode_instruction(
                source,
                0x808000u,
                {
                    .emulation = bit_state_t::set,
                    .accumulator_width = bit_state_t::set,
                    .index_width = bit_state_t::set,
                    .data_bank = 0u
                }
            )
        };
        if (format_instruction(decoded) != "PEA $2100")
            return fail("absolute value is not a hardware-register reference");
    }

    {
        fake_debug_target_t target{};
        const debug_target_byte_source_t source{ target, 1u, 2u };
        const static_listing_result_t listing{
            build_static_listing(
                source,
                {
                    .start_address = 0x008000u,
                    .maximum_instructions = 2,
                    .maximum_bytes = 4,
                    .context = {
                        bit_state_t::clear,
                        bit_state_t::clear,
                        bit_state_t::clear
                    }
                }
            )
        };
        if (listing.instructions.size() != 2
            || listing.instructions[0].encoded_size != 3
            || listing.instructions[1].instruction != instruction_id_t::nop
            || target.translation_count != 4
            || target.canonical_inspection_count != 4)
        {
            return fail("Stage 0 translation-backed listing");
        }
    }

    {
        const std::array<std::byte, 3> immediate{
            std::byte{ 0xa2u }, std::byte{ 0x34u }, std::byte{ 0x12u }
        };
        const span_byte_source_t source{ immediate, 0x008000u };
        const static_listing_result_t listing{
            build_static_listing(
                source,
                {
                    .start_address = 0x008000u,
                    .maximum_instructions = 4,
                    .maximum_bytes = 4,
                    .context = {}
                }
            )
        };
        if (listing.stop_reason != listing_stop_reason_t::ambiguous_context
            || listing.instructions.size() != 1
            || listing.next_address != 0x008000u)
        {
            return fail("listing refuses ambiguous advancement");
        }
    }

    std::printf(
        "Decoder tests passed: 256-opcode golden matrix, executor dispatch, "
        "and Stage 0 listing integration\n"
    );
    return 0;
}
