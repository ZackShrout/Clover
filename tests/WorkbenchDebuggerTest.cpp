//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/SnesDebugger.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_debug_rom()
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0xeau });
        const uint8_t program[]{
            0xa9u, 0x42u,             // $8000 LDA #$42
            0x8du, 0x00u, 0x21u,      // $8002 STA $2100
            0x20u, 0x10u, 0x80u,      // $8005 JSR $8010
            0xeau,                     // $8008 NOP
            0x80u, 0xfeu               // $8009 BRA $8009
        };
        for (size_t index{ 0 }; index < std::size(program); ++index)
            rom[index] = static_cast<std::byte>(program[index]);
        rom[0x10u] = std::byte{ 0xe8u }; // $8010 INX
        rom[0x11u] = std::byte{ 0x60u }; // $8011 RTS

        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x02u };
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint, const std::string& error = {})
    {
        std::fprintf(
            stderr,
            "WorkbenchDebuggerTest failed at %s%s%s\n",
            checkpoint,
            error.empty() ? "" : ": ",
            error.c_str()
        );
        return 1;
    }
}

int main()
{
    using namespace clover;
    using namespace clover::workbench;

    std::unique_ptr<frontend::emulator_core_t> emulator{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    const std::vector<std::byte> rom{ make_debug_rom() };
    if (emulator == nullptr || !emulator->load_media(rom))
        return fail("load");
    emulator->power_on();

    frontend::debug_target_t* const target{ emulator->debug_target() };
    std::string error{};
    snes_debugger_t debugger{};
    if (target == nullptr || !debugger.initialize(*target, error))
        return fail("initialize", error);

    live_processor_state_t state{};
    if (!debugger.live_state(state, error)
        || state.instruction_address.value != 0x008000u
        || state.decode_context.emulation
            != analysis::snes::bit_state_t::set
        || state.decode_context.accumulator_width
            != analysis::snes::bit_state_t::set
        || state.decode_context.index_width
            != analysis::snes::bit_state_t::set)
    {
        return fail("runtime_decode_context", error);
    }

    if (!debugger.step_instruction(error)
        || debugger.last_stop().reason
            != debugger_stop_reason_t::instruction_step
        || debugger.last_stop().address.value != 0x008002u)
    {
        return fail("instruction_step", error);
    }

    const uint64_t hardware_watch{
        debugger.add_watchpoint(
            { frontend::snes_debug::k_cpu_bus_space, 0x002100u },
            1u,
            watch_access_t::write
        )
    };
    if (!debugger.resume(error)
        || debugger.pump(8u, error) != 1u
        || debugger.last_stop().reason != debugger_stop_reason_t::watchpoint
        || debugger.last_stop().watchpoint_id != hardware_watch
        || !debugger.last_stop().memory_access.has_value()
        || debugger.last_stop().memory_access->address.value != 0x002100u
        || debugger.last_stop().memory_access->instruction_address.value
            != 0x008002u
        || debugger.last_stop().memory_access->value != 0x42u)
    {
        return fail("hardware_register_watchpoint", error);
    }
    if (!debugger.remove_watchpoint(hardware_watch))
        return fail("remove_hardware_watchpoint");

    if (!debugger.step_over(error)
        || debugger.run_state() != debugger_run_state_t::running)
    {
        return fail("begin_step_over", error);
    }
    const size_t over_steps{ debugger.pump(16u, error) };
    if (over_steps != 3u
        || debugger.last_stop().reason != debugger_stop_reason_t::step_over
        || debugger.last_stop().address.value != 0x008008u
        || debugger.observed_call_count() != 1u
        || debugger.observed_return_count() != 1u
        || debugger.control_flow_observations().size() != 2u)
    {
        return fail("step_over_and_call_return_observation", error);
    }

    const uint64_t loop_breakpoint{
        debugger.add_breakpoint(
            { frontend::snes_debug::k_cpu_bus_space, 0x008009u }
        )
    };
    if (!debugger.resume(error)
        || debugger.pump(8u, error) != 1u
        || debugger.last_stop().reason != debugger_stop_reason_t::breakpoint
        || debugger.last_stop().breakpoint_id != loop_breakpoint
        || debugger.breakpoints().front().hit_count != 1u)
    {
        return fail("execution_breakpoint", error);
    }
    if (!debugger.remove_breakpoint(loop_breakpoint))
        return fail("remove_breakpoint");

    emulator->reset();
    if (!debugger.run_to(
            { frontend::snes_debug::k_cpu_bus_space, 0x008010u },
            error
        )
        || debugger.pump(16u, error) != 3u
        || debugger.last_stop().reason
            != debugger_stop_reason_t::run_to_cursor
        || debugger.last_stop().address.value != 0x008010u)
    {
        return fail("run_to_cursor", error);
    }

    if (!debugger.step_out(error)
        || debugger.pump(16u, error) != 2u
        || debugger.last_stop().reason != debugger_stop_reason_t::step_out
        || debugger.last_stop().address.value != 0x008008u)
    {
        return fail("step_out", error);
    }

    emulator->reset();
    const uint64_t opcode_read_watch{
        debugger.add_watchpoint(
            { frontend::snes_debug::k_cpu_bus_space, 0x008000u },
            1u,
            watch_access_t::read
        )
    };
    if (!debugger.resume(error)
        || debugger.pump(4u, error) != 1u
        || debugger.last_stop().reason != debugger_stop_reason_t::watchpoint
        || debugger.last_stop().watchpoint_id != opcode_read_watch)
    {
        return fail("read_watchpoint", error);
    }

    std::array<std::byte, 8> memory{};
    const frontend::memory_inspection_result_t memory_result{
        debugger.inspect_memory(
            { frontend::snes_debug::k_cpu_bus_space, 0x008000u },
            memory
        )
    };
    if (memory_result.status != frontend::memory_inspection_status_t::complete
        || memory_result.bytes_read != memory.size()
        || memory[0] != std::byte{ 0xa9u }
        || memory[2] != std::byte{ 0x8du })
    {
        return fail("live_memory");
    }

    frontend::debug_session_control_t* const session{
        target->debug_session_control()
    };
    if (session == nullptr
        || session->debug_session_state()
            != frontend::debug_session_state_t::paused)
    {
        return fail("debugger_owns_pause");
    }
    debugger.shutdown();
    if (session->debug_session_state()
        != frontend::debug_session_state_t::running)
    {
        return fail("shutdown_restores_session");
    }

    std::printf(
        "Workbench debugger tests passed: runtime state, stepping, "
        "breakpoints, watchpoints, run-to, and call/return observation\n"
    );
    return 0;
}
