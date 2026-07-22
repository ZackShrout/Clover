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
        dsp4
    };

    struct cartridge_header_t
    {
        cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::none };
        uint8_t raw_map_mode{ 0 };
        uint8_t raw_cartridge_type{ 0 };
        uint8_t raw_ram_size{ 0 };
        uint8_t destination_code{ 0 };
        uint16_t reset_vector{ 0 };
    };

    struct cartridge_t
    {
    public:
        static constexpr uint32_t k_bootstrap_program_rom_size{ 64 * 1024u };

        void reset() noexcept;
        [[nodiscard]] bool load(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address, uint8_t open_bus = 0) const noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] bool loaded() const noexcept;
        [[nodiscard]] cartridge_mapping_mode_t mapping_mode() const noexcept;
        [[nodiscard]] const cartridge_header_t& header() const noexcept;
        [[nodiscard]] cartridge_hardware_t hardware() const noexcept;
        [[nodiscard]] video_standard_t declared_video_standard() const noexcept;
        [[nodiscard]] std::span<const std::byte> persistent_memory() const noexcept;
        [[nodiscard]] bool load_persistent_memory(std::span<const std::byte> data) noexcept;
        [[nodiscard]] bool persistent_memory_dirty() const noexcept;
        void mark_persistent_memory_clean() noexcept;

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
        bool _loaded{ false };
        bool _ram_dirty{ false };
    };
}
