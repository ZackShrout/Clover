//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Cpu.h"
#include "clover/core/snes/StartupEntropy.h"

#include <array>
#include <cstdint>
#include <span>

namespace clover::core
{
    struct apu_t;
    struct cartridge_t;
    struct cpu_t;
    struct dma_t;
    struct ppu_t;

    struct bus_t
    {
    public:
        static constexpr size_t k_ppu_register_write_trace_capacity{ 255 };
        static constexpr size_t k_system_register_write_trace_capacity{ 255 };
        static constexpr size_t k_watched_write_trace_capacity{ 4096 };
        static constexpr size_t k_apu_port_trace_capacity{ 1024 };

        struct ppu_register_write_trace_t
        {
            uint64_t frame_index{ 0 };
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            timing_snapshot_t timing{};
            cpu_state_t cpu{};
        };

        struct watched_write_trace_t
        {
            uint64_t frame_index{ 0 };
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            timing_snapshot_t timing{};
            cpu_state_t cpu{};
        };

        struct system_register_write_trace_t
        {
            uint64_t frame_index{ 0 };
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            timing_snapshot_t timing{};
            cpu_state_t cpu{};
        };

        struct apu_port_trace_t
        {
            uint64_t frame_index{ 0 };
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            bool is_write{ false };
            master_clock_delta_t apply_after_clocks{ 0 };
            timing_snapshot_t timing{};
            cpu_state_t cpu{};
        };

        struct pending_cpu_write_t
        {
            uint32_t address{ 0 };
            uint8_t value{ 0 };
        };

        struct pending_ppu_write_t
        {
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            master_clock_delta_t apply_after_clocks{ 0 };
        };

        struct pending_apu_write_t
        {
            uint32_t address{ 0 };
            uint8_t value{ 0 };
            master_clock_delta_t apply_after_clocks{ 0 };
        };

        static constexpr uint32_t k_wram_base_address{ 0x7e0000u };
        static constexpr uint32_t k_wram_size{ 128 * 1024 };
        static constexpr uint32_t k_low_wram_mirror_size{ 0x2000u };
        void connect_apu(apu_t& apu) noexcept;
        void connect_cartridge(cartridge_t& cartridge) noexcept;
        void connect_cpu(cpu_t& cpu) noexcept;
        void connect_ppu(ppu_t& ppu) noexcept;
        void connect_dma(dma_t& dma) noexcept;
        void power_on() noexcept;
        void reset() noexcept;
        void set_entropy_mode(startup_entropy_mode_t mode) noexcept;
        [[nodiscard]] startup_entropy_mode_t entropy_mode() const noexcept;
        void set_entropy_seed(uint32_t seed, uint32_t sequence = 0u) noexcept;
        void clear_entropy_seed() noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address) noexcept;
        [[nodiscard]] uint8_t read_cpu_u8(uint32_t address, master_clock_delta_t apply_after_clocks) noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;
        void write_cpu_u8(uint32_t address, uint8_t value, master_clock_delta_t apply_after_clocks) noexcept;
        void commit_cpu_writes() noexcept;
        [[nodiscard]] ppu_step_result_t step_ppu_with_cpu_writes(master_clock_delta_t elapsed_master_clocks) noexcept;
        void step_apu_with_cpu_writes(master_clock_delta_t elapsed_master_clocks) noexcept;
        void step_apu(master_clock_delta_t elapsed_master_clocks) noexcept;
        void synchronize_apu_io_access(master_clock_delta_t target_clocks) noexcept;
        [[nodiscard]] uint8_t open_bus() const noexcept;
        [[nodiscard]] uint8_t ppu_register_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<ppu_register_write_trace_t, k_ppu_register_write_trace_capacity>& ppu_register_write_trace() const noexcept;
        [[nodiscard]] uint8_t system_register_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<system_register_write_trace_t, k_system_register_write_trace_capacity>& system_register_write_trace() const noexcept;
        [[nodiscard]] uint8_t watched_write_trace_count() const noexcept;
        [[nodiscard]] const std::array<watched_write_trace_t, k_watched_write_trace_capacity>& watched_write_trace() const noexcept;
        [[nodiscard]] std::span<const uint8_t> wram_span(uint32_t offset, uint32_t length) const noexcept;
        void trace_cpu_apu_port_access(uint32_t address,
                                       uint8_t value,
                                       bool is_write,
                                       master_clock_delta_t apply_after_clocks) noexcept;
        [[nodiscard]] uint16_t apu_port_trace_count() const noexcept;
        [[nodiscard]] const std::array<apu_port_trace_t, k_apu_port_trace_capacity>& apu_port_trace() const noexcept;

    private:
        void initialize(bool warm_reset) noexcept;
        void dispatch_write_u8(uint32_t address, uint8_t value) noexcept;
        void advance_apu_to(master_clock_delta_t target_clocks) noexcept;
        void dispatch_pending_apu_writes_to(master_clock_delta_t target_clocks) noexcept;
        [[nodiscard]] static bool is_wram_address(uint32_t address) noexcept;
        [[nodiscard]] static uint32_t wram_offset(uint32_t address) noexcept;
        [[nodiscard]] static bool is_apu_register_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_cpu_register_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_ppu_register_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dma_register_address(uint32_t address) noexcept;

        apu_t* _apu{ nullptr };
        std::array<uint8_t, k_wram_size> _wram{};
        cartridge_t* _cartridge{ nullptr };
        cpu_t* _cpu{ nullptr };
        dma_t* _dma{ nullptr };
        ppu_t* _ppu{ nullptr };
        startup_entropy_mode_t _entropy_mode{ startup_entropy_mode_t::none };
        bool _entropy_seed_override_enabled{ false };
        uint32_t _entropy_seed{ 0u };
        uint32_t _entropy_sequence{ 0u };
        uint8_t _open_bus{ 0 };
        std::array<pending_cpu_write_t, 16> _pending_cpu_writes{};
        uint8_t _pending_cpu_write_count{ 0 };
        std::array<pending_ppu_write_t, 16> _pending_ppu_writes{};
        uint8_t _pending_ppu_write_count{ 0 };
        std::array<pending_apu_write_t, 16> _pending_apu_writes{};
        uint8_t _pending_apu_write_count{ 0 };
        master_clock_delta_t _apu_progressed_cpu_clocks{ 0 };
        std::array<ppu_register_write_trace_t, k_ppu_register_write_trace_capacity> _ppu_register_write_trace{};
        uint8_t _ppu_register_write_trace_count{ 0 };
        std::array<system_register_write_trace_t, k_system_register_write_trace_capacity> _system_register_write_trace{};
        uint8_t _system_register_write_trace_count{ 0 };
        std::array<watched_write_trace_t, k_watched_write_trace_capacity> _watched_write_trace{};
        uint8_t _watched_write_trace_count{ 0 };
        std::array<apu_port_trace_t, k_apu_port_trace_capacity> _apu_port_trace{};
        uint16_t _apu_port_trace_count{ 0 };
    };
}
