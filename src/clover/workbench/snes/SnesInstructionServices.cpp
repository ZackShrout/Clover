//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesInstructionServices.h"

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
