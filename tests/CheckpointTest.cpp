//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/SnesCheckpoint.h"

#include <cstdio>
#include <memory>
#include <span>
#include <vector>

namespace
{
    using clover::frontend::checkpoint_result_t;

    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "CheckpointTest failed at %s\n", checkpoint);
        return 1;
    }

    [[nodiscard]] std::vector<std::byte> make_lorom(
        uint8_t marker = 0u,
        uint8_t cartridge_type = 0x00u
    )
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0 });
        constexpr size_t header{ 0x7fc0u };
        rom[0x100u] = static_cast<std::byte>(marker);
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = static_cast<std::byte>(cartridge_type);
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    void write_u16(std::vector<std::byte>& bytes, size_t offset, uint16_t value)
    {
        bytes[offset] = static_cast<std::byte>(value);
        bytes[offset + 1u] = static_cast<std::byte>(value >> 8u);
    }

    void write_u32(std::vector<std::byte>& bytes, size_t offset, uint32_t value)
    {
        for (size_t index{ 0 }; index < 4u; ++index)
            bytes[offset + index] = static_cast<std::byte>(value >> (index * 8u));
    }

    void write_u64(std::vector<std::byte>& bytes, size_t offset, uint64_t value)
    {
        for (size_t index{ 0 }; index < 8u; ++index)
            bytes[offset + index] = static_cast<std::byte>(value >> (index * 8u));
    }

    [[nodiscard]] uint32_t crc32(std::span<const std::byte> data)
    {
        uint32_t crc{ 0xffff'ffffu };
        for (const std::byte byte : data)
        {
            crc ^= static_cast<uint8_t>(byte);
            for (uint8_t bit{ 0 }; bit < 8u; ++bit)
                crc = (crc >> 1u) ^ (0xedb8'8320u & (0u - (crc & 1u)));
        }
        return ~crc;
    }

    void refresh_checksum(std::vector<std::byte>& checkpoint)
    {
        write_u32(checkpoint, 120u, crc32(
            std::span<const std::byte>{ checkpoint }.subspan(128u)
        ));
    }

    [[nodiscard]] bool unchanged(
        clover::core::console_t& console,
        const std::vector<std::byte>& expected
    )
    {
        std::vector<std::byte> actual{};
        return clover::frontend::capture_snes_checkpoint(console, actual)
                == checkpoint_result_t::success
            && actual == expected;
    }
}

int main()
{
    using namespace clover;

    core::console_t console{};
    const std::vector<std::byte> rom{ make_lorom() };
    if (!console.load_cartridge(rom))
        return fail("load_media");
    console.power_on();
    console.run_frame();
    console.write_u8(0x7e1234u, 0xa5u);

    std::vector<std::byte> checkpoint{};
    if (frontend::capture_snes_checkpoint(console, checkpoint)
            != checkpoint_result_t::success
        || checkpoint.size() <= 128u)
    {
        return fail("capture");
    }
    auto expected_state{ std::make_unique<core::console_causal_state_t>() };
    if (console.capture_causal_state(*expected_state)
        != core::console_checkpoint_result_t::success)
    {
        return fail("capture_expected_state");
    }

    console.run_frame();
    console.write_u8(0x7e1234u, 0x5au);
    if (frontend::restore_snes_checkpoint(console, checkpoint)
            != checkpoint_result_t::success
        || !unchanged(console, checkpoint))
    {
        return fail("round_trip");
    }
    auto actual_state{ std::make_unique<core::console_causal_state_t>() };
    if (console.capture_causal_state(*actual_state)
            != core::console_checkpoint_result_t::success
        || *actual_state != *expected_state)
    {
        return fail("fieldwise_state_equality");
    }

    auto invalid{ checkpoint };
    invalid[0] = std::byte{ 0 };
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::invalid_magic
        || !unchanged(console, checkpoint))
    {
        return fail("magic_atomic");
    }

    invalid = checkpoint;
    write_u16(invalid, 8u, 2u);
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::unsupported_format_version)
    {
        return fail("format_version");
    }

    invalid = checkpoint;
    write_u32(invalid, 84u, 2u);
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::unsupported_subsystem_version)
    {
        return fail("subsystem_version");
    }

    invalid = checkpoint;
    invalid.back() ^= std::byte{ 0x01u };
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::checksum_mismatch
        || !unchanged(console, checkpoint))
    {
        return fail("checksum_atomic");
    }

    invalid = checkpoint;
    invalid.pop_back();
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::truncated)
    {
        return fail("truncated");
    }

    invalid = checkpoint;
    invalid.push_back(std::byte{ 0 });
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::trailing_data)
    {
        return fail("trailing_data");
    }

    invalid = checkpoint;
    write_u64(invalid, 112u, frontend::checkpoint_decode_limits_t::
        k_default_max_payload_bytes + 1u);
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::payload_too_large)
    {
        return fail("payload_bound");
    }

    invalid = checkpoint;
    invalid[128u] = std::byte{ 2u };
    refresh_checksum(invalid);
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::malformed_payload
        || !unchanged(console, checkpoint))
    {
        return fail("bounded_decode_atomic");
    }

    invalid = checkpoint;
    invalid[60u] ^= std::byte{ 1u };
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::hardware_mismatch
        || !unchanged(console, checkpoint))
    {
        return fail("hardware_envelope_atomic");
    }

    invalid = checkpoint;
    invalid[132u] ^= std::byte{ 1u }; // scheduler master clock in payload
    refresh_checksum(invalid);
    if (frontend::restore_snes_checkpoint(console, invalid)
            != checkpoint_result_t::core_restore_failed
        || !unchanged(console, checkpoint))
    {
        return fail("core_transaction_atomic");
    }

    core::console_t other_media_console{};
    const std::vector<std::byte> other_rom{ make_lorom(0x5au) };
    if (!other_media_console.load_cartridge(other_rom))
        return fail("load_other_media");
    other_media_console.power_on();
    if (frontend::restore_snes_checkpoint(other_media_console, checkpoint)
            != checkpoint_result_t::media_mismatch)
    {
        return fail("canonical_media_hash");
    }

    core::console_t cx4_console{};
    const std::vector<std::byte> cx4_rom{ make_lorom(0u, 0xf3u) };
    if (!cx4_console.load_cartridge(cx4_rom))
        return fail("cx4_load");
    cx4_console.power_on();
    cx4_console.write_u8(0x006123u, 0x42u);
    std::vector<std::byte> cx4_checkpoint{};
    if (frontend::capture_snes_checkpoint(cx4_console, cx4_checkpoint)
            != checkpoint_result_t::success)
    {
        return fail("cx4_capture");
    }
    cx4_console.write_u8(0x006123u, 0x24u);
    if (frontend::restore_snes_checkpoint(cx4_console, cx4_checkpoint)
            != checkpoint_result_t::success
        || cx4_console.read_u8(0x006123u) != 0x42u)
    {
        return fail("cx4_round_trip");
    }

    return 0;
}
