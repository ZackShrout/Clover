//
// Created by Zack Shrout on 6/28/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <array>
#include <cstdint>

namespace clover::core
{
    struct cartridge_t;
    struct cpu_t;
    struct dma_t;
    struct ppu_t;

    struct bus_t
    {
    public:
        static constexpr uint32_t k_wram_base_address{ 0x7e0000u };
        static constexpr uint32_t k_wram_size{ 128 * 1024 };
        static constexpr uint32_t k_low_wram_mirror_size{ 0x2000u };
        void connect_cartridge(cartridge_t& cartridge) noexcept;
        void connect_cpu(cpu_t& cpu) noexcept;
        void connect_ppu(ppu_t& ppu) noexcept;
        void connect_dma(dma_t& dma) noexcept;
        void reset() noexcept;
        [[nodiscard]] uint8_t read_u8(uint32_t address) noexcept;
        void write_u8(uint32_t address, uint8_t value) noexcept;

    private:
        [[nodiscard]] static bool is_wram_address(uint32_t address) noexcept;
        [[nodiscard]] static uint32_t wram_offset(uint32_t address) noexcept;
        [[nodiscard]] static bool is_cpu_register_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_ppu_register_address(uint32_t address) noexcept;
        [[nodiscard]] static bool is_dma_register_address(uint32_t address) noexcept;

        std::array<uint8_t, k_wram_size> _wram{};
        cartridge_t* _cartridge{ nullptr };
        cpu_t* _cpu{ nullptr };
        dma_t* _dma{ nullptr };
        ppu_t* _ppu{ nullptr };
        uint8_t _open_bus{ 0 };
    };
}
