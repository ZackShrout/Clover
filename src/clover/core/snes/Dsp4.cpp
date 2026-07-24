//
// Created by Zack Shrout on 7/23/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Dsp4.h"

#include <cstdint>
#include <cstring>

namespace clover::core::dsp4_legacy
{
    using bool8 = uint8_t;
    using int8 = int8_t;
    using int16 = int16_t;
    using int32 = int32_t;
    using int64 = int64_t;
    using uint8 = uint8_t;
    using uint16 = uint16_t;
    using uint32 = uint32_t;
    using uint64 = uint64_t;

    [[nodiscard]] inline uint16 read_word(const uint8* address) noexcept
    {
        return static_cast<uint16>(
            address[0] | (static_cast<uint16>(address[1]) << 8u)
        );
    }

    [[nodiscard]] inline uint32 read_dword(const uint8* address) noexcept
    {
        return static_cast<uint32>(
            address[0]
            | (static_cast<uint32>(address[1]) << 8u)
            | (static_cast<uint32>(address[2]) << 16u)
            | (static_cast<uint32>(address[3]) << 24u)
        );
    }

    inline void write_word(uint8* address, uint16 value) noexcept
    {
        address[0] = static_cast<uint8>(value);
        address[1] = static_cast<uint8>(value >> 8u);
    }

#define READ_WORD(address) read_word(address)
#define READ_DWORD(address) read_dword(address)
#define WRITE_WORD(address, value) write_word(address, value)
#include "clover/core/snes/Dsp4Legacy.inc"
#undef WRITE_WORD
#undef READ_DWORD
#undef READ_WORD
}

namespace clover::core
{
    void dsp4_t::power_on() noexcept
    {
        dsp4_legacy::InitDSP4();
    }

    uint8_t dsp4_t::read_data() noexcept
    {
        dsp4_legacy::DSP4GetByte();
        return dsp4_legacy::dsp4_byte;
    }

    uint8_t dsp4_t::read_status() noexcept
    {
        return 0x80u;
    }

    void dsp4_t::write_data(uint8_t value) noexcept
    {
        dsp4_legacy::dsp4_byte = value;
        dsp4_legacy::DSP4SetByte();
    }
}
