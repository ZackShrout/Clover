//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cartridge.h"

#include <algorithm>
#include <span>

namespace clover::core
{
    void cartridge_t::reset() noexcept
    {
        if (!_loaded)
            std::fill(_bootstrap_program_rom.begin(), _bootstrap_program_rom.end(), 0);
    }

    bool cartridge_t::load(std::span<const std::byte> rom_data) noexcept
    {
        unload();

        if (rom_data.empty())
            return false;

        if (has_copier_header(rom_data))
            rom_data = rom_data.subspan(512);

        if (rom_data.empty())
            return false;

        _rom_data.resize(rom_data.size());
        for (size_t index{ 0 }; index < rom_data.size(); ++index)
            _rom_data[index] = static_cast<uint8_t>(rom_data[index]);

        if (!detect_header())
        {
            unload();
            return false;
        }

        _loaded = true;
        return true;
    }

    uint8_t cartridge_t::read_u8(uint32_t address) const noexcept
    {
        if (_loaded)
        {
            if (!_ram_data.empty())
            {
                if (_mapping_mode == cartridge_mapping_mode_t::lorom
                    && is_lorom_ram_address(address))
                {
                    return _ram_data[lorom_ram_offset(address, _ram_data.size())];
                }
                if (_mapping_mode == cartridge_mapping_mode_t::hirom
                    && is_hirom_ram_address(address))
                {
                    return _ram_data[hirom_ram_offset(address, _ram_data.size())];
                }
            }

            switch (_mapping_mode)
            {
            case cartridge_mapping_mode_t::lorom:
                if (!is_lorom_address(address))
                    return 0;
                return _rom_data[lorom_rom_offset(address, _rom_data.size())];
            case cartridge_mapping_mode_t::hirom:
                if (!is_hirom_address(address))
                    return 0;
                return _rom_data[hirom_rom_offset(address, _rom_data.size())];
            default:
                return 0;
            }
        }

        if (!is_bootstrap_program_rom_address(address))
            return 0;

        return _bootstrap_program_rom[address & 0xffffu];
    }

    void cartridge_t::write_u8(uint32_t address, uint8_t value) noexcept
    {
        if (_loaded)
        {
            if (!_ram_data.empty())
            {
                if (_mapping_mode == cartridge_mapping_mode_t::lorom
                    && is_lorom_ram_address(address))
                {
                    _ram_data[lorom_ram_offset(address, _ram_data.size())] = value;
                }
                else if (_mapping_mode == cartridge_mapping_mode_t::hirom
                         && is_hirom_ram_address(address))
                {
                    _ram_data[hirom_ram_offset(address, _ram_data.size())] = value;
                }
            }
            return;
        }

        if (!is_bootstrap_program_rom_address(address))
            return;

        _bootstrap_program_rom[address & 0xffffu] = value;
    }

    bool cartridge_t::loaded() const noexcept
    {
        return _loaded;
    }

    cartridge_mapping_mode_t cartridge_t::mapping_mode() const noexcept
    {
        return _mapping_mode;
    }

    const cartridge_header_t& cartridge_t::header() const noexcept
    {
        return _header;
    }

    bool cartridge_t::is_bootstrap_program_rom_address(uint32_t address) noexcept
    {
        return (address & 0xff0000u) == 0x000000u;
    }

    bool cartridge_t::has_copier_header(std::span<const std::byte> rom_data) noexcept
    {
        return (rom_data.size() % 0x8000u) == 512u;
    }

    uint32_t cartridge_t::lorom_rom_offset(uint32_t address, size_t rom_size) noexcept
    {
        const uint32_t bank{ (address >> 16u) & 0xffu };
        const uint32_t half_bank_offset{ address & 0x7fffu };
        const uint32_t linear_offset{ ((bank & 0x7fu) << 15u) | half_bank_offset };
        return linear_offset % static_cast<uint32_t>(rom_size);
    }

    uint32_t cartridge_t::hirom_rom_offset(uint32_t address, size_t rom_size) noexcept
    {
        const uint32_t bank{ (address >> 16u) & 0xffu };
        const uint32_t bank_offset{ address & 0xffffu };
        const uint32_t linear_offset{ ((bank & 0x3fu) << 16u) | bank_offset };
        return linear_offset % static_cast<uint32_t>(rom_size);
    }

    bool cartridge_t::is_lorom_ram_address(uint32_t address) noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        return ((bank >= 0x70u && bank <= 0x7du) || bank >= 0xf0u) && offset < 0x8000u;
    }

    bool cartridge_t::is_hirom_ram_address(uint32_t address) noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        return ((bank >= 0x20u && bank <= 0x3fu) || (bank >= 0xa0u && bank <= 0xbfu))
            && offset >= 0x6000u && offset < 0x8000u;
    }

    uint32_t cartridge_t::lorom_ram_offset(uint32_t address, size_t ram_size) noexcept
    {
        const uint32_t linear_offset{
            ((address >> 16u) & 0x0fu) * 0x8000u + (address & 0x7fffu)
        };
        return linear_offset % static_cast<uint32_t>(ram_size);
    }

    uint32_t cartridge_t::hirom_ram_offset(uint32_t address, size_t ram_size) noexcept
    {
        const uint32_t linear_offset{
            ((address >> 16u) & 0x1fu) * 0x2000u + (address & 0x1fffu)
        };
        return linear_offset % static_cast<uint32_t>(ram_size);
    }

    bool cartridge_t::is_lorom_address(uint32_t address) noexcept
    {
        const uint32_t bank{ (address >> 16u) & 0xffu };
        const uint16_t offset{ static_cast<uint16_t>(address & 0xffffu) };

        if ((bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu)) && offset >= 0x8000u)
            return true;

        return bank >= 0x40u && bank <= 0xffu;
    }

    bool cartridge_t::is_hirom_address(uint32_t address) noexcept
    {
        const uint32_t bank{ (address >> 16u) & 0xffu };
        const uint16_t offset{ static_cast<uint16_t>(address & 0xffffu) };

        if ((bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu)) && offset >= 0x8000u)
            return true;

        return (bank >= 0x40u && bank <= 0x7du) || bank >= 0xc0u;
    }

    cartridge_t::header_candidate_t cartridge_t::score_lorom_header(
        std::span<const uint8_t> rom_data
    ) noexcept
    {
        header_candidate_t candidate{};
        candidate.mapping_mode = cartridge_mapping_mode_t::lorom;
        if (rom_data.size() < 0x8000u)
            return candidate;

        const size_t header_offset{ 0x7fc0u };
        candidate.raw_map_mode = rom_data[header_offset + 0x15u];
        candidate.raw_ram_size = rom_data[header_offset + 0x18u];
        candidate.reset_vector = static_cast<uint16_t>(
            rom_data[header_offset + 0x3cu] | (rom_data[header_offset + 0x3du] << 8u)
        );
        candidate.score = score_header_candidate(rom_data,
                                                 header_offset,
                                                 cartridge_mapping_mode_t::lorom);
        return candidate;
    }

    cartridge_t::header_candidate_t cartridge_t::score_hirom_header(
        std::span<const uint8_t> rom_data
    ) noexcept
    {
        header_candidate_t candidate{};
        candidate.mapping_mode = cartridge_mapping_mode_t::hirom;
        if (rom_data.size() < 0x10000u)
            return candidate;

        const size_t header_offset{ 0xffc0u };
        candidate.raw_map_mode = rom_data[header_offset + 0x15u];
        candidate.raw_ram_size = rom_data[header_offset + 0x18u];
        candidate.reset_vector = static_cast<uint16_t>(
            rom_data[header_offset + 0x3cu] | (rom_data[header_offset + 0x3du] << 8u)
        );
        candidate.score = score_header_candidate(rom_data,
                                                 header_offset,
                                                 cartridge_mapping_mode_t::hirom);
        return candidate;
    }

    int cartridge_t::score_header_candidate(std::span<const uint8_t> rom_data,
                                            size_t header_offset,
                                            cartridge_mapping_mode_t expected_mode) noexcept
    {
        if (rom_data.size() < header_offset + 0x40u)
            return -1;

        int score{ 0 };
        const uint8_t raw_map_mode{ rom_data[header_offset + 0x15u] };
        const uint16_t checksum{
            static_cast<uint16_t>(rom_data[header_offset + 0x1eu] | (rom_data[header_offset + 0x1fu] << 8u))
        };
        const uint16_t complement{
            static_cast<uint16_t>(rom_data[header_offset + 0x1cu] | (rom_data[header_offset + 0x1du] << 8u))
        };
        const uint16_t reset_vector{
            static_cast<uint16_t>(rom_data[header_offset + 0x3cu] | (rom_data[header_offset + 0x3du] << 8u))
        };

        if (((checksum ^ complement) & 0xffffu) == 0xffffu)
            score += 8;

        if (reset_vector != 0 && reset_vector != 0xffffu)
            score += 8;

        const uint8_t mapper_family{ static_cast<uint8_t>(raw_map_mode & 0x0fu) };
        if ((expected_mode == cartridge_mapping_mode_t::lorom && mapper_family == 0x00u)
            || (expected_mode == cartridge_mapping_mode_t::hirom && mapper_family == 0x01u))
        {
            score += 4;
        }

        if (rom_data[header_offset + 0x16u] != 0)
            score += 1;

        return score;
    }

    bool cartridge_t::detect_header() noexcept
    {
        const std::span<const uint8_t> rom_span{ _rom_data.data(), _rom_data.size() };
        const header_candidate_t lorom_candidate{ score_lorom_header(rom_span) };
        const header_candidate_t hirom_candidate{ score_hirom_header(rom_span) };
        const header_candidate_t winner{
            hirom_candidate.score > lorom_candidate.score ? hirom_candidate : lorom_candidate
        };

        if (winner.score < 0)
            return false;

        _mapping_mode = winner.mapping_mode;
        _header.mapping_mode = winner.mapping_mode;
        _header.raw_map_mode = winner.raw_map_mode;
        _header.raw_ram_size = winner.raw_ram_size;
        _header.reset_vector = winner.reset_vector;
        if (winner.raw_ram_size != 0u && winner.raw_ram_size < 16u)
            _ram_data.assign(static_cast<size_t>(1024u) << winner.raw_ram_size, 0u);
        return true;
    }

    void cartridge_t::unload() noexcept
    {
        _rom_data.clear();
        _ram_data.clear();
        _header = {};
        _mapping_mode = cartridge_mapping_mode_t::bootstrap;
        _loaded = false;
    }
}
