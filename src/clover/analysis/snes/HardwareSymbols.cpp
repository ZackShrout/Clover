//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/HardwareSymbols.h"

#include <array>
#include <cstdio>

namespace
{
    using clover::analysis::snes::hardware_symbol_t;

    struct named_register_t
    {
        uint16_t address{ 0 };
        std::string_view name{};
        std::string_view description{};
    };

    constexpr std::array<named_register_t, 104> k_named_registers{{
        { 0x2100u, "INIDISP", "Screen display control" },
        { 0x2101u, "OBSEL", "Object size and character data area" },
        { 0x2102u, "OAMADDL", "OAM address low" },
        { 0x2103u, "OAMADDH", "OAM address high and priority rotation" },
        { 0x2104u, "OAMDATA", "OAM data write" },
        { 0x2105u, "BGMODE", "Background mode and character size" },
        { 0x2106u, "MOSAIC", "Mosaic size and enable" },
        { 0x2107u, "BG1SC", "BG1 tile-map address and size" },
        { 0x2108u, "BG2SC", "BG2 tile-map address and size" },
        { 0x2109u, "BG3SC", "BG3 tile-map address and size" },
        { 0x210au, "BG4SC", "BG4 tile-map address and size" },
        { 0x210bu, "BG12NBA", "BG1/BG2 character-data address" },
        { 0x210cu, "BG34NBA", "BG3/BG4 character-data address" },
        { 0x210du, "BG1HOFS", "BG1 horizontal scroll" },
        { 0x210eu, "BG1VOFS", "BG1 vertical scroll" },
        { 0x210fu, "BG2HOFS", "BG2 horizontal scroll" },
        { 0x2110u, "BG2VOFS", "BG2 vertical scroll" },
        { 0x2111u, "BG3HOFS", "BG3 horizontal scroll" },
        { 0x2112u, "BG3VOFS", "BG3 vertical scroll" },
        { 0x2113u, "BG4HOFS", "BG4 horizontal scroll" },
        { 0x2114u, "BG4VOFS", "BG4 vertical scroll" },
        { 0x2115u, "VMAIN", "VRAM address increment mode" },
        { 0x2116u, "VMADDL", "VRAM address low" },
        { 0x2117u, "VMADDH", "VRAM address high" },
        { 0x2118u, "VMDATAL", "VRAM data write low" },
        { 0x2119u, "VMDATAH", "VRAM data write high" },
        { 0x211au, "M7SEL", "Mode 7 settings" },
        { 0x211bu, "M7A", "Mode 7 matrix A" },
        { 0x211cu, "M7B", "Mode 7 matrix B" },
        { 0x211du, "M7C", "Mode 7 matrix C" },
        { 0x211eu, "M7D", "Mode 7 matrix D" },
        { 0x211fu, "M7X", "Mode 7 center X" },
        { 0x2120u, "M7Y", "Mode 7 center Y" },
        { 0x2121u, "CGADD", "CGRAM address" },
        { 0x2122u, "CGDATA", "CGRAM data write" },
        { 0x2123u, "W12SEL", "BG1/BG2 window mask settings" },
        { 0x2124u, "W34SEL", "BG3/BG4 window mask settings" },
        { 0x2125u, "WOBJSEL", "OBJ/color window mask settings" },
        { 0x2126u, "WH0", "Window 1 left position" },
        { 0x2127u, "WH1", "Window 1 right position" },
        { 0x2128u, "WH2", "Window 2 left position" },
        { 0x2129u, "WH3", "Window 2 right position" },
        { 0x212au, "WBGLOG", "Background window logic" },
        { 0x212bu, "WOBJLOG", "OBJ/color window logic" },
        { 0x212cu, "TM", "Main-screen designation" },
        { 0x212du, "TS", "Sub-screen designation" },
        { 0x212eu, "TMW", "Main-screen window mask" },
        { 0x212fu, "TSW", "Sub-screen window mask" },
        { 0x2130u, "CGWSEL", "Color-math control A" },
        { 0x2131u, "CGADSUB", "Color-math control B" },
        { 0x2132u, "COLDATA", "Fixed color data" },
        { 0x2133u, "SETINI", "Display mode settings" },
        { 0x2134u, "MPYL", "Mode 7 multiplication result low" },
        { 0x2135u, "MPYM", "Mode 7 multiplication result middle" },
        { 0x2136u, "MPYH", "Mode 7 multiplication result high" },
        { 0x2137u, "SLHV", "Latch H/V counter" },
        { 0x2138u, "OAMDATAREAD", "OAM data read" },
        { 0x2139u, "VMDATALREAD", "VRAM data read low" },
        { 0x213au, "VMDATAHREAD", "VRAM data read high" },
        { 0x213bu, "CGDATAREAD", "CGRAM data read" },
        { 0x213cu, "OPHCT", "Horizontal counter read" },
        { 0x213du, "OPVCT", "Vertical counter read" },
        { 0x213eu, "STAT77", "PPU1 status" },
        { 0x213fu, "STAT78", "PPU2 status" },
        { 0x2140u, "APUIO0", "APU communication port 0" },
        { 0x2141u, "APUIO1", "APU communication port 1" },
        { 0x2142u, "APUIO2", "APU communication port 2" },
        { 0x2143u, "APUIO3", "APU communication port 3" },
        { 0x2180u, "WMDATA", "WRAM data port" },
        { 0x2181u, "WMADDL", "WRAM address low" },
        { 0x2182u, "WMADDM", "WRAM address middle" },
        { 0x2183u, "WMADDH", "WRAM address high" },
        { 0x4016u, "JOYSER0", "Controller port 1 serial data" },
        { 0x4017u, "JOYSER1", "Controller port 2 serial data" },
        { 0x4200u, "NMITIMEN", "Interrupt and auto-joypad enable" },
        { 0x4201u, "WRIO", "Programmable I/O port" },
        { 0x4202u, "WRMPYA", "Multiplicand A" },
        { 0x4203u, "WRMPYB", "Multiplier B" },
        { 0x4204u, "WRDIVL", "Dividend low" },
        { 0x4205u, "WRDIVH", "Dividend high" },
        { 0x4206u, "WRDIVB", "Divisor" },
        { 0x4207u, "HTIMEL", "H-IRQ position low" },
        { 0x4208u, "HTIMEH", "H-IRQ position high" },
        { 0x4209u, "VTIMEL", "V-IRQ position low" },
        { 0x420au, "VTIMEH", "V-IRQ position high" },
        { 0x420bu, "MDMAEN", "General DMA enable" },
        { 0x420cu, "HDMAEN", "HDMA enable" },
        { 0x420du, "MEMSEL", "FastROM access enable" },
        { 0x4210u, "RDNMI", "NMI status and CPU version" },
        { 0x4211u, "TIMEUP", "IRQ status" },
        { 0x4212u, "HVBJOY", "HBlank/VBlank/auto-joypad status" },
        { 0x4213u, "RDIO", "Programmable I/O read" },
        { 0x4214u, "RDDIVL", "Division quotient low" },
        { 0x4215u, "RDDIVH", "Division quotient high" },
        { 0x4216u, "RDMPYL", "Multiplication product or division remainder low" },
        { 0x4217u, "RDMPYH", "Multiplication product or division remainder high" },
        { 0x4218u, "JOY1L", "Auto-joypad 1 low" },
        { 0x4219u, "JOY1H", "Auto-joypad 1 high" },
        { 0x421au, "JOY2L", "Auto-joypad 2 low" },
        { 0x421bu, "JOY2H", "Auto-joypad 2 high" },
        { 0x421cu, "JOY3L", "Auto-joypad 3 low" },
        { 0x421du, "JOY3H", "Auto-joypad 3 high" },
        { 0x421eu, "JOY4L", "Auto-joypad 4 low" },
        { 0x421fu, "JOY4H", "Auto-joypad 4 high" }
    }};

    constexpr std::array<std::string_view, 16> k_dma_register_names{
        "DMAP", "BBAD", "A1TL", "A1TH", "A1B", "DASL", "DASH", "DASB",
        "A2AL", "A2AH", "NTRL", "UNUSED_B", "UNUSED_C", "UNUSED_D", "UNUSED_E", "UNUSED_F"
    };
}

namespace clover::analysis::snes
{
    std::optional<hardware_symbol_t> hardware_symbol(uint32_t cpu_address)
    {
        const uint8_t bank{ static_cast<uint8_t>((cpu_address >> 16u) & 0xffu) };
        if (!((bank <= 0x3fu) || (bank >= 0x80u && bank <= 0xbfu)))
            return std::nullopt;

        const uint16_t address{ static_cast<uint16_t>(cpu_address) };
        for (const named_register_t& reg : k_named_registers)
        {
            if (reg.address == address)
                return hardware_symbol_t{ std::string{ reg.name }, reg.description };
        }

        if (address >= 0x4300u && address <= 0x437fu)
        {
            const uint8_t channel{ static_cast<uint8_t>((address - 0x4300u) >> 4u) };
            const uint8_t reg{ static_cast<uint8_t>(address & 0x0fu) };
            std::array<char, 24> name{};
            std::snprintf(
                name.data(),
                name.size(),
                "%.*s%u",
                static_cast<int>(k_dma_register_names[reg].size()),
                k_dma_register_names[reg].data(),
                channel
            );
            return hardware_symbol_t{ std::string{ name.data() }, "DMA channel register" };
        }

        return std::nullopt;
    }
}
