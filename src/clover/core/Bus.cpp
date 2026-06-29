//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Bus.h"

#include "clover/core/Cartridge.h"
#include "clover/core/Cpu.h"
#include "clover/core/Dma.h"
#include "clover/core/Ppu.h"

#include <algorithm>

namespace
{
    [[nodiscard]] bool is_low_wram_mirror_bank(uint8_t bank) noexcept
    {
        return bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu);
    }
} // anonymous namespace

namespace clover::core
{
    void bus_t::connect_cartridge(cartridge_t& cartridge) noexcept
    {
        _cartridge = &cartridge;
    }

    void bus_t::connect_cpu(cpu_t& cpu) noexcept
    {
        _cpu = &cpu;
    }

    void bus_t::connect_ppu(ppu_t& ppu) noexcept
    {
        _ppu = &ppu;
    }

    void bus_t::connect_dma(dma_t& dma) noexcept
    {
        _dma = &dma;
    }

    void bus_t::reset() noexcept
    {
        std::fill(_wram.begin(), _wram.end(), 0);
        _open_bus = 0;
        _pending_cpu_write_count = 0;
    }

    uint8_t bus_t::read_u8(uint32_t address) noexcept
    {
        if (is_wram_address(address))
        {
            _open_bus = _wram[wram_offset(address)];
            return _open_bus;
        }

        if (_ppu != nullptr && is_ppu_register_address(address))
        {
            _open_bus = _ppu->read_register(static_cast<uint16_t>(address & 0xffffu));
            return _open_bus;
        }

        if (_dma != nullptr && is_dma_register_address(address))
        {
            _open_bus = _dma->read_register(static_cast<uint16_t>(address & 0xffffu));
            return _open_bus;
        }

        if (_cpu != nullptr && is_cpu_register_address(address))
        {
            _open_bus = _cpu->read_register(static_cast<uint16_t>(address & 0xffffu));
            return _open_bus;
        }

        if (_cartridge != nullptr)
        {
            _open_bus = _cartridge->read_u8(address);
            return _open_bus;
        }

        return _open_bus;
    }

    void bus_t::write_u8(uint32_t address, uint8_t value) noexcept
    {
        _open_bus = value;
        dispatch_write_u8(address, value);
    }

    void bus_t::write_cpu_u8(uint32_t address, uint8_t value) noexcept
    {
        _open_bus = value;

        if (is_cpu_register_address(address) || is_dma_register_address(address))
        {
            if (_pending_cpu_write_count < _pending_cpu_writes.size())
            {
                _pending_cpu_writes[_pending_cpu_write_count++] = {
                    .address = address,
                    .value = value
                };
            }
            return;
        }

        dispatch_write_u8(address, value);
    }

    void bus_t::commit_cpu_writes() noexcept
    {
        const uint8_t pending_count{ _pending_cpu_write_count };
        _pending_cpu_write_count = 0;

        for (uint8_t index{ 0 }; index < pending_count; ++index)
        {
            const pending_cpu_write_t pending_write{ _pending_cpu_writes[index] };
            dispatch_write_u8(pending_write.address, pending_write.value);
        }
    }

    uint8_t bus_t::open_bus() const noexcept
    {
        return _open_bus;
    }

    void bus_t::dispatch_write_u8(uint32_t address, uint8_t value) noexcept
    {
        if (is_wram_address(address))
        {
            _wram[wram_offset(address)] = value;
            return;
        }

        if (_ppu != nullptr && is_ppu_register_address(address))
        {
            _ppu->write_register(static_cast<uint16_t>(address & 0xffffu), value);
            return;
        }

        if (_dma != nullptr && is_dma_register_address(address))
        {
            _dma->write_register(static_cast<uint16_t>(address & 0xffffu), value);
            return;
        }

        if (_cpu != nullptr && is_cpu_register_address(address))
            _cpu->write_register(static_cast<uint16_t>(address & 0xffffu), value);

        if (_cartridge != nullptr)
            _cartridge->write_u8(address, value);
    }

    bool bus_t::is_wram_address(uint32_t address) noexcept
    {
        if (address >= k_wram_base_address && address < (k_wram_base_address + k_wram_size))
            return true;

        const uint8_t bank{ static_cast<uint8_t>(address >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(address & 0xffffu) };
        return is_low_wram_mirror_bank(bank) && offset < k_low_wram_mirror_size;
    }

    uint32_t bus_t::wram_offset(uint32_t address) noexcept
    {
        if (address >= k_wram_base_address && address < (k_wram_base_address + k_wram_size))
            return address - k_wram_base_address;

        return address & 0x001fffu;
    }

    bool bus_t::is_cpu_register_address(uint32_t address) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return (register_address >= 0x2180u && register_address <= 0x2183u)
            || register_address == 0x4016u
            || register_address == 0x4017u
            || (register_address >= 0x4200u && register_address <= 0x421fu);
    }

    bool bus_t::is_ppu_register_address(uint32_t address) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return register_address >= 0x2100u && register_address <= 0x213fu;
    }

    bool bus_t::is_dma_register_address(uint32_t address) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return (register_address >= 0x420bu && register_address <= 0x420cu)
            || (register_address >= 0x4300u && register_address <= 0x437fu);
    }
}
