//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"
#include "clover/frontend/DebugTarget.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace clover::analysis::snes
{
    struct span_byte_source_t final : byte_source_t
    {
    public:
        span_byte_source_t(std::span<const std::byte> bytes, uint32_t origin) noexcept;
        [[nodiscard]] inspected_byte_t inspect(uint32_t cpu_address) const noexcept override;
        [[nodiscard]] byte_identity_t identity(
            uint32_t cpu_address
        ) const noexcept override;

    private:
        std::span<const std::byte> _bytes{};
        uint32_t _origin{ 0 };
    };

    struct debug_target_byte_source_t final : byte_source_t
    {
    public:
        debug_target_byte_source_t(
            const frontend::debug_target_t& target,
            frontend::address_space_id_t cpu_bus_space,
            frontend::address_space_id_t canonical_media_space
        ) noexcept;
        [[nodiscard]] inspected_byte_t inspect(uint32_t cpu_address) const noexcept override;
        [[nodiscard]] byte_identity_t identity(
            uint32_t cpu_address
        ) const noexcept override;

    private:
        const frontend::debug_target_t& _target;
        frontend::address_space_id_t _cpu_bus_space{ 0 };
        frontend::address_space_id_t _canonical_media_space{ 0 };
    };

    enum class listing_stop_reason_t : uint8_t
    {
        instruction_limit,
        byte_limit,
        ambiguous_context,
        contradictory_context,
        unavailable
    };

    struct static_listing_options_t
    {
        uint32_t start_address{ 0 };
        size_t maximum_instructions{ 64 };
        size_t maximum_bytes{ 1024 };
        cpu_decode_context_t context{};
    };

    struct static_listing_result_t
    {
        std::vector<decoded_instruction_t> instructions{};
        listing_stop_reason_t stop_reason{ listing_stop_reason_t::instruction_limit };
        uint32_t next_address{ 0 };
        size_t bytes_consumed{ 0 };
    };

    [[nodiscard]] static_listing_result_t build_static_listing(
        const byte_source_t& source,
        const static_listing_options_t& options
    );
    [[nodiscard]] std::string_view listing_stop_reason_name(
        listing_stop_reason_t reason
    ) noexcept;
}
