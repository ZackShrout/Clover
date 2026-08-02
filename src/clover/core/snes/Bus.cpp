//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Bus.h"

#include "clover/core/snes/Apu.h"
#include "clover/core/snes/Cartridge.h"
#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/Dma.h"
#include "clover/core/snes/Observation.h"
#include "clover/core/snes/Ppu.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace
{
    [[nodiscard]] uint64_t parse_trace_u64_env(const char* name, uint64_t fallback) noexcept;

    constexpr uint32_t k_watch_brightness_offset{ 0x0daeu };
    constexpr uint32_t k_watch_hdmaen_offset{ 0x0d9fu };
    constexpr uint32_t k_watch_lowram_target{ 0x0003u };
    constexpr uint32_t k_watch_upload_window_start{ 0x0d84u };
    constexpr uint32_t k_watch_upload_window_end{ 0x0d99u };
    constexpr uint32_t k_watch_stack_window_start{ 0x15f0u };
    constexpr uint32_t k_watch_stack_window_end{ 0x160fu };

    struct wram_write_live_trace_filter_t
    {
        bool enabled{ false };
        uint32_t address_min{ 0u };
        uint32_t address_max{ 0u };
        uint64_t frame_min{ 0u };
        uint64_t frame_max{ UINT64_MAX };
    };

    [[nodiscard]] wram_write_live_trace_filter_t load_wram_write_live_trace_filter() noexcept
    {
        wram_write_live_trace_filter_t filter{};
        const char* min_raw{ std::getenv("CLOVER_LIVE_WRAM_TRACE_ADDR_MIN") };
        if (min_raw == nullptr || *min_raw == '\0')
            return filter;

        filter.enabled = true;
        char* end{ nullptr };
        filter.address_min = static_cast<uint32_t>(std::strtoull(min_raw, &end, 0) & 0x1ffffu);
        if (end == min_raw)
            filter.address_min = 0u;

        const char* max_raw{ std::getenv("CLOVER_LIVE_WRAM_TRACE_ADDR_MAX") };
        if (max_raw != nullptr && *max_raw != '\0')
        {
            end = nullptr;
            filter.address_max = static_cast<uint32_t>(std::strtoull(max_raw, &end, 0) & 0x1ffffu);
            if (end == max_raw)
                filter.address_max = filter.address_min;
        }
        else
        {
            filter.address_max = filter.address_min;
        }

        if (filter.address_min > filter.address_max)
            std::swap(filter.address_min, filter.address_max);
        filter.frame_min = parse_trace_u64_env("CLOVER_LIVE_WRAM_TRACE_FRAME_MIN", 0u);
        filter.frame_max = parse_trace_u64_env("CLOVER_LIVE_WRAM_TRACE_FRAME_MAX", UINT64_MAX);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

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
        static const wram_write_live_trace_filter_t live_filter{ load_wram_write_live_trace_filter() };

        return offset == k_watch_brightness_offset
            || offset == k_watch_hdmaen_offset
            || offset == k_watch_lowram_target
            || (offset >= k_watch_upload_window_start && offset <= k_watch_upload_window_end)
            || (offset >= k_watch_stack_window_start && offset <= k_watch_stack_window_end)
            || (live_filter.enabled && offset >= live_filter.address_min && offset <= live_filter.address_max);
    }

    [[nodiscard]] bool cpu_internal_mmio_preserves_open_bus(uint32_t address) noexcept
    {
        // Match bsnes CPU MDR behavior: reads in $00-3f,80-bf:$4000-$43ff are
        // internal to the CPU complex and do not refresh the CPU MDR/open bus.
        return (address & 0x40fc00u) == 0x4000u;
    }

    struct ppu_register_live_trace_filter_t
    {
        bool enabled{ false };
        uint16_t address_min{ 0 };
        uint16_t address_max{ 0 };
        uint64_t frame_min{ 0 };
        uint64_t frame_max{ 0 };
    };

    struct system_register_live_trace_filter_t
    {
        bool enabled{ false };
        uint16_t address_min{ 0 };
        uint16_t address_max{ 0 };
        uint64_t frame_min{ 0 };
        uint64_t frame_max{ 0 };
    };

    [[nodiscard]] uint64_t parse_trace_u64_env(const char* name, uint64_t fallback) noexcept
    {
        const char* raw{ std::getenv(name) };
        if (raw == nullptr || *raw == '\0')
            return fallback;

        char* end{ nullptr };
        const unsigned long long parsed{ std::strtoull(raw, &end, 0) };
        if (end == raw)
            return fallback;
        return static_cast<uint64_t>(parsed);
    }

    [[nodiscard]] ppu_register_live_trace_filter_t load_ppu_register_live_trace_filter() noexcept
    {
        ppu_register_live_trace_filter_t filter{};
        const char* addr_raw{ std::getenv("CLOVER_LIVE_PPU_REG_TRACE_ADDR") };
        const char* addr_min_raw{ std::getenv("CLOVER_LIVE_PPU_REG_TRACE_ADDR_MIN") };
        if ((addr_raw == nullptr || *addr_raw == '\0')
            && (addr_min_raw == nullptr || *addr_min_raw == '\0'))
        {
            return filter;
        }

        filter.enabled = true;
        if (addr_raw != nullptr && *addr_raw != '\0')
        {
            filter.address_min = static_cast<uint16_t>(
                parse_trace_u64_env("CLOVER_LIVE_PPU_REG_TRACE_ADDR", 0u) & 0xffffu
            );
            filter.address_max = filter.address_min;
        }
        else
        {
            filter.address_min = static_cast<uint16_t>(
                parse_trace_u64_env("CLOVER_LIVE_PPU_REG_TRACE_ADDR_MIN", 0x2100u) & 0xffffu
            );
            filter.address_max = static_cast<uint16_t>(
                parse_trace_u64_env("CLOVER_LIVE_PPU_REG_TRACE_ADDR_MAX", 0x2133u) & 0xffffu
            );
            if (filter.address_min > filter.address_max)
                std::swap(filter.address_min, filter.address_max);
        }
        filter.frame_min = parse_trace_u64_env("CLOVER_LIVE_PPU_REG_TRACE_FRAME_MIN", 0u);
        filter.frame_max = parse_trace_u64_env("CLOVER_LIVE_PPU_REG_TRACE_FRAME_MAX", UINT64_MAX);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

    [[nodiscard]] system_register_live_trace_filter_t load_system_register_live_trace_filter() noexcept
    {
        system_register_live_trace_filter_t filter{};
        const char* min_raw{ std::getenv("CLOVER_LIVE_SYSTEM_REG_TRACE_ADDR_MIN") };
        if (min_raw == nullptr || *min_raw == '\0')
            return filter;

        filter.enabled = true;
        filter.address_min = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_LIVE_SYSTEM_REG_TRACE_ADDR_MIN", 0u));
        filter.address_max = static_cast<uint16_t>(parse_trace_u64_env("CLOVER_LIVE_SYSTEM_REG_TRACE_ADDR_MAX", filter.address_min));
        filter.frame_min = parse_trace_u64_env("CLOVER_LIVE_SYSTEM_REG_TRACE_FRAME_MIN", 0u);
        filter.frame_max = parse_trace_u64_env("CLOVER_LIVE_SYSTEM_REG_TRACE_FRAME_MAX", UINT64_MAX);
        if (filter.address_min > filter.address_max)
            std::swap(filter.address_min, filter.address_max);
        if (filter.frame_min > filter.frame_max)
            std::swap(filter.frame_min, filter.frame_max);
        return filter;
    }

    void accumulate_ppu_step_result(clover::core::ppu_step_result_t& aggregate,
                                    const clover::core::ppu_step_result_t& step) noexcept
    {
        aggregate.timing = step.timing;
        aggregate.visible_scanlines = step.visible_scanlines;
        aggregate.interlace = step.interlace;
        aggregate.frame_complete = aggregate.frame_complete || step.frame_complete;
        aggregate.frames_completed += step.frames_completed;
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

    void bus_t::step_cartridge(master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (_cartridge != nullptr)
            _cartridge->step_coprocessor(elapsed_master_clocks);
    }

    bool bus_t::cartridge_irq_pending() const noexcept
    {
        return _cartridge != nullptr && _cartridge->coprocessor_irq_pending();
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
            warm_reset ? _wram : std::array<uint8_t, k_wram_size>{}
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
        else if (warm_reset)
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
            _apu->synchronize_cpu_thread();
            _open_bus = _apu->read_cpu_port(static_cast<uint16_t>(address & 0xffffu));
            return _open_bus;
        }

        if (_dma != nullptr && is_dma_register_address(address))
        {
            const uint8_t value{ _dma->read_register(static_cast<uint16_t>(address & 0xffffu)) };
            if (!cpu_internal_mmio_preserves_open_bus(address))
                _open_bus = value;
            return value;
        }

        if (_cpu != nullptr && is_cpu_register_address(address))
        {
            const uint8_t value{ _cpu->read_register(static_cast<uint16_t>(address & 0xffffu)) };
            if (!cpu_internal_mmio_preserves_open_bus(address))
                _open_bus = value;
            return value;
        }

        if (_cartridge != nullptr)
        {
            _open_bus = _cartridge->read_u8(address, _open_bus);
            return _open_bus;
        }

        return _open_bus;
    }

    bool bus_t::inspect_u8(uint32_t address, uint8_t& value) const noexcept
    {
        address &= 0x00ffffffu;
        if (is_wram_address(address))
        {
            value = _wram[wram_offset(address)];
            return true;
        }

        if (is_ppu_register_address(address)
            || is_apu_register_address(address)
            || is_dma_register_address(address)
            || is_cpu_register_address(address))
        {
            return false;
        }

        return _cartridge != nullptr && _cartridge->inspect_u8(address, value);
    }

    uint8_t bus_t::read_cpu_u8(uint32_t address, master_clock_delta_t apply_after_clocks) noexcept
    {
        if (_apu != nullptr && is_apu_register_address(address))
            advance_apu_to(apply_after_clocks);

        if (_cpu != nullptr && is_cpu_register_address(address))
        {
            const uint8_t value{ _cpu->read_register(static_cast<uint16_t>(address & 0xffffu), apply_after_clocks) };
            if (!cpu_internal_mmio_preserves_open_bus(address))
                _open_bus = value;
            return value;
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
            const ppu_step_result_t result{
                _ppu != nullptr ? _ppu->step(elapsed_master_clocks) : ppu_step_result_t{}
            };
            // DMA B-bus writes to CPU MMIO (notably $2180) take effect at the
            // end of the current eight-clock transfer unit. Committing here
            // prevents a long DMA from accumulating more writes than the
            // small intra-cycle queue can hold.
            commit_cpu_writes();
            return result;
        }

        ppu_step_result_t aggregate{};
        master_clock_delta_t elapsed_so_far{ 0 };
        std::array<pending_ppu_write_t, 16> remaining_writes{};
        uint8_t remaining_write_count{ 0 };

        for (uint8_t index{ 0 }; index < _pending_ppu_write_count; ++index)
        {
            const pending_ppu_write_t write{ _pending_ppu_writes[index] };
            if (write.apply_after_clocks > elapsed_master_clocks)
            {
                remaining_writes[remaining_write_count++] = {
                    .address = write.address,
                    .value = write.value,
                    .apply_after_clocks = static_cast<master_clock_delta_t>(
                        write.apply_after_clocks - elapsed_master_clocks
                    )
                };
                continue;
            }

            const master_clock_delta_t target_clocks{ write.apply_after_clocks };
            if (target_clocks > elapsed_so_far)
            {
                accumulate_ppu_step_result(aggregate, _ppu->step(target_clocks - elapsed_so_far));
                elapsed_so_far = target_clocks;
            }

            dispatch_write_u8(write.address, write.value);
        }

        if (elapsed_master_clocks > elapsed_so_far)
            accumulate_ppu_step_result(aggregate, _ppu->step(elapsed_master_clocks - elapsed_so_far));

        _pending_ppu_writes = remaining_writes;
        _pending_ppu_write_count = remaining_write_count;
        commit_cpu_writes();
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

    void bus_t::step_apu(master_clock_delta_t elapsed_master_clocks) noexcept
    {
        if (_apu != nullptr && elapsed_master_clocks != 0)
            _apu->step(elapsed_master_clocks);
    }

    void bus_t::synchronize_apu_cpu_thread() noexcept
    {
        if (_apu != nullptr)
            _apu->synchronize_cpu_thread();
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

    void bus_t::set_apu_port_trace_enabled(bool enabled) noexcept
    {
        _apu_port_trace_enabled = enabled;
        _apu_port_trace_count = 0;
    }

    void bus_t::set_legacy_trace_enabled(bool enabled) noexcept
    {
        _legacy_trace_enabled = enabled;
        if (!enabled)
        {
            _ppu_register_write_trace_count = 0;
            _system_register_write_trace_count = 0;
            _watched_write_trace_count = 0;
        }
    }

    void bus_t::set_observation_sink(snes_observation_sink_t* sink) noexcept
    {
        _observation_sink = sink;
        _cpu_memory_observation_enabled = sink != nullptr
            && sink->enabled(k_snes_observe_cpu_memory_access);
    }

    void bus_t::record_cpu_memory_access(uint32_t address,
                                         uint8_t value,
                                         bool is_write,
                                         uint32_t instruction_address) noexcept
    {
        if (_observation_sink == nullptr || _cpu == nullptr)
            return;
        _observation_sink->push({
            .kind = snes_observation_kind_t::cpu_memory_access,
            .master_clock = _cpu->master_clock(),
            .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
            .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
            .cpu_memory_access = {
                .kind = is_write
                    ? snes_memory_access_kind_t::write
                    : snes_memory_access_kind_t::read,
                .address = address & 0x00ffffffu,
                .instruction_address = instruction_address & 0x00ffffffu,
                .value = value,
                .cpu = _cpu->state()
            }
        });
    }

#if defined(CLOVER_WORKBENCH_DMA_PROVENANCE)
    void bus_t::record_workbench_dma_control_write(
        uint32_t address,
        uint8_t value,
        bool is_write,
        uint32_t instruction_address
    ) noexcept
    {
        const uint16_t register_address{ static_cast<uint16_t>(address) };
        if (!is_write || _dma == nullptr
            || (register_address != 0x420bu && register_address != 0x420cu))
        {
            return;
        }

        _dma->record_control_write(
            register_address,
            value,
            instruction_address,
            workbench_dma_frame_index(),
            workbench_dma_timing()
        );
    }

    master_clock_count_t bus_t::workbench_dma_master_clock() const noexcept
    {
        return _cpu != nullptr ? _cpu->master_clock() : 0u;
    }

    uint64_t bus_t::workbench_dma_frame_index() const noexcept
    {
        return _ppu != nullptr ? _ppu->frame_index() : 0u;
    }

    timing_snapshot_t bus_t::workbench_dma_timing() const noexcept
    {
        return _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{};
    }
#endif

    void bus_t::trace_cpu_apu_port_access(uint32_t address,
                                          uint8_t value,
                                          bool is_write,
                                          master_clock_delta_t apply_after_clocks) noexcept
    {
        if (!_apu_port_trace_enabled || _cpu == nullptr || !is_apu_register_address(address))
            return;

        const apu_port_trace_t entry{
            .frame_index = _ppu != nullptr ? _ppu->frame_index() : 0u,
            .address = address,
            .value = value,
            .is_write = is_write,
            .apply_after_clocks = apply_after_clocks,
            .timing = _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{},
            .cpu = _cpu->state(),
            .apu_ram_probe = _apu != nullptr
                ? std::array<uint8_t, 7>{
                    _apu->peek_ram(0x0047u), _apu->peek_ram(0x0048u),
                    _apu->peek_ram(0x0090u), _apu->peek_ram(0x0091u),
                    _apu->peek_ram(0x00c8u), _apu->peek_ram(0x00f4u),
                    _apu->peek_ram(0x00f5u)
                }
                : std::array<uint8_t, 7>{}
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

    bus_t::causal_state_t bus_t::capture_causal_state() const noexcept
    {
        causal_state_t state{
            .wram = _wram,
            .entropy_mode = _entropy_mode,
            .entropy_seed_override_enabled = _entropy_seed_override_enabled,
            .entropy_seed = _entropy_seed,
            .entropy_sequence = _entropy_sequence,
            .open_bus = _open_bus,
            .pending_cpu_write_count = _pending_cpu_write_count,
            .pending_ppu_write_count = _pending_ppu_write_count,
            .pending_apu_write_count = _pending_apu_write_count,
            .apu_progressed_cpu_clocks = _apu_progressed_cpu_clocks,
        };
        std::copy_n(
            _pending_cpu_writes.begin(),
            _pending_cpu_write_count,
            state.pending_cpu_writes.begin());
        std::copy_n(
            _pending_ppu_writes.begin(),
            _pending_ppu_write_count,
            state.pending_ppu_writes.begin());
        std::copy_n(
            _pending_apu_writes.begin(),
            _pending_apu_write_count,
            state.pending_apu_writes.begin());
        return state;
    }

    bool bus_t::restore_causal_state(const causal_state_t& state) noexcept
    {
        const bool valid_entropy_mode{
            state.entropy_mode == startup_entropy_mode_t::none
            || state.entropy_mode == startup_entropy_mode_t::low
            || state.entropy_mode == startup_entropy_mode_t::high
        };
        if (!valid_entropy_mode
            || state.pending_cpu_write_count > state.pending_cpu_writes.size()
            || state.pending_ppu_write_count > state.pending_ppu_writes.size()
            || state.pending_apu_write_count > state.pending_apu_writes.size())
        {
            return false;
        }

        _wram = state.wram;
        _entropy_mode = state.entropy_mode;
        _entropy_seed_override_enabled = state.entropy_seed_override_enabled;
        _entropy_seed = state.entropy_seed;
        _entropy_sequence = state.entropy_sequence;
        _open_bus = state.open_bus;
        _pending_cpu_writes = {};
        std::copy_n(
            state.pending_cpu_writes.begin(),
            state.pending_cpu_write_count,
            _pending_cpu_writes.begin());
        _pending_cpu_write_count = state.pending_cpu_write_count;
        _pending_ppu_writes = {};
        std::copy_n(
            state.pending_ppu_writes.begin(),
            state.pending_ppu_write_count,
            _pending_ppu_writes.begin());
        _pending_ppu_write_count = state.pending_ppu_write_count;
        _pending_apu_writes = {};
        std::copy_n(
            state.pending_apu_writes.begin(),
            state.pending_apu_write_count,
            _pending_apu_writes.begin());
        _pending_apu_write_count = state.pending_apu_write_count;
        _apu_progressed_cpu_clocks = state.apu_progressed_cpu_clocks;

        // Trace contents belong to the abandoned observation timeline. Keep
        // trace policy, but expose no entries from before the restore.
        _ppu_register_write_trace_count = 0;
        _system_register_write_trace_count = 0;
        _watched_write_trace_count = 0;
        _apu_port_trace_count = 0;
        return true;
    }

    void bus_t::dispatch_write_u8(uint32_t address, uint8_t value) noexcept
    {
        static const wram_write_live_trace_filter_t live_wram_trace_filter{ load_wram_write_live_trace_filter() };
        if (_legacy_trace_enabled
            && _cpu != nullptr
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

            const uint32_t offset{ wram_offset(address) };
            const uint64_t frame_index{ _ppu != nullptr ? _ppu->frame_index() : 0u };
            if (live_wram_trace_filter.enabled
                && offset >= live_wram_trace_filter.address_min
                && offset <= live_wram_trace_filter.address_max
                && frame_index >= live_wram_trace_filter.frame_min
                && frame_index <= live_wram_trace_filter.frame_max)
            {
                const timing_snapshot_t timing{ _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{} };
                const cpu_state_t cpu{ _cpu->state() };
                std::printf("Bus WRAM write: frame=%llu scanline=%u dot=%u addr=%06x offset=%05x value=%02x "
                            "PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                            static_cast<unsigned long long>(frame_index),
                            static_cast<unsigned>(timing.raster.scanline),
                            static_cast<unsigned>(timing.raster.dot),
                            static_cast<unsigned>(address),
                            static_cast<unsigned>(offset),
                            static_cast<unsigned>(value),
                            cpu.pb,
                            cpu.pc,
                            cpu.a,
                            cpu.x,
                            cpu.y,
                            cpu.sp,
                            cpu.d,
                            cpu.db,
                            cpu.p,
                            cpu.emulation_mode ? 1u : 0u);
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
            const uint64_t frame_index{ _ppu != nullptr ? _ppu->frame_index() : 0u };
            if (_legacy_trace_enabled
                && _cpu != nullptr
                && should_trace_ppu_register_write(register_address))
            {
                const ppu_register_write_trace_t entry{
                    .frame_index = frame_index,
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

            static const ppu_register_live_trace_filter_t live_trace_filter{ load_ppu_register_live_trace_filter() };
            if (_cpu != nullptr
                && live_trace_filter.enabled
                && register_address >= live_trace_filter.address_min
                && register_address <= live_trace_filter.address_max
                && frame_index >= live_trace_filter.frame_min
                && frame_index <= live_trace_filter.frame_max)
            {
                const timing_snapshot_t timing{ _ppu->timing() };
                const timing_snapshot_t cpu_timing{ _cpu->timing(_ppu->video_timing()) };
                const cpu_state_t cpu{ _cpu->state() };
                std::printf("Bus PPU write: frame=%llu scanline=%u dot=%u cpu_scanline=%u cpu_dot=%u addr=%04x value=%02x "
                            "PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                            static_cast<unsigned long long>(frame_index),
                            static_cast<unsigned>(timing.raster.scanline),
                            static_cast<unsigned>(timing.raster.dot),
                            static_cast<unsigned>(cpu_timing.raster.scanline),
                            static_cast<unsigned>(cpu_timing.raster.dot),
                            static_cast<unsigned>(register_address),
                            static_cast<unsigned>(value),
                            cpu.pb,
                            cpu.pc,
                            cpu.a,
                            cpu.x,
                            cpu.y,
                            cpu.sp,
                            cpu.d,
                            cpu.db,
                            cpu.p,
                            cpu.emulation_mode ? 1u : 0u);
            }
            _ppu->write_register(static_cast<uint16_t>(address & 0xffffu), value);
            return;
        }

        if (_legacy_trace_enabled
            && _cpu != nullptr
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

        if (_cpu != nullptr && (is_cpu_register_address(address) || is_dma_register_address(address)))
        {
            static const system_register_live_trace_filter_t live_filter{
                load_system_register_live_trace_filter()
            };
            const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
            const uint64_t frame_index{ _ppu != nullptr ? _ppu->frame_index() : 0u };
            if (live_filter.enabled
                && register_address >= live_filter.address_min
                && register_address <= live_filter.address_max
                && frame_index >= live_filter.frame_min
                && frame_index <= live_filter.frame_max)
            {
                const timing_snapshot_t timing{ _ppu != nullptr ? _ppu->timing() : timing_snapshot_t{} };
                const cpu_state_t cpu{ _cpu->state() };
                std::printf("Bus system write: frame=%llu scanline=%u dot=%u addr=%04x value=%02x "
                            "PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x D:%04x DB:%02x P:%02x E:%u\n",
                            static_cast<unsigned long long>(frame_index), timing.raster.scanline,
                            timing.raster.dot, register_address, value, cpu.pb, cpu.pc, cpu.a,
                            cpu.x, cpu.y, cpu.sp, cpu.d, cpu.db, cpu.p,
                            cpu.emulation_mode ? 1u : 0u);
            }
        }

        if (_apu != nullptr && is_apu_register_address(address))
        {
            _apu->synchronize_cpu_thread();
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
        if (!is_low_wram_mirror_bank(static_cast<uint8_t>(address >> 16u)))
            return false;

        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return register_address >= 0x2140u && register_address <= 0x217fu;
    }

    bool bus_t::is_cpu_register_address(uint32_t address) noexcept
    {
        if (!is_low_wram_mirror_bank(static_cast<uint8_t>(address >> 16u)))
            return false;

        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return (register_address >= 0x2180u && register_address <= 0x2183u)
            || register_address == 0x4016u
            || register_address == 0x4017u
            || (register_address >= 0x4200u && register_address <= 0x421fu);
    }

    bool bus_t::is_ppu_register_address(uint32_t address) noexcept
    {
        if (!is_low_wram_mirror_bank(static_cast<uint8_t>(address >> 16u)))
            return false;

        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return register_address >= 0x2100u && register_address <= 0x213fu;
    }

    bool bus_t::is_dma_register_address(uint32_t address) noexcept
    {
        if (!is_low_wram_mirror_bank(static_cast<uint8_t>(address >> 16u)))
            return false;

        const uint16_t register_address{ static_cast<uint16_t>(address & 0xffffu) };
        return (register_address >= 0x420bu && register_address <= 0x420cu)
            || (register_address >= 0x4300u && register_address <= 0x437fu);
    }
}
