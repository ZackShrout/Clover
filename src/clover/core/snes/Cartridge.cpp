//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cartridge.h"

#include <algorithm>
#include <span>
#include <string_view>

namespace clover::core
{
    void cartridge_t::reset() noexcept
    {
        if (!_loaded)
            std::fill(_bootstrap_program_rom.begin(), _bootstrap_program_rom.end(), 0);
        else if (_hardware == cartridge_hardware_t::cx4)
            _cx4.power_on(_rom_data);
        else if (_hardware == cartridge_hardware_t::dsp1)
            _dsp1->power_on();
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

    uint8_t cartridge_t::read_u8(uint32_t address, uint8_t open_bus) const noexcept
    {
        if (_loaded)
        {
            if (_hardware == cartridge_hardware_t::cx4 && is_cx4_address(address))
                return _cx4.read(address, open_bus);
            if (_hardware == cartridge_hardware_t::dsp1)
            {
                if (is_dsp1_data_address(address))
                    return _dsp1->read_data();
                if (is_dsp1_status_address(address))
                    return _dsp1->read_status();
            }
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
            if (_hardware == cartridge_hardware_t::cx4 && is_cx4_address(address))
            {
                _cx4.write(address, value);
                return;
            }
            if (_hardware == cartridge_hardware_t::dsp1 && is_dsp1_data_address(address))
            {
                _dsp1->write_data(value);
                return;
            }
            if (!_ram_data.empty())
            {
                if (_mapping_mode == cartridge_mapping_mode_t::lorom
                    && is_lorom_ram_address(address))
                {
                    uint8_t& destination{ _ram_data[lorom_ram_offset(address, _ram_data.size())] };
                    if (destination != value)
                    {
                        destination = value;
                        _ram_dirty = true;
                    }
                }
                else if (_mapping_mode == cartridge_mapping_mode_t::hirom
                         && is_hirom_ram_address(address))
                {
                    uint8_t& destination{ _ram_data[hirom_ram_offset(address, _ram_data.size())] };
                    if (destination != value)
                    {
                        destination = value;
                        _ram_dirty = true;
                    }
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

    cartridge_hardware_t cartridge_t::hardware() const noexcept
    {
        return _hardware;
    }

    video_standard_t cartridge_t::declared_video_standard() const noexcept
    {
        // Nintendo destination codes $02-$0c and $11 designate 50 Hz markets.
        // Brazil ($10) is PAL-M encoding on 60 Hz console timing.
        const uint8_t destination{ _header.destination_code };
        return (destination >= 0x02u && destination <= 0x0cu) || destination == 0x11u
            ? video_standard_t::pal
            : video_standard_t::ntsc;
    }

    std::span<const std::byte> cartridge_t::persistent_memory() const noexcept
    {
        return std::as_bytes(std::span<const uint8_t>{ _ram_data });
    }

    bool cartridge_t::load_persistent_memory(std::span<const std::byte> data) noexcept
    {
        if (data.size() != _ram_data.size())
            return false;

        for (size_t index{ 0 }; index < data.size(); ++index)
            _ram_data[index] = static_cast<uint8_t>(data[index]);
        _ram_dirty = false;
        return true;
    }

    bool cartridge_t::persistent_memory_dirty() const noexcept
    {
        return _ram_dirty;
    }

    void cartridge_t::mark_persistent_memory_clean() noexcept
    {
        _ram_dirty = false;
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

    bool cartridge_t::is_cx4_address(uint32_t address) noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        return (bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu))
            && offset >= 0x6000u && offset <= 0x7fffu;
    }

    bool cartridge_t::is_dsp1_data_address(uint32_t address) const noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>((address >> 16u) & 0x7fu) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        if ((_header.raw_map_mode & 0x0fu) == 0x01u)
            return bank <= 0x0fu && offset >= 0x6000u && offset <= 0x6fffu;
        return bank >= 0x30u && bank <= 0x3fu && offset >= 0x8000u && offset <= 0xbfffu;
    }

    bool cartridge_t::is_dsp1_status_address(uint32_t address) const noexcept
    {
        const uint8_t bank{ static_cast<uint8_t>((address >> 16u) & 0x7fu) };
        const uint16_t offset{ static_cast<uint16_t>(address) };
        if ((_header.raw_map_mode & 0x0fu) == 0x01u)
            return bank <= 0x0fu && offset >= 0x7000u && offset <= 0x7fffu;
        return bank >= 0x30u && bank <= 0x3fu && offset >= 0xc000u;
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
        candidate.raw_cartridge_type = rom_data[header_offset + 0x16u];
        candidate.raw_ram_size = rom_data[header_offset + 0x18u];
        candidate.destination_code = rom_data[header_offset + 0x19u];
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
        candidate.raw_cartridge_type = rom_data[header_offset + 0x16u];
        candidate.raw_ram_size = rom_data[header_offset + 0x18u];
        candidate.destination_code = rom_data[header_offset + 0x19u];
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
        _header.raw_cartridge_type = winner.raw_cartridge_type;
        _header.raw_ram_size = winner.raw_ram_size;
        _header.destination_code = winner.destination_code;
        _header.reset_vector = winner.reset_vector;
        _hardware = cartridge_hardware_t::base;
        if (winner.raw_cartridge_type == 0xf3u)
            _hardware = cartridge_hardware_t::cx4;
        else if ((winner.raw_cartridge_type & 0xf0u) == 0u
                 && (winner.raw_cartridge_type & 0x0fu) >= 3u
                 && (winner.raw_cartridge_type & 0x0fu) <= 6u)
        {
            const size_t header_offset{ winner.mapping_mode == cartridge_mapping_mode_t::hirom
                ? 0xffc0u : 0x7fc0u };
            const std::string_view title{
                reinterpret_cast<const char*>(_rom_data.data() + header_offset), 21u
            };
            if (title.starts_with("DUNGEON MASTER"))
                _hardware = cartridge_hardware_t::dsp2;
            else if (title.starts_with("SD") && title.find("GX") != std::string_view::npos)
                _hardware = cartridge_hardware_t::dsp3;
            else if (title.starts_with("PLANETS CHAMP TG3000") || title.starts_with("TOP GEAR 3000"))
                _hardware = cartridge_hardware_t::dsp4;
            else
                _hardware = cartridge_hardware_t::dsp1;
        }
        if (_hardware == cartridge_hardware_t::dsp1)
        {
            _dsp1 = std::make_unique<dsp1_t>();
            _dsp1->power_on();
        }
        if (winner.raw_ram_size != 0u && winner.raw_ram_size < 16u)
            _ram_data.assign(static_cast<size_t>(1024u) << winner.raw_ram_size, 0u);
        _ram_dirty = false;
        return true;
    }

    void cartridge_t::unload() noexcept
    {
        _rom_data.clear();
        _ram_data.clear();
        _header = {};
        _mapping_mode = cartridge_mapping_mode_t::bootstrap;
        _hardware = cartridge_hardware_t::base;
        _loaded = false;
        _dsp1.reset();
        _ram_dirty = false;
    }
}
