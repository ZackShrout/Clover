//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesInstructionServices.h"

#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/SnesEmulatorCore.h"

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace
{
    [[nodiscard]] uint64_t register_value(
        const clover::workbench::live_processor_state_t& state,
        std::string_view stable_id
    ) noexcept
    {
        const size_t count{ std::min(state.descriptors.size(), state.values.size()) };
        for (size_t index{ 0 }; index < count; ++index)
        {
            if (state.descriptors[index].stable_id == stable_id)
                return state.values[index].value;
        }
        return 0u;
    }
}

namespace clover::workbench::snes
{
    frontend::execution_domain_id_t
        snes_instruction_services_t::execution_domain() const noexcept
    {
        return frontend::snes_debug::k_main_cpu_domain;
    }

    frontend::address_space_id_t
        snes_instruction_services_t::instruction_address_space() const noexcept
    {
        return frontend::snes_debug::k_cpu_bus_space;
    }

    bool snes_instruction_services_t::valid_instruction_address(
        frontend::debug_address_t address
    ) const noexcept
    {
        return address.space == instruction_address_space()
            && address.value <= 0x00ffffffu;
    }

    frontend::debug_address_t snes_instruction_services_t::advance_address(
        frontend::debug_address_t address,
        uint8_t encoded_size
    ) const noexcept
    {
        address.value = analysis::snes::advance_program_address(
            static_cast<uint32_t>(address.value),
            encoded_size
        );
        return address;
    }

    bool snes_instruction_services_t::decode_semantics(
        const frontend::debug_target_t& target,
        const live_processor_state_t& state,
        instruction_semantics_t& semantics,
        std::string& error
    ) const
    {
        error.clear();
        if (!valid_instruction_address(state.instruction_address))
        {
            error = "Instruction address is outside the SNES CPU bus";
            return false;
        }
        const analysis::snes::debug_target_byte_source_t source{
            target,
            frontend::snes_debug::k_cpu_bus_space,
            frontend::snes_debug::k_canonical_media_space
        };
        const analysis::snes::decoded_instruction_t instruction{
            analysis::snes::decode_instruction(
                source,
                static_cast<uint32_t>(state.instruction_address.value),
                decode_context(state)
            )
        };
        if (instruction.status == analysis::snes::decode_status_t::unavailable
            || instruction.encoded_size == 0u)
        {
            error = "Unable to decode the current instruction";
            return false;
        }
        instruction_control_flow_t control_flow{
            instruction_control_flow_t::linear
        };
        if (instruction.control_flow == analysis::snes::control_flow_kind_t::call)
            control_flow = instruction_control_flow_t::call;
        else if (instruction.control_flow
                 == analysis::snes::control_flow_kind_t::return_)
            control_flow = instruction_control_flow_t::return_;
        semantics = {
            .address = state.instruction_address,
            .encoded_size = instruction.encoded_size,
            .control_flow = control_flow
        };
        return true;
    }

    analysis::snes::cpu_decode_context_t decode_context(
        const live_processor_state_t& state
    ) noexcept
    {
        const uint8_t status{ static_cast<uint8_t>(register_value(state, "p")) };
        const bool emulation{ register_value(state, "e") != 0u };
        return {
            .emulation = emulation
                ? analysis::snes::bit_state_t::set
                : analysis::snes::bit_state_t::clear,
            .accumulator_width = (status & 0x20u) != 0u
                ? analysis::snes::bit_state_t::set
                : analysis::snes::bit_state_t::clear,
            .index_width = (status & 0x10u) != 0u
                ? analysis::snes::bit_state_t::set
                : analysis::snes::bit_state_t::clear,
            .direct_page = static_cast<uint16_t>(register_value(state, "d")),
            .data_bank = static_cast<uint8_t>(register_value(state, "db"))
        };
    }
}
