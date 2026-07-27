//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/DebugTarget.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
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

    struct live_processor_state_t
    {
        frontend::debug_address_t instruction_address{};
        std::vector<frontend::processor_register_descriptor_t> descriptors{};
        std::vector<frontend::processor_register_value_t> values{};
        analysis::snes::cpu_decode_context_t decode_context{};
    };

    class snes_debugger_t
    {
    public:
        snes_debugger_t() = default;
        ~snes_debugger_t();

        snes_debugger_t(const snes_debugger_t&) = delete;
        snes_debugger_t& operator=(const snes_debugger_t&) = delete;

        [[nodiscard]] bool initialize(frontend::debug_target_t& target,
                                      std::string& error);
        void shutdown() noexcept;
        [[nodiscard]] bool is_initialized() const noexcept;
        [[nodiscard]] debugger_run_state_t run_state() const noexcept;
        [[nodiscard]] const debugger_stop_t& last_stop() const noexcept;

        [[nodiscard]] bool pause(std::string& error);
        [[nodiscard]] bool resume(std::string& error);
        [[nodiscard]] bool step_instruction(std::string& error);
        [[nodiscard]] bool step_over(std::string& error);
        [[nodiscard]] bool step_out(std::string& error);
        [[nodiscard]] bool run_to(frontend::debug_address_t address,
                                  std::string& error);
        [[nodiscard]] size_t pump(size_t instruction_budget, std::string& error);

        [[nodiscard]] uint64_t add_breakpoint(frontend::debug_address_t address,
                                              bool temporary = false);
        [[nodiscard]] bool remove_breakpoint(uint64_t id);
        [[nodiscard]] bool set_breakpoint_enabled(uint64_t id, bool enabled);
        [[nodiscard]] const std::vector<breakpoint_t>& breakpoints() const noexcept;

        [[nodiscard]] uint64_t add_watchpoint(frontend::debug_address_t start,
                                              uint64_t length,
                                              watch_access_t access);
        [[nodiscard]] bool remove_watchpoint(uint64_t id);
        [[nodiscard]] bool set_watchpoint_enabled(uint64_t id, bool enabled);
        [[nodiscard]] const std::vector<watchpoint_t>& watchpoints() const noexcept;

        [[nodiscard]] bool live_state(live_processor_state_t& state,
                                      std::string& error) const;
        [[nodiscard]] frontend::memory_inspection_result_t inspect_memory(
            frontend::debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept;
        [[nodiscard]] std::optional<analysis::snes::decoded_instruction_t>
            current_instruction(std::string& error) const;
        [[nodiscard]] const std::vector<control_flow_observation_t>&
            control_flow_observations() const noexcept;
        [[nodiscard]] uint64_t observed_call_count() const noexcept;
        [[nodiscard]] uint64_t observed_return_count() const noexcept;

    private:
        enum class operation_t : uint8_t
        {
            none,
            continue_,
            run_to,
            step_over,
            step_out
        };

        [[nodiscard]] bool begin_run(operation_t operation,
                                     std::optional<frontend::debug_address_t> target,
                                     std::string& error);
        [[nodiscard]] bool execute_one(debugger_stop_reason_t manual_reason,
                                       std::string& error);
        [[nodiscard]] bool snapshot(live_processor_state_t& state,
                                    std::string& error) const;
        [[nodiscard]] std::optional<breakpoint_t*> breakpoint_at(
            frontend::debug_address_t address
        );
        [[nodiscard]] std::optional<watchpoint_t*> matching_watchpoint(
            const frontend::memory_access_observation_t& access
        );
        void stop(debugger_stop_reason_t reason,
                  frontend::debug_address_t address,
                  std::string detail = {});
        void observe_control_flow(
            const analysis::snes::decoded_instruction_t& instruction,
            frontend::debug_address_t after
        );

        frontend::debug_target_t* _target{ nullptr };
        frontend::execution_control_t* _execution{ nullptr };
        frontend::observation_control_t* _observations{ nullptr };
        frontend::debug_session_control_t* _session{ nullptr };
        std::unique_ptr<analysis::snes::debug_target_byte_source_t> _source{};
        debugger_run_state_t _run_state{ debugger_run_state_t::stopped };
        operation_t _operation{ operation_t::none };
        std::optional<frontend::debug_address_t> _operation_target{};
        std::optional<frontend::debug_address_t> _skip_breakpoint_once{};
        uint32_t _step_out_depth{ 0 };
        debugger_stop_t _last_stop{};
        std::vector<breakpoint_t> _breakpoints{};
        std::vector<watchpoint_t> _watchpoints{};
        std::vector<control_flow_observation_t> _control_flow{};
        uint64_t _observed_calls{ 0 };
        uint64_t _observed_returns{ 0 };
        uint64_t _next_breakpoint_id{ 1 };
        uint64_t _next_watchpoint_id{ 1 };
        bool _resume_session_on_shutdown{ false };
    };
}
