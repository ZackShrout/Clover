//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <memory>
#include <vector>

#include "clover/core/snes/Timing.h"
#include "clover/core/snes/Cx4.h"
#include "clover/core/snes/Dsp1.h"
#include "clover/core/snes/Dsp2.h"
#include "clover/core/snes/Dsp3.h"
#include "clover/core/snes/Dsp4.h"
#include "clover/core/snes/SuperFx.h"

namespace clover::core
{
    enum class cartridge_mapping_mode_t : uint8_t
    {
        none,
        bootstrap,
        lorom,
        hirom
    };

    enum class cartridge_hardware_t : uint8_t
    {
        base,
        cx4,
        dsp1,
        dsp2,
        dsp3,
        dsp4,
        super_fx
    };

    enum class cartridge_address_kind_t : uint8_t
    {
        unmapped,
        program_rom,
        cartridge_ram,
        device,
        bootstrap_program
    };

    enum class cartridge_state_result_t : uint8_t
    {
        success,
        unsupported_hardware,
        topology_mismatch,
        invalid_state,
        allocation_failed
    };

    struct cartridge_address_mapping_t
    {
        cartridge_address_kind_t kind{ cartridge_address_kind_t::unmapped };
        uint32_t storage_offset{ 0 };
    };

    struct cartridge_header_t
    {
        cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::none };
        uint8_t raw_map_mode{ 0 };
        uint8_t raw_cartridge_type{ 0 };
        uint8_t raw_ram_size{ 0 };
        uint8_t destination_code{ 0 };
        uint16_t reset_vector{ 0 };

        [[nodiscard]] bool operator==(const cartridge_header_t&) const noexcept = default;
    };

    struct cartridge_t
    {
    public:
        static constexpr uint32_t k_bootstrap_program_rom_size{ 64 * 1024u };

        struct causal_state_t
        {
            static constexpr uint32_t schema_version{ 1 };

            std::array<uint8_t, k_bootstrap_program_rom_size> bootstrap_program_rom{};
            std::vector<uint8_t> ram_data{};
            size_t canonical_media_size{ 0 };
            cartridge_header_t header{};
            cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::bootstrap };
            cartridge_hardware_t hardware{ cartridge_hardware_t::base };
            bool loaded{ false };
            bool ram_persistent{ false };
            bool ram_dirty{ false };

            [[nodiscard]] bool operator==(const causal_state_t&) const noexcept = default;
        };

        void reset() noexcept;
        [[nodiscard]] bool load(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address, uint8_t open_bus = 0) const noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] bool loaded() const noexcept;
        [[nodiscard]] std::span<const std::byte> canonical_media() const noexcept;
        [[nodiscard]] cartridge_address_mapping_t translate_address(uint32_t address) const noexcept;
        [[nodiscard]] bool inspect_u8(uint32_t address, uint8_t& value) const noexcept;
        [[nodiscard]] cartridge_mapping_mode_t mapping_mode() const noexcept;
        [[nodiscard]] const cartridge_header_t& header() const noexcept;
        [[nodiscard]] cartridge_hardware_t hardware() const noexcept;
        [[nodiscard]] video_standard_t declared_video_standard() const noexcept;
        [[nodiscard]] std::span<const uint8_t> expansion_memory() const noexcept;
        [[nodiscard]] std::span<const std::byte> persistent_memory() const noexcept;
        [[nodiscard]] bool load_persistent_memory(std::span<const std::byte> data) noexcept;
        [[nodiscard]] bool persistent_memory_dirty() const noexcept;
        void mark_persistent_memory_clean() noexcept;
        void step_coprocessor(master_clock_delta_t clocks) noexcept;
        [[nodiscard]] bool coprocessor_irq_pending() const noexcept;
        [[nodiscard]] cartridge_state_result_t capture_causal_state(
            causal_state_t& state
        ) const noexcept;
        [[nodiscard]] cartridge_state_result_t restore_causal_state(
            const causal_state_t& state
        ) noexcept;

    private:
        struct header_candidate_t
        {
            cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::none };
            uint8_t raw_map_mode{ 0 };
            uint8_t raw_cartridge_type{ 0 };
            uint8_t raw_ram_size{ 0 };
            uint8_t destination_code{ 0 };
            uint16_t reset_vector{ 0 };
            int score{ -1 };
        };

        [[nodiscard]] static bool is_bootstrap_program_rom_address(uint32_t address) noexcept;
        [[nodiscard]] static bool has_copier_header(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] static uint32_t lorom_rom_offset(uint32_t address, size_t rom_size) noexcept;
        [[nodiscard]] static uint32_t hirom_rom_offset(uint32_t address, size_t rom_size) noexcept;
        [[nodiscard]] static bool is_lorom_ram_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_hirom_ram_address(uint32_t address) noexcept;
        [[nodiscard]] static uint32_t lorom_ram_offset(uint32_t address, size_t ram_size) noexcept;
        [[nodiscard]] static uint32_t hirom_ram_offset(uint32_t address, size_t ram_size) noexcept;
        [[nodiscard]] static bool is_lorom_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_hirom_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_cx4_address(uint32_t address) noexcept;
        [[nodiscard]] bool is_dsp1_data_address(uint32_t address) const noexcept;
        [[nodiscard]] bool is_dsp1_status_address(uint32_t address) const noexcept;
        [[nodiscard]] static bool is_dsp2_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dsp3_data_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dsp3_status_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dsp4_data_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dsp4_status_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_super_fx_register_address(uint32_t address) noexcept;
        [[nodiscard]] bool is_super_fx_rom_address(uint32_t address) const noexcept;
        [[nodiscard]] bool is_super_fx_ram_address(uint32_t address) const noexcept;
        [[nodiscard]] uint32_t super_fx_rom_offset(uint32_t address) const noexcept;
        [[nodiscard]] uint32_t super_fx_ram_offset(uint32_t address) const noexcept;
        [[nodiscard]] static header_candidate_t score_lorom_header(std::span<const uint8_t> rom_data) noexcept;
        [[nodiscard]] static header_candidate_t score_hirom_header(std::span<const uint8_t> rom_data) noexcept;
        [[nodiscard]] static int score_header_candidate(std::span<const uint8_t> rom_data,
                                                        size_t header_offset,
                                                        cartridge_mapping_mode_t expected_mode) noexcept;
        [[nodiscard]] bool detect_header() noexcept;
        void unload() noexcept;

        // Temporary bring-up surface for vectors and synthetic CPU programs.
        std::array<uint8_t, k_bootstrap_program_rom_size> _bootstrap_program_rom{};
        std::vector<uint8_t> _rom_data{};
        std::vector<uint8_t> _ram_data{};
        cartridge_header_t _header{};
        cartridge_mapping_mode_t _mapping_mode{ cartridge_mapping_mode_t::bootstrap };
        cartridge_hardware_t _hardware{ cartridge_hardware_t::base };
        cx4_t _cx4{};
        mutable std::unique_ptr<dsp1_t> _dsp1{};
        mutable dsp2_t _dsp2{};
        mutable std::unique_ptr<dsp3_t> _dsp3{};
        mutable std::unique_ptr<dsp4_t> _dsp4{};
        mutable super_fx_t _super_fx{};
        bool _loaded{ false };
        bool _ram_persistent{ false };
        bool _ram_dirty{ false };
    };

    using cartridge_causal_state_t = cartridge_t::causal_state_t;
}
