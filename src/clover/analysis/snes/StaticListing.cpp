//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/StaticListing.h"

#include <array>

namespace clover::analysis::snes
{
    span_byte_source_t::span_byte_source_t(
        std::span<const std::byte> bytes,
        uint32_t origin
    ) noexcept
        : _bytes{ bytes }
        , _origin{ origin & 0x00ffffffu }
    {
    }

    inspected_byte_t span_byte_source_t::inspect(uint32_t cpu_address) const noexcept
    {
        if ((cpu_address & 0x00ff0000u) != (_origin & 0x00ff0000u))
            return {};

        const uint16_t origin_pc{ static_cast<uint16_t>(_origin) };
        const uint16_t address_pc{ static_cast<uint16_t>(cpu_address) };
        const uint16_t relative{ static_cast<uint16_t>(address_pc - origin_pc) };
        if (relative >= _bytes.size())
            return {};
        return {
            .status = byte_inspection_status_t::available,
            .value = std::to_integer<uint8_t>(_bytes[relative])
        };
    }

    debug_target_byte_source_t::debug_target_byte_source_t(
        const frontend::debug_target_t& target,
        frontend::address_space_id_t cpu_bus_space,
        frontend::address_space_id_t canonical_media_space
    ) noexcept
        : _target{ target }
        , _cpu_bus_space{ cpu_bus_space }
        , _canonical_media_space{ canonical_media_space }
    {
    }

    inspected_byte_t debug_target_byte_source_t::inspect(uint32_t cpu_address) const noexcept
    {
        frontend::debug_address_t inspection_address{
            .space = _cpu_bus_space,
            .value = cpu_address & 0x00ffffffu
        };
        const frontend::address_translation_result_t translation{
            _target.translate_address(inspection_address, _canonical_media_space)
        };
        if (translation.status == frontend::address_translation_status_t::complete)
            inspection_address = translation.address;

        std::array<std::byte, 1> byte{};
        const frontend::memory_inspection_result_t result{
            _target.inspect_memory(inspection_address, byte)
        };
        if (result.status != frontend::memory_inspection_status_t::complete
            || result.bytes_read != 1)
        {
            return {};
        }
        return {
            .status = byte_inspection_status_t::available,
            .value = std::to_integer<uint8_t>(byte[0])
        };
    }

    static_listing_result_t build_static_listing(
        const byte_source_t& source,
        const static_listing_options_t& options
    )
    {
        static_listing_result_t result{};
        result.next_address = options.start_address & 0x00ffffffu;
        result.instructions.reserve(options.maximum_instructions);

        while (result.instructions.size() < options.maximum_instructions)
        {
            if (result.bytes_consumed >= options.maximum_bytes)
            {
                result.stop_reason = listing_stop_reason_t::byte_limit;
                return result;
            }

            decoded_instruction_t instruction{
                decode_instruction(source, result.next_address, options.context)
            };
            result.instructions.push_back(instruction);

            if (instruction.status == decode_status_t::unavailable)
            {
                result.stop_reason = listing_stop_reason_t::unavailable;
                return result;
            }
            if (instruction.status == decode_status_t::ambiguous_context)
            {
                result.stop_reason = listing_stop_reason_t::ambiguous_context;
                return result;
            }
            if (instruction.status == decode_status_t::contradictory_context)
            {
                result.stop_reason = listing_stop_reason_t::contradictory_context;
                return result;
            }
            if (result.bytes_consumed + instruction.encoded_size > options.maximum_bytes)
            {
                result.instructions.pop_back();
                result.stop_reason = listing_stop_reason_t::byte_limit;
                return result;
            }

            result.bytes_consumed += instruction.encoded_size;
            result.next_address = advance_program_address(
                result.next_address,
                instruction.encoded_size
            );
        }

        result.stop_reason = listing_stop_reason_t::instruction_limit;
        return result;
    }

    std::string_view listing_stop_reason_name(listing_stop_reason_t reason) noexcept
    {
        switch (reason)
        {
        case listing_stop_reason_t::instruction_limit: return "instruction_limit";
        case listing_stop_reason_t::byte_limit: return "byte_limit";
        case listing_stop_reason_t::ambiguous_context: return "ambiguous_context";
        case listing_stop_reason_t::contradictory_context: return "contradictory_context";
        case listing_stop_reason_t::unavailable: return "unavailable";
        }
        return "unavailable";
    }
}
