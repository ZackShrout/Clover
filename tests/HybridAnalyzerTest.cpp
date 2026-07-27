//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/HybridAnalyzer.h"
#include "clover/analysis/snes/StaticListing.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "HybridAnalyzerTest failed at %s\n", checkpoint);
        return 1;
    }

    [[nodiscard]] bool equivalent(
        const clover::analysis::program_model_t& left,
        const clover::analysis::program_model_t& right
    )
    {
        const auto ids = [](const auto& values)
        {
            std::vector<std::string> result{};
            for (const auto& value : values)
                result.push_back(value.stable_id);
            return result;
        };
        return ids(left.instructions) == ids(right.instructions)
            && ids(left.basic_blocks) == ids(right.basic_blocks)
            && ids(left.functions) == ids(right.functions)
            && ids(left.function_blocks) == ids(right.function_blocks)
            && ids(left.edges) == ids(right.edges)
            && ids(left.cross_references) == ids(right.cross_references)
            && ids(left.evidence) == ids(right.evidence)
            && ids(left.conflicts) == ids(right.conflicts);
    }
}

int main()
{
    using namespace clover::analysis;
    using namespace clover::analysis::snes;

    std::vector<std::byte> bytes(0x100u, std::byte{ 0xeau });
    const auto write = [&](size_t offset, uint8_t value)
    {
        bytes[offset] = static_cast<std::byte>(value);
    };

    // $8000: REP #$30
    // $8002: LDA #$1234
    // $8005: JSR $8010
    // $8008: NOP
    // $8009: BRA $800D
    // $800D: RTS
    // $8010: LDA #$5678
    // $8013: RTS
    write(0x00u, 0xc2u);
    write(0x01u, 0x30u);
    write(0x02u, 0xa9u);
    write(0x03u, 0x34u);
    write(0x04u, 0x12u);
    write(0x05u, 0x20u);
    write(0x06u, 0x10u);
    write(0x07u, 0x80u);
    write(0x08u, 0xeau);
    write(0x09u, 0x80u);
    write(0x0au, 0x02u);
    write(0x0du, 0x60u);
    write(0x10u, 0xa9u);
    write(0x11u, 0x78u);
    write(0x12u, 0x56u);
    write(0x13u, 0x60u);
    write(0x20u, 0x6cu); // $8020 JMP ($8030)
    write(0x21u, 0x30u);
    write(0x22u, 0x80u);
    write(0x40u, 0x60u); // observed target: RTS
    write(0x50u, 0x22u); // $8050 JSL $008100
    write(0x51u, 0x00u);
    write(0x52u, 0x81u);
    write(0x53u, 0x00u);
    write(0x60u, 0x20u); // $8060 JSR $8070
    write(0x61u, 0x70u);
    write(0x62u, 0x80u);
    write(0x63u, 0x60u); // $8063 RTS
    write(0x70u, 0x4cu); // $8070 JMP $8080 (tail jump)
    write(0x71u, 0x80u);
    write(0x72u, 0x80u);
    write(0x80u, 0x60u); // $8080 shared RTS
    write(0x90u, 0x4cu); // $8090 JMP $8080
    write(0x91u, 0x80u);
    write(0x92u, 0x80u);

    span_byte_source_t source{ bytes, 0x008000u };
    cpu_decode_context_t native_8{};
    native_8.emulation = bit_state_t::clear;
    native_8.accumulator_width = bit_state_t::set;
    native_8.index_width = bit_state_t::set;
    native_8.direct_page = 0u;
    native_8.data_bank = 0u;

    hybrid_analysis_options_t options{};
    options.seeds.push_back({
        .address = 0x008000u,
        .context = native_8,
        .kind = analysis_seed_kind_t::vector,
        .source = "reset"
    });
    const hybrid_analysis_result_t first{ analyze_program(source, options) };
    const hybrid_analysis_result_t second{ analyze_program(source, options) };
    if (first.limit_reached || !equivalent(first.model, second.model))
        return fail("deterministic");
    if (first.model.instructions.size() < 7u
        || first.model.basic_blocks.size() < 4u
        || first.model.functions.size() != 2u
        || first.model.function_blocks.empty())
    {
        return fail("recursive_model");
    }
    const bool has_call{
        std::ranges::any_of(
            first.model.edges,
            [](const edge_fact_t& edge)
            {
                return edge.kind == edge_kind_t::call
                    && edge.target.has_value()
                    && edge.target->address == 0x008010u;
            }
        )
    };
    if (!has_call)
        return fail("call_edge");

    cpu_decode_context_t native_16{ native_8 };
    native_16.accumulator_width = bit_state_t::clear;
    native_16.index_width = bit_state_t::clear;
    hybrid_analysis_options_t ambiguity_options{};
    ambiguity_options.seeds.push_back({
        .address = 0x008002u,
        .context = {},
        .kind = analysis_seed_kind_t::user,
        .source = "unknown-width"
    });
    ambiguity_options.seeds.push_back({
        .address = 0x008002u,
        .context = native_8,
        .kind = analysis_seed_kind_t::user,
        .source = "8-bit-width"
    });
    ambiguity_options.seeds.push_back({
        .address = 0x008002u,
        .context = native_16,
        .kind = analysis_seed_kind_t::user,
        .source = "16-bit-width"
    });
    const auto ambiguous{ analyze_program(source, ambiguity_options) };
    if (!std::ranges::any_of(
            ambiguous.model.conflicts,
            [](const conflict_fact_t& conflict)
            {
                return conflict.kind == conflict_kind_t::ambiguous_width;
            }
        )
        || !std::ranges::any_of(
            ambiguous.model.conflicts,
            [](const conflict_fact_t& conflict)
            {
                return conflict.kind
                    == conflict_kind_t::incompatible_context;
            }
        ))
    {
        return fail("ambiguous_and_conflicting_contexts");
    }

    hybrid_analysis_options_t overlap_options{};
    overlap_options.seeds.push_back({
        .address = 0x008050u,
        .context = native_8,
        .kind = analysis_seed_kind_t::user,
        .source = "long-instruction"
    });
    overlap_options.seeds.push_back({
        .address = 0x008051u,
        .context = native_8,
        .kind = analysis_seed_kind_t::user,
        .source = "overlapping-entry"
    });
    const auto overlap{ analyze_program(source, overlap_options) };
    if (!std::ranges::any_of(
            overlap.model.conflicts,
            [](const conflict_fact_t& conflict)
            {
                return conflict.kind
                    == conflict_kind_t::overlapping_instruction;
            }
        ))
    {
        return fail("overlapping_instruction");
    }

    hybrid_analysis_options_t shared_options{};
    shared_options.seeds.push_back({
        .address = 0x008060u,
        .context = native_8,
        .kind = analysis_seed_kind_t::user,
        .source = "caller"
    });
    shared_options.seeds.push_back({
        .address = 0x008090u,
        .context = native_8,
        .kind = analysis_seed_kind_t::user,
        .source = "second-tail-entry"
    });
    const auto shared{ analyze_program(source, shared_options) };
    const std::string shared_block{
        "block@008080[" + context_signature(native_8) + "]"
    };
    const size_t owners{
        static_cast<size_t>(std::count_if(
            shared.model.function_blocks.begin(),
            shared.model.function_blocks.end(),
            [&shared_block](const function_block_fact_t& membership)
            {
                return membership.block_id == shared_block;
            }
        ))
    };
    if (owners < 2u
        || !std::ranges::any_of(
            shared.model.edges,
            [](const edge_fact_t& edge)
            {
                return edge.kind == edge_kind_t::jump
                    && edge.target.has_value()
                    && edge.target->address == 0x008080u;
            }
        ))
    {
        return fail("shared_block_and_tail_jump");
    }

    hybrid_analysis_options_t conflict_options{ options };
    conflict_options.classifications.push_back({
        .address = 0x008010u,
        .length = 4u,
        .code = false
    });
    const auto conflicted{ analyze_program(source, conflict_options) };
    if (!std::ranges::any_of(
            conflicted.model.conflicts,
            [](const conflict_fact_t& conflict)
            {
                return conflict.kind == conflict_kind_t::user_data_boundary
                    && conflict.location.address == 0x008010u;
            }
        ))
    {
        return fail("user_data_conflict");
    }

    hybrid_analysis_options_t runtime_options{};
    runtime_options.runtime_edges.push_back({
        .from = 0x008020u,
        .to = 0x008040u,
        .context_before = native_8,
        .context_after = native_8,
        .from_identity = code_identity_t::writable_memory,
        .to_identity = code_identity_t::writable_memory,
        .session = "session-1",
        .hit_count = 7u
    });
    const auto runtime{ analyze_program(source, runtime_options) };
    if (runtime.model.coverage.size() != 1u
        || runtime.model.coverage.front().hit_count != 7u
        || runtime.model.coverage.front().session != "session-1"
        || !std::ranges::any_of(
            runtime.model.cross_references,
            [](const cross_reference_fact_t& reference)
            {
                return reference.kind
                    == cross_reference_kind_t::runtime_observed;
            }
        )
        || !std::ranges::any_of(
            runtime.model.edges,
            [](const edge_fact_t& edge)
            {
                return edge.kind == edge_kind_t::jump
                    && edge.target.has_value()
                    && edge.target->address == 0x008040u
                    && edge.confidence == confidence_t::confirmed;
            }
        )
        || !std::ranges::any_of(
            runtime.model.evidence,
            [](const evidence_fact_t& evidence)
            {
                return evidence.kind
                    == evidence_kind_t::runtime_indirect_target;
            }
        )
        || !std::ranges::any_of(
            runtime.model.instructions,
            [](const instruction_fact_t& instruction)
            {
                return instruction.location.address == 0x008020u
                    && instruction.code_identity
                        == code_identity_t::writable_memory;
            }
        )
        || hybrid_analysis_fingerprint("media", runtime_options).empty()
        || hybrid_analysis_fingerprint("media", runtime_options)
            == hybrid_analysis_fingerprint("other-media", runtime_options))
    {
        return fail("runtime_provenance");
    }

    std::printf(
        "Hybrid analyzer tests passed: deterministic traversal, blocks, "
        "functions, conflicts, and runtime evidence\n"
    );
    return 0;
}
