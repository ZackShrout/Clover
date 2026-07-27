//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/SnesDebugger.h"

#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/SnesEmulatorCore.h"

#include <algorithm>
#include <array>

namespace
{
    [[nodiscard]] bool same_address(
        clover::frontend::debug_address_t left,
        clover::frontend::debug_address_t right
    ) noexcept
    {
        return left.space == right.space && left.value == right.value;
    }

    [[nodiscard]] bool includes_access(
        clover::workbench::watch_access_t configured,
        clover::frontend::memory_access_kind_t actual
    ) noexcept
    {
        const uint8_t mask{ static_cast<uint8_t>(configured) };
        const uint8_t bit{
            actual == clover::frontend::memory_access_kind_t::read
                ? static_cast<uint8_t>(clover::workbench::watch_access_t::read)
                : static_cast<uint8_t>(clover::workbench::watch_access_t::write)
        };
        return (mask & bit) != 0u;
    }

    [[nodiscard]] uint64_t register_value(
        const clover::workbench::live_processor_state_t& state,
        std::string_view stable_id
    ) noexcept
    {
        for (size_t index{ 0 }; index < state.descriptors.size(); ++index)
        {
            if (state.descriptors[index].stable_id == stable_id)
                return state.values[index].value;
        }
        return 0u;
    }
}

namespace clover::workbench
{
    snes_debugger_t::~snes_debugger_t()
    {
        shutdown();
    }

    bool snes_debugger_t::initialize(frontend::debug_target_t& target,
                                     std::string& error)
    {
        shutdown();
        error.clear();
        _breakpoints.clear();
        _watchpoints.clear();
        _control_flow.clear();
        _observed_calls = 0u;
        _observed_returns = 0u;
        _next_breakpoint_id = 1u;
        _next_watchpoint_id = 1u;
        _target = &target;
        _execution = target.execution_control();
        _observations = target.observation_control();
        _session = target.debug_session_control();
        if (_execution == nullptr || _observations == nullptr || _session == nullptr)
        {
            error = "Debug target lacks required run-control capabilities";
            shutdown();
            return false;
        }
        if ((_observations->available_observations()
                & (frontend::k_observe_execution_boundary
                    | frontend::k_observe_memory_access))
            != (frontend::k_observe_execution_boundary
                | frontend::k_observe_memory_access))
        {
            error = "Debug target lacks required observation capabilities";
            shutdown();
            return false;
        }
        _resume_session_on_shutdown =
            _session->debug_session_state() == frontend::debug_session_state_t::running;
        if (_session->pause_debug_session().status
            != frontend::debug_session_transition_status_t::complete)
        {
            error = "Debug target is not running";
            shutdown();
            return false;
        }
        if (!_observations->set_observation_mask(
                frontend::k_observe_execution_boundary
                    | frontend::k_observe_memory_access
            ))
        {
            error = "Unable to enable debugger observations";
            shutdown();
            return false;
        }
        _observations->clear_observations();
        _source = std::make_unique<analysis::snes::debug_target_byte_source_t>(
            target,
            frontend::snes_debug::k_cpu_bus_space,
            frontend::snes_debug::k_canonical_media_space
        );
        _run_state = debugger_run_state_t::stopped;
        live_processor_state_t state{};
        if (!snapshot(state, error))
        {
            shutdown();
            return false;
        }
        stop(
            debugger_stop_reason_t::pause,
            state.instruction_address,
            "Debugger attached"
        );
        return true;
    }

    void snes_debugger_t::shutdown() noexcept
    {
        if (_observations != nullptr)
        {
            _observations->clear_observations();
            static_cast<void>(_observations->set_observation_mask(0u));
        }
        _source.reset();
        _target = nullptr;
        _execution = nullptr;
        _observations = nullptr;
        if (_session != nullptr && _resume_session_on_shutdown)
            static_cast<void>(_session->resume_debug_session());
        _session = nullptr;
        _run_state = debugger_run_state_t::stopped;
        _operation = operation_t::none;
        _operation_target.reset();
        _skip_breakpoint_once.reset();
        _step_out_depth = 0u;
        _resume_session_on_shutdown = false;
    }

    bool snes_debugger_t::is_initialized() const noexcept
    {
        return _target != nullptr;
    }

    debugger_run_state_t snes_debugger_t::run_state() const noexcept
    {
        return _run_state;
    }

    const debugger_stop_t& snes_debugger_t::last_stop() const noexcept
    {
        return _last_stop;
    }

    bool snes_debugger_t::pause(std::string& error)
    {
        error.clear();
        if (!is_initialized())
        {
            error = "Debugger is not initialized";
            return false;
        }
        live_processor_state_t state{};
        if (!snapshot(state, error))
            return false;
        stop(debugger_stop_reason_t::pause, state.instruction_address, "Paused");
        return true;
    }

    bool snes_debugger_t::begin_run(
        operation_t operation,
        std::optional<frontend::debug_address_t> target,
        std::string& error
    )
    {
        error.clear();
        if (!is_initialized())
        {
            error = "Debugger is not initialized";
            return false;
        }
        live_processor_state_t state{};
        if (!snapshot(state, error))
            return false;
        _run_state = debugger_run_state_t::running;
        _operation = operation;
        _operation_target = target;
        _last_stop = {};
        _skip_breakpoint_once = state.instruction_address;
        return true;
    }

    bool snes_debugger_t::resume(std::string& error)
    {
        return begin_run(operation_t::continue_, std::nullopt, error);
    }

    bool snes_debugger_t::run_to(frontend::debug_address_t address,
                                 std::string& error)
    {
        error.clear();
        if (address.space != frontend::snes_debug::k_cpu_bus_space
            || address.value > 0x00ffffffu)
        {
            error = "Run-to address is outside the SNES CPU bus";
            return false;
        }
        return begin_run(operation_t::run_to, address, error);
    }

    bool snes_debugger_t::step_instruction(std::string& error)
    {
        error.clear();
        if (_run_state == debugger_run_state_t::running)
        {
            error = "Pause before stepping";
            return false;
        }
        return execute_one(debugger_stop_reason_t::instruction_step, error);
    }

    bool snes_debugger_t::step_over(std::string& error)
    {
        error.clear();
        if (_run_state == debugger_run_state_t::running)
        {
            error = "Pause before stepping";
            return false;
        }
        const auto instruction{ current_instruction(error) };
        if (!instruction.has_value())
            return false;
        if (instruction->control_flow != analysis::snes::control_flow_kind_t::call)
            return execute_one(debugger_stop_reason_t::step_over, error);
        const frontend::debug_address_t return_address{
            frontend::snes_debug::k_cpu_bus_space,
            analysis::snes::advance_program_address(
                instruction->address,
                instruction->encoded_size
            )
        };
        return begin_run(operation_t::step_over, return_address, error);
    }

    bool snes_debugger_t::step_out(std::string& error)
    {
        error.clear();
        if (_run_state == debugger_run_state_t::running)
        {
            error = "Pause before stepping";
            return false;
        }
        _step_out_depth = 1u;
        return begin_run(operation_t::step_out, std::nullopt, error);
    }

    bool snes_debugger_t::snapshot(live_processor_state_t& state,
                                   std::string& error) const
    {
        if (_target == nullptr)
        {
            error = "Debugger is not initialized";
            return false;
        }
        const auto descriptors{
            _target->processor_registers(frontend::snes_debug::k_main_cpu_domain)
        };
        if (descriptors.empty())
        {
            error = "Main CPU register metadata is unavailable";
            return false;
        }
        state.descriptors.assign(descriptors.begin(), descriptors.end());
        state.values.resize(descriptors.size());
        const frontend::processor_state_result_t result{
            _target->inspect_processor_state(
                frontend::snes_debug::k_main_cpu_domain,
                state.values
            )
        };
        if (result.status != frontend::processor_state_status_t::complete)
        {
            error = "Unable to inspect main CPU state";
            return false;
        }
        state.instruction_address = result.instruction_address;
        const uint8_t status{
            static_cast<uint8_t>(register_value(state, "p"))
        };
        const bool emulation{ register_value(state, "e") != 0u };
        state.decode_context = {
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
        return true;
    }

    bool snes_debugger_t::live_state(live_processor_state_t& state,
                                     std::string& error) const
    {
        error.clear();
        return snapshot(state, error);
    }

    frontend::memory_inspection_result_t snes_debugger_t::inspect_memory(
        frontend::debug_address_t address,
        std::span<std::byte> destination
    ) const noexcept
    {
        if (_target == nullptr)
            return {};
        return _target->inspect_memory(address, destination);
    }

    std::optional<analysis::snes::decoded_instruction_t>
        snes_debugger_t::current_instruction(std::string& error) const
    {
        live_processor_state_t state{};
        if (!snapshot(state, error) || _source == nullptr)
            return std::nullopt;
        return analysis::snes::decode_instruction(
            *_source,
            static_cast<uint32_t>(state.instruction_address.value),
            state.decode_context
        );
    }

    std::optional<breakpoint_t*> snes_debugger_t::breakpoint_at(
        frontend::debug_address_t address
    )
    {
        for (breakpoint_t& breakpoint : _breakpoints)
        {
            if (breakpoint.enabled && same_address(breakpoint.address, address))
                return &breakpoint;
        }
        return std::nullopt;
    }

    std::optional<watchpoint_t*> snes_debugger_t::matching_watchpoint(
        const frontend::memory_access_observation_t& access
    )
    {
        for (watchpoint_t& watchpoint : _watchpoints)
        {
            if (!watchpoint.enabled
                || watchpoint.start.space != access.address.space
                || !includes_access(watchpoint.access, access.kind)
                || access.address.value < watchpoint.start.value)
            {
                continue;
            }
            const uint64_t relative{
                access.address.value - watchpoint.start.value
            };
            if (relative < watchpoint.length)
                return &watchpoint;
        }
        return std::nullopt;
    }

    bool snes_debugger_t::execute_one(debugger_stop_reason_t manual_reason,
                                      std::string& error)
    {
        if (!is_initialized())
        {
            error = "Debugger is not initialized";
            return false;
        }
        const auto instruction{ current_instruction(error) };
        if (!instruction.has_value())
            return false;
        _observations->clear_observations();
        const frontend::execution_step_result_t step{
            _execution->step_execution_domain(
                frontend::snes_debug::k_main_cpu_domain
            )
        };
        if (step.status != frontend::execution_step_status_t::complete)
        {
            error = "Main CPU instruction step failed";
            live_processor_state_t state{};
            static_cast<void>(snapshot(state, error));
            stop(debugger_stop_reason_t::error, state.instruction_address, error);
            return false;
        }

        std::array<frontend::observation_event_t, 1024> events{};
        const frontend::observation_drain_result_t drain{
            _observations->drain_observations(events)
        };
        live_processor_state_t after_state{};
        if (!snapshot(after_state, error))
        {
            stop(debugger_stop_reason_t::error, {}, error);
            return false;
        }
        observe_control_flow(*instruction, after_state.instruction_address);
        if (drain.events_dropped != 0u)
        {
            stop(
                debugger_stop_reason_t::observation_overflow,
                after_state.instruction_address,
                "Memory observation buffer overflowed"
            );
            return true;
        }
        for (size_t index{ 0 }; index < drain.events_written; ++index)
        {
            if (events[index].kind != frontend::observation_kind_t::memory_access)
                continue;
            const auto watchpoint{
                matching_watchpoint(events[index].memory_access)
            };
            if (!watchpoint.has_value())
                continue;
            ++(*watchpoint)->hit_count;
            stop(
                debugger_stop_reason_t::watchpoint,
                after_state.instruction_address,
                "Memory watchpoint hit"
            );
            _last_stop.watchpoint_id = (*watchpoint)->id;
            _last_stop.memory_access = events[index].memory_access;
            return true;
        }
        if (step.boundary == frontend::execution_boundary_t::waiting)
        {
            stop(
                debugger_stop_reason_t::waiting,
                after_state.instruction_address,
                "Processor entered WAI"
            );
            return true;
        }
        if (step.boundary == frontend::execution_boundary_t::stopped)
        {
            stop(
                debugger_stop_reason_t::processor_stopped,
                after_state.instruction_address,
                "Processor entered STP"
            );
            return true;
        }
        if (manual_reason != debugger_stop_reason_t::none)
        {
            stop(manual_reason, after_state.instruction_address);
            return true;
        }
        return true;
    }

    size_t snes_debugger_t::pump(size_t instruction_budget, std::string& error)
    {
        error.clear();
        size_t executed{};
        while (_run_state == debugger_run_state_t::running
               && executed < instruction_budget)
        {
            live_processor_state_t before{};
            if (!snapshot(before, error))
            {
                stop(debugger_stop_reason_t::error, {}, error);
                break;
            }
            const bool skip{
                _skip_breakpoint_once.has_value()
                && same_address(*_skip_breakpoint_once, before.instruction_address)
            };
            _skip_breakpoint_once.reset();
            if (!skip)
            {
                const auto breakpoint{ breakpoint_at(before.instruction_address) };
                if (breakpoint.has_value())
                {
                    ++(*breakpoint)->hit_count;
                    stop(
                        debugger_stop_reason_t::breakpoint,
                        before.instruction_address,
                        "Execution breakpoint hit"
                    );
                    _last_stop.breakpoint_id = (*breakpoint)->id;
                    if ((*breakpoint)->temporary)
                        static_cast<void>(remove_breakpoint((*breakpoint)->id));
                    break;
                }
            }
            if (!execute_one(debugger_stop_reason_t::none, error))
                break;
            ++executed;
            if (_run_state != debugger_run_state_t::running)
                break;

            live_processor_state_t after{};
            if (!snapshot(after, error))
            {
                stop(debugger_stop_reason_t::error, {}, error);
                break;
            }
            bool operation_complete{ false };
            debugger_stop_reason_t reason{ debugger_stop_reason_t::none };
            if ((_operation == operation_t::run_to
                    || _operation == operation_t::step_over)
                && _operation_target.has_value()
                && same_address(after.instruction_address, *_operation_target))
            {
                operation_complete = true;
                reason = _operation == operation_t::run_to
                    ? debugger_stop_reason_t::run_to_cursor
                    : debugger_stop_reason_t::step_over;
            }
            else if (_operation == operation_t::step_out
                     && _step_out_depth == 0u)
            {
                operation_complete = true;
                reason = debugger_stop_reason_t::step_out;
            }
            if (operation_complete)
            {
                stop(reason, after.instruction_address);
                break;
            }
        }
        return executed;
    }

    void snes_debugger_t::stop(debugger_stop_reason_t reason,
                               frontend::debug_address_t address,
                               std::string detail)
    {
        _run_state = debugger_run_state_t::stopped;
        _operation = operation_t::none;
        _operation_target.reset();
        _skip_breakpoint_once.reset();
        _last_stop = {
            .reason = reason,
            .address = address,
            .detail = std::move(detail)
        };
    }

    void snes_debugger_t::observe_control_flow(
        const analysis::snes::decoded_instruction_t& instruction,
        frontend::debug_address_t after
    )
    {
        std::optional<control_flow_observation_kind_t> kind{};
        if (instruction.control_flow == analysis::snes::control_flow_kind_t::call)
        {
            kind = control_flow_observation_kind_t::call;
            ++_observed_calls;
            if (_operation == operation_t::step_out)
                ++_step_out_depth;
        }
        else if (instruction.control_flow
                 == analysis::snes::control_flow_kind_t::return_)
        {
            kind = control_flow_observation_kind_t::return_;
            ++_observed_returns;
            if (_operation == operation_t::step_out && _step_out_depth != 0u)
                --_step_out_depth;
        }
        if (!kind.has_value())
            return;
        _control_flow.push_back({
            .kind = *kind,
            .from = {
                frontend::snes_debug::k_cpu_bus_space,
                instruction.address
            },
            .to = after
        });
        if (_control_flow.size() > 512u)
            _control_flow.erase(_control_flow.begin());
    }

    uint64_t snes_debugger_t::add_breakpoint(
        frontend::debug_address_t address,
        bool temporary
    )
    {
        for (breakpoint_t& breakpoint : _breakpoints)
        {
            if (same_address(breakpoint.address, address))
            {
                breakpoint.temporary = breakpoint.temporary && temporary;
                return breakpoint.id;
            }
        }
        const uint64_t id{ _next_breakpoint_id++ };
        _breakpoints.push_back({
            .id = id,
            .address = address,
            .temporary = temporary
        });
        return id;
    }

    bool snes_debugger_t::remove_breakpoint(uint64_t id)
    {
        const auto found{
            std::find_if(
                _breakpoints.begin(),
                _breakpoints.end(),
                [id](const breakpoint_t& breakpoint)
                {
                    return breakpoint.id == id;
                }
            )
        };
        if (found == _breakpoints.end())
            return false;
        _breakpoints.erase(found);
        return true;
    }

    bool snes_debugger_t::set_breakpoint_enabled(uint64_t id, bool enabled)
    {
        for (breakpoint_t& breakpoint : _breakpoints)
        {
            if (breakpoint.id == id)
            {
                breakpoint.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    const std::vector<breakpoint_t>& snes_debugger_t::breakpoints() const noexcept
    {
        return _breakpoints;
    }

    uint64_t snes_debugger_t::add_watchpoint(
        frontend::debug_address_t start,
        uint64_t length,
        watch_access_t access
    )
    {
        const uint64_t normalized_length{ std::max<uint64_t>(length, 1u) };
        for (const watchpoint_t& watchpoint : _watchpoints)
        {
            if (same_address(watchpoint.start, start)
                && watchpoint.length == normalized_length
                && watchpoint.access == access)
            {
                return watchpoint.id;
            }
        }
        const uint64_t id{ _next_watchpoint_id++ };
        _watchpoints.push_back({
            .id = id,
            .start = start,
            .length = normalized_length,
            .access = access
        });
        return id;
    }

    bool snes_debugger_t::remove_watchpoint(uint64_t id)
    {
        const auto found{
            std::find_if(
                _watchpoints.begin(),
                _watchpoints.end(),
                [id](const watchpoint_t& watchpoint)
                {
                    return watchpoint.id == id;
                }
            )
        };
        if (found == _watchpoints.end())
            return false;
        _watchpoints.erase(found);
        return true;
    }

    bool snes_debugger_t::set_watchpoint_enabled(uint64_t id, bool enabled)
    {
        for (watchpoint_t& watchpoint : _watchpoints)
        {
            if (watchpoint.id == id)
            {
                watchpoint.enabled = enabled;
                return true;
            }
        }
        return false;
    }

    const std::vector<watchpoint_t>& snes_debugger_t::watchpoints() const noexcept
    {
        return _watchpoints;
    }

    const std::vector<control_flow_observation_t>&
        snes_debugger_t::control_flow_observations() const noexcept
    {
        return _control_flow;
    }

    uint64_t snes_debugger_t::observed_call_count() const noexcept
    {
        return _observed_calls;
    }

    uint64_t snes_debugger_t::observed_return_count() const noexcept
    {
        return _observed_returns;
    }
}
