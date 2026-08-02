//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/ProgramModel.h"
#include "clover/frontend/DebugTarget.h"
#include "clover/workbench/Debugger.h"
#include "clover/workbench/Project.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clover::workbench
{
    struct disassembly_row_t
    {
        uint64_t address{ 0u };
        uint8_t encoded_size{ 0u };
        std::optional<uint64_t> direct_target{};
        std::string formatted_bytes{};
        std::string formatted_instruction{};
    };

    struct disassembly_listing_t
    {
        std::vector<disassembly_row_t> instructions{};
        uint64_t next_address{ 0u };
    };

    struct analysis_publication_t
    {
        analysis::program_model_t model{};
        uint64_t generation{ 0u };
        std::string status{};
    };

    // System-specific decoding and analysis exposed as system-neutral rows and
    // published facts. The application host never handles an architecture's
    // decoder types or runtime-evidence representation directly.
    class analysis_services_t
    {
    public:
        virtual ~analysis_services_t() = default;

        [[nodiscard]] virtual frontend::address_space_id_t
            instruction_address_space() const noexcept = 0;
        [[nodiscard]] virtual std::string_view
            instruction_address_space_name() const noexcept = 0;
        [[nodiscard]] virtual std::optional<uint64_t>
            default_entry() const = 0;
        [[nodiscard]] virtual disassembly_listing_t build_listing(
            uint64_t address,
            size_t maximum_instructions,
            const live_processor_state_t& state
        ) const = 0;
        [[nodiscard]] virtual bool analyze_and_publish(
            project_t& project,
            std::span<const classification_t> classifications,
            const debugger_t& debugger,
            analysis_publication_t& publication,
            std::string& error
        ) const = 0;
    };
}
