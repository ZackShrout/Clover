//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cartridge.h"
#include "clover/core/snes/Console.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/MediaIdentity.h"
#include "clover/frontend/SnesEmulatorCore.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <string_view>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_headered_lorom()
    {
        std::vector<std::byte> media(512u + 0x8000u, std::byte{ 0xa5u });
        std::span<std::byte> rom{ media.data() + 512u, 0x8000u };
        rom[0] = std::byte{ 0x42u };
        rom[1] = std::byte{ 0x24u };

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
        return media;
    }

    [[nodiscard]] std::vector<std::byte> make_hirom()
    {
        std::vector<std::byte> rom(0x10000u, std::byte{ 0 });
        rom[0x1234u] = std::byte{ 0x66u };

        constexpr size_t header{ 0xffc0u };
        rom[header + 0x15u] = std::byte{ 0x21u };
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

    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "AnalysisBoundaryTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover;

    const std::vector<std::byte> media{ make_headered_lorom() };
    const std::span<const std::byte> canonical_input{
        frontend::canonical_media(frontend::system_id_t::snes, media)
    };
    if (canonical_input.size() != 0x8000u
        || frontend::media_identity(frontend::system_id_t::snes, media)
            != frontend::media_identity(frontend::system_id_t::snes, canonical_input))
    {
        return fail("shared_media_identity");
    }

    core::cartridge_t cartridge{};
    if (!cartridge.load(media)
        || cartridge.canonical_media().size() != 0x8000u
        || cartridge.canonical_media()[0] != std::byte{ 0x42u })
    {
        return fail("canonical_media");
    }

    const core::cartridge_address_mapping_t low_rom{
        cartridge.translate_address(0x008000u)
    };
    const core::cartridge_address_mapping_t high_rom{
        cartridge.translate_address(0x808000u)
    };
    const core::cartridge_address_mapping_t low_ram{
        cartridge.translate_address(0x700123u)
    };
    const core::cartridge_address_mapping_t high_ram{
        cartridge.translate_address(0xf00123u)
    };
    if (low_rom.kind != core::cartridge_address_kind_t::program_rom
        || low_rom.storage_offset != 0u
        || high_rom.kind != core::cartridge_address_kind_t::program_rom
        || high_rom.storage_offset != low_rom.storage_offset
        || low_ram.kind != core::cartridge_address_kind_t::cartridge_ram
        || low_ram.storage_offset != 0x0123u
        || high_ram.kind != core::cartridge_address_kind_t::cartridge_ram
        || high_ram.storage_offset != low_ram.storage_offset)
    {
        return fail("cartridge_translation");
    }

    core::cartridge_t hirom_cartridge{};
    if (!hirom_cartridge.load(make_hirom()))
        return fail("hirom_load");
    const core::cartridge_address_mapping_t hirom_rom{
        hirom_cartridge.translate_address(0xc01234u)
    };
    const core::cartridge_address_mapping_t hirom_ram{
        hirom_cartridge.translate_address(0x206123u)
    };
    if (hirom_rom.kind != core::cartridge_address_kind_t::program_rom
        || hirom_rom.storage_offset != 0x1234u
        || hirom_ram.kind != core::cartridge_address_kind_t::cartridge_ram
        || hirom_ram.storage_offset != 0x0123u)
    {
        return fail("hirom_translation");
    }

    const auto console{ std::make_unique<core::console_t>() };
    if (!console->load_cartridge(media))
        return fail("console_load");
    console->power_on();
    console->write_u8(0x7e1234u, 0x5au);
    console->write_u8(0x7e4321u, 0xa5u);
    const uint8_t open_bus_before{ console->open_bus() };
    uint8_t inspected{};
    if (!console->inspect_u8(0x7e1234u, inspected)
        || inspected != 0x5au
        || console->open_bus() != open_bus_before)
    {
        return fail("side_effect_free_wram");
    }
    if (console->inspect_u8(0x002100u, inspected)
        || console->open_bus() != open_bus_before)
    {
        return fail("volatile_mmio_unavailable");
    }

    const auto boundary_console{ std::make_unique<core::console_t>() };
    const auto hardware_console{ std::make_unique<core::console_t>() };
    const auto observed_console{ std::make_unique<core::console_t>() };
    std::array<core::snes_observation_event_t, 4> disabled_observation_storage{};
    std::array<core::snes_observation_event_t, 4> enabled_observation_storage{};
    core::snes_observation_sink_t disabled_observation_sink{};
    core::snes_observation_sink_t enabled_observation_sink{};
    disabled_observation_sink.configure(disabled_observation_storage, 0u);
    enabled_observation_sink.configure(
        enabled_observation_storage,
        core::k_snes_observe_cpu_boundary
    );
    boundary_console->set_observation_sink(&disabled_observation_sink);
    observed_console->set_observation_sink(&enabled_observation_sink);
    boundary_console->power_on();
    hardware_console->power_on();
    observed_console->power_on();
    boundary_console->write_u8(0x000000u, 0xeau);
    hardware_console->write_u8(0x000000u, 0xeau);
    observed_console->write_u8(0x000000u, 0xeau);
    const core::cpu_boundary_step_result_t boundary_step{
        boundary_console->step_cpu_boundary()
    };
    const core::cpu_boundary_step_result_t observed_step{
        observed_console->step_cpu_boundary()
    };
    core::hardware_step_result_t hardware_step{};
    do
    {
        hardware_step = hardware_console->step_hardware();
    }
    while (hardware_step.cpu_boundary == core::cpu_step_boundary_t::none);
    if (boundary_step.status != core::cpu_boundary_step_status_t::complete
        || boundary_step.boundary != core::cpu_step_boundary_t::instruction_retired
        || boundary_step.elapsed_master_clocks == 0u
        || hardware_step.cpu_boundary != core::cpu_step_boundary_t::instruction_retired
        || boundary_console->master_clock() != hardware_console->master_clock()
        || boundary_console->cpu_state().pc != hardware_console->cpu_state().pc
        || boundary_console->cpu_state().p != hardware_console->cpu_state().p
        || observed_step.boundary != core::cpu_step_boundary_t::instruction_retired
        || observed_console->master_clock() != hardware_console->master_clock()
        || observed_console->cpu_state().pc != hardware_console->cpu_state().pc
        || observed_console->cpu_state().p != hardware_console->cpu_state().p
        || observed_console->timing().raster.scanline
            != hardware_console->timing().raster.scanline
        || observed_console->timing().raster.dot != hardware_console->timing().raster.dot
        || boundary_console->timing().raster.scanline
            != hardware_console->timing().raster.scanline
        || boundary_console->timing().raster.dot != hardware_console->timing().raster.dot)
    {
        return fail("cpu_boundary_step_equivalence");
    }
    if (!disabled_observation_sink.events().empty()
        || disabled_observation_sink.take_dropped() != 0u)
    {
        return fail("disabled_observation_non_interference");
    }
    if (enabled_observation_sink.events().size() != 2u
        || enabled_observation_sink.events()[0].cpu_boundary.boundary
            != core::cpu_step_boundary_t::reset_completed
        || enabled_observation_sink.events()[1].cpu_boundary.boundary
            != core::cpu_step_boundary_t::instruction_retired)
    {
        return fail("enabled_observation_non_interference");
    }

    const auto wait_console{ std::make_unique<core::console_t>() };
    wait_console->power_on();
    wait_console->write_u8(0x000000u, 0xcbu);
    const core::cpu_boundary_step_result_t wait_instruction{
        wait_console->step_cpu_boundary()
    };
    const core::cpu_boundary_step_result_t waiting{
        wait_console->step_cpu_boundary()
    };
    if (wait_instruction.boundary != core::cpu_step_boundary_t::instruction_retired
        || waiting.boundary != core::cpu_step_boundary_t::waiting)
    {
        return fail("cpu_wait_boundary");
    }

    const auto stop_console{ std::make_unique<core::console_t>() };
    stop_console->power_on();
    stop_console->write_u8(0x000000u, 0xdbu);
    const core::cpu_boundary_step_result_t stop_instruction{
        stop_console->step_cpu_boundary()
    };
    const core::cpu_boundary_step_result_t stopped{
        stop_console->step_cpu_boundary()
    };
    if (stop_instruction.boundary != core::cpu_step_boundary_t::instruction_retired
        || stopped.boundary != core::cpu_step_boundary_t::stopped)
    {
        return fail("cpu_stop_boundary");
    }

    std::unique_ptr<frontend::emulator_core_t> emulator{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    if (!emulator || !emulator->load_media(media))
        return fail("frontend_load");

    frontend::debug_target_t* const target{ emulator->debug_target() };
    if (target == nullptr)
        return fail("debug_target_capability");
    frontend::execution_control_t* const execution_control{
        target->execution_control()
    };
    if (execution_control == nullptr)
        return fail("execution_control_capability");
    frontend::observation_control_t* const observation_control{
        target->observation_control()
    };
    if (observation_control == nullptr
        || observation_control->available_observations()
            != (frontend::k_observe_execution_boundary
                | frontend::k_observe_memory_access)
        || observation_control->observation_mask() != 0u)
    {
        return fail("observation_control_capability");
    }
    if (observation_control->set_observation_mask(uint64_t{ 1u } << 63u)
        || observation_control->observation_mask() != 0u)
    {
        return fail("observation_mask_rejection");
    }
    frontend::debug_session_control_t* const session_control{
        target->debug_session_control()
    };
    frontend::checkpoint_control_t* const checkpoint_control{
        target->checkpoint_control()
    };
    if (session_control == nullptr
        || checkpoint_control == nullptr
        || session_control->debug_session_state()
            != frontend::debug_session_state_t::not_running)
    {
        return fail("debug_session_capability");
    }
    const std::span<const frontend::processor_register_descriptor_t> registers{
        target->processor_registers(frontend::snes_debug::k_main_cpu_domain)
    };
    std::array<frontend::processor_register_value_t, 10> register_values{};
    if (registers.size() != register_values.size()
        || registers[0].stable_id != std::string_view{ "pc" }
        || registers[9].stable_id != std::string_view{ "e" }
        || target->inspect_processor_state(
            frontend::snes_debug::k_main_cpu_domain,
            register_values
        ).status != frontend::processor_state_status_t::not_running)
    {
        return fail("processor_state_capability");
    }
    const frontend::debug_session_transition_result_t pre_power_pause{
        session_control->pause_debug_session()
    };
    if (pre_power_pause.status
            != frontend::debug_session_transition_status_t::not_running
        || pre_power_pause.state != frontend::debug_session_state_t::not_running)
    {
        return fail("pre_power_pause");
    }
    const frontend::execution_step_result_t pre_power_step{
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    };
    if (pre_power_step.status != frontend::execution_step_status_t::not_running)
        return fail("pre_power_execution_step");
    std::vector<std::byte> debug_checkpoint{};
    if (checkpoint_control->capture_checkpoint(debug_checkpoint).status
            != frontend::checkpoint_operation_status_t::not_running)
    {
        return fail("pre_power_checkpoint");
    }
    if (!observation_control->set_observation_mask(
        frontend::k_observe_execution_boundary
    ))
    {
        return fail("enable_observation");
    }

    emulator->power_on();
    if (session_control->debug_session_state() != frontend::debug_session_state_t::running)
        return fail("running_after_power");
    const frontend::processor_state_result_t reset_state{
        target->inspect_processor_state(
            frontend::snes_debug::k_main_cpu_domain,
            register_values
        )
    };
    if (reset_state.status != frontend::processor_state_status_t::complete
        || reset_state.registers_written != register_values.size()
        || reset_state.instruction_address.value != 0x008000u
        || register_values[0].value != 0x8000u
        || register_values[8].value != 0u
        || register_values[9].value != 1u)
    {
        return fail("live_processor_state");
    }
    const frontend::execution_step_result_t running_step{
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    };
    if (running_step.status != frontend::execution_step_status_t::not_paused)
        return fail("step_requires_pause");
    if (checkpoint_control->capture_checkpoint(debug_checkpoint).status
            != frontend::checkpoint_operation_status_t::not_paused)
    {
        return fail("checkpoint_requires_pause");
    }
    const frontend::debug_session_transition_result_t pause_result{
        session_control->pause_debug_session()
    };
    if (pause_result.status != frontend::debug_session_transition_status_t::complete
        || pause_result.state != frontend::debug_session_state_t::paused
        || session_control->debug_session_state() != frontend::debug_session_state_t::paused)
    {
        return fail("pause_debug_session");
    }
    emulator->run_frame();
    const frontend::execution_step_result_t main_cpu_step{
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    };
    if (main_cpu_step.status != frontend::execution_step_status_t::complete
        || main_cpu_step.domain != frontend::snes_debug::k_main_cpu_domain
        || main_cpu_step.boundary != frontend::execution_boundary_t::instruction
        || main_cpu_step.machine_clocks_elapsed == 0u)
    {
        return fail("main_cpu_execution_step");
    }

    std::array<frontend::observation_event_t, 4> observed_events{};
    const frontend::observation_drain_result_t initial_observations{
        observation_control->drain_observations(observed_events)
    };
    if (initial_observations.events_written != 2u
        || initial_observations.events_dropped != 0u
        || observed_events[0].domain != frontend::snes_debug::k_main_cpu_domain
        || observed_events[0].kind != frontend::observation_kind_t::execution_boundary
        || observed_events[0].execution_boundary.boundary
            != frontend::execution_boundary_t::reset
        || observed_events[0].execution_boundary.address_after.value != 0x008000u
        || observed_events[1].execution_boundary.boundary
            != frontend::execution_boundary_t::instruction
        || observed_events[1].execution_boundary.address_before.value != 0x008000u
        || observed_events[1].execution_boundary.address_after.value != 0x008002u
        || observed_events[0].machine_clock >= observed_events[1].machine_clock)
    {
        return fail("initial_boundary_observations");
    }

    if (checkpoint_control->capture_checkpoint(debug_checkpoint).status
            != frontend::checkpoint_operation_status_t::success
        || debug_checkpoint.empty())
    {
        return fail("debug_checkpoint_capture");
    }
    static_cast<void>(
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    );
    if (checkpoint_control->restore_checkpoint(debug_checkpoint).status
            != frontend::checkpoint_operation_status_t::success)
    {
        return fail("debug_checkpoint_restore");
    }
    std::vector<std::byte> recaptured_debug_checkpoint{};
    if (checkpoint_control->capture_checkpoint(recaptured_debug_checkpoint).status
            != frontend::checkpoint_operation_status_t::success
        || recaptured_debug_checkpoint != debug_checkpoint)
    {
        return fail("debug_checkpoint_round_trip");
    }

    observation_control->clear_observations();
    if (!observation_control->set_observation_mask(0u))
        return fail("disable_observation");
    static_cast<void>(
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    );
    const frontend::observation_drain_result_t disabled_drain{
        observation_control->drain_observations(observed_events)
    };
    if (disabled_drain.events_written != 0u
        || disabled_drain.events_dropped != 0u)
    {
        return fail("disabled_observation_drain");
    }

    if (!observation_control->set_observation_mask(
        frontend::k_observe_execution_boundary
    ))
    {
        return fail("reenable_observation");
    }
    constexpr size_t overflow_step_count{ 1100u };
    for (size_t index{ 0 }; index < overflow_step_count; ++index)
    {
        const frontend::execution_step_result_t step{
            execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
        };
        if (step.status != frontend::execution_step_status_t::complete)
            return fail("overflow_step");
    }
    std::array<frontend::observation_event_t, overflow_step_count> overflow_events{};
    const frontend::observation_drain_result_t overflow_drain{
        observation_control->drain_observations(overflow_events)
    };
    if (overflow_drain.events_written == 0u
        || overflow_drain.events_written >= overflow_step_count
        || overflow_drain.events_dropped
            != overflow_step_count - overflow_drain.events_written)
    {
        return fail("bounded_observation_overflow");
    }
    const frontend::observation_drain_result_t empty_drain{
        observation_control->drain_observations(overflow_events)
    };
    if (empty_drain.events_written != 0u || empty_drain.events_dropped != 0u)
        return fail("observation_drain_reset");

    observation_control->clear_observations();
    if (!observation_control->set_observation_mask(
            frontend::k_observe_execution_boundary
                | frontend::k_observe_memory_access
        ))
    {
        return fail("enable_memory_access_observation");
    }
    const frontend::processor_state_result_t before_memory_step{
        target->inspect_processor_state(
            frontend::snes_debug::k_main_cpu_domain,
            register_values
        )
    };
    if (before_memory_step.status != frontend::processor_state_status_t::complete)
        return fail("memory_access_state");
    static_cast<void>(
        execution_control->step_execution_domain(frontend::snes_debug::k_main_cpu_domain)
    );
    std::array<frontend::observation_event_t, 32> memory_events{};
    const frontend::observation_drain_result_t memory_drain{
        observation_control->drain_observations(memory_events)
    };
    bool saw_opcode_read{ false };
    bool saw_boundary{ false };
    for (size_t index{ 0 }; index < memory_drain.events_written; ++index)
    {
        const frontend::observation_event_t& event{ memory_events[index] };
        saw_boundary = saw_boundary
            || event.kind == frontend::observation_kind_t::execution_boundary;
        saw_opcode_read = saw_opcode_read
            || (event.kind == frontend::observation_kind_t::memory_access
                && event.memory_access.kind
                    == frontend::memory_access_kind_t::read
                && event.memory_access.address.value
                    == before_memory_step.instruction_address.value
                && event.memory_access.instruction_address.value
                    == before_memory_step.instruction_address.value);
    }
    if (!saw_opcode_read || !saw_boundary || memory_drain.events_dropped != 0u)
        return fail("memory_access_observation");
    if (!observation_control->set_observation_mask(
            frontend::k_observe_execution_boundary
        ))
    {
        return fail("restore_boundary_observation");
    }

    emulator->reset();
    if (session_control->debug_session_state() != frontend::debug_session_state_t::paused)
        return fail("reset_preserves_debug_pause");
    const frontend::observation_drain_result_t reset_drain{
        observation_control->drain_observations(observed_events)
    };
    if (reset_drain.events_written != 1u
        || observed_events[0].execution_boundary.boundary
            != frontend::execution_boundary_t::reset)
    {
        return fail("paused_reset_observation");
    }
    if (!emulator->load_media(media)
        || session_control->debug_session_state() != frontend::debug_session_state_t::paused)
    {
        return fail("media_load_preserves_debug_pause");
    }
    const frontend::observation_drain_result_t media_load_drain{
        observation_control->drain_observations(observed_events)
    };
    if (media_load_drain.events_written != 1u
        || observed_events[0].execution_boundary.boundary
            != frontend::execution_boundary_t::reset)
    {
        return fail("paused_media_load_observation");
    }
    emulator->run_frame();
    const frontend::observation_drain_result_t paused_frame_drain{
        observation_control->drain_observations(observed_events)
    };
    if (paused_frame_drain.events_written != 0u
        || paused_frame_drain.events_dropped != 0u)
    {
        return fail("paused_frame_suppression");
    }

    const frontend::debug_session_transition_result_t resume_result{
        session_control->resume_debug_session()
    };
    if (resume_result.status != frontend::debug_session_transition_status_t::complete
        || resume_result.state != frontend::debug_session_state_t::running
        || session_control->debug_session_state() != frontend::debug_session_state_t::running)
    {
        return fail("resume_debug_session");
    }
    emulator->run_frame();
    const frontend::observation_drain_result_t resumed_frame_drain{
        observation_control->drain_observations(observed_events)
    };
    if (resumed_frame_drain.events_written == 0u)
        return fail("resumed_frame_advancement");

    const frontend::execution_step_result_t audio_cpu_step{
        execution_control->step_execution_domain(frontend::snes_debug::k_audio_cpu_domain)
    };
    if (audio_cpu_step.status != frontend::execution_step_status_t::unsupported)
        return fail("unsupported_audio_cpu_step");
    const frontend::execution_step_result_t invalid_domain_step{
        execution_control->step_execution_domain(999u)
    };
    if (invalid_domain_step.status != frontend::execution_step_status_t::invalid_domain)
        return fail("invalid_execution_domain_step");

    const std::span<const frontend::execution_domain_descriptor_t> domains{
        target->execution_domains()
    };
    if (domains.size() != 2u
        || domains[0].stable_id != std::string_view{ "snes.main-cpu" }
        || domains[0].architecture != frontend::processor_architecture_t::wdc_65c816
        || domains[1].stable_id != std::string_view{ "snes.audio-cpu" }
        || domains[1].architecture != frontend::processor_architecture_t::sony_spc700)
    {
        return fail("execution_domain_descriptors");
    }

    const std::span<const frontend::address_space_descriptor_t> spaces{
        target->address_spaces()
    };
    if (spaces.size() != 7u
        || spaces[0].stable_id != std::string_view{ "snes.cpu-bus" }
        || spaces[2].stable_id != std::string_view{ "media.canonical" }
        || spaces[2].size_bytes != 0x8000u
        || spaces[4].stable_id != std::string_view{ "snes.cgram" }
        || spaces[4].size_bytes != 512u
        || spaces[5].stable_id != std::string_view{ "snes.vram" }
        || spaces[5].size_bytes != 65536u
        || spaces[6].stable_id != std::string_view{ "snes.oam" }
        || spaces[6].size_bytes != 544u)
    {
        return fail("address_space_descriptors");
    }
    std::array<std::byte, 2> bytes{};
    const frontend::memory_inspection_result_t canonical_result{
        target->inspect_memory(
            { frontend::snes_debug::k_canonical_media_space, 0u },
            bytes
        )
    };
    if (canonical_result.status != frontend::memory_inspection_status_t::complete
        || canonical_result.bytes_read != bytes.size()
        || bytes[0] != std::byte{ 0x42u }
        || bytes[1] != std::byte{ 0x24u })
    {
        return fail("canonical_space_inspection");
    }
    std::array<std::byte, 544> oam_bytes{};
    const frontend::memory_inspection_result_t oam_result{
        target->inspect_memory(
            { frontend::snes_debug::k_oam_space, 0u },
            oam_bytes
        )
    };
    if (oam_result.status != frontend::memory_inspection_status_t::complete
        || oam_result.bytes_read != oam_bytes.size()
        || target->inspect_memory(
            { frontend::snes_debug::k_oam_space, 543u },
            bytes
        ).status != frontend::memory_inspection_status_t::out_of_range)
    {
        return fail("oam_space_inspection");
    }

    bytes.fill(std::byte{ 0 });
    const frontend::memory_inspection_result_t cpu_result{
        target->inspect_memory(
            { frontend::snes_debug::k_cpu_bus_space, 0x008000u },
            bytes
        )
    };
    if (cpu_result.status != frontend::memory_inspection_status_t::complete
        || bytes[0] != std::byte{ 0x42u }
        || bytes[1] != std::byte{ 0x24u })
    {
        return fail("cpu_bus_inspection");
    }

    const frontend::memory_inspection_result_t mmio_result{
        target->inspect_memory(
            { frontend::snes_debug::k_cpu_bus_space, 0x002100u },
            bytes
        )
    };
    if (mmio_result.status != frontend::memory_inspection_status_t::unavailable
        || mmio_result.bytes_read != 0u)
    {
        return fail("cpu_bus_mmio_rejection");
    }

    bytes.fill(std::byte{ 0xffu });
    const frontend::memory_inspection_result_t cgram_result{
        target->inspect_memory(
            { frontend::snes_debug::k_cgram_space, 0u },
            bytes
        )
    };
    if (cgram_result.status != frontend::memory_inspection_status_t::complete
        || cgram_result.bytes_read != bytes.size()
        || (std::to_integer<uint8_t>(bytes[1]) & 0x80u) != 0u)
    {
        return fail("cgram_space_inspection");
    }
    const frontend::memory_inspection_result_t cgram_range_result{
        target->inspect_memory(
            { frontend::snes_debug::k_cgram_space, 511u },
            bytes
        )
    };
    if (cgram_range_result.status
        != frontend::memory_inspection_status_t::out_of_range)
    {
        return fail("cgram_space_bounds");
    }

    bytes.fill(std::byte{ 0xffu });
    const frontend::memory_inspection_result_t vram_result{
        target->inspect_memory(
            { frontend::snes_debug::k_vram_space, 0u },
            bytes
        )
    };
    if (vram_result.status != frontend::memory_inspection_status_t::complete
        || vram_result.bytes_read != bytes.size())
    {
        return fail("vram_space_inspection");
    }
    const frontend::memory_inspection_result_t vram_range_result{
        target->inspect_memory(
            { frontend::snes_debug::k_vram_space, 65535u },
            bytes
        )
    };
    if (vram_range_result.status
        != frontend::memory_inspection_status_t::out_of_range)
    {
        return fail("vram_space_bounds");
    }

    const frontend::memory_inspection_result_t invalid_result{
        target->inspect_memory({ 999u, 0u }, bytes)
    };
    if (invalid_result.status != frontend::memory_inspection_status_t::invalid_address_space)
        return fail("invalid_address_space");

    const frontend::address_translation_result_t low_translation{
        target->translate_address(
            { frontend::snes_debug::k_cpu_bus_space, 0x008000u },
            frontend::snes_debug::k_canonical_media_space
        )
    };
    const frontend::address_translation_result_t high_translation{
        target->translate_address(
            { frontend::snes_debug::k_cpu_bus_space, 0x808000u },
            frontend::snes_debug::k_canonical_media_space
        )
    };
    if (low_translation.status != frontend::address_translation_status_t::complete
        || high_translation.status != frontend::address_translation_status_t::complete
        || low_translation.address.space != frontend::snes_debug::k_canonical_media_space
        || low_translation.address.value != 0u
        || high_translation.address.value != low_translation.address.value)
    {
        return fail("frontend_address_translation");
    }

    return 0;
}
