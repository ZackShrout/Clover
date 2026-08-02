//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/workbench/DebuggerModel.h"

#include <cstddef>
#include <span>
#include <string>
#include <vector>

namespace clover::workbench
{
    // System-neutral run-control contract. Processor decoding, analysis
    // evidence, and hardware tools are supplied as separate architecture and
    // system services rather than being baked into this interface.
    class debugger_t
    {
    public:
        virtual ~debugger_t() = default;

        [[nodiscard]] virtual bool initialize(
            frontend::debug_target_t& target,
            std::string& error,
            std::string analysis_session = {}
        ) = 0;
        virtual void shutdown() noexcept = 0;
        [[nodiscard]] virtual bool is_initialized() const noexcept = 0;
        [[nodiscard]] virtual debugger_run_state_t run_state() const noexcept = 0;
        [[nodiscard]] virtual const debugger_stop_t& last_stop() const noexcept = 0;

        [[nodiscard]] virtual bool pause(std::string& error) = 0;
        [[nodiscard]] virtual bool resume(std::string& error) = 0;
        [[nodiscard]] virtual bool step_instruction(std::string& error) = 0;
        [[nodiscard]] virtual bool step_over(std::string& error) = 0;
        [[nodiscard]] virtual bool step_out(std::string& error) = 0;
        [[nodiscard]] virtual bool run_to(
            frontend::debug_address_t address,
            std::string& error
        ) = 0;
        [[nodiscard]] virtual size_t pump(
            size_t instruction_budget,
            std::string& error
        ) = 0;
        [[nodiscard]] virtual size_t pump_fast(
            size_t instruction_budget,
            std::string& error
        ) = 0;

        [[nodiscard]] virtual uint64_t add_breakpoint(
            frontend::debug_address_t address,
            bool temporary = false
        ) = 0;
        [[nodiscard]] virtual bool remove_breakpoint(uint64_t id) = 0;
        [[nodiscard]] virtual bool set_breakpoint_enabled(
            uint64_t id,
            bool enabled
        ) = 0;
        [[nodiscard]] virtual const std::vector<breakpoint_t>&
            breakpoints() const noexcept = 0;

        [[nodiscard]] virtual uint64_t add_watchpoint(
            frontend::debug_address_t start,
            uint64_t length,
            watch_access_t access
        ) = 0;
        [[nodiscard]] virtual bool remove_watchpoint(uint64_t id) = 0;
        [[nodiscard]] virtual bool set_watchpoint_enabled(
            uint64_t id,
            bool enabled
        ) = 0;
        [[nodiscard]] virtual const std::vector<watchpoint_t>&
            watchpoints() const noexcept = 0;

        [[nodiscard]] virtual bool live_state(
            live_processor_state_t& state,
            std::string& error
        ) const = 0;
        [[nodiscard]] virtual frontend::memory_inspection_result_t inspect_memory(
            frontend::debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept = 0;
    };
}
