//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/snes/HybridAnalyzer.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/DebugTarget.h"
#include "clover/workbench/AnalysisServices.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace clover::workbench::snes
{
    inline constexpr std::string_view k_snes_analyzer_version{
        "clover-analyzer-2"
    };
    inline constexpr std::string_view k_snes_decoder_version{
        "wdc65c816-decoder-1"
    };

    class snes_analysis_services_t final : public analysis_services_t
    {
    public:
        explicit snes_analysis_services_t(
            const frontend::debug_target_t& target
        ) noexcept;

        [[nodiscard]] frontend::address_space_id_t
            instruction_address_space() const noexcept override;
        [[nodiscard]] std::string_view
            instruction_address_space_name() const noexcept override;
        [[nodiscard]] std::optional<uint64_t>
            default_entry() const override;
        [[nodiscard]] disassembly_listing_t build_listing(
            uint64_t address,
            size_t maximum_instructions,
            const live_processor_state_t& state
        ) const override;
        [[nodiscard]] bool analyze_and_publish(
            project_t& project,
            std::span<const classification_t> classifications,
            const debugger_t& debugger,
            analysis_publication_t& publication,
            std::string& error
        ) const override;

    private:
        analysis::snes::debug_target_byte_source_t _source;
    };
}
