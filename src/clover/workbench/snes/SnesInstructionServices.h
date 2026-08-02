//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"
#include "clover/workbench/DebuggerModel.h"
#include "clover/workbench/InstructionServices.h"

namespace clover::workbench::snes
{
    class snes_instruction_services_t final : public instruction_services_t
    {
    public:
        [[nodiscard]] frontend::execution_domain_id_t
            execution_domain() const noexcept override;
        [[nodiscard]] frontend::address_space_id_t
            instruction_address_space() const noexcept override;
        [[nodiscard]] bool valid_instruction_address(
            frontend::debug_address_t address
        ) const noexcept override;
        [[nodiscard]] frontend::debug_address_t advance_address(
            frontend::debug_address_t address,
            uint8_t encoded_size
        ) const noexcept override;
        [[nodiscard]] bool decode_semantics(
            const frontend::debug_target_t& target,
            const live_processor_state_t& state,
            instruction_semantics_t& semantics,
            std::string& error
        ) const override;
    };

    [[nodiscard]] analysis::snes::cpu_decode_context_t decode_context(
        const live_processor_state_t& state
    ) noexcept;
}
