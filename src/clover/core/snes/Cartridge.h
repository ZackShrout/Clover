//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clover::core
{
    enum class cartridge_mapping_mode_t : uint8_t
    {
        none,
        bootstrap,
        lorom,
        hirom
    };

    struct cartridge_header_t
    {
        cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::none };
        uint8_t raw_map_mode{ 0 };
        uint16_t reset_vector{ 0 };
    };

    struct cartridge_t
    {
    public:
        static constexpr uint32_t k_bootstrap_program_rom_size{ 64 * 1024u };

        void reset() noexcept;
        [[nodiscard]] bool load(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address) const noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] bool loaded() const noexcept;
        [[nodiscard]] cartridge_mapping_mode_t mapping_mode() const noexcept;
        [[nodiscard]] const cartridge_header_t& header() const noexcept;

    private:
        struct header_candidate_t
        {
            cartridge_mapping_mode_t mapping_mode{ cartridge_mapping_mode_t::none };
            uint8_t raw_map_mode{ 0 };
            uint16_t reset_vector{ 0 };
            int score{ -1 };
        };

        [[nodiscard]] static bool is_bootstrap_program_rom_address(uint32_t address) noexcept;
        [[nodiscard]] static bool has_copier_header(std::span<const std::byte> rom_data) noexcept;
        [[nodiscard]] static uint32_t lorom_rom_offset(uint32_t address, size_t rom_size) noexcept;
        [[nodiscard]] static uint32_t hirom_rom_offset(uint32_t address, size_t rom_size) noexcept;
        [[nodiscard]] static bool is_lorom_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_hirom_address(uint32_t address) noexcept;
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
        cartridge_header_t _header{};
        cartridge_mapping_mode_t _mapping_mode{ cartridge_mapping_mode_t::bootstrap };
        bool _loaded{ false };
    };
}
