//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/DebugTarget.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clover::workbench
{
    enum class debugger_run_state_t : uint8_t
    {
        stopped,
        running
    };

    enum class debugger_stop_reason_t : uint8_t
    {
        none,
        pause,
        instruction_step,
        breakpoint,
        watchpoint,
        run_to_cursor,
        step_over,
        step_out,
        waiting,
        processor_stopped,
        observation_overflow,
        error
    };

    enum class watch_access_t : uint8_t
    {
        read = 1u << 0u,
        write = 1u << 1u,
        read_write = (1u << 0u) | (1u << 1u)
    };

    struct breakpoint_t
    {
        uint64_t id{ 0 };
        frontend::debug_address_t address{};
        bool enabled{ true };
        bool temporary{ false };
        uint64_t hit_count{ 0 };
    };

    struct watchpoint_t
    {
        uint64_t id{ 0 };
        frontend::debug_address_t start{};
        uint64_t length{ 1 };
        watch_access_t access{ watch_access_t::read_write };
        bool enabled{ true };
        uint64_t hit_count{ 0 };
    };

    enum class control_flow_observation_kind_t : uint8_t
    {
        call,
        return_
    };

    struct control_flow_observation_t
    {
        control_flow_observation_kind_t kind{
            control_flow_observation_kind_t::call
        };
        frontend::debug_address_t from{};
        frontend::debug_address_t to{};
    };

    struct debugger_stop_t
    {
        debugger_stop_reason_t reason{ debugger_stop_reason_t::none };
        frontend::debug_address_t address{};
        std::optional<uint64_t> breakpoint_id{};
        std::optional<uint64_t> watchpoint_id{};
        std::optional<frontend::memory_access_observation_t> memory_access{};
        std::string detail{};
    };

    // A processor snapshot is deliberately architecture-neutral. Register
    // meaning and instruction-width state are interpreted by the processor
    // services selected by the active system toolkit.
    struct live_processor_state_t
    {
        frontend::debug_address_t instruction_address{};
        std::vector<frontend::processor_register_descriptor_t> descriptors{};
        std::vector<frontend::processor_register_value_t> values{};
    };
}
