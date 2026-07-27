//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"

#include <string>

namespace clover::analysis::snes
{
    struct formatting_options_t
    {
        bool use_hardware_symbols{ true };
        bool include_context_on_ambiguity{ true };
    };

    [[nodiscard]] std::string format_instruction(
        const decoded_instruction_t& instruction,
        formatting_options_t options = {}
    );
    [[nodiscard]] std::string format_instruction_json(
        const decoded_instruction_t& instruction
    );
}
