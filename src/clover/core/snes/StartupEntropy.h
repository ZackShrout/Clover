//
// Created by Zack Shrout on 7/8/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>

namespace clover::core
{
    enum class startup_entropy_mode_t : uint8_t
    {
        none,
        low,
        high
    };

    struct startup_entropy_generator_t
    {
        explicit startup_entropy_generator_t(uint32_t seed, uint32_t sequence) noexcept
        {
            reseed(seed, sequence);
        }

        void reseed(uint32_t seed, uint32_t sequence) noexcept
        {
            _state = 0u;
            _increment = (static_cast<uint64_t>(sequence) << 1u) | 1u;
            static_cast<void>(step());
            _state += seed;
            static_cast<void>(step());
        }

        [[nodiscard]] uint32_t step() noexcept
        {
            const uint64_t state{ _state };
            _state = state * 6364136223846793005ull + _increment;
            const uint32_t xorshift{
                static_cast<uint32_t>(((state >> 18u) ^ state) >> 27u)
            };
            const uint32_t rotate{ static_cast<uint32_t>(state >> 59u) };
            return static_cast<uint32_t>(
                (xorshift >> rotate) | (xorshift << ((32u - rotate) & 31u))
            );
        }

        [[nodiscard]] uint64_t random_u64() noexcept
        {
            return (static_cast<uint64_t>(step()) << 32u) | step();
        }

        [[nodiscard]] bool random_bool() noexcept
        {
            return (step() & 0x01u) != 0u;
        }

        [[nodiscard]] uint8_t random_u8() noexcept
        {
            return static_cast<uint8_t>(step());
        }

        [[nodiscard]] uint16_t random_u16() noexcept
        {
            return static_cast<uint16_t>(step());
        }

        void fill_low_entropy(uint8_t* data, size_t size) noexcept
        {
            const uint32_t lobit{ step() & 0x03u };
            const uint32_t hibit{ static_cast<uint32_t>((lobit + 8u + (step() & 0x03u)) & 15u) };
            uint8_t lovalue{ static_cast<uint8_t>(step()) };
            uint8_t hivalue{ static_cast<uint8_t>(step()) };
            if ((step() & 0x03u) == 0u)
                lovalue = 0u;
            if ((step() & 0x01u) == 0u)
                hivalue = static_cast<uint8_t>(~lovalue);

            for (size_t address{ 0 }; address < size; ++address)
            {
                uint8_t value{
                    (address & (size_t{ 1u } << lobit)) != 0u ? lovalue : hivalue
                };
                if ((address & (size_t{ 1u } << hibit)) != 0u)
                    value = static_cast<uint8_t>(~value);
                if ((step() & 511u) == 0u)
                    value ^= static_cast<uint8_t>(1u << (step() & 0x07u));
                if ((step() & 2047u) == 0u)
                    value ^= static_cast<uint8_t>(1u << (step() & 0x07u));
                data[address] = value;
            }
        }

        void fill_high_entropy(uint8_t* data, size_t size) noexcept
        {
            for (size_t address{ 0 }; address < size; ++address)
                data[address] = random_u8();
        }

    private:
        uint64_t _state{ 0u };
        uint64_t _increment{ 0u };
    };

    [[nodiscard]] inline uint32_t default_startup_entropy_seed() noexcept
    {
        const auto now{
            std::chrono::steady_clock::now().time_since_epoch().count()
        };
        return static_cast<uint32_t>(static_cast<uint64_t>(now));
    }

    inline void fill_entropy_buffer(startup_entropy_mode_t mode,
                                    startup_entropy_generator_t& generator,
                                    uint8_t* data,
                                    size_t size) noexcept
    {
        switch (mode)
        {
        case startup_entropy_mode_t::high:
            generator.fill_high_entropy(data, size);
            return;
        case startup_entropy_mode_t::low:
            generator.fill_low_entropy(data, size);
            return;
        case startup_entropy_mode_t::none:
        default:
            return;
        }
    }
}
