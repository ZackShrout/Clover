//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/workbench/DebuggerModel.h"

#include <cstdint>
#include <string>

namespace clover::workbench
{
    enum class instruction_control_flow_t : uint8_t
    {
        linear,
        call,
        return_
    };

    struct instruction_semantics_t
    {
        frontend::debug_address_t address{};
        uint8_t encoded_size{ 0u };
        instruction_control_flow_t control_flow{
            instruction_control_flow_t::linear
        };
    };

    // Architecture-specific decoding is injected at the debugger boundary.
    // The debugger consumes only the semantics needed for generic run policy.
    class instruction_services_t
    {
    public:
        virtual ~instruction_services_t() = default;

        [[nodiscard]] virtual frontend::execution_domain_id_t
            execution_domain() const noexcept = 0;
        [[nodiscard]] virtual frontend::address_space_id_t
            instruction_address_space() const noexcept = 0;
        [[nodiscard]] virtual bool valid_instruction_address(
            frontend::debug_address_t address
        ) const noexcept = 0;
        [[nodiscard]] virtual frontend::debug_address_t advance_address(
            frontend::debug_address_t address,
            uint8_t encoded_size
        ) const noexcept = 0;
        [[nodiscard]] virtual bool decode_semantics(
            const frontend::debug_target_t& target,
            const live_processor_state_t& state,
            instruction_semantics_t& semantics,
            std::string& error
        ) const = 0;
    };
}
