//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"
#include "clover/analysis/snes/HybridAnalyzer.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/DebugTarget.h"
#include "clover/workbench/Debugger.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace clover::workbench
{
    class snes_debugger_t final : public debugger_t
    {
    public:
        snes_debugger_t() = default;
        ~snes_debugger_t() override;

        snes_debugger_t(const snes_debugger_t&) = delete;
        snes_debugger_t& operator=(const snes_debugger_t&) = delete;

        [[nodiscard]] bool initialize(frontend::debug_target_t& target,
                                      std::string& error,
                                      std::string analysis_session = {}) override;
        void shutdown() noexcept override;
        [[nodiscard]] bool is_initialized() const noexcept override;
        [[nodiscard]] debugger_run_state_t run_state() const noexcept override;
        [[nodiscard]] const debugger_stop_t& last_stop() const noexcept override;

        [[nodiscard]] bool pause(std::string& error) override;
        [[nodiscard]] bool resume(std::string& error) override;
        [[nodiscard]] bool step_instruction(std::string& error) override;
        [[nodiscard]] bool step_over(std::string& error) override;
        [[nodiscard]] bool step_out(std::string& error) override;
        [[nodiscard]] bool run_to(frontend::debug_address_t address,
                                  std::string& error) override;
        [[nodiscard]] size_t pump(size_t instruction_budget,
                                  std::string& error) override;
        [[nodiscard]] size_t pump_fast(size_t instruction_budget,
                                       std::string& error) override;

        [[nodiscard]] uint64_t add_breakpoint(frontend::debug_address_t address,
                                              bool temporary = false) override;
        [[nodiscard]] bool remove_breakpoint(uint64_t id) override;
        [[nodiscard]] bool set_breakpoint_enabled(uint64_t id,
                                                  bool enabled) override;
        [[nodiscard]] const std::vector<breakpoint_t>&
            breakpoints() const noexcept override;

        [[nodiscard]] uint64_t add_watchpoint(frontend::debug_address_t start,
                                              uint64_t length,
                                              watch_access_t access) override;
        [[nodiscard]] bool remove_watchpoint(uint64_t id) override;
        [[nodiscard]] bool set_watchpoint_enabled(uint64_t id,
                                                  bool enabled) override;
        [[nodiscard]] const std::vector<watchpoint_t>&
            watchpoints() const noexcept override;

        [[nodiscard]] bool live_state(live_processor_state_t& state,
                                      std::string& error) const override;
        [[nodiscard]] frontend::memory_inspection_result_t inspect_memory(
            frontend::debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept override;
        [[nodiscard]] std::optional<analysis::snes::decoded_instruction_t>
            current_instruction(std::string& error) const;
        [[nodiscard]] const std::vector<control_flow_observation_t>&
            control_flow_observations() const noexcept;
        [[nodiscard]] uint64_t observed_call_count() const noexcept;
        [[nodiscard]] uint64_t observed_return_count() const noexcept;
        [[nodiscard]] const std::vector<analysis::snes::runtime_edge_t>&
            runtime_edges() const noexcept;
        [[nodiscard]] uint64_t dropped_runtime_edges() const noexcept;
        [[nodiscard]] const std::string& analysis_session() const noexcept;
        void clear_runtime_evidence() noexcept;

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
                                       std::string& error,
                                       const live_processor_state_t* known_before = nullptr,
                                       live_processor_state_t* resulting_after = nullptr);
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
        void observe_runtime_edge(
            const live_processor_state_t& before,
            const live_processor_state_t& after
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
        std::vector<analysis::snes::runtime_edge_t> _runtime_edges{};
        std::unordered_map<std::string, size_t> _runtime_edge_indexes{};
        std::string _analysis_session{};
        uint64_t _dropped_runtime_edges{ 0 };
        uint64_t _observed_calls{ 0 };
        uint64_t _observed_returns{ 0 };
        uint64_t _next_breakpoint_id{ 1 };
        uint64_t _next_watchpoint_id{ 1 };
        bool _resume_session_on_shutdown{ false };
    };
}
