//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace clover::analysis::snes
{
    struct hardware_symbol_t
    {
        std::string name{};
        std::string_view description{};
    };

    [[nodiscard]] std::optional<hardware_symbol_t> hardware_symbol(
        uint32_t cpu_address
    );
}
