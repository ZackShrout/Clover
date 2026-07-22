//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

#include <array>
#include <cstddef>
#include <cstdio>
#include <span>

namespace
{
    [[nodiscard]] std::array<std::byte, 0x8000> make_lorom(uint8_t destination)
    {
        std::array<std::byte, 0x8000> rom{};
        rom[0] = std::byte{ 0x4cu }; // JMP $8000
        rom[1] = std::byte{ 0x00u };
        rom[2] = std::byte{ 0x80u };
        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x01u };
        rom[header + 0x19u] = std::byte{ destination };
        rom[header + 0x1cu] = std::byte{ 0xffu };
        rom[header + 0x1du] = std::byte{ 0xffu };
        rom[header + 0x1eu] = std::byte{ 0x00u };
        rom[header + 0x1fu] = std::byte{ 0x00u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "HardwareProfileTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    using namespace clover::core;

    if (k_default_snes_hardware_profile.key != "late-3chip"
        || k_default_snes_hardware_profile.cpu_revision != snes_cpu_revision_t::b
        || k_default_snes_hardware_profile.ppu_family != snes_ppu_family_t::discrete_3chip
        || k_default_snes_hardware_profile.apu_revision != snes_apu_revision_t::integrated_s_apu
        || k_default_snes_hardware_profile.cpu_version != 2u
        || k_default_snes_hardware_profile.ppu1_version != 1u
        || k_default_snes_hardware_profile.ppu2_version != 3u)
    {
        return fail("canonical_profile_contract");
    }

    if (find_snes_hardware_profile("early-3chip") == nullptr
        || find_snes_hardware_profile("scpu-a-3chip") == nullptr
        || find_snes_hardware_profile("1chip") == nullptr
        || find_snes_hardware_profile("unknown") != nullptr)
    {
        return fail("profile_catalog");
    }

    static console_t unsupported{};
    if (unsupported.set_hardware_configuration({
            .model = snes_hardware_model_t::one_chip,
            .region = snes_region_selection_t::ntsc,
        }))
    {
        return fail("unimplemented_profile_rejected");
    }

    static console_t pal_override{};
    if (!pal_override.set_hardware_configuration({
            .model = snes_hardware_model_t::late_3chip,
            .region = snes_region_selection_t::pal,
        }))
    {
        return fail("pal_override_accepted");
    }
    pal_override.power_on();
    if (pal_override.hardware_identity().video_standard != video_standard_t::pal
        || pal_override.video_timing().scanlines_per_frame != 312u
        || (pal_override.read_u8(0x004210u) & 0x0fu) != 2u
        || (pal_override.read_u8(0x00213eu) & 0x0fu) != 1u
        || (pal_override.read_u8(0x00213fu) & 0x1fu) != 0x13u)
    {
        return fail("pal_identity_registers");
    }
    const auto pal_rom{ make_lorom(0x02u) };
    static console_t automatic_pal{};
    if (!automatic_pal.load_cartridge(std::as_bytes(std::span{ pal_rom })))
        return fail("automatic_pal_load");
    automatic_pal.power_on();
    if (automatic_pal.hardware_identity().video_standard != video_standard_t::pal)
        return fail("automatic_pal_selection");
    automatic_pal.run_frame();
    if (automatic_pal.frame_index() != 1u
        || automatic_pal.audio_samples().size() < 1200u)
    {
        return fail("pal_frame_and_apu_timing");
    }

    const auto ntsc_rom{ make_lorom(0x01u) };
    static console_t automatic_ntsc{};
    if (!automatic_ntsc.load_cartridge(std::as_bytes(std::span{ ntsc_rom })))
        return fail("automatic_ntsc_load");
    automatic_ntsc.power_on();
    if (automatic_ntsc.hardware_identity().video_standard != video_standard_t::ntsc
        || automatic_ntsc.video_timing().scanlines_per_frame != 262u
        || (automatic_ntsc.read_u8(0x00213fu) & 0x10u) != 0u)
    {
        return fail("automatic_ntsc_selection");
    }
    automatic_ntsc.run_frame();
    if (automatic_ntsc.frame_index() != 1u
        || automatic_ntsc.audio_samples().size() >= 1200u)
    {
        return fail("ntsc_frame_and_apu_timing");
    }

    static console_t forced_ntsc{};
    if (!forced_ntsc.set_hardware_configuration({
            .model = snes_hardware_model_t::late_3chip,
            .region = snes_region_selection_t::ntsc,
        })
        || !forced_ntsc.load_cartridge(std::as_bytes(std::span{ pal_rom })))
    {
        return fail("forced_ntsc_setup");
    }
    forced_ntsc.power_on();
    if (forced_ntsc.hardware_identity().video_standard != video_standard_t::ntsc)
        return fail("forced_ntsc_override");

    return 0;
}
