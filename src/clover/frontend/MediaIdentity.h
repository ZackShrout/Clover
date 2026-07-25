//
// Created by Zack Shrout on 7/24/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/EmulatorCore.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace clover::frontend
{
    using media_digest_t = std::array<uint8_t, 32>;

    [[nodiscard]] std::span<const std::byte> canonical_media(
        system_id_t system,
        std::span<const std::byte> media
    ) noexcept;
    [[nodiscard]] media_digest_t media_sha256(std::span<const std::byte> media) noexcept;
    [[nodiscard]] std::string media_identity(
        system_id_t system,
        std::span<const std::byte> media
    );
}
