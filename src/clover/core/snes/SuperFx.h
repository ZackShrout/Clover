//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "clover/core/snes/Timing.h"

namespace clover::core
{
    // Super FX / GSU cartridge processor. The public surface intentionally
    // mirrors the pins visible to the SNES CPU; instruction and pixel-cache
    // details remain private to the cartridge device.
    class super_fx_t
    {
    public:
        void power_on(std::span<const uint8_t> rom, std::span<uint8_t> ram) noexcept;
        void step_master_clocks(master_clock_delta_t clocks) noexcept;

        [[nodiscard]] uint8_t cpu_read_register(uint16_t address,
                                                uint8_t open_bus) noexcept;
        void cpu_write_register(uint16_t address, uint8_t value) noexcept;

        [[nodiscard]] bool running() const noexcept;
        [[nodiscard]] bool owns_rom_bus() const noexcept;
        [[nodiscard]] bool owns_ram_bus() const noexcept;
        [[nodiscard]] bool irq_pending() const noexcept;
        [[nodiscard]] bool take_ram_written() noexcept;
        [[nodiscard]] bool capture_causal_state(
            std::vector<std::byte>& state
        ) const noexcept;
        [[nodiscard]] bool restore_causal_state(
            std::span<const std::byte> state
        ) noexcept;

    private:
        struct pixel_cache_t
        {
            uint16_t offset{ 0xffffu };
            uint8_t pending{ 0 };
            std::array<uint8_t, 8> pixels{};
        };

        static constexpr uint16_t k_flag_zero{ 1u << 1u };
        static constexpr uint16_t k_flag_carry{ 1u << 2u };
        static constexpr uint16_t k_flag_sign{ 1u << 3u };
        static constexpr uint16_t k_flag_overflow{ 1u << 4u };
        static constexpr uint16_t k_flag_go{ 1u << 5u };
        static constexpr uint16_t k_flag_rom_pending{ 1u << 6u };
        static constexpr uint16_t k_flag_alt1{ 1u << 8u };
        static constexpr uint16_t k_flag_alt2{ 1u << 9u };
        static constexpr uint16_t k_flag_with{ 1u << 12u };
        static constexpr uint16_t k_flag_irq{ 1u << 15u };
        static constexpr uint16_t k_sfr_visible_mask{ 0x9f7eu };

        void execute_one() noexcept;
        void execute(uint8_t opcode) noexcept;

        [[nodiscard]] uint16_t source() const noexcept;
        void write_destination(uint16_t value) noexcept;
        void write_register(uint8_t index, uint16_t value) noexcept;
        void reset_prefixes() noexcept;
        void set_sz(uint16_t value) noexcept;

        [[nodiscard]] uint8_t current_alt() const noexcept;
        [[nodiscard]] bool flag(uint16_t mask) const noexcept;
        void set_flag(uint16_t mask, bool value) noexcept;

        void consume(uint32_t clocks) noexcept;
        void advance_buffer_clocks(uint32_t clocks) noexcept;
        void synchronize_rom_buffer() noexcept;
        void synchronize_ram_buffer() noexcept;
        [[nodiscard]] uint32_t next_opcode_fetch_clocks() const noexcept;
        [[nodiscard]] bool opcode_bus_available() const noexcept;
        [[nodiscard]] uint8_t fetch_opcode(uint16_t address) noexcept;
        [[nodiscard]] uint8_t peek_pipeline() noexcept;
        [[nodiscard]] uint8_t immediate_byte() noexcept;
        void invalidate_cache() noexcept;
        [[nodiscard]] uint8_t read_cache(uint16_t address) const noexcept;
        void write_cache(uint16_t address, uint8_t value) noexcept;

        [[nodiscard]] uint8_t read_memory(uint32_t address) noexcept;
        void write_memory(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_ram(uint16_t address) noexcept;
        void write_ram(uint16_t address, uint8_t value) noexcept;
        [[nodiscard]] uint8_t read_rom_buffer() noexcept;
        void update_rom_buffer() noexcept;

        [[nodiscard]] uint8_t apply_color_mode(uint8_t source) const noexcept;
        void plot(uint8_t x, uint8_t y) noexcept;
        [[nodiscard]] uint8_t read_pixel(uint8_t x, uint8_t y) noexcept;
        void flush_pixel_cache(pixel_cache_t& cache) noexcept;
        [[nodiscard]] uint32_t pixel_address(uint8_t x, uint8_t y) const noexcept;
        [[nodiscard]] uint8_t bits_per_pixel() const noexcept;

        std::span<const uint8_t> _rom{};
        std::span<uint8_t> _ram{};
        std::array<uint16_t, 16> _r{};
        uint16_t _sfr{ 0 };
        uint8_t _pbr{ 0 };
        uint8_t _rombr{ 0 };
        uint8_t _rambr{ 0 };
        uint16_t _cbr{ 0 };
        uint8_t _scbr{ 0 };
        uint8_t _scmr{ 0 };
        uint8_t _colr{ 0 };
        uint8_t _por{ 0 };
        uint8_t _bramr{ 0 };
        uint8_t _vcr{ 0x04 };
        uint8_t _cfgr{ 0 };
        uint8_t _clsr{ 0 };
        uint8_t _pipeline{ 0x01 };
        uint16_t _last_ram_address{ 0 };
        uint8_t _rom_data{ 0 };
        uint8_t _rom_buffer_clocks{ 0 };
        uint8_t _ram_buffer_clocks{ 0 };
        uint16_t _ram_buffer_address{ 0 };
        uint8_t _ram_buffer_data{ 0 };
        uint8_t _source_register{ 0 };
        uint8_t _destination_register{ 0 };
        bool _r14_modified{ false };
        bool _r15_modified{ false };
        bool _waiting_for_bus{ false };
        bool _ram_written{ false };
        int64_t _clock_credit{ 0 };
        std::array<uint8_t, 512> _cache{};
        std::array<bool, 32> _cache_valid{};
        std::array<pixel_cache_t, 2> _pixel_cache{};
    };
}
