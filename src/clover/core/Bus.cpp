//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Bus.h"

#include "clover/core/Apu.h"
#include "clover/core/Cartridge.h"
#include "clover/core/Cpu.h"
#include "clover/core/Dma.h"
#include "clover/core/Ppu.h"

#include <algorithm>

namespace
{
    constexpr uint32_t k_watch_brightness_offset{ 0x0daeu };
    constexpr uint32_t k_watch_hdmaen_offset{ 0x0d9fu };
    constexpr uint32_t k_watch_lowram_target{ 0x0003u };
    constexpr uint32_t k_watch_upload_window_start{ 0x0d84u };
    constexpr uint32_t k_watch_upload_window_end{ 0x0d99u };
    constexpr uint32_t k_watch_stack_window_start{ 0x15f0u };
    constexpr uint32_t k_watch_stack_window_end{ 0x160fu };

    [[nodiscard]] bool is_low_wram_mirror_bank(uint8_t bank) noexcept
    {
        return bank <= 0x3fu || (bank >= 0x80u && bank <= 0xbfu);
    }

    [[nodiscard]] bool should_trace_ppu_register_write(uint16_t address) noexcept
    {
        switch (address)
        {
        case 0x2100u:
        case 0x2102u:
        case 0x2103u:
        case 0x2104u:
        case 0x2115u:
        case 0x2116u:
        case 0x2117u:
        case 0x2118u:
        case 0x2119u:
        case 0x2121u:
        case 0x2122u:
            return true;
        default:
            return false;
        }
    }

    [[nodiscard]] bool should_trace_system_register_write(uint32_t address) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        if (register_address >= 0x2181u && register_address <= 0x2183u)
            return true;

        if (register_address == 0x420bu || register_address == 0x420cu)
            return true;

        return register_address >= 0x4300u && register_address <= 0x437fu;
    }

    [[nodiscard]] bool should_trace_wram_write(uint32_t offset) noexcept
    {
        return offset == k_watch_brightness_offset
            || offset == k_watch_hdmaen_offset
            || offset == k_watch_lowram_target
            || (offset >= k_watch_upload_window_start && offset <= k_watch_upload_window_end)
            || (offset >= k_watch_stack_window_start && offset <= k_watch_stack_window_end);
    }

    void accumulate_ppu_step_result(clover::core::ppu_step_result_t& aggregate,
                                    const clover::core::ppu_step_result_t& step) noexcept
    {
        aggregate.timing = step.timing;
        aggregate.visible_scanlines = step.visible_scanlines;
        aggregate.interlace = step.interlace;
        aggregate.frame_complete = aggregate.frame_complete || step.frame_complete;
        aggregate.entered_scanline = aggregate.entered_scanline || step.entered_scanline;
        aggregate.entered_frame_start = aggregate.entered_frame_start || step.entered_frame_start;
        aggregate.entered_hblank = aggregate.entered_hblank || step.entered_hblank;
        aggregate.entered_vblank = aggregate.entered_vblank || step.entered_vblank;
        aggregate.hdma_setup_triggered = aggregate.hdma_setup_triggered || step.hdma_setup_triggered;
        aggregate.hdma_transfer_triggered = aggregate.hdma_transfer_triggered || step.hdma_transfer_triggered;
        aggregate.nmi_requested = aggregate.nmi_requested || step.nmi_requested;
        aggregate.irq_requested = aggregate.irq_requested || step.irq_requested;
    }
} // anonymous namespace

namespace clover::core
{
    void bus_t::connect_apu(apu_t& apu) noexcept
    {
        _apu = &apu;
    }

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

    void bus_t::power_on() noexcept
    {
        initialize(false);
    }

    void bus_t::reset() noexcept
    {
        initialize(true);
    }

    void bus_t::set_entropy_mode(startup_entropy_mode_t mode) noexcept
    {
        _entropy_mode = mode;
    }

    startup_entropy_mode_t bus_t::entropy_mode() const noexcept
    {
        return _entropy_mode;
    }

    void bus_t::set_entropy_seed(uint32_t seed, uint32_t sequence) noexcept
    {
        _entropy_seed_override_enabled = true;
        _entropy_seed = seed;
        _entropy_sequence = sequence;
    }

    void bus_t::clear_entropy_seed() noexcept
    {
        _entropy_seed_override_enabled = false;
        _entropy_seed = 0u;
        _entropy_sequence = 0u;
    }

    void bus_t::initialize(bool warm_reset) noexcept
    {
        const std::array<uint8_t, k_wram_size> preserved_wram{
            warm_reset && _entropy_mode != startup_entropy_mode_t::none ? _wram : std::array<uint8_t, k_wram_size>{}
        };

        std::fill(_wram.begin(), _wram.end(), 0);
        if (!warm_reset && _entropy_mode != startup_entropy_mode_t::none)
        {
            const uint32_t seed{
                _entropy_seed_override_enabled ? _entropy_seed : default_startup_entropy_seed()
            };
            const uint32_t sequence{
                _entropy_seed_override_enabled ? _entropy_sequence : static_cast<uint32_t>(reinterpret_cast<uintptr_t>(this))
            };
            startup_entropy_generator_t entropy{ seed, sequence };
            fill_entropy_buffer(_entropy_mode, entropy, _wram.data(), _wram.size());
        }
        else if (warm_reset && _entropy_mode != startup_entropy_mode_t::none)
        {
            _wram = preserved_wram;
        }

        _open_bus = 0;
        _pending_cpu_write_count = 0;
        _pending_ppu_write_count = 0;
        _pending_apu_write_count = 0;
        _apu_progressed_cpu_clocks = 0;
        _ppu_register_write_trace_count = 0;
        _system_register_write_trace_count = 0;
        _watched_write_trace_count = 0;
        _apu_port_trace_count = 0;
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
            _open_bus = _ppu->read_register(static_cast<uint16_t>(address & 0xffffu), _open_bus);
            return _open_bus;
        }

        if (_apu != nullptr && is_apu_register_address(address))
        {
            _open_bus = _apu->read_cpu_port(static_cast<uint16_t>(address & 0xffffu));
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

    uint8_t bus_t::read_cpu_u8(uint32_t address, master_clock_delta_t apply_after_clocks) noexcept
    {
        if (_apu != nullptr && is_apu_register_address(address))
            advance_apu_to(apply_after_clocks);

        if (_cpu != nullptr && is_cpu_register_address(address))
        {
            _open_bus = _cpu->read_register(static_cast<uint16_t>(address & 0xffffu), apply_after_clocks);
            return _open_bus;
        }

        return read_u8(address);
    }

    void bus_t::write_u8(uint32_t address, uint8_t value) noexcept
    {
        _open_bus = value;
        dispatch_write_u8(address, value);
    }

    void bus_t::write_cpu_u8(uint32_t address, uint8_t value, master_clock_delta_t apply_after_clocks) noexcept
    {
        _open_bus = value;

        if (is_ppu_register_address(address))
        {
            if (_pending_ppu_write_count < _pending_ppu_writes.size())
            {
                _pending_ppu_writes[_pending_ppu_write_count++] = {
                    .address = address,
                    .value = value,
                    .apply_after_clocks = apply_after_clocks
                };
            }
            return;
        }

        if (is_apu_register_address(address))
        {
            if (_pending_apu_write_count < _pending_apu_writes.size())
            {
                _pending_apu_writes[_pending_apu_write_count++] = {
                    .address = address,
                    .value = value,
                    .apply_after_clocks = apply_after_clocks
                };
            }
            return;
        }

        if (is_cpu_register_address(address)
            || is_dma_register_address(address))
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

    ppu_step_result_t bus_t::step_ppu_with_cpu_writes(master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (_ppu == nullptr || _pending_ppu_write_count == 0)
        {
            if (_ppu != nullptr)
                return _ppu->step(elapsed_master_clocks);

            return {};
        }

        ppu_step_result_t aggregate{};
        master_clock_delta_t elapsed_so_far{ 0 };

        for (uint8_t index{ 0 }; index < _pending_ppu_write_count; ++index)
        {
            const pending_ppu_write_t write{ _pending_ppu_writes[index] };
            const master_clock_delta_t target_clocks{
                write.apply_after_clocks <= elapsed_master_clocks ? write.apply_after_clocks : elapsed_master_clocks
            };
            if (target_clocks > elapsed_so_far)
            {
                accumulate_ppu_step_result(aggregate, _ppu->step(target_clocks - elapsed_so_far));
                elapsed_so_far = target_clocks;
            }

            dispatch_write_u8(write.address, write.value);
        }

        if (elapsed_master_clocks > elapsed_so_far)
            accumulate_ppu_step_result(aggregate, _ppu->step(elapsed_master_clocks - elapsed_so_far));

        _pending_ppu_write_count = 0;
        return aggregate;
    }

    void bus_t::step_apu_with_cpu_writes(master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (_apu != nullptr)
            _apu->begin_cpu_io_window(*this, elapsed_master_clocks);

        if (_apu == nullptr || _pending_apu_write_count == 0)
        {
            if (_apu != nullptr)
            {
                if (elapsed_master_clocks > _apu_progressed_cpu_clocks)
                    _apu->step(elapsed_master_clocks - _apu_progressed_cpu_clocks);
                _apu->end_cpu_io_window();
            }

            _apu_progressed_cpu_clocks = 0;

            return;
        }

        advance_apu_to(elapsed_master_clocks);
        _apu->end_cpu_io_window();
        _pending_apu_write_count = 0;
        _apu_progressed_cpu_clocks = 0;
    }

    void bus_t::advance_apu_to(master_clock_delta_t target_clocks) noexcept
    {
        if (_apu == nullptr || target_clocks <= _apu_progressed_cpu_clocks)
            return;

        dispatch_pending_apu_writes_to(target_clocks);

        if (target_clocks > _apu_progressed_cpu_clocks)
        {
            _apu->step(target_clocks - _apu_progressed_cpu_clocks);
            _apu_progressed_cpu_clocks = target_clocks;
        }
    }

    void bus_t::synchronize_apu_io_access(master_clock_delta_t target_clocks) noexcept
    {
        if (_apu == nullptr || target_clocks <= _apu_progressed_cpu_clocks)
            return;

        dispatch_pending_apu_writes_to(target_clocks);
        if (target_clocks > _apu_progressed_cpu_clocks)
            _apu_progressed_cpu_clocks = target_clocks;
    }

    void bus_t::dispatch_pending_apu_writes_to(master_clock_delta_t target_clocks) noexcept
    {
        for (uint8_t index{ 0 }; index < _pending_apu_write_count; )
        {
            const pending_apu_write_t write{ _pending_apu_writes[index] };
            if (write.apply_after_clocks > target_clocks)
            {
                ++index;
                continue;
            }

            dispatch_write_u8(write.address, write.value);
            for (uint8_t shift{ static_cast<uint8_t>(index + 1u) }; shift < _pending_apu_write_count; ++shift)
                _pending_apu_writes[shift - 1] = _pending_apu_writes[shift];
            --_pending_apu_write_count;
        }
    }

    uint8_t bus_t::open_bus() const noexcept
    {
        return _open_bus;
    }

    uint8_t bus_t::ppu_register_write_trace_count() const noexcept
    {
        return _ppu_register_write_trace_count;
    }

    const std::array<bus_t::ppu_register_write_trace_t, bus_t::k_ppu_register_write_trace_capacity>& bus_t::ppu_register_write_trace() const noexcept
    {
        return _ppu_register_write_trace;
    }

    uint8_t bus_t::system_register_write_trace_count() const noexcept
    {
        return _system_register_write_trace_count;
    }

    const std::array<bus_t::system_register_write_trace_t, bus_t::k_system_register_write_trace_capacity>& bus_t::system_register_write_trace() const noexcept
    {
        return _system_register_write_trace;
    }

    uint8_t bus_t::watched_write_trace_count() const noexcept
    {
        return _watched_write_trace_count;
    }

    const std::array<bus_t::watched_write_trace_t, bus_t::k_watched_write_trace_capacity>& bus_t::watched_write_trace() const noexcept
    {
        return _watched_write_trace;
    }

    std::span<const uint8_t> bus_t::wram_span(uint32_t offset, uint32_t length) const noexcept
    {
        const uint32_t clamped_offset{ std::min(offset, k_wram_size) };
        const uint32_t remaining{ k_wram_size - clamped_offset };
        const uint32_t clamped_length{ std::min(length, remaining) };
        return std::span<const uint8_t>{ _wram.data() + clamped_offset, clamped_length };
    }

    void bus_t::trace_cpu_apu_port_access(uint32_t address,
                                          uint8_t value,
                                          bool is_write,
                                          master_clock_delta_t apply_after_clocks) noexcept
    {
        if (_cpu == nullptr || !is_apu_register_address(address))
            return;

        const apu_port_trace_t entry{
            .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
            .address = address,
            .value = value,
            .is_write = is_write,
            .apply_after_clocks = apply_after_clocks,
            .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
            .cpu = _cpu->state()
        };

        if (_apu_port_trace_count < _apu_port_trace.size())
        {
            _apu_port_trace[_apu_port_trace_count++] = entry;
        }
        else
        {
            std::move(std::begin(_apu_port_trace) + 1u,
                      std::end(_apu_port_trace),
                      std::begin(_apu_port_trace));
            _apu_port_trace[_apu_port_trace.size() - 1u] = entry;
        }
    }

    uint16_t bus_t::apu_port_trace_count() const noexcept
    {
        return _apu_port_trace_count;
    }

    const std::array<bus_t::apu_port_trace_t, bus_t::k_apu_port_trace_capacity>& bus_t::apu_port_trace() const noexcept
    {
        return _apu_port_trace;
    }

    void bus_t::dispatch_write_u8(uint32_t address, uint8_t value) noexcept
    {
        if (_cpu != nullptr
            && is_wram_address(address)
            && should_trace_wram_write(wram_offset(address)))
        {
            const watched_write_trace_t entry{
                .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
                .address = address,
                .value = value,
                .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
                .cpu = _cpu->state()
            };

            if (_watched_write_trace_count < _watched_write_trace.size())
            {
                _watched_write_trace[_watched_write_trace_count++] = entry;
            }
            else
            {
                std::move(std::begin(_watched_write_trace) + 1u,
                          std::end(_watched_write_trace),
                          std::begin(_watched_write_trace));
                _watched_write_trace[_watched_write_trace.size() - 1u] = entry;
            }
        }

        if (is_wram_address(address))
        {
            _wram[wram_offset(address)] = value;
            return;
        }

        if (_ppu != nullptr && is_ppu_register_address(address))
        {
            const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
            if (_cpu != nullptr && should_trace_ppu_register_write(register_address))
            {
                const ppu_register_write_trace_t entry{
                    .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
                    .address = address,
                    .value = value,
                    .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
                    .cpu = _cpu->state()
                };

                if (_ppu_register_write_trace_count < _ppu_register_write_trace.size())
                {
                    _ppu_register_write_trace[_ppu_register_write_trace_count++] = entry;
                }
                else
                {
                    std::move(std::begin(_ppu_register_write_trace) + 1u,
                              std::end(_ppu_register_write_trace),
                              std::begin(_ppu_register_write_trace));
                    _ppu_register_write_trace[_ppu_register_write_trace.size() - 1u] = entry;
                }
            }
            _ppu->write_register(static_cast<uint16_t>(address & 0xffffu), value);
            return;
        }

        if (_cpu != nullptr
            && (is_cpu_register_address(address) || is_dma_register_address(address))
            && should_trace_system_register_write(address))
        {
            const system_register_write_trace_t entry{
                .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
                .address = address,
                .value = value,
                .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
                .cpu = _cpu->state()
            };

            if (_system_register_write_trace_count < _system_register_write_trace.size())
            {
                _system_register_write_trace[_system_register_write_trace_count++] = entry;
            }
            else
            {
                std::move(std::begin(_system_register_write_trace) + 1u,
                          std::end(_system_register_write_trace),
                          std::begin(_system_register_write_trace));
                _system_register_write_trace[_system_register_write_trace.size() - 1u] = entry;
            }
        }

        if (_apu != nullptr && is_apu_register_address(address))
        {
            _apu->write_cpu_port(static_cast<uint16_t>(address & 0xffffu), value);
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

    bool bus_t::is_apu_register_address(uint32_t address) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return register_address >= 0x2140u && register_address <= 0x217fu;
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
