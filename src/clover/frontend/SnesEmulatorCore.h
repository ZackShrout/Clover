//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Console.h"
#include "clover/frontend/EmulatorCore.h"

#include <array>
#include <memory>

namespace clover::frontend
{
    namespace snes_debug
    {
        inline constexpr execution_domain_id_t k_main_cpu_domain{ 1u };
        inline constexpr execution_domain_id_t k_audio_cpu_domain{ 2u };
        inline constexpr address_space_id_t k_cpu_bus_space{ 1u };
        inline constexpr address_space_id_t k_wram_space{ 2u };
        inline constexpr address_space_id_t k_canonical_media_space{ 3u };
        inline constexpr address_space_id_t k_apu_ram_space{ 4u };
        inline constexpr address_space_id_t k_cgram_space{ 5u };
        inline constexpr address_space_id_t k_vram_space{ 6u };
    }

    [[nodiscard]] uint16_t snes_joypad_state(const gamepad_state_t& state) noexcept;

    struct snes_emulator_core_t final
        : emulator_core_t
        , video_plane_control_t
        , debug_target_t
        , execution_control_t
        , observation_control_t
        , debug_session_control_t
        , checkpoint_control_t
    {
    public:
        [[nodiscard]] system_id_t system() const noexcept override;
        [[nodiscard]] bool load_media(std::span<const std::byte> media) noexcept override;
        void power_on() noexcept override;
        void reset() noexcept override;
        void set_gamepad_state(uint32_t port, const gamepad_state_t& state) noexcept override;
        void run_frame() noexcept override;
        [[nodiscard]] display_info_t display_info() const noexcept override;
        [[nodiscard]] video_frame_view_t video_frame() const noexcept override;
        [[nodiscard]] audio_frame_view_t audio_frame() const noexcept override;
        [[nodiscard]] std::span<const std::byte> persistent_memory() const noexcept override;
        [[nodiscard]] bool load_persistent_memory(std::span<const std::byte> data) noexcept override;
        [[nodiscard]] bool persistent_memory_dirty() const noexcept override;
        void mark_persistent_memory_clean() noexcept override;
        [[nodiscard]] video_plane_control_t* video_plane_control() noexcept override;
        [[nodiscard]] debug_target_t* debug_target() noexcept override;
        [[nodiscard]] std::span<const video_plane_descriptor_t> video_planes() const noexcept override;
        [[nodiscard]] bool set_video_plane_enabled(video_plane_id_t id,
                                                   bool enabled) noexcept override;
        [[nodiscard]] std::span<const execution_domain_descriptor_t>
            execution_domains() const noexcept override;
        [[nodiscard]] std::span<const address_space_descriptor_t>
            address_spaces() const noexcept override;
        [[nodiscard]] memory_inspection_result_t inspect_memory(
            debug_address_t address,
            std::span<std::byte> destination
        ) const noexcept override;
        [[nodiscard]] address_translation_result_t translate_address(
            debug_address_t source,
            address_space_id_t destination_space
        ) const noexcept override;
        [[nodiscard]] std::span<const processor_register_descriptor_t>
            processor_registers(execution_domain_id_t domain) const noexcept override;
        [[nodiscard]] processor_state_result_t inspect_processor_state(
            execution_domain_id_t domain,
            std::span<processor_register_value_t> destination
        ) const noexcept override;
        [[nodiscard]] execution_control_t* execution_control() noexcept override;
        [[nodiscard]] execution_step_result_t step_execution_domain(
            execution_domain_id_t domain
        ) noexcept override;
        [[nodiscard]] observation_control_t* observation_control() noexcept override;
        [[nodiscard]] observation_mask_t available_observations() const noexcept override;
        [[nodiscard]] observation_mask_t observation_mask() const noexcept override;
        [[nodiscard]] bool set_observation_mask(observation_mask_t mask) noexcept override;
        [[nodiscard]] observation_drain_result_t drain_observations(
            std::span<observation_event_t> destination
        ) noexcept override;
        void clear_observations() noexcept override;
        [[nodiscard]] debug_session_control_t* debug_session_control() noexcept override;
        [[nodiscard]] debug_session_state_t debug_session_state() const noexcept override;
        [[nodiscard]] debug_session_transition_result_t pause_debug_session() noexcept override;
        [[nodiscard]] debug_session_transition_result_t resume_debug_session() noexcept override;
        [[nodiscard]] checkpoint_control_t* checkpoint_control() noexcept override;
        [[nodiscard]] checkpoint_operation_result_t capture_checkpoint(
            std::vector<std::byte>& checkpoint
        ) noexcept override;
        [[nodiscard]] checkpoint_operation_result_t restore_checkpoint(
            std::span<const std::byte> checkpoint
        ) noexcept override;
        [[nodiscard]] bool set_hardware_configuration(
            core::snes_hardware_configuration_t configuration
        ) noexcept;
        [[nodiscard]] core::snes_hardware_identity_t hardware_identity() const noexcept;

    private:
        core::console_t _console{};
        uint8_t _visible_layer_mask{ core::ppu_presentation_options_t::k_all_layers_visible };
        std::array<video_plane_descriptor_t, 5> _video_planes{
            video_plane_descriptor_t{ 0u, "BG1", true },
            video_plane_descriptor_t{ 1u, "BG2", true },
            video_plane_descriptor_t{ 2u, "BG3", true },
            video_plane_descriptor_t{ 3u, "BG4", true },
            video_plane_descriptor_t{ 4u, "Objects", true }
        };
        std::array<execution_domain_descriptor_t, 2> _execution_domains{
            execution_domain_descriptor_t{
                snes_debug::k_main_cpu_domain,
                "snes.main-cpu",
                "S-CPU",
                processor_architecture_t::wdc_65c816
            },
            execution_domain_descriptor_t{
                snes_debug::k_audio_cpu_domain,
                "snes.audio-cpu",
                "S-SMP",
                processor_architecture_t::sony_spc700
            }
        };
        std::array<address_space_descriptor_t, 6> _address_spaces{
            address_space_descriptor_t{
                snes_debug::k_cpu_bus_space,
                "snes.cpu-bus",
                "CPU Bus",
                address_space_kind_t::bus,
                24u,
                0x01000000u
            },
            address_space_descriptor_t{
                snes_debug::k_wram_space,
                "snes.wram",
                "WRAM",
                address_space_kind_t::memory,
                17u,
                core::bus_t::k_wram_size
            },
            address_space_descriptor_t{
                snes_debug::k_canonical_media_space,
                "media.canonical",
                "Canonical Media",
                address_space_kind_t::canonical_media,
                32u,
                0u
            },
            address_space_descriptor_t{
                snes_debug::k_apu_ram_space,
                "snes.apu-ram",
                "APU RAM",
                address_space_kind_t::memory,
                16u,
                0x00010000u
            },
            address_space_descriptor_t{
                snes_debug::k_cgram_space,
                "snes.cgram",
                "PPU CGRAM",
                address_space_kind_t::memory,
                9u,
                512u
            },
            address_space_descriptor_t{
                snes_debug::k_vram_space,
                "snes.vram",
                "PPU VRAM",
                address_space_kind_t::memory,
                16u,
                65536u
            }
        };
        std::array<processor_register_descriptor_t, 10> _main_cpu_registers{
            processor_register_descriptor_t{ "pc", "PC", 16u },
            processor_register_descriptor_t{ "sp", "SP", 16u },
            processor_register_descriptor_t{ "a", "A", 16u },
            processor_register_descriptor_t{ "x", "X", 16u },
            processor_register_descriptor_t{ "y", "Y", 16u },
            processor_register_descriptor_t{ "d", "D", 16u },
            processor_register_descriptor_t{ "p", "P", 8u },
            processor_register_descriptor_t{ "db", "DB", 8u },
            processor_register_descriptor_t{ "pb", "PB", 8u },
            processor_register_descriptor_t{ "e", "E", 1u }
        };
        static constexpr size_t k_observation_capacity{ 1024u };
        std::unique_ptr<core::snes_observation_event_t[]> _observation_storage{};
        core::snes_observation_sink_t _observation_sink{};
        observation_mask_t _observation_mask{ 0 };
        bool _machine_running{ false };
        bool _debug_paused{ false };
    };
}
