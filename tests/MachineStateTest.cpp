//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Apu.h"
#include "clover/core/snes/Bus.h"
#include "clover/core/snes/Cartridge.h"
#include "clover/core/snes/Console.h"
#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/Dma.h"
#include "clover/core/snes/Interrupts.h"
#include "clover/core/snes/Ppu.h"
#include "clover/core/snes/Scheduler.h"

#include <cstdio>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "MachineStateTest failed at %s\n", checkpoint);
        return 1;
    }

    [[nodiscard]] std::vector<std::byte> make_lorom(
        uint8_t cartridge_type,
        std::string_view title = {}
    )
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0 });
        constexpr size_t header{ 0x7fc0u };
        for (size_t index{ 0 }; index < title.size() && index < 21u; ++index)
            rom[header + index] = static_cast<std::byte>(title[index]);
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = static_cast<std::byte>(cartridge_type);
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }
}

int main()
{
    using namespace clover::core;

    static_assert(scheduler_causal_state_t::schema_version == 1);
    static_assert(cpu_causal_state_t::schema_version == 1);
    static_assert(dma_causal_state_t::schema_version == 1);
    static_assert(interrupt_controller_causal_state_t::schema_version == 1);
    static_assert(bus_causal_state_t::schema_version == 1);
    static_assert(cartridge_causal_state_t::schema_version == 2);
    static_assert(ppu_causal_state_t::schema_version == 1);
    static_assert(apu_causal_state_t::schema_version == 1);
    static_assert(console_causal_state_t::schema_version == 1);

    scheduler_t scheduler{};
    const scheduler_causal_state_t scheduler_expected{
        .master_clock = 0x1234'5678u,
        .frame_index = 91u,
    };
    scheduler.restore_causal_state(scheduler_expected);
    if (scheduler.capture_causal_state() != scheduler_expected)
        return fail("scheduler_round_trip");

    interrupt_controller_t interrupts{};
    interrupts.reset();
    interrupt_controller_causal_state_t interrupt_expected{};
    interrupt_expected.state = {
        .nmi_line = true,
        .nmi_hold = true,
        .nmi_transition = true,
        .irq_line = true,
        .irq_hold = true,
        .irq_transition = true,
        .nmi_pending = true,
        .irq_pending = true,
        .irq_lock = true,
    };
    interrupt_expected.cpu_irq_line = true;
    interrupt_expected.cartridge_irq_line = false;
    interrupt_expected.nmi_transition_clock = 0x1010u;
    interrupt_expected.irq_transition_clock = 0x2020u;
    if (!interrupts.restore_causal_state(interrupt_expected)
        || interrupts.capture_causal_state() != interrupt_expected)
    {
        return fail("interrupt_round_trip");
    }
    auto invalid_interrupts{ interrupt_expected };
    invalid_interrupts.state.irq_line = false;
    if (interrupts.restore_causal_state(invalid_interrupts)
        || interrupts.capture_causal_state() != interrupt_expected)
    {
        return fail("interrupt_reject_atomic");
    }

    dma_t dma{};
    dma.reset();
    dma_causal_state_t dma_expected{ dma.capture_causal_state() };
    dma_expected.channels[3] = {
        .dma_enabled = true,
        .hdma_enabled = true,
        .hdma_active = true,
        .hdma_completed = false,
        .hdma_do_transfer = true,
        .control = 0x87u,
        .target_address = 0x18u,
        .source_address = 0x4567u,
        .source_bank = 0x7eu,
        .indirect_bank = 0x7fu,
        .indirect_address = 0x2345u,
        .hdma_table_address = 0x3456u,
        .line_counter = 0x82u,
        .unused = 0xa5u,
        .transfer_units = 4u,
        .transfer_size = 0x3210u,
    };
    dma_expected.pending_general_dma_mask = 0x08u;
    dma_expected.pending_hdma_setup_mask = 0x18u;
    dma_expected.pending_hdma_transfer_mask = 0x88u;
    dma_expected.activity = dma_activity_t::general_dma;
    dma_expected.active_channel_index = 3u;
    dma_expected.substep = dma_substep_t::general_transfer;
    dma_expected.alignment_pending = true;
    dma_expected.general_dma_batch_started = true;
    dma_expected.cpu_bus_cycle_clocks = 8u;
    dma_expected.dma_counter = 0x111u;
    dma_expected.general_dma_units_remaining = 0x12345u;
    dma_expected.general_dma_transfer_index = 2u;
    dma_expected.hdma_transfer_index = 3u;
    dma_expected.hdma_reload_pending = true;
    dma_expected.general_dma_suspended = true;
    dma_expected.suspended_general_dma_channel_index = 2u;
    dma_expected.suspended_general_dma_substep = dma_substep_t::general_setup;
    dma_expected.suspended_general_dma_alignment_pending = true;
    dma_expected.suspended_general_dma_batch_started = true;
    dma_expected.suspended_general_dma_units_remaining = 0x54321u;
    dma_expected.suspended_general_dma_transfer_index = 1u;
    if (!dma.restore_causal_state(dma_expected)
        || dma.capture_causal_state() != dma_expected)
    {
        return fail("dma_round_trip");
    }
    auto invalid_dma{ dma_expected };
    invalid_dma.active_channel_index = 8u;
    if (dma.restore_causal_state(invalid_dma)
        || dma.capture_causal_state() != dma_expected)
    {
        return fail("dma_reject_channel_atomic");
    }
    invalid_dma = dma_expected;
    invalid_dma.substep = static_cast<dma_substep_t>(0xffu);
    if (dma.restore_causal_state(invalid_dma)
        || dma.capture_causal_state() != dma_expected)
    {
        return fail("dma_reject_substep_atomic");
    }

    cpu_t cpu{};
    cpu.power_on();
    cpu_causal_state_t cpu_expected{ cpu.capture_causal_state() };
    cpu_expected.registers = {
        .pc = 0x8123u,
        .sp = 0x01f0u,
        .a = 0x1234u,
        .x = 0x2345u,
        .y = 0x3456u,
        .d = 0x4567u,
        .p = 0x85u,
        .db = 0x7eu,
        .pb = 0x80u,
        .emulation_mode = false,
    };
    cpu_expected.io = {
        .auto_joypad_poll = true,
        .hirq_enabled = true,
        .virq_enabled = true,
        .nmi_enabled = true,
        .irq_enabled = true,
        .nmi_flag = true,
        .irq_flag = true,
        .in_hblank = true,
        .in_vblank = true,
        .fast_rom_enabled = true,
        .controller_port_1_latch = true,
        .controller_port_1_shift_count = 7u,
        .controller_port_2_shift_count = 8u,
        .nmi_hold_clocks = 3u,
        .irq_hold_clocks = 2u,
        .pio = 0xa5u,
        .multiply_a = 0x12u,
        .multiply_b = 0x34u,
        .dividend = 0x5678u,
        .divisor = 0x9au,
        .quotient = 0x1111u,
        .multiply_or_remainder = 0x2222u,
        .auto_joypad_busy_clocks = 0x333u,
        .auto_joypad_latched_1 = 0x4444u,
        .auto_joypad_latched_2 = 0x5555u,
        .joy1 = 0x6666u,
        .joy2 = 0x7777u,
        .joy3 = 0x8888u,
        .joy4 = 0x9999u,
        .htime = 0x100u,
        .vtime = 0x101u,
        .wram_address = 0x01'2345u,
    };
    cpu_expected.master_clock = 0x10000u;
    cpu_expected.dma_counter = 0x10008u;
    cpu_expected.counter = {
        .master_clock = 0x10000u,
        .scanline = 100u,
        .dot = 500u,
        .odd_field = true,
    };
    cpu_expected.interrupt_poll_phase = 3u;
    cpu_expected.last_timing = {
        .master_clock = 0x10000u,
        .raster = { .scanline = 100u, .dot = 500u },
        .in_hblank = false,
        .in_vblank = false,
    };
    cpu_expected.last_irq_timing = {
        .master_clock = 0xfff6u,
        .raster = { .scanline = 100u, .dot = 490u },
        .in_hblank = false,
        .in_vblank = false,
    };
    cpu_expected.last_irq_gate_timing = {
        .master_clock = 0xfffau,
        .raster = { .scanline = 100u, .dot = 494u },
        .in_hblank = false,
        .in_vblank = false,
    };
    cpu_expected.irq_condition_valid = true;
    cpu_expected.nmi_poll_valid = true;
    cpu_expected.dma_active = true;
    cpu_expected.reset_pending = false;
    cpu_expected.waiting = false;
    cpu_expected.wait_wake_idle_pending = true;
    cpu_expected.stopped = false;
    cpu_expected.visible_scanlines = k_pal_video_timing.overscan_visible_scanlines;
    cpu_expected.video_timing = k_pal_video_timing;
    cpu_expected.cpu_version = 1u;
    cpu_expected.interlace = true;
    cpu_expected.dram_refresh_dot = 535u;
    cpu_expected.dram_refresh_pending = false;
    cpu_expected.hdma_setup_dot = 15u;
    cpu_expected.hdma_setup_pending = false;
    cpu_expected.multiply_counter = 4u;
    cpu_expected.divide_counter = 8u;
    cpu_expected.math_shift = 0x1234'5678u;
    cpu_expected.controller_state = { 0xa55au, 0x5aa5u };
    if (!cpu.restore_causal_state(cpu_expected)
        || cpu.capture_causal_state() != cpu_expected)
    {
        return fail("cpu_round_trip");
    }
    auto invalid_cpu{ cpu_expected };
    invalid_cpu.video_timing.standard = static_cast<video_standard_t>(0xffu);
    if (cpu.restore_causal_state(invalid_cpu)
        || cpu.capture_causal_state() != cpu_expected)
    {
        return fail("cpu_reject_standard_atomic");
    }
    invalid_cpu = cpu_expected;
    invalid_cpu.counter.scanline = invalid_cpu.video_timing.scanlines_per_frame;
    if (cpu.restore_causal_state(invalid_cpu)
        || cpu.capture_causal_state() != cpu_expected)
    {
        return fail("cpu_reject_raster_atomic");
    }
    invalid_cpu = cpu_expected;
    invalid_cpu.waiting = true;
    if (cpu.restore_causal_state(invalid_cpu)
        || cpu.capture_causal_state() != cpu_expected)
    {
        return fail("cpu_reject_wait_state_atomic");
    }

    static bus_t bus{};
    static apu_t bus_apu{};
    static cartridge_t bus_cartridge{};
    static cpu_t bus_cpu{};
    static dma_t bus_dma{};
    static ppu_t bus_ppu{};
    bus.connect_apu(bus_apu);
    bus.connect_cartridge(bus_cartridge);
    bus.connect_cpu(bus_cpu);
    bus.connect_dma(bus_dma);
    bus.connect_ppu(bus_ppu);
    bus_apu.power_on();
    bus_cartridge.reset();
    bus_cpu.power_on();
    bus_dma.reset();
    bus_ppu.power_on();
    bus.power_on();
    bus.set_entropy_mode(startup_entropy_mode_t::high);
    bus.set_entropy_seed(0x1234'5678u, 0x9abc'def0u);
    bus.write_u8(0x7e1234u, 0x42u);
    bus.write_cpu_u8(0x002100u, 0x0fu, 4u);
    bus.write_cpu_u8(0x002140u, 0x5au, 4u);
    bus.write_cpu_u8(0x00420du, 0x01u, 0u);
    bus.write_cpu_u8(0x004300u, 0x87u, 0u);
    bus_apu.step(3u);
    bus.synchronize_apu_io_access(3u);
    static_cast<void>(bus.read_u8(0x7e1234u));
    bus.set_apu_port_trace_enabled(true);
    bus.trace_cpu_apu_port_access(0x002140u, 0x5au, true, 4u);

    const bus_causal_state_t bus_expected{ bus.capture_causal_state() };
    if (bus_expected.open_bus != 0x42u
        || bus_expected.wram[0x1234u] != 0x42u
        || bus_expected.pending_cpu_write_count != 2u
        || bus_expected.pending_ppu_write_count != 1u
        || bus_expected.pending_apu_write_count != 1u
        || bus_expected.apu_progressed_cpu_clocks != 3u
        || bus.apu_port_trace_count() != 1u)
    {
        return fail("bus_capture_contents");
    }

    bus.write_u8(0x002100u, 0x80u);
    bus.write_u8(0x00420cu, 0x00u);
    if (bus.ppu_register_write_trace_count() != 0u
        || bus.system_register_write_trace_count() != 0u
        || bus.watched_write_trace_count() != 0u)
    {
        return fail("bus_legacy_trace_default_off");
    }
    bus.set_legacy_trace_enabled(true);
    bus.power_on();
    bus.write_u8(0x7e0daeu, 0x33u);
    bus.write_u8(0x002100u, 0x80u);
    bus.write_u8(0x00420cu, 0x00u);
    bus.trace_cpu_apu_port_access(0x002140u, 0x11u, true, 0u);
    if (bus.ppu_register_write_trace_count() == 0u
        || bus.system_register_write_trace_count() == 0u
        || bus.watched_write_trace_count() == 0u
        || bus.apu_port_trace_count() == 0u)
    {
        return fail("bus_trace_setup");
    }
    if (!bus.restore_causal_state(bus_expected)
        || bus.capture_causal_state() != bus_expected)
    {
        return fail("bus_round_trip");
    }
    if (bus.ppu_register_write_trace_count() != 0u
        || bus.system_register_write_trace_count() != 0u
        || bus.watched_write_trace_count() != 0u
        || bus.apu_port_trace_count() != 0u)
    {
        return fail("bus_restore_clears_trace_timeline");
    }
    bus.trace_cpu_apu_port_access(0x002140u, 0x22u, true, 0u);
    if (bus.apu_port_trace_count() != 1u)
        return fail("bus_restore_preserves_trace_policy");

    auto noncanonical_bus{ bus_expected };
    noncanonical_bus.pending_cpu_writes[15] = {
        .address = 0x00ffffffu,
        .value = 0xffu,
    };
    if (!bus.restore_causal_state(noncanonical_bus)
        || bus.capture_causal_state() != bus_expected)
    {
        return fail("bus_restore_normalizes_inactive_queue_entries");
    }

    auto invalid_bus{ bus_expected };
    invalid_bus.pending_ppu_write_count = 17u;
    if (bus.restore_causal_state(invalid_bus)
        || bus.capture_causal_state() != bus_expected)
    {
        return fail("bus_reject_queue_count_atomic");
    }
    invalid_bus = bus_expected;
    invalid_bus.entropy_mode = static_cast<startup_entropy_mode_t>(0xffu);
    if (bus.restore_causal_state(invalid_bus)
        || bus.capture_causal_state() != bus_expected)
    {
        return fail("bus_reject_entropy_atomic");
    }

    static_cast<void>(bus.step_ppu_with_cpu_writes(8u));
    bus.step_apu_with_cpu_writes(8u);
    const auto ppu_render_state{ bus_ppu.render_state_snapshot() };
    if (ppu_render_state.display_disabled
        || ppu_render_state.brightness != 0x0fu
        || !bus_cpu.capture_causal_state().io.fast_rom_enabled
        || bus_dma.read_register(0x4300u) != 0x87u
        || bus_apu.read_input_port(0u) != 0x5au)
    {
        return fail("bus_restored_pending_writes_continue");
    }

    static ppu_t checkpoint_ppu{};
    static ppu_causal_state_t ppu_expected{};
    static ppu_causal_state_t ppu_actual{};
    static ppu_causal_state_t invalid_ppu{};
    checkpoint_ppu.configure_hardware(k_pal_video_timing, 2u, 4u);
    checkpoint_ppu.set_entropy_mode(ppu_entropy_mode_t::high);
    checkpoint_ppu.set_entropy_seed(0x1234'5678u, 0x9abc'def0u);
    checkpoint_ppu.power_on();
    checkpoint_ppu.write_register(0x2102u, 0u);
    checkpoint_ppu.write_register(0x2103u, 0u);
    checkpoint_ppu.write_register(0x2104u, 0u);
    checkpoint_ppu.write_register(0x2104u, 0u);
    checkpoint_ppu.write_register(0x2121u, 0u);
    checkpoint_ppu.write_register(0x2122u, 0u);
    checkpoint_ppu.write_register(0x2122u, 0u);
    if (checkpoint_ppu.cgram_write_trace_count() != 0u
        || checkpoint_ppu.oam_write_trace_count() != 0u)
    {
        return fail("ppu_legacy_trace_default_off");
    }
    checkpoint_ppu.power_on();
    checkpoint_ppu.set_cgram_write_trace_start_frame(0u);
    checkpoint_ppu.set_oam_write_trace_start_frame(0u);
    checkpoint_ppu.write_register(0x2100u, 0x0du);
    checkpoint_ppu.write_register(0x2101u, 0xbbu);
    checkpoint_ppu.write_register(0x2105u, 0xd9u);
    checkpoint_ppu.write_register(0x2106u, 0x93u);
    checkpoint_ppu.write_register(0x2115u, 0x85u);
    checkpoint_ppu.write_register(0x2116u, 0x34u);
    checkpoint_ppu.write_register(0x2117u, 0x12u);
    checkpoint_ppu.write_register(0x2118u, 0x5au);
    checkpoint_ppu.write_register(0x2119u, 0xa5u);
    checkpoint_ppu.write_register(0x2102u, 0x40u);
    checkpoint_ppu.write_register(0x2103u, 0x81u);
    checkpoint_ppu.write_register(0x2104u, 0x66u);
    checkpoint_ppu.write_register(0x2104u, 0x99u);
    checkpoint_ppu.write_register(0x2121u, 0x2au);
    checkpoint_ppu.write_register(0x2122u, 0x34u);
    checkpoint_ppu.write_register(0x2122u, 0x52u);
    checkpoint_ppu.write_register(0x2123u, 0xe4u);
    checkpoint_ppu.write_register(0x212au, 0x93u);
    checkpoint_ppu.write_register(0x212bu, 0x0eu);
    checkpoint_ppu.write_register(0x2130u, 0xb2u);
    checkpoint_ppu.write_register(0x2131u, 0xf7u);
    checkpoint_ppu.write_register(0x2132u, 0xffu);
    checkpoint_ppu.write_register(0x2133u, 0x49u);
    static_cast<void>(checkpoint_ppu.step(4096u));
    checkpoint_ppu.latch_counters_external();
    static_cast<void>(checkpoint_ppu.read_register(0x213bu, 0u));
    static_cast<void>(checkpoint_ppu.read_register(0x213cu, 0u));
    checkpoint_ppu.capture_causal_state(ppu_expected);

    checkpoint_ppu.write_register(0x2100u, 0x80u);
    checkpoint_ppu.write_register(0x2121u, 0x2au);
    checkpoint_ppu.write_register(0x2122u, 0x00u);
    checkpoint_ppu.write_register(0x2122u, 0x00u);
    checkpoint_ppu.write_register(0x2102u, 0x40u);
    checkpoint_ppu.write_register(0x2103u, 0x01u);
    checkpoint_ppu.write_register(0x2104u, 0u);
    checkpoint_ppu.write_register(0x2104u, 0u);
    if (checkpoint_ppu.cgram_write_trace_count() == 0u
        || checkpoint_ppu.oam_write_trace_count() == 0u
        || !checkpoint_ppu.restore_causal_state(ppu_expected))
    {
        return fail("ppu_restore");
    }
    checkpoint_ppu.capture_causal_state(ppu_actual);
    if (ppu_actual != ppu_expected
        || checkpoint_ppu.cgram_write_trace_count() != 0u
        || checkpoint_ppu.oam_write_trace_count() != 0u)
    {
        return fail("ppu_round_trip_and_trace_reset");
    }

    invalid_ppu = ppu_expected;
    invalid_ppu.pipeline_state.next_object_fetch_index = 35u;
    if (checkpoint_ppu.restore_causal_state(invalid_ppu))
        return fail("ppu_reject_pipeline_index");
    checkpoint_ppu.capture_causal_state(ppu_actual);
    if (ppu_actual != ppu_expected)
        return fail("ppu_reject_pipeline_index_atomic");

    invalid_ppu = ppu_expected;
    invalid_ppu.cgram[0] = 0xffffu;
    if (checkpoint_ppu.restore_causal_state(invalid_ppu))
        return fail("ppu_reject_cgram_value");
    checkpoint_ppu.capture_causal_state(ppu_actual);
    if (ppu_actual != ppu_expected)
        return fail("ppu_reject_cgram_value_atomic");

    static apu_t checkpoint_apu{};
    static apu_t replay_apu{};
    static apu_causal_state_t apu_expected{};
    static apu_causal_state_t apu_actual{};
    static apu_causal_state_t invalid_apu{};
    checkpoint_apu.configure_master_clock(
        master_clock_frequency_hz(video_standard_t::pal));
    checkpoint_apu.power_on();
    checkpoint_apu.begin_audio_frame();
    checkpoint_apu.write_cpu_port(0u, 0xccu);
    checkpoint_apu.write_cpu_port(1u, 0x33u);
    checkpoint_apu.write_output_port(2u, 0x5au);
    checkpoint_apu.step(50'000u);
    if (checkpoint_apu.capture_causal_state(apu_expected)
            != apu_causal_state_result_t::success
        || apu_expected.master_clock != 50'000u
        || apu_expected.cpu_to_apu_ports[0] != 0xccu
        || apu_expected.apu_to_cpu_ports[2] != 0x5au
        || apu_expected.audio_output.dsp_output.primary_sample_count == 0)
    {
        return fail("apu_capture");
    }

    checkpoint_apu.write_cpu_port(0u, 0u);
    checkpoint_apu.write_output_port(2u, 0u);
    checkpoint_apu.step(20'000u);
    const apu_causal_state_result_t apu_restore_result{
        checkpoint_apu.restore_causal_state(apu_expected)
    };
    if (apu_restore_result != apu_causal_state_result_t::success)
    {
        return fail("apu_restore");
    }
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != apu_expected
        || checkpoint_apu.instruction_trace_count() != 0u
        || checkpoint_apu.io_trace_count() != 0u)
    {
        return fail("apu_round_trip_and_trace_reset");
    }

    invalid_apu = apu_expected;
    invalid_apu.instruction_context.active = true;
    invalid_apu.instruction_context.abort_requested = true;
    invalid_apu.instruction_context.start_registers = invalid_apu.registers;
    invalid_apu.instruction_context.start_current_opcode_pc =
        invalid_apu.current_opcode_pc;
    invalid_apu.instruction_context.start_last_opcode = invalid_apu.last_opcode;
    invalid_apu.instruction_context.access_count = 1u;
    invalid_apu.instruction_context.replay_cursor = 1u;
    invalid_apu.instruction_context.accesses[0].awaiting_cpu_sync = true;
    invalid_apu.smp_suspended_for_cpu = true;
    if (checkpoint_apu.restore_causal_state(invalid_apu)
            != apu_causal_state_result_t::success)
    {
        return fail("apu_instruction_journal_restore");
    }
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != invalid_apu)
        return fail("apu_instruction_journal_round_trip");
    if (checkpoint_apu.restore_causal_state(apu_expected)
            != apu_causal_state_result_t::success)
    {
        return fail("apu_restore_after_instruction_journal");
    }

    if (replay_apu.capture_causal_state(apu_actual)
            != apu_causal_state_result_t::uninitialized_dsp)
    {
        return fail("apu_uninitialized_capture_rejected");
    }
    if (replay_apu.restore_causal_state(apu_expected)
            != apu_causal_state_result_t::success)
    {
        return fail("apu_restore_into_uninitialized_instance");
    }
    checkpoint_apu.step(25'000u);
    replay_apu.step(25'000u);
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    static_cast<void>(replay_apu.capture_causal_state(invalid_apu));
    if (apu_actual != invalid_apu)
        return fail("apu_continuation_equivalence");

    if (checkpoint_apu.restore_causal_state(apu_expected)
            != apu_causal_state_result_t::success)
    {
        return fail("apu_restore_before_rejection");
    }
    invalid_apu = apu_expected;
    invalid_apu.io.internal_wait_states = 4u;
    if (checkpoint_apu.restore_causal_state(invalid_apu)
            != apu_causal_state_result_t::invalid_state)
    {
        return fail("apu_reject_wait_state");
    }
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != apu_expected)
        return fail("apu_reject_wait_state_atomic");

    invalid_apu = apu_expected;
    invalid_apu.dsp_state.fill(0u);
    if (checkpoint_apu.restore_causal_state(invalid_apu)
            != apu_causal_state_result_t::invalid_dsp_state)
    {
        return fail("apu_reject_dsp_state");
    }
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != apu_expected)
        return fail("apu_reject_dsp_state_atomic");

    invalid_apu = apu_expected;
    invalid_apu.audio_output.dsp_output.primary_sample_count = 1;
    if (checkpoint_apu.restore_causal_state(invalid_apu)
            != apu_causal_state_result_t::invalid_audio_output)
    {
        return fail("apu_reject_audio_output");
    }
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != apu_expected)
        return fail("apu_reject_audio_output_atomic");

    bus_t active_window_bus{};
    checkpoint_apu.begin_cpu_io_window(active_window_bus, 8u);
    if (checkpoint_apu.capture_causal_state(apu_actual)
            != apu_causal_state_result_t::active_cpu_io_window
        || checkpoint_apu.restore_causal_state(apu_expected)
            != apu_causal_state_result_t::active_cpu_io_window
        || apu_actual != apu_expected)
    {
        return fail("apu_reject_active_cpu_io_window");
    }
    checkpoint_apu.end_cpu_io_window();
    static_cast<void>(checkpoint_apu.capture_causal_state(apu_actual));
    if (apu_actual != apu_expected)
        return fail("apu_active_cpu_io_window_rejection_atomic");

    cartridge_t bootstrap_cartridge{};
    bootstrap_cartridge.write_u8(0x008123u, 0x5au);
    cartridge_causal_state_t bootstrap_state{};
    if (bootstrap_cartridge.capture_causal_state(bootstrap_state)
            != cartridge_state_result_t::success
        || bootstrap_state.loaded
        || bootstrap_state.mapping_mode != cartridge_mapping_mode_t::bootstrap)
    {
        return fail("cartridge_bootstrap_capture");
    }
    bootstrap_cartridge.write_u8(0x008123u, 0xa5u);
    if (bootstrap_cartridge.restore_causal_state(bootstrap_state)
            != cartridge_state_result_t::success
        || bootstrap_cartridge.read_u8(0x008123u) != 0x5au)
    {
        return fail("cartridge_bootstrap_restore");
    }
    auto invalid_bootstrap_state{ bootstrap_state };
    invalid_bootstrap_state.ram_dirty = true;
    if (bootstrap_cartridge.restore_causal_state(invalid_bootstrap_state)
            != cartridge_state_result_t::invalid_state
        || bootstrap_cartridge.read_u8(0x008123u) != 0x5au)
    {
        return fail("cartridge_bootstrap_reject_atomic");
    }

    cartridge_t base_cartridge{};
    const std::vector<std::byte> base_rom{ make_lorom(0x02u) };
    if (!base_cartridge.load(base_rom))
        return fail("cartridge_base_load");
    base_cartridge.write_u8(0x700123u, 0x6cu);
    cartridge_causal_state_t base_state{};
    if (base_cartridge.capture_causal_state(base_state)
            != cartridge_state_result_t::success
        || base_state.ram_data.size() != 8u * 1024u
        || !base_state.ram_dirty)
    {
        return fail("cartridge_base_capture");
    }
    base_cartridge.write_u8(0x700123u, 0x39u);
    base_cartridge.mark_persistent_memory_clean();
    if (base_cartridge.restore_causal_state(base_state)
            != cartridge_state_result_t::success
        || base_cartridge.read_u8(0x700123u) != 0x6cu
        || !base_cartridge.persistent_memory_dirty())
    {
        return fail("cartridge_base_restore");
    }
    auto mismatched_base_state{ base_state };
    mismatched_base_state.ram_data.pop_back();
    if (base_cartridge.restore_causal_state(mismatched_base_state)
            != cartridge_state_result_t::topology_mismatch
        || base_cartridge.read_u8(0x700123u) != 0x6cu)
    {
        return fail("cartridge_base_reject_ram_topology_atomic");
    }
    mismatched_base_state = base_state;
    mismatched_base_state.header.reset_vector ^= 1u;
    if (base_cartridge.restore_causal_state(mismatched_base_state)
            != cartridge_state_result_t::topology_mismatch
        || base_cartridge.read_u8(0x700123u) != 0x6cu)
    {
        return fail("cartridge_base_reject_header_atomic");
    }

    cartridge_t enhanced_cartridge{};
    const std::vector<std::byte> cx4_rom{ make_lorom(0xf3u) };
    cartridge_causal_state_t enhanced_state{};
    if (!enhanced_cartridge.load(cx4_rom)
        || enhanced_cartridge.hardware() != cartridge_hardware_t::cx4
        || enhanced_cartridge.capture_causal_state(enhanced_state)
            != cartridge_state_result_t::success
        || enhanced_state.enhancement_state.empty())
    {
        return fail("cartridge_enhancement_capture");
    }
    enhanced_cartridge.write_u8(0x006123u, 0x5au);
    if (enhanced_cartridge.restore_causal_state(enhanced_state)
            != cartridge_state_result_t::success
        || enhanced_cartridge.read_u8(0x006123u) != 0u)
    {
        return fail("cartridge_enhancement_restore");
    }
    auto invalid_enhanced_state{ enhanced_state };
    invalid_enhanced_state.enhancement_state.pop_back();
    if (enhanced_cartridge.restore_causal_state(invalid_enhanced_state)
            != cartridge_state_result_t::invalid_state
        || enhanced_cartridge.read_u8(0x006123u) != 0u)
    {
        return fail("cartridge_enhancement_reject_atomic");
    }

    const auto enhancement_round_trip{
        [](uint8_t type,
           std::string_view title,
           cartridge_hardware_t expected_hardware)
        {
            cartridge_t cartridge{};
            const std::vector<std::byte> rom{ make_lorom(type, title) };
            cartridge_causal_state_t state{};
            cartridge_causal_state_t restored{};
            return cartridge.load(rom)
                && cartridge.hardware() == expected_hardware
                && cartridge.capture_causal_state(state)
                    == cartridge_state_result_t::success
                && !state.enhancement_state.empty()
                && cartridge.restore_causal_state(state)
                    == cartridge_state_result_t::success
                && cartridge.capture_causal_state(restored)
                    == cartridge_state_result_t::success
                && restored == state;
        }
    };
    if (!enhancement_round_trip(0x03u, {}, cartridge_hardware_t::dsp1)
        || !enhancement_round_trip(
            0x03u, "DUNGEON MASTER", cartridge_hardware_t::dsp2
        )
        || !enhancement_round_trip(
            0x03u, "SD GUNDAM GX", cartridge_hardware_t::dsp3
        )
        || !enhancement_round_trip(
            0x03u, "TOP GEAR 3000", cartridge_hardware_t::dsp4
        )
        || !enhancement_round_trip(0x13u, {}, cartridge_hardware_t::super_fx))
    {
        return fail("all_enhancement_state_round_trips");
    }

    static console_t checkpoint_console{};
    static console_t replay_console{};
    static console_t mismatched_console{};
    static console_t unpowered_console{};
    static console_causal_state_t console_expected{};
    static console_causal_state_t console_actual{};
    static console_causal_state_t invalid_console{};
    const snes_hardware_configuration_t pal_configuration{
        .model = snes_hardware_model_t::late_3chip,
        .region = snes_region_selection_t::pal,
    };
    if (unpowered_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::not_powered_on)
    {
        return fail("console_unpowered_capture_rejected");
    }
    if (!checkpoint_console.set_hardware_configuration(pal_configuration)
        || !checkpoint_console.load_cartridge(base_rom))
    {
        return fail("console_checkpoint_setup");
    }
    checkpoint_console.power_on();
    checkpoint_console.begin_audio_frame();
    checkpoint_console.set_controller_state(0u, 0xa55au);
    checkpoint_console.write_u8(0x7e1234u, 0x6du);
    checkpoint_console.write_u8(0x700321u, 0x9bu);
    for (uint8_t index{ 0 }; index < 12u; ++index)
    {
        if (checkpoint_console.step_cpu_boundary().status
            != cpu_boundary_step_status_t::complete)
        {
            return fail("console_checkpoint_advance");
        }
    }
    if (checkpoint_console.capture_causal_state(console_expected)
            != console_checkpoint_result_t::success
        || console_expected.resolved_video_standard != video_standard_t::pal
        || console_expected.bus.wram[0x1234u] != 0x6du
        || console_expected.cartridge.ram_data[0x0321u] != 0x9bu)
    {
        return fail("console_checkpoint_capture");
    }

    checkpoint_console.write_u8(0x7e1234u, 0u);
    checkpoint_console.write_u8(0x700321u, 0u);
    for (uint8_t index{ 0 }; index < 5u; ++index)
        static_cast<void>(checkpoint_console.step_cpu_boundary());
    const console_checkpoint_result_t console_restore_result{
        checkpoint_console.restore_causal_state(console_expected)
    };
    if (console_restore_result != console_checkpoint_result_t::success)
    {
        return fail("console_checkpoint_restore");
    }
    if (checkpoint_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || console_actual != console_expected)
    {
        return fail("console_checkpoint_round_trip");
    }

    if (!replay_console.set_hardware_configuration(pal_configuration)
        || !replay_console.load_cartridge(base_rom))
    {
        return fail("console_replay_setup");
    }
    replay_console.power_on();
    if (replay_console.restore_causal_state(console_expected)
            != console_checkpoint_result_t::success)
    {
        return fail("console_replay_restore");
    }
    for (uint8_t index{ 0 }; index < 20u; ++index)
    {
        static_cast<void>(checkpoint_console.step_cpu_boundary());
        static_cast<void>(replay_console.step_cpu_boundary());
    }
    if (checkpoint_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || replay_console.capture_causal_state(invalid_console)
            != console_checkpoint_result_t::success
        || console_actual != invalid_console)
    {
        return fail("console_checkpoint_continuation_equivalence");
    }

    if (checkpoint_console.restore_causal_state(console_expected)
            != console_checkpoint_result_t::success)
    {
        return fail("console_checkpoint_restore_before_rejection");
    }
    invalid_console = console_expected;
    ++invalid_console.scheduler.master_clock;
    if (checkpoint_console.restore_causal_state(invalid_console)
            != console_checkpoint_result_t::cross_subsystem_mismatch
        || checkpoint_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || console_actual != console_expected)
    {
        return fail("console_checkpoint_reject_cross_clock_atomic");
    }

    invalid_console = console_expected;
    invalid_console.ppu.cgram[0] = 0xffffu;
    if (checkpoint_console.restore_causal_state(invalid_console)
            != console_checkpoint_result_t::invalid_subsystem_state
        || checkpoint_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || console_actual != console_expected)
    {
        return fail("console_checkpoint_reject_subsystem_atomic");
    }

    std::vector<std::byte> mismatched_rom{ base_rom };
    mismatched_rom[0x7ffcu] = std::byte{ 0x01u };
    if (!mismatched_console.set_hardware_configuration(pal_configuration)
        || !mismatched_console.load_cartridge(mismatched_rom))
    {
        return fail("console_mismatched_media_setup");
    }
    mismatched_console.power_on();
    if (mismatched_console.capture_causal_state(invalid_console)
            != console_checkpoint_result_t::success
        || mismatched_console.restore_causal_state(console_expected)
            != console_checkpoint_result_t::media_mismatch
        || mismatched_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || console_actual != invalid_console)
    {
        return fail("console_checkpoint_reject_media_atomic");
    }

    static console_t bootstrap_console{};
    bootstrap_console.power_on();
    bootstrap_console.write_u8(0x008765u, 0x42u);
    if (bootstrap_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success)
    {
        return fail("console_bootstrap_capture");
    }
    bootstrap_console.write_u8(0x008765u, 0x24u);
    if (bootstrap_console.restore_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || bootstrap_console.read_u8(0x008765u) != 0x42u)
    {
        return fail("console_bootstrap_restore");
    }

    static console_t enhanced_console{};
    if (!enhanced_console.load_cartridge(cx4_rom))
        return fail("console_enhancement_setup");
    enhanced_console.power_on();
    enhanced_console.write_u8(0x006123u, 0x42u);
    if (enhanced_console.capture_causal_state(console_actual)
            != console_checkpoint_result_t::success)
    {
        return fail("console_enhancement_capture");
    }
    enhanced_console.write_u8(0x006123u, 0x24u);
    if (enhanced_console.restore_causal_state(console_actual)
            != console_checkpoint_result_t::success
        || enhanced_console.read_u8(0x006123u) != 0x42u)
    {
        return fail("console_enhancement_restore");
    }

    return 0;
}
