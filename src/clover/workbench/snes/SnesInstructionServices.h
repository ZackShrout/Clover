//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/Decoder.h"
#include "clover/workbench/DebuggerModel.h"

namespace clover::workbench::snes
{
    [[nodiscard]] analysis::snes::cpu_decode_context_t decode_context(
        const live_processor_state_t& state
    ) noexcept;
}
