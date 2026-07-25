//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/core/snes/Console.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clover::frontend
{
    enum class checkpoint_result_t : uint8_t
    {
        success,
        capture_failed,
        allocation_failed,
        invalid_magic,
        unsupported_format_version,
        unsupported_system,
        unsupported_core_state_version,
        unsupported_subsystem_version,
        truncated,
        payload_too_large,
        trailing_data,
        checksum_mismatch,
        media_mismatch,
        hardware_mismatch,
        malformed_payload,
        core_restore_failed
    };

    struct checkpoint_decode_limits_t
    {
        static constexpr uint64_t k_default_max_payload_bytes{ 8u * 1024u * 1024u };

        uint64_t max_payload_bytes{ k_default_max_payload_bytes };
    };

    [[nodiscard]] checkpoint_result_t capture_snes_checkpoint(
        core::console_t& console,
        std::vector<std::byte>& checkpoint
    ) noexcept;

    [[nodiscard]] checkpoint_result_t restore_snes_checkpoint(
        core::console_t& console,
        std::span<const std::byte> checkpoint,
        checkpoint_decode_limits_t limits = {}
    ) noexcept;
}
