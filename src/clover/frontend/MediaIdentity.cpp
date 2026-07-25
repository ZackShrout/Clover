//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/MediaIdentity.h"

#include <bit>
#include <iomanip>
#include <sstream>
#include <vector>

namespace clover::frontend
{
    namespace
    {
        constexpr std::array<uint32_t, 64> k_sha256_round_constants{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };
    }

    std::span<const std::byte> canonical_media(
        system_id_t system,
        std::span<const std::byte> media
    ) noexcept
    {
        if (system == system_id_t::snes
            && media.size() >= 512u
            && (media.size() % 1024u) == 512u)
        {
            return media.subspan(512u);
        }
        return media;
    }

    media_digest_t media_sha256(std::span<const std::byte> media) noexcept
    {
        std::array<uint32_t, 8> state{
            0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
            0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
        };
        const uint64_t bit_count{ static_cast<uint64_t>(media.size()) * 8u };
        const size_t padded_size{ ((media.size() + 9u + 63u) / 64u) * 64u };
        std::vector<uint8_t> padded(padded_size, 0u);
        for (size_t index{ 0 }; index < media.size(); ++index)
            padded[index] = static_cast<uint8_t>(media[index]);
        padded[media.size()] = 0x80u;
        for (uint8_t index{ 0 }; index < 8u; ++index)
            padded[padded_size - 1u - index] = static_cast<uint8_t>(bit_count >> (index * 8u));

        for (size_t block{ 0 }; block < padded.size(); block += 64u)
        {
            std::array<uint32_t, 64> words{};
            for (size_t index{ 0 }; index < 16u; ++index)
            {
                const size_t offset{ block + index * 4u };
                words[index] = (static_cast<uint32_t>(padded[offset]) << 24u)
                    | (static_cast<uint32_t>(padded[offset + 1u]) << 16u)
                    | (static_cast<uint32_t>(padded[offset + 2u]) << 8u)
                    | padded[offset + 3u];
            }
            for (size_t index{ 16u }; index < words.size(); ++index)
            {
                const uint32_t s0{ std::rotr(words[index - 15u], 7)
                    ^ std::rotr(words[index - 15u], 18) ^ (words[index - 15u] >> 3u) };
                const uint32_t s1{ std::rotr(words[index - 2u], 17)
                    ^ std::rotr(words[index - 2u], 19) ^ (words[index - 2u] >> 10u) };
                words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
            }

            uint32_t a{ state[0] };
            uint32_t b{ state[1] };
            uint32_t c{ state[2] };
            uint32_t d{ state[3] };
            uint32_t e{ state[4] };
            uint32_t f{ state[5] };
            uint32_t g{ state[6] };
            uint32_t h{ state[7] };
            for (size_t index{ 0 }; index < words.size(); ++index)
            {
                const uint32_t sum1{ std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25) };
                const uint32_t choice{ (e & f) ^ (~e & g) };
                const uint32_t temporary1{
                    h + sum1 + choice + k_sha256_round_constants[index] + words[index]
                };
                const uint32_t sum0{ std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22) };
                const uint32_t majority{ (a & b) ^ (a & c) ^ (b & c) };
                const uint32_t temporary2{ sum0 + majority };
                h = g;
                g = f;
                f = e;
                e = d + temporary1;
                d = c;
                c = b;
                b = a;
                a = temporary1 + temporary2;
            }
            state[0] += a;
            state[1] += b;
            state[2] += c;
            state[3] += d;
            state[4] += e;
            state[5] += f;
            state[6] += g;
            state[7] += h;
        }

        media_digest_t result{};
        for (size_t index{ 0 }; index < state.size(); ++index)
        {
            result[index * 4u] = static_cast<uint8_t>(state[index] >> 24u);
            result[index * 4u + 1u] = static_cast<uint8_t>(state[index] >> 16u);
            result[index * 4u + 2u] = static_cast<uint8_t>(state[index] >> 8u);
            result[index * 4u + 3u] = static_cast<uint8_t>(state[index]);
        }
        return result;
    }

    std::string media_identity(
        system_id_t system,
        std::span<const std::byte> media
    )
    {
        const media_digest_t digest{ media_sha256(canonical_media(system, media)) };
        std::ostringstream output{};
        output << std::hex << std::setfill('0');
        for (const uint8_t byte : digest)
            output << std::setw(2) << static_cast<unsigned>(byte);
        return output.str();
    }
}
