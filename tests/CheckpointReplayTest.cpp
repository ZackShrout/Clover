//
// Created by OpenAI Codex on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"
#include "clover/frontend/SnesCheckpoint.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace
{
    using namespace clover;

    struct step_observation_t
    {
        core::hardware_slot_owner_t owner{};
        core::master_clock_delta_t elapsed{};
        core::timing_snapshot_t ppu_timing{};
        core::cpu_step_boundary_t cpu_boundary{};
        bool frame_complete{};
        bool entered_scanline{};
        bool entered_frame_start{};
        bool entered_hblank{};
        bool entered_vblank{};
        bool hdma_setup_triggered{};
        bool hdma_transfer_triggered{};
        bool nmi_requested{};
        bool irq_requested{};

        [[nodiscard]] bool operator==(const step_observation_t&) const noexcept = default;
    };

    struct replay_outcome_t
    {
        std::unique_ptr<core::console_causal_state_t> state{};
        std::unique_ptr<core::framebuffer_t> framebuffer{};
        std::vector<int16_t> audio{};
        std::vector<std::byte> portable_checkpoint{};
        std::vector<step_observation_t> observations{};
        std::vector<uint8_t> device_io{};
    };

    inline constexpr uint8_t k_require_interrupt{ 1u << 0u };
    inline constexpr uint8_t k_require_hdma_transfer{ 1u << 1u };
    inline constexpr uint8_t k_require_waiting{ 1u << 2u };

    [[nodiscard]] std::vector<std::byte> make_lorom(
        uint8_t cartridge_type = 0x02u,
        std::string_view title = {},
        uint8_t ram_size = 0x03u
    )
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0xeau });
        constexpr size_t header{ 0x7fc0u };
        for (size_t index{}; index < 21u; ++index)
            rom[header + index] = std::byte{ 0x20u };
        for (size_t index{}; index < title.size() && index < 21u; ++index)
            rom[header + index] = static_cast<std::byte>(title[index]);
        rom[0x7fbdu] = std::byte{ 0x05u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = static_cast<std::byte>(cartridge_type);
        rom[header + 0x18u] = static_cast<std::byte>(ram_size);
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    void observe_step(const core::hardware_step_result_t& step,
                      std::vector<step_observation_t>& observations)
    {
        observations.push_back({
            .owner = step.slot_owner,
            .elapsed = step.elapsed_master_clocks,
            .ppu_timing = step.ppu.timing,
            .cpu_boundary = step.cpu_boundary,
            .frame_complete = step.ppu.frame_complete,
            .entered_scanline = step.ppu.entered_scanline,
            .entered_frame_start = step.ppu.entered_frame_start,
            .entered_hblank = step.ppu.entered_hblank,
            .entered_vblank = step.ppu.entered_vblank,
            .hdma_setup_triggered = step.ppu.hdma_setup_triggered,
            .hdma_transfer_triggered = step.ppu.hdma_transfer_triggered,
            .nmi_requested = step.ppu.nmi_requested,
            .irq_requested = step.ppu.irq_requested,
        });
    }

    template <typename resume_t>
    [[nodiscard]] bool execute_script(core::console_t& console,
                                      resume_t&& resume,
                                      replay_outcome_t& outcome)
    {
        outcome.observations.reserve(70'000u);
        if (!resume(console, outcome.device_io))
            return false;

        console.begin_audio_frame();
        const uint64_t initial_frame{ console.frame_index() };
        for (size_t step_index{}; step_index < 100'000u; ++step_index)
        {
            if (step_index == 0u)
                console.set_controller_state(0u, 0x0000u);
            else if (step_index == 97u)
                console.set_controller_state(0u, 0xa55au);
            else if (step_index == 509u)
                console.set_controller_state(0u, 0x5aa5u);

            observe_step(console.step_hardware(), outcome.observations);
            if (console.frame_index() != initial_frame)
                break;
        }
        if (console.frame_index() == initial_frame)
            return false;

        outcome.state = std::make_unique<core::console_causal_state_t>();
        outcome.framebuffer = std::make_unique<core::framebuffer_t>(
            console.framebuffer()
        );
        outcome.audio.assign(
            console.audio_samples().begin(),
            console.audio_samples().end()
        );
        return console.capture_causal_state(*outcome.state)
                == core::console_checkpoint_result_t::success
            && frontend::capture_snes_checkpoint(
                console, outcome.portable_checkpoint
            ) == frontend::checkpoint_result_t::success;
    }

    [[nodiscard]] bool equal_outcomes(const replay_outcome_t& expected,
                                      const replay_outcome_t& actual)
    {
        return expected.state != nullptr
            && actual.state != nullptr
            && *expected.state == *actual.state
            && expected.framebuffer != nullptr
            && actual.framebuffer != nullptr
            && *expected.framebuffer == *actual.framebuffer
            && expected.audio == actual.audio
            && expected.portable_checkpoint == actual.portable_checkpoint
            && expected.observations == actual.observations
            && expected.device_io == actual.device_io;
    }

    [[nodiscard]] uint8_t observed_requirements(const replay_outcome_t& outcome)
    {
        bool saw_interrupt{ false };
        bool saw_hdma_transfer{ false };
        bool saw_waiting{ false };
        for (const step_observation_t& observation : outcome.observations)
        {
            saw_interrupt = saw_interrupt
                || observation.cpu_boundary == core::cpu_step_boundary_t::interrupt_entered;
            saw_hdma_transfer = saw_hdma_transfer
                || observation.hdma_transfer_triggered;
            saw_waiting = saw_waiting
                || observation.cpu_boundary == core::cpu_step_boundary_t::waiting;
        }
        return static_cast<uint8_t>(
            (saw_interrupt ? k_require_interrupt : 0u)
            | (saw_hdma_transfer ? k_require_hdma_transfer : 0u)
            | (saw_waiting ? k_require_waiting : 0u)
        );
    }

    template <typename prime_t, typename resume_t>
    [[nodiscard]] bool run_replay_case(
        std::string_view name,
        std::span<const std::byte> rom,
        core::snes_hardware_configuration_t configuration,
        prime_t&& prime,
        resume_t&& resume,
        uint8_t requirements = 0u
    )
    {
        auto console{ std::make_unique<core::console_t>() };
        if (!console->set_hardware_configuration(configuration)
            || (!rom.empty() && !console->load_cartridge(rom)))
        {
            std::fprintf(stderr, "CheckpointReplayTest setup failed: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }
        console->power_on();
        if (!prime(*console))
        {
            std::fprintf(stderr, "CheckpointReplayTest prime failed: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }

        std::vector<std::byte> checkpoint{};
        if (frontend::capture_snes_checkpoint(*console, checkpoint)
                != frontend::checkpoint_result_t::success)
        {
            std::fprintf(stderr, "CheckpointReplayTest capture failed: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }

        replay_outcome_t expected{};
        if (!execute_script(*console, resume, expected))
        {
            std::fprintf(stderr, "CheckpointReplayTest script failed: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }
        const uint8_t observed{ observed_requirements(expected) };
        if ((observed & requirements) != requirements)
        {
            std::fprintf(stderr,
                         "CheckpointReplayTest requirement failed: %.*s"
                         " required=%02x observed=%02x\n",
                         static_cast<int>(name.size()), name.data(),
                         requirements, observed);
            return false;
        }
        if (frontend::restore_snes_checkpoint(*console, checkpoint)
                != frontend::checkpoint_result_t::success)
        {
            std::fprintf(stderr, "CheckpointReplayTest restore failed: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }

        replay_outcome_t actual{};
        if (!execute_script(*console, resume, actual)
            || !equal_outcomes(expected, actual))
        {
            std::fprintf(stderr, "CheckpointReplayTest mismatch: %.*s\n",
                         static_cast<int>(name.size()), name.data());
            return false;
        }
        return true;
    }

    constexpr core::snes_hardware_configuration_t k_ntsc{
        .model = core::snes_hardware_model_t::late_3chip,
        .region = core::snes_region_selection_t::ntsc,
    };
    constexpr core::snes_hardware_configuration_t k_pal{
        .model = core::snes_hardware_model_t::late_3chip,
        .region = core::snes_region_selection_t::pal,
    };

    const auto no_resume{
        [](core::console_t&, std::vector<uint8_t>&) { return true; }
    };

    const auto midframe_prime{
        [](core::console_t& console)
        {
            console.write_u8(0x7e1234u, 0x5au);
            console.write_u8(0x700321u, 0x9bu);
            for (size_t index{}; index < 512u; ++index)
                static_cast<void>(console.step_hardware());
            return console.timing().raster.dot != 0u;
        }
    };
}

int main()
{
    using namespace clover;

    const std::vector<std::byte> base_rom{ make_lorom() };
    if (!run_replay_case(
            "ntsc-midframe-sram",
            base_rom,
            k_ntsc,
            midframe_prime,
            [](core::console_t& console, std::vector<uint8_t>&)
            {
                console.write_u8(0x700321u, 0x6du);
                return true;
            }
        )
        || !run_replay_case(
            "pal-midframe",
            base_rom,
            k_pal,
            midframe_prime,
            no_resume
        )
        || !run_replay_case(
            "reset-boundary",
            base_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.reset();
                return true;
            },
            no_resume
        ))
    {
        return 1;
    }

    const std::vector<std::byte> wai_rom{};
    if (!run_replay_case(
            "wai-irq-entry",
            wai_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.write_u8(0x000000u, 0x58u); // CLI
                console.write_u8(0x000001u, 0xcbu); // WAI
                console.write_u8(0x000002u, 0xeau); // NOP
                console.write_u8(0x001234u, 0xeau);
                console.write_u8(0x00fffeu, 0x34u);
                console.write_u8(0x00ffffu, 0x12u);
                console.write_u8(0x004207u, 0x40u);
                console.write_u8(0x004208u, 0x00u);
                console.write_u8(0x004200u, 0x10u);
                console.set_cpu_interrupt_poll_phase_for_testing(0u);
                static_cast<void>(console.step_hardware());
                static_cast<void>(console.step_hardware());
                return true;
            },
            no_resume,
            static_cast<uint8_t>(k_require_interrupt | k_require_waiting)
        ))
    {
        return 1;
    }

    if (!run_replay_case(
            "active-general-dma",
            base_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.write_u8(0x7e2000u, 0x42u);
                console.write_u8(0x7e2001u, 0x24u);
                console.write_u8(0x004300u, 0x01u);
                console.write_u8(0x004301u, 0x18u);
                console.write_u8(0x004302u, 0x00u);
                console.write_u8(0x004303u, 0x20u);
                console.write_u8(0x004304u, 0x7eu);
                console.write_u8(0x004305u, 0x00u);
                console.write_u8(0x004306u, 0x01u);
                console.write_u8(0x00420bu, 0x01u);
                return console.general_dma_pending();
            },
            no_resume
        )
        || !run_replay_case(
            "active-hdma",
            base_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.write_u8(0x7e2000u, 0x01u);
                console.write_u8(0x7e2001u, 0x5au);
                console.write_u8(0x7e2002u, 0x00u);
                console.write_u8(0x004300u, 0x00u);
                console.write_u8(0x004301u, 0x22u);
                console.write_u8(0x004302u, 0x00u);
                console.write_u8(0x004303u, 0x20u);
                console.write_u8(0x004304u, 0x7eu);
                console.write_u8(0x00420cu, 0x01u);
                for (size_t index{}; index < 100'000u; ++index)
                {
                    static_cast<void>(console.step_hardware());
                    if (console.hdma_pending())
                        return true;
                }
                return false;
            },
            no_resume,
            k_require_hdma_transfer
        )
        || !run_replay_case(
            "apu-port-synchronization",
            base_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.write_u8(0x002140u, 0xccu);
                console.write_u8(0x002141u, 0x33u);
                for (size_t index{}; index < 37u; ++index)
                    static_cast<void>(console.step_hardware());
                return true;
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                io.push_back(console.read_u8(0x002140u));
                io.push_back(console.read_u8(0x002141u));
                console.write_u8(0x002142u, 0x5au);
                return true;
            }
        ))
    {
        return 1;
    }

    const auto enhancement_prime{
        [](core::console_t& console)
        {
            for (size_t index{}; index < 64u; ++index)
                static_cast<void>(console.step_hardware());
            return true;
        }
    };

    const std::vector<std::byte> cx4_rom{ make_lorom(0xf3u) };
    if (!run_replay_case(
            "cx4-protocol",
            cx4_rom,
            k_ntsc,
            [&enhancement_prime](core::console_t& console)
            {
                console.write_u8(0x006123u, 0x42u);
                return enhancement_prime(console);
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                io.push_back(console.read_u8(0x006123u));
                console.write_u8(0x007f80u, 0xfeu);
                console.write_u8(0x007f81u, 0xffu);
                console.write_u8(0x007f82u, 0xffu);
                console.write_u8(0x007f83u, 0x03u);
                console.write_u8(0x007f4fu, 0x25u);
                io.push_back(console.read_u8(0x007f80u));
                return true;
            }
        ))
    {
        return 1;
    }

    const std::vector<std::byte> dsp1_rom{ make_lorom(0x03u) };
    if (!run_replay_case(
            "dsp1-word-phase",
            dsp1_rom,
            k_ntsc,
            [&enhancement_prime](core::console_t& console)
            {
                console.write_u8(0x308000u, 0x00u);
                console.write_u8(0x308000u, 0x00u);
                return enhancement_prime(console);
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                console.write_u8(0x308000u, 0x40u);
                console.write_u8(0x308000u, 0x00u);
                console.write_u8(0x308000u, 0x40u);
                io.push_back(console.read_u8(0x308000u));
                io.push_back(console.read_u8(0x308000u));
                return true;
            }
        ))
    {
        return 1;
    }

    const std::vector<std::byte> dsp2_rom{
        make_lorom(0x03u, "DUNGEON MASTER")
    };
    if (!run_replay_case(
            "dsp2-byte-phase",
            dsp2_rom,
            k_ntsc,
            [&enhancement_prime](core::console_t& console)
            {
                console.write_u8(0x206000u, 0x09u);
                console.write_u8(0x206000u, 0x34u);
                return enhancement_prime(console);
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                console.write_u8(0x206000u, 0x12u);
                console.write_u8(0x206000u, 0x02u);
                console.write_u8(0x206000u, 0x00u);
                for (size_t index{}; index < 4u; ++index)
                    io.push_back(console.read_u8(0x206000u));
                return true;
            }
        ))
    {
        return 1;
    }

    const std::vector<std::byte> dsp3_rom{
        make_lorom(0x03u, "SD GUNDAM GX")
    };
    if (!run_replay_case(
            "dsp3-byte-phase",
            dsp3_rom,
            k_ntsc,
            [&enhancement_prime](core::console_t& console)
            {
                console.write_u8(0x208000u, 0x06u);
                return enhancement_prime(console);
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                console.write_u8(0x208000u, 0x00u);
                console.write_u8(0x208000u, 0x03u);
                console.write_u8(0x208000u, 0x00u);
                io.push_back(console.read_u8(0x20c000u));
                return true;
            }
        ))
    {
        return 1;
    }

    const std::vector<std::byte> dsp4_rom{
        make_lorom(0x03u, "TOP GEAR 3000")
    };
    if (!run_replay_case(
            "dsp4-byte-phase",
            dsp4_rom,
            k_ntsc,
            [&enhancement_prime](core::console_t& console)
            {
                console.write_u8(0x308000u, 0x00u);
                return enhancement_prime(console);
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                console.write_u8(0x308000u, 0x00u);
                console.write_u8(0x308000u, 0x34u);
                console.write_u8(0x308000u, 0x12u);
                console.write_u8(0x308000u, 0x20u);
                console.write_u8(0x308000u, 0x00u);
                for (size_t index{}; index < 4u; ++index)
                    io.push_back(console.read_u8(0x308000u));
                return true;
            }
        ))
    {
        return 1;
    }

    const std::vector<std::byte> super_fx_rom{ make_lorom(0x15u) };
    if (!run_replay_case(
            "super-fx-running",
            super_fx_rom,
            k_ntsc,
            [](core::console_t& console)
            {
                console.write_u8(0x00303au, 0x18u);
                constexpr std::array<uint8_t, 8> program{
                    0xf0u, 0x34u, 0x12u, 0x51u,
                    0x01u, 0x01u, 0x01u, 0x00u
                };
                for (uint16_t index{}; index < program.size(); ++index)
                    console.write_u8(0x003100u + index, program[index]);
                console.write_u8(0x00301eu, 0x00u);
                console.write_u8(0x00301fu, 0x00u);
                for (size_t index{}; index < 4u; ++index)
                    static_cast<void>(console.step_hardware());
                return true;
            },
            [](core::console_t& console, std::vector<uint8_t>& io)
            {
                io.push_back(console.read_u8(0x003030u));
                io.push_back(console.read_u8(0x003031u));
                return true;
            }
        ))
    {
        return 1;
    }

    return 0;
}
