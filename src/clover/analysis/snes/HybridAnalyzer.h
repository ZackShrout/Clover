//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/ProgramModel.h"
#include "clover/analysis/snes/Decoder.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace clover::analysis::snes
{
    inline constexpr std::string_view k_cpu_bus_address_space{ "snes.cpu-bus" };

    enum class analysis_seed_kind_t : uint8_t
    {
        vector,
        user,
        runtime
    };

    struct analysis_seed_t
    {
        uint32_t address{ 0 };
        cpu_decode_context_t context{};
        analysis_seed_kind_t kind{ analysis_seed_kind_t::user };
        std::string source{};
    };

    struct classified_range_t
    {
        uint32_t address{ 0 };
        uint32_t length{ 0 };
        bool code{ false };
    };

    struct runtime_edge_t
    {
        uint32_t from{ 0 };
        uint32_t to{ 0 };
        cpu_decode_context_t context_before{};
        cpu_decode_context_t context_after{};
        code_identity_t from_identity{ code_identity_t::unavailable };
        code_identity_t to_identity{ code_identity_t::unavailable };
        std::string session{};
        uint64_t hit_count{ 1 };
    };

    struct hybrid_analysis_options_t
    {
        std::vector<analysis_seed_t> seeds{};
        std::vector<classified_range_t> classifications{};
        std::vector<runtime_edge_t> runtime_edges{};
        size_t maximum_instructions{ 250000u };
        size_t maximum_blocks{ 100000u };
    };

    struct hybrid_analysis_result_t
    {
        program_model_t model{};
        bool limit_reached{ false };
    };

    [[nodiscard]] std::string context_signature(
        const cpu_decode_context_t& context
    );
    [[nodiscard]] std::vector<analysis_seed_t> default_snes_vector_seeds(
        const byte_source_t& source
    );
    [[nodiscard]] hybrid_analysis_result_t analyze_program(
        const byte_source_t& source,
        const hybrid_analysis_options_t& options
    );
    [[nodiscard]] std::string hybrid_analysis_fingerprint(
        std::string_view canonical_media_identity,
        const hybrid_analysis_options_t& options
    );
}
