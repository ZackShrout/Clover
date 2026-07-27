//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace clover::analysis
{
    struct address_t
    {
        std::string address_space{};
        uint64_t address{ 0 };

        [[nodiscard]] bool operator==(const address_t&) const noexcept = default;
    };

    enum class confidence_t : uint8_t
    {
        confirmed,
        strongly_inferred,
        weakly_inferred,
        unresolved,
        conflicting
    };

    enum class edge_kind_t : uint8_t
    {
        fallthrough,
        conditional_branch,
        jump,
        call,
        return_,
        interrupt,
        unresolved
    };

    enum class cross_reference_kind_t : uint8_t
    {
        code,
        data,
        pointer,
        hardware_register,
        runtime_observed
    };

    enum class evidence_kind_t : uint8_t
    {
        vector,
        user_classification,
        recursive_decode,
        direct_target,
        runtime_execution,
        runtime_edge,
        runtime_indirect_target
    };

    enum class conflict_kind_t : uint8_t
    {
        incompatible_context,
        overlapping_instruction,
        user_data_boundary,
        unavailable_byte,
        ambiguous_width,
        contradictory_context,
        unresolved_transfer,
        analysis_limit
    };

    enum class code_identity_t : uint8_t
    {
        canonical_media,
        writable_memory,
        unavailable
    };

    struct instruction_fact_t
    {
        std::string stable_id{};
        address_t location{};
        std::string context{};
        uint8_t opcode{ 0 };
        uint8_t encoded_size{ 0 };
        code_identity_t code_identity{ code_identity_t::unavailable };
        confidence_t confidence{ confidence_t::weakly_inferred };
    };

    struct basic_block_fact_t
    {
        std::string stable_id{};
        address_t start{};
        address_t end{};
        std::string context{};
        confidence_t confidence{ confidence_t::weakly_inferred };
    };

    struct function_fact_t
    {
        std::string stable_id{};
        address_t entry{};
        confidence_t confidence{ confidence_t::weakly_inferred };
    };

    struct function_block_fact_t
    {
        std::string stable_id{};
        std::string function_id{};
        std::string block_id{};
    };

    struct edge_fact_t
    {
        std::string stable_id{};
        std::string source_block_id{};
        std::optional<std::string> target_block_id{};
        std::optional<address_t> target{};
        edge_kind_t kind{ edge_kind_t::unresolved };
        confidence_t confidence{ confidence_t::weakly_inferred };
    };

    struct cross_reference_fact_t
    {
        std::string stable_id{};
        address_t source{};
        address_t target{};
        cross_reference_kind_t kind{ cross_reference_kind_t::code };
        confidence_t confidence{ confidence_t::weakly_inferred };
    };

    struct evidence_fact_t
    {
        std::string stable_id{};
        std::string subject_id{};
        evidence_kind_t kind{ evidence_kind_t::recursive_decode };
        std::string source{};
        std::string session{};
        uint64_t observation_count{ 1 };
    };

    struct conflict_fact_t
    {
        std::string stable_id{};
        address_t location{};
        conflict_kind_t kind{ conflict_kind_t::unresolved_transfer };
        std::string detail{};
    };

    struct coverage_fact_t
    {
        address_t location{};
        std::string session{};
        uint64_t hit_count{ 0 };
    };

    struct program_model_t
    {
        std::vector<instruction_fact_t> instructions{};
        std::vector<basic_block_fact_t> basic_blocks{};
        std::vector<function_fact_t> functions{};
        std::vector<function_block_fact_t> function_blocks{};
        std::vector<edge_fact_t> edges{};
        std::vector<cross_reference_fact_t> cross_references{};
        std::vector<evidence_fact_t> evidence{};
        std::vector<conflict_fact_t> conflicts{};
        std::vector<coverage_fact_t> coverage{};
    };
}
