//
// Created by Zack Shrout on 7/22/26.
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
    // Command-level model of Capcom's CX4 (Hitachi HG51BS169) cartridge DSP.
    // The public bus is 3 KiB of data RAM at $6000-$6bff and 256 bytes of
    // registers at $7f00-$7fff, mirrored through the CX4 cartridge window.
    class cx4_t
    {
    public:
        void power_on(std::span<const uint8_t> program_rom) noexcept;
        [[nodiscard]] uint8_t read(uint32_t address, uint8_t open_bus = 0) const noexcept;
        void write(uint32_t address, uint8_t value) noexcept;
        [[nodiscard]] bool capture_causal_state(
            std::vector<std::byte>& state
        ) const noexcept;
        [[nodiscard]] bool restore_causal_state(
            std::span<const std::byte> state
        ) noexcept;

    private:
        [[nodiscard]] uint8_t bus_read(uint32_t address) const noexcept;
        [[nodiscard]] uint8_t read_local(uint16_t address) const noexcept;
        [[nodiscard]] uint16_t read_word(uint16_t address) const noexcept;
        [[nodiscard]] uint32_t read_long(uint16_t address) const noexcept;
        void write_local(uint16_t address, uint8_t value) noexcept;
        void write_word(uint16_t address, uint16_t value) noexcept;
        void write_long(uint16_t address, uint32_t value) noexcept;
        [[nodiscard]] uint32_t load_register(uint8_t index) const noexcept;
        void store_register(uint8_t index, uint32_t value) noexcept;
        void execute(uint8_t command) noexcept;
        void transfer_data() noexcept;

        void command_sprite() noexcept;
        void build_oam() noexcept;
        void transform_lines() noexcept;
        void disintegrate() noexcept;
        void wave() noexcept;
        void scale_rotate(int row_padding) noexcept;
        void draw_wireframe() noexcept;
        void draw_line(int32_t x1,
                       int32_t y1,
                       int16_t z1,
                       int32_t x2,
                       int32_t y2,
                       int16_t z2,
                       uint8_t color) noexcept;
        void transform_perspective() noexcept;
        void transform_vector() noexcept;
        void calculate_line() noexcept;

        [[nodiscard]] static int16_t sin_fixed(uint16_t angle) noexcept;
        [[nodiscard]] static int16_t cos_fixed(uint16_t angle) noexcept;
        [[nodiscard]] static uint32_t cx4_sin(uint32_t angle) noexcept;
        static void multiply_24(uint32_t x, uint32_t y, uint32_t& low, uint32_t& high) noexcept;
        void immediate(uint32_t start) noexcept;

        std::array<uint8_t, 0x0c00> _ram{};
        std::array<uint8_t, 0x0100> _registers{};
        std::span<const uint8_t> _program_rom{};
        int16_t _x{};
        int16_t _y{};
        int16_t _z{};
        int16_t _x2{};
        int16_t _y2{};
        int16_t _distance{};
        int16_t _scale{};
    };
}
