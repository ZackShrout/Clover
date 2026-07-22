//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Timing.h"

#include <array>
#include <cstdint>
#include <string_view>

namespace clover::core
{
    enum class snes_hardware_model_t : uint8_t
    {
        late_3chip,
        early_3chip,
        scpu_a_3chip,
        one_chip
    };

    enum class snes_cpu_revision_t : uint8_t
    {
        original,
        a,
        b,
        cpuna
    };

    enum class snes_ppu_family_t : uint8_t
    {
        discrete_3chip,
        integrated_1chip
    };

    enum class snes_apu_revision_t : uint8_t
    {
        discrete_smp_dsp,
        integrated_s_apu
    };

    enum class snes_region_selection_t : uint8_t
    {
        automatic,
        ntsc,
        pal
    };

    struct snes_hardware_profile_t
    {
        snes_hardware_model_t model{};
        std::string_view key{};
        std::string_view display_name{};
        snes_cpu_revision_t cpu_revision{};
        snes_ppu_family_t ppu_family{};
        snes_apu_revision_t apu_revision{};
        uint8_t cpu_version{};
        uint8_t ppu1_version{};
        uint8_t ppu2_version{};
        bool implemented{};
    };

    inline constexpr std::array<snes_hardware_profile_t, 4> k_snes_hardware_profiles{
        snes_hardware_profile_t{
            .model = snes_hardware_model_t::late_3chip,
            .key = "late-3chip",
            .display_name = "Late 3-chip (S-CPU B / PPU1 1 / PPU2 3 / S-APU)",
            .cpu_revision = snes_cpu_revision_t::b,
            .ppu_family = snes_ppu_family_t::discrete_3chip,
            .apu_revision = snes_apu_revision_t::integrated_s_apu,
            .cpu_version = 2,
            .ppu1_version = 1,
            .ppu2_version = 3,
            .implemented = true,
        },
        snes_hardware_profile_t{
            .model = snes_hardware_model_t::early_3chip,
            .key = "early-3chip",
            .display_name = "Early 3-chip (original S-CPU / PPU1 1 / PPU2 1)",
            .cpu_revision = snes_cpu_revision_t::original,
            .ppu_family = snes_ppu_family_t::discrete_3chip,
            .apu_revision = snes_apu_revision_t::discrete_smp_dsp,
            .cpu_version = 1,
            .ppu1_version = 1,
            .ppu2_version = 1,
            .implemented = false,
        },
        snes_hardware_profile_t{
            .model = snes_hardware_model_t::scpu_a_3chip,
            .key = "scpu-a-3chip",
            .display_name = "S-CPU A 3-chip",
            .cpu_revision = snes_cpu_revision_t::a,
            .ppu_family = snes_ppu_family_t::discrete_3chip,
            .apu_revision = snes_apu_revision_t::discrete_smp_dsp,
            .cpu_version = 2,
            .ppu1_version = 1,
            .ppu2_version = 3,
            .implemented = false,
        },
        snes_hardware_profile_t{
            .model = snes_hardware_model_t::one_chip,
            .key = "1chip",
            .display_name = "1CHIP / S-CPUN A",
            .cpu_revision = snes_cpu_revision_t::cpuna,
            .ppu_family = snes_ppu_family_t::integrated_1chip,
            .apu_revision = snes_apu_revision_t::integrated_s_apu,
            .cpu_version = 2,
            .ppu1_version = 1,
            .ppu2_version = 3,
            .implemented = false,
        },
    };

    inline constexpr const snes_hardware_profile_t& k_default_snes_hardware_profile{
        k_snes_hardware_profiles[0]
    };

    [[nodiscard]] constexpr const snes_hardware_profile_t* find_snes_hardware_profile(
        std::string_view key
    ) noexcept
    {
        for (const auto& profile : k_snes_hardware_profiles)
        {
            if (profile.key == key)
                return &profile;
        }
        return nullptr;
    }

    struct snes_hardware_configuration_t
    {
        snes_hardware_model_t model{ snes_hardware_model_t::late_3chip };
        snes_region_selection_t region{ snes_region_selection_t::automatic };
    };

    struct snes_hardware_identity_t
    {
        const snes_hardware_profile_t* profile{ &k_default_snes_hardware_profile };
        video_standard_t video_standard{ video_standard_t::ntsc };
    };

    [[nodiscard]] constexpr const snes_hardware_profile_t* snes_hardware_profile(
        snes_hardware_model_t model
    ) noexcept
    {
        for (const auto& profile : k_snes_hardware_profiles)
        {
            if (profile.model == model)
                return &profile;
        }
        return nullptr;
    }
}
