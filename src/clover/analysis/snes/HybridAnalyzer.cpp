//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/HybridAnalyzer.h"

#include <algorithm>
#include <array>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>

namespace
{
    using namespace clover::analysis;
    using namespace clover::analysis::snes;

    constexpr uint32_t k_address_mask{ 0x00ffffffu };

    [[nodiscard]] std::string hex_value(uint64_t value, int width)
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0')
               << std::setw(width) << value;
        return output.str();
    }

    [[nodiscard]] address_t cpu_address(uint32_t address)
    {
        return {
            .address_space = std::string{ k_cpu_bus_address_space },
            .address = address & k_address_mask
        };
    }

    [[nodiscard]] std::string instruction_id(uint32_t address,
                                             std::string_view context)
    {
        return "instruction@" + hex_value(address & k_address_mask, 6)
            + "[" + std::string{ context } + "]";
    }

    [[nodiscard]] std::string block_id(uint32_t address,
                                       std::string_view context)
    {
        return "block@" + hex_value(address & k_address_mask, 6)
            + "[" + std::string{ context } + "]";
    }

    [[nodiscard]] std::string function_id(uint32_t address)
    {
        return "function@" + hex_value(address & k_address_mask, 6);
    }

    [[nodiscard]] std::string edge_id(std::string_view source,
                                      edge_kind_t kind,
                                      std::optional<uint32_t> target)
    {
        return "edge[" + std::string{ source } + ":"
            + std::to_string(static_cast<uint32_t>(kind)) + ":"
            + (target.has_value() ? hex_value(*target & k_address_mask, 6) : "?")
            + "]";
    }

    [[nodiscard]] std::string xref_id(uint32_t source,
                                      uint32_t target,
                                      cross_reference_kind_t kind)
    {
        return "xref@" + hex_value(source & k_address_mask, 6)
            + "->" + hex_value(target & k_address_mask, 6)
            + ":" + std::to_string(static_cast<uint32_t>(kind));
    }

    [[nodiscard]] std::string conflict_id(uint32_t address,
                                          conflict_kind_t kind,
                                          std::string_view context)
    {
        return "conflict@" + hex_value(address & k_address_mask, 6)
            + ":" + std::to_string(static_cast<uint32_t>(kind))
            + "[" + std::string{ context } + "]";
    }

    [[nodiscard]] bool range_contains(const classified_range_t& range,
                                      uint32_t address)
    {
        if (range.length == 0u)
            return false;
        const uint64_t start{ range.address & k_address_mask };
        const uint64_t end{ start + range.length };
        return address >= start && address < end && end <= 0x01000000ull;
    }

    [[nodiscard]] bool user_data_at(
        const std::vector<classified_range_t>& classifications,
        uint32_t address
    )
    {
        return std::any_of(
            classifications.begin(),
            classifications.end(),
            [address](const classified_range_t& range)
            {
                return !range.code && range_contains(range, address);
            }
        );
    }

    [[nodiscard]] confidence_t seed_confidence(analysis_seed_kind_t kind)
    {
        switch (kind)
        {
        case analysis_seed_kind_t::vector:
        case analysis_seed_kind_t::user:
        case analysis_seed_kind_t::runtime:
            return confidence_t::confirmed;
        }
        return confidence_t::weakly_inferred;
    }

    [[nodiscard]] evidence_kind_t seed_evidence(analysis_seed_kind_t kind)
    {
        switch (kind)
        {
        case analysis_seed_kind_t::vector:
            return evidence_kind_t::vector;
        case analysis_seed_kind_t::user:
            return evidence_kind_t::user_classification;
        case analysis_seed_kind_t::runtime:
            return evidence_kind_t::runtime_execution;
        }
        return evidence_kind_t::recursive_decode;
    }

    [[nodiscard]] cpu_decode_context_t context_after(
        const decoded_instruction_t& instruction
    )
    {
        cpu_decode_context_t result{ instruction.context };
        const uint8_t operand{
            static_cast<uint8_t>(instruction.operand_value.value_or(0u))
        };
        if (instruction.instruction == instruction_id_t::rep)
        {
            if ((operand & 0x20u) != 0u)
                result.accumulator_width = bit_state_t::clear;
            if ((operand & 0x10u) != 0u)
                result.index_width = bit_state_t::clear;
        }
        else if (instruction.instruction == instruction_id_t::sep)
        {
            if ((operand & 0x20u) != 0u)
                result.accumulator_width = bit_state_t::set;
            if ((operand & 0x10u) != 0u)
                result.index_width = bit_state_t::set;
        }
        else if (instruction.instruction == instruction_id_t::xce)
        {
            result.emulation = bit_state_t::unknown;
            result.accumulator_width = bit_state_t::unknown;
            result.index_width = bit_state_t::unknown;
        }
        else if (instruction.instruction == instruction_id_t::plp
                 || instruction.instruction == instruction_id_t::rti)
        {
            result.accumulator_width = bit_state_t::unknown;
            result.index_width = bit_state_t::unknown;
        }

        if (result.emulation == bit_state_t::set)
        {
            result.accumulator_width = bit_state_t::set;
            result.index_width = bit_state_t::set;
        }
        if (instruction.instruction == instruction_id_t::plb)
            result.data_bank.reset();
        if (instruction.instruction == instruction_id_t::pld
            || instruction.instruction == instruction_id_t::tcd)
        {
            result.direct_page.reset();
        }
        return result;
    }

    [[nodiscard]] cpu_decode_context_t context_after_call(
        const cpu_decode_context_t& before
    )
    {
        cpu_decode_context_t result{ before };
        result.emulation = bit_state_t::unknown;
        result.accumulator_width = bit_state_t::unknown;
        result.index_width = bit_state_t::unknown;
        result.direct_page.reset();
        result.data_bank.reset();
        return result;
    }

    [[nodiscard]] std::optional<uint32_t> data_reference(
        const decoded_instruction_t& instruction
    )
    {
        if (!instruction.operand_value.has_value())
            return std::nullopt;
        const uint32_t operand{ *instruction.operand_value };
        switch (instruction.addressing_mode)
        {
        case addressing_mode_t::direct:
        case addressing_mode_t::direct_x:
        case addressing_mode_t::direct_y:
            if (instruction.context.direct_page.has_value())
            {
                return (
                    static_cast<uint32_t>(*instruction.context.direct_page)
                    + (operand & 0xffu)
                ) & 0xffffu;
            }
            return std::nullopt;
        case addressing_mode_t::absolute:
        case addressing_mode_t::absolute_x:
        case addressing_mode_t::absolute_y:
            if (instruction.context.data_bank.has_value())
            {
                return (static_cast<uint32_t>(*instruction.context.data_bank) << 16u)
                    | (operand & 0xffffu);
            }
            return std::nullopt;
        case addressing_mode_t::absolute_long:
        case addressing_mode_t::absolute_long_x:
            return operand & k_address_mask;
        default:
            return std::nullopt;
        }
    }

    [[nodiscard]] cross_reference_kind_t reference_kind(uint32_t target)
    {
        const uint8_t bank{ static_cast<uint8_t>(target >> 16u) };
        const uint16_t offset{ static_cast<uint16_t>(target) };
        if ((bank == 0x00u || bank == 0x80u)
            && offset >= 0x2000u && offset <= 0x5fffu)
        {
            return cross_reference_kind_t::hardware_register;
        }
        return cross_reference_kind_t::data;
    }

    [[nodiscard]] code_identity_t model_identity(byte_identity_t identity)
    {
        switch (identity)
        {
        case byte_identity_t::canonical_media:
            return code_identity_t::canonical_media;
        case byte_identity_t::writable_memory:
            return code_identity_t::writable_memory;
        case byte_identity_t::unavailable:
            return code_identity_t::unavailable;
        }
        return code_identity_t::unavailable;
    }

    struct block_seed_t
    {
        uint32_t address{ 0 };
        cpu_decode_context_t context{};
        std::optional<uint32_t> function_entry{};
        confidence_t confidence{ confidence_t::weakly_inferred };
        evidence_kind_t evidence{ evidence_kind_t::recursive_decode };
        std::string source{};
        std::string session{};
        uint64_t observation_count{ 1 };
    };

    void sort_model(program_model_t& model)
    {
        const auto by_id = [](const auto& left, const auto& right)
        {
            return left.stable_id < right.stable_id;
        };
        std::ranges::sort(model.instructions, by_id);
        std::ranges::sort(model.basic_blocks, by_id);
        std::ranges::sort(model.functions, by_id);
        std::ranges::sort(model.function_blocks, by_id);
        std::ranges::sort(model.edges, by_id);
        std::ranges::sort(model.cross_references, by_id);
        std::ranges::sort(model.evidence, by_id);
        std::ranges::sort(model.conflicts, by_id);
        std::ranges::sort(
            model.coverage,
            [](const coverage_fact_t& left, const coverage_fact_t& right)
            {
                return std::tie(
                    left.location.address_space,
                    left.location.address,
                    left.session
                ) < std::tie(
                    right.location.address_space,
                    right.location.address,
                    right.session
                );
            }
        );
    }
}

namespace clover::analysis::snes
{
    std::string context_signature(const cpu_decode_context_t& context)
    {
        std::string result{
            "E=" + std::string{ bit_state_name(context.emulation) }
            + ";M=" + std::string{ bit_state_name(context.accumulator_width) }
            + ";X=" + std::string{ bit_state_name(context.index_width) }
            + ";D="
        };
        result += context.direct_page.has_value()
            ? hex_value(*context.direct_page, 4)
            : "????";
        result += ";DB=";
        result += context.data_bank.has_value()
            ? hex_value(*context.data_bank, 2)
            : "??";
        return result;
    }

    std::vector<analysis_seed_t> default_snes_vector_seeds(
        const byte_source_t& source
    )
    {
        cpu_decode_context_t emulation{};
        emulation.emulation = bit_state_t::set;
        emulation.accumulator_width = bit_state_t::set;
        emulation.index_width = bit_state_t::set;
        emulation.direct_page = 0u;
        emulation.data_bank = 0u;

        cpu_decode_context_t native{};
        native.emulation = bit_state_t::clear;

        struct vector_descriptor_t
        {
            uint32_t address{ 0 };
            cpu_decode_context_t context{};
            const char* source{ nullptr };
        };
        const std::array vectors{
            vector_descriptor_t{ 0x00fffcu, emulation, "reset-vector" },
            vector_descriptor_t{ 0x00fff4u, emulation, "emulation-cop-vector" },
            vector_descriptor_t{ 0x00fff8u, emulation, "emulation-abort-vector" },
            vector_descriptor_t{ 0x00fffau, emulation, "emulation-nmi-vector" },
            vector_descriptor_t{ 0x00fffeu, emulation, "emulation-irq-vector" },
            vector_descriptor_t{ 0x00ffe4u, native, "native-cop-vector" },
            vector_descriptor_t{ 0x00ffe6u, native, "native-brk-vector" },
            vector_descriptor_t{ 0x00ffe8u, native, "native-abort-vector" },
            vector_descriptor_t{ 0x00ffeau, native, "native-nmi-vector" },
            vector_descriptor_t{ 0x00ffeeu, native, "native-irq-vector" }
        };

        std::vector<analysis_seed_t> result{};
        for (const vector_descriptor_t& vector : vectors)
        {
            const inspected_byte_t low{ source.inspect(vector.address) };
            const inspected_byte_t high{
                source.inspect(advance_program_address(vector.address, 1u))
            };
            if (low.status != byte_inspection_status_t::available
                || high.status != byte_inspection_status_t::available)
            {
                continue;
            }
            const uint32_t entry{
                static_cast<uint32_t>(low.value)
                | (static_cast<uint32_t>(high.value) << 8u)
            };
            if (entry == 0u)
                continue;
            result.push_back({
                .address = entry,
                .context = vector.context,
                .kind = analysis_seed_kind_t::vector,
                .source = vector.source
            });
        }
        return result;
    }

    std::string hybrid_analysis_fingerprint(
        std::string_view canonical_media_identity,
        const hybrid_analysis_options_t& options
    )
    {
        std::vector<std::string> inputs{};
        inputs.reserve(
            1u + options.seeds.size() + options.classifications.size()
                + options.runtime_edges.size()
        );
        inputs.emplace_back("media:" + std::string{ canonical_media_identity });
        for (const analysis_seed_t& seed : options.seeds)
        {
            inputs.push_back(
                "seed:" + hex_value(seed.address & k_address_mask, 6)
                + ":" + context_signature(seed.context)
                + ":" + std::to_string(static_cast<uint32_t>(seed.kind))
                + ":" + seed.source
            );
        }
        for (const classified_range_t& range : options.classifications)
        {
            inputs.push_back(
                "classification:"
                + hex_value(range.address & k_address_mask, 6)
                + ":" + std::to_string(range.length)
                + ":" + (range.code ? "code" : "data")
            );
        }
        for (const runtime_edge_t& edge : options.runtime_edges)
        {
            inputs.push_back(
                "runtime:" + hex_value(edge.from & k_address_mask, 6)
                + ">" + hex_value(edge.to & k_address_mask, 6)
                + ":" + context_signature(edge.context_before)
                + ":" + context_signature(edge.context_after)
                + ":" + std::to_string(
                    static_cast<uint32_t>(edge.from_identity)
                )
                + ":" + std::to_string(
                    static_cast<uint32_t>(edge.to_identity)
                )
                + ":" + edge.session
                + ":" + std::to_string(edge.hit_count)
            );
        }
        inputs.push_back(
            "limits:" + std::to_string(options.maximum_instructions)
                + ":" + std::to_string(options.maximum_blocks)
        );
        std::ranges::sort(inputs);

        uint64_t hash{ 14695981039346656037ull };
        for (const std::string& input : inputs)
        {
            for (const unsigned char byte : input)
            {
                hash ^= byte;
                hash *= 1099511628211ull;
            }
            hash ^= 0xffu;
            hash *= 1099511628211ull;
        }
        return hex_value(hash, 16);
    }

    hybrid_analysis_result_t analyze_program(
        const byte_source_t& source,
        const hybrid_analysis_options_t& options
    )
    {
        hybrid_analysis_result_t result{};
        program_model_t& model{ result.model };

        std::vector<block_seed_t> worklist{};
        worklist.reserve(
            options.seeds.size() + options.runtime_edges.size() * 2u
        );
        for (const analysis_seed_t& seed : options.seeds)
        {
            worklist.push_back({
                .address = seed.address & k_address_mask,
                .context = seed.context,
                .function_entry = seed.address & k_address_mask,
                .confidence = seed_confidence(seed.kind),
                .evidence = seed_evidence(seed.kind),
                .source = seed.source
            });
        }
        for (const runtime_edge_t& edge : options.runtime_edges)
        {
            worklist.push_back({
                .address = edge.from & k_address_mask,
                .context = edge.context_before,
                .confidence = confidence_t::confirmed,
                .evidence = evidence_kind_t::runtime_execution,
                .source = "runtime",
                .session = edge.session,
                .observation_count = edge.hit_count
            });
            worklist.push_back({
                .address = edge.to & k_address_mask,
                .context = edge.context_after,
                .confidence = confidence_t::confirmed,
                .evidence = evidence_kind_t::runtime_execution,
                .source = "runtime",
                .session = edge.session,
                .observation_count = edge.hit_count
            });
        }
        std::ranges::sort(
            worklist,
            [](const block_seed_t& left, const block_seed_t& right)
            {
                return std::tie(left.address, left.source, left.session)
                    < std::tie(right.address, right.source, right.session);
            }
        );

        std::set<std::string> queued_blocks{};
        std::set<std::string> instruction_ids{};
        std::set<std::string> block_ids{};
        std::set<std::string> function_ids{};
        std::set<std::string> function_block_ids{};
        std::set<std::string> edge_ids{};
        std::set<std::string> xref_ids{};
        std::set<std::string> evidence_ids{};
        std::set<std::string> conflict_ids{};
        std::map<uint32_t, std::string> first_context_at_address{};
        std::map<uint32_t, uint32_t> occupied_bytes{};

        const auto add_conflict = [&](uint32_t address,
                                      conflict_kind_t kind,
                                      std::string detail,
                                      std::string_view context)
        {
            const std::string id{ conflict_id(address, kind, context) };
            if (!conflict_ids.insert(id).second)
                return;
            model.conflicts.push_back({
                .stable_id = id,
                .location = cpu_address(address),
                .kind = kind,
                .detail = std::move(detail)
            });
        };

        const auto add_function = [&](uint32_t entry, confidence_t confidence)
        {
            const std::string id{ function_id(entry) };
            if (!function_ids.insert(id).second)
                return;
            model.functions.push_back({
                .stable_id = id,
                .entry = cpu_address(entry),
                .confidence = confidence
            });
        };

        const auto enqueue = [&](block_seed_t seed)
        {
            seed.address &= k_address_mask;
            const std::string block{
                block_id(seed.address, context_signature(seed.context))
            };
            const std::string queue_id{
                block + "|"
                + (seed.function_entry.has_value()
                    ? function_id(*seed.function_entry)
                    : "unowned")
            };
            if (queued_blocks.insert(queue_id).second)
                worklist.push_back(std::move(seed));
        };

        for (const block_seed_t& seed : worklist)
        {
            queued_blocks.insert(
                block_id(seed.address, context_signature(seed.context))
                + "|"
                + (seed.function_entry.has_value()
                    ? function_id(*seed.function_entry)
                    : "unowned")
            );
            if (seed.function_entry.has_value())
                add_function(*seed.function_entry, seed.confidence);
        }

        size_t work_index{ 0 };
        while (work_index < worklist.size())
        {
            if (model.basic_blocks.size() >= options.maximum_blocks
                || model.instructions.size() >= options.maximum_instructions)
            {
                result.limit_reached = true;
                const block_seed_t& limited{ worklist[work_index] };
                add_conflict(
                    limited.address,
                    conflict_kind_t::analysis_limit,
                    "Analyzer resource limit reached",
                    context_signature(limited.context)
                );
                break;
            }

            const block_seed_t seed{ worklist[work_index++] };
            const std::string entry_context{ context_signature(seed.context) };
            const std::string current_block_id{
                block_id(seed.address, entry_context)
            };
            if (seed.function_entry.has_value())
            {
                add_function(*seed.function_entry, seed.confidence);
                const std::string owner{ function_id(*seed.function_entry) };
                const std::string membership_id{
                    "membership[" + owner + ":" + current_block_id + "]"
                };
                if (function_block_ids.insert(membership_id).second)
                {
                    model.function_blocks.push_back({
                        .stable_id = membership_id,
                        .function_id = owner,
                        .block_id = current_block_id
                    });
                }
            }
            if (block_ids.contains(current_block_id))
                continue;
            block_ids.insert(current_block_id);

            uint32_t address{ seed.address };
            cpu_decode_context_t context{ seed.context };
            uint32_t end_address{ address };
            bool block_complete{ false };

            while (!block_complete)
            {
                const std::string signature{ context_signature(context) };
                if (address != seed.address
                    && block_ids.contains(block_id(address, signature)))
                {
                    const std::string id{
                        edge_id(
                            current_block_id,
                            edge_kind_t::fallthrough,
                            address
                        )
                    };
                    if (edge_ids.insert(id).second)
                    {
                        model.edges.push_back({
                            .stable_id = id,
                            .source_block_id = current_block_id,
                            .target_block_id = block_id(address, signature),
                            .target = cpu_address(address),
                            .kind = edge_kind_t::fallthrough,
                            .confidence = confidence_t::strongly_inferred
                        });
                    }
                    end_address = address;
                    break;
                }
                if (user_data_at(options.classifications, address))
                {
                    add_conflict(
                        address,
                        conflict_kind_t::user_data_boundary,
                        "Recursive traversal reached user-classified data",
                        signature
                    );
                    end_address = address;
                    break;
                }

                if (const auto existing{ first_context_at_address.find(address) };
                    existing != first_context_at_address.end()
                    && existing->second != signature)
                {
                    add_conflict(
                        address,
                        conflict_kind_t::incompatible_context,
                        "Address reached with contexts "
                            + existing->second + " and " + signature,
                        signature
                    );
                }
                else
                {
                    first_context_at_address.emplace(address, signature);
                }

                if (const auto overlap{ occupied_bytes.find(address) };
                    overlap != occupied_bytes.end() && overlap->second != address)
                {
                    add_conflict(
                        address,
                        conflict_kind_t::overlapping_instruction,
                        "Instruction entry overlaps instruction at $"
                            + hex_value(overlap->second, 6),
                        signature
                    );
                    end_address = address;
                    break;
                }

                const decoded_instruction_t instruction{
                    decode_instruction(source, address, context)
                };
                if (instruction.status != decode_status_t::complete)
                {
                    conflict_kind_t kind{ conflict_kind_t::unavailable_byte };
                    if (instruction.status == decode_status_t::ambiguous_context)
                        kind = conflict_kind_t::ambiguous_width;
                    else if (instruction.status
                             == decode_status_t::contradictory_context)
                    {
                        kind = conflict_kind_t::contradictory_context;
                    }
                    add_conflict(
                        address,
                        kind,
                        "Decode stopped: "
                            + std::string{ decode_status_name(instruction.status) },
                        signature
                    );
                    end_address = address;
                    break;
                }

                const std::string current_instruction_id{
                    instruction_id(address, signature)
                };
                if (instruction_ids.insert(current_instruction_id).second)
                {
                    model.instructions.push_back({
                        .stable_id = current_instruction_id,
                        .location = cpu_address(address),
                        .context = signature,
                        .opcode = instruction.opcode,
                        .encoded_size = instruction.encoded_size,
                        .code_identity = model_identity(
                            source.identity(address)
                        ),
                        .confidence = seed.confidence
                    });
                }
                else if (seed.confidence == confidence_t::confirmed)
                {
                    const auto existing{
                        std::find_if(
                            model.instructions.begin(),
                            model.instructions.end(),
                            [&current_instruction_id](
                                const instruction_fact_t& fact
                            )
                            {
                                return fact.stable_id == current_instruction_id;
                            }
                        )
                    };
                    if (existing != model.instructions.end())
                        existing->confidence = confidence_t::confirmed;
                }
                const std::string instruction_evidence_id{
                    "evidence[" + current_instruction_id + ":"
                        + std::to_string(
                            static_cast<uint32_t>(seed.evidence)
                        )
                        + ":" + seed.source + ":" + seed.session + "]"
                };
                if (evidence_ids.insert(instruction_evidence_id).second)
                {
                    model.evidence.push_back({
                        .stable_id = instruction_evidence_id,
                        .subject_id = current_instruction_id,
                        .kind = seed.evidence,
                        .source = seed.source,
                        .session = seed.session,
                        .observation_count = seed.observation_count
                    });
                }

                for (uint8_t offset{ 0 }; offset < instruction.encoded_size; ++offset)
                {
                    occupied_bytes.emplace(
                        advance_program_address(address, offset),
                        address
                    );
                }

                if (const std::optional<uint32_t> data{
                        data_reference(instruction)
                    };
                    data.has_value()
                    && instruction.control_flow
                        == control_flow_kind_t::linear)
                {
                    const cross_reference_kind_t kind{ reference_kind(*data) };
                    const std::string id{ xref_id(address, *data, kind) };
                    if (xref_ids.insert(id).second)
                    {
                        model.cross_references.push_back({
                            .stable_id = id,
                            .source = cpu_address(address),
                            .target = cpu_address(*data),
                            .kind = kind,
                            .confidence = confidence_t::strongly_inferred
                        });
                    }
                }

                const uint32_t fallthrough{
                    advance_program_address(address, instruction.encoded_size)
                };
                const cpu_decode_context_t next_context{
                    context_after(instruction)
                };
                end_address = fallthrough;

                const auto add_target = [&](edge_kind_t kind,
                                            uint32_t target,
                                            cpu_decode_context_t target_context,
                                            std::optional<uint32_t> function_entry,
                                            confidence_t confidence)
                {
                    const std::string target_signature{
                        context_signature(target_context)
                    };
                    const std::string id{
                        edge_id(current_block_id, kind, target)
                    };
                    if (edge_ids.insert(id).second)
                    {
                        model.edges.push_back({
                            .stable_id = id,
                            .source_block_id = current_block_id,
                            .target_block_id = block_id(
                                target,
                                target_signature
                            ),
                            .target = cpu_address(target),
                            .kind = kind,
                            .confidence = confidence
                        });
                    }
                    const std::string reference_id{
                        xref_id(
                            address,
                            target,
                            cross_reference_kind_t::code
                        )
                    };
                    if (xref_ids.insert(reference_id).second)
                    {
                        model.cross_references.push_back({
                            .stable_id = reference_id,
                            .source = cpu_address(address),
                            .target = cpu_address(target),
                            .kind = cross_reference_kind_t::code,
                            .confidence = confidence
                        });
                    }
                    enqueue({
                        .address = target,
                        .context = target_context,
                        .function_entry = function_entry,
                        .confidence = confidence,
                        .evidence = evidence_kind_t::direct_target,
                        .source = "recursive-traversal"
                    });
                };

                switch (instruction.control_flow)
                {
                case control_flow_kind_t::linear:
                    address = fallthrough;
                    context = next_context;
                    break;
                case control_flow_kind_t::conditional_branch:
                    if (instruction.direct_target.has_value())
                    {
                        add_target(
                            edge_kind_t::conditional_branch,
                            *instruction.direct_target,
                            next_context,
                            seed.function_entry,
                            confidence_t::strongly_inferred
                        );
                    }
                    add_target(
                        edge_kind_t::fallthrough,
                        fallthrough,
                        next_context,
                        seed.function_entry,
                        confidence_t::strongly_inferred
                    );
                    block_complete = true;
                    break;
                case control_flow_kind_t::unconditional_branch:
                case control_flow_kind_t::jump:
                    if (instruction.direct_target.has_value())
                    {
                        add_target(
                            edge_kind_t::jump,
                            *instruction.direct_target,
                            next_context,
                            seed.function_entry,
                            confidence_t::strongly_inferred
                        );
                    }
                    else
                    {
                        const std::string id{
                            edge_id(
                                current_block_id,
                                edge_kind_t::unresolved,
                                std::nullopt
                            )
                        };
                        if (edge_ids.insert(id).second)
                        {
                            model.edges.push_back({
                                .stable_id = id,
                                .source_block_id = current_block_id,
                                .kind = edge_kind_t::unresolved,
                                .confidence = confidence_t::unresolved
                            });
                        }
                        add_conflict(
                            address,
                            conflict_kind_t::unresolved_transfer,
                            "Indirect control-flow target is unresolved",
                            signature
                        );
                    }
                    block_complete = true;
                    break;
                case control_flow_kind_t::call:
                    if (instruction.direct_target.has_value())
                    {
                        add_function(
                            *instruction.direct_target,
                            confidence_t::strongly_inferred
                        );
                        add_target(
                            edge_kind_t::call,
                            *instruction.direct_target,
                            next_context,
                            *instruction.direct_target,
                            confidence_t::strongly_inferred
                        );
                    }
                    else
                    {
                        add_conflict(
                            address,
                            conflict_kind_t::unresolved_transfer,
                            "Indirect call target is unresolved",
                            signature
                        );
                    }
                    add_target(
                        edge_kind_t::fallthrough,
                        fallthrough,
                        context_after_call(next_context),
                        seed.function_entry,
                        confidence_t::weakly_inferred
                    );
                    block_complete = true;
                    break;
                case control_flow_kind_t::return_:
                    model.edges.push_back({
                        .stable_id = edge_id(
                            current_block_id,
                            edge_kind_t::return_,
                            std::nullopt
                        ),
                        .source_block_id = current_block_id,
                        .kind = edge_kind_t::return_,
                        .confidence = confidence_t::strongly_inferred
                    });
                    block_complete = true;
                    break;
                case control_flow_kind_t::interrupt:
                    model.edges.push_back({
                        .stable_id = edge_id(
                            current_block_id,
                            edge_kind_t::interrupt,
                            std::nullopt
                        ),
                        .source_block_id = current_block_id,
                        .kind = edge_kind_t::interrupt,
                        .confidence = confidence_t::strongly_inferred
                    });
                    block_complete = true;
                    break;
                case control_flow_kind_t::wait:
                case control_flow_kind_t::stop:
                    block_complete = true;
                    break;
                }
            }

            model.basic_blocks.push_back({
                .stable_id = current_block_id,
                .start = cpu_address(seed.address),
                .end = cpu_address(end_address),
                .context = entry_context,
                .confidence = seed.confidence
            });
        }

        std::map<std::pair<uint32_t, std::string>, uint64_t> coverage{};
        for (const runtime_edge_t& runtime : options.runtime_edges)
        {
            coverage[{ runtime.from & k_address_mask, runtime.session }]
                += runtime.hit_count;
            const std::string runtime_instruction_id{
                instruction_id(
                    runtime.from,
                    context_signature(runtime.context_before)
                )
            };
            if (auto executed{
                    std::find_if(
                        model.instructions.begin(),
                        model.instructions.end(),
                        [&runtime_instruction_id](
                            const instruction_fact_t& fact
                        )
                        {
                            return fact.stable_id == runtime_instruction_id;
                        }
                    )
                };
                executed != model.instructions.end())
            {
                executed->confidence = confidence_t::confirmed;
                if (runtime.from_identity != code_identity_t::unavailable)
                    executed->code_identity = runtime.from_identity;
                const std::string id{
                    "evidence[" + runtime_instruction_id + ":"
                    + std::to_string(static_cast<uint32_t>(
                        evidence_kind_t::runtime_execution
                    ))
                    + ":runtime:" + runtime.session + "]"
                };
                if (evidence_ids.insert(id).second)
                {
                    model.evidence.push_back({
                        .stable_id = id,
                        .subject_id = runtime_instruction_id,
                        .kind = evidence_kind_t::runtime_execution,
                        .source = "runtime",
                        .session = runtime.session,
                        .observation_count = runtime.hit_count
                    });
                }
            }
            const std::string runtime_xref{
                xref_id(
                    runtime.from,
                    runtime.to,
                    cross_reference_kind_t::runtime_observed
                )
            };
            if (xref_ids.insert(runtime_xref).second)
            {
                model.cross_references.push_back({
                    .stable_id = runtime_xref,
                    .source = cpu_address(runtime.from),
                    .target = cpu_address(runtime.to),
                    .kind = cross_reference_kind_t::runtime_observed,
                    .confidence = confidence_t::confirmed
                });
            }

            const decoded_instruction_t observed_instruction{
                decode_instruction(
                    source,
                    runtime.from & k_address_mask,
                    runtime.context_before
                )
            };
            edge_kind_t observed_kind{ edge_kind_t::fallthrough };
            if (observed_instruction.status == decode_status_t::complete)
            {
                switch (observed_instruction.control_flow)
                {
                case control_flow_kind_t::conditional_branch:
                    observed_kind = edge_kind_t::conditional_branch;
                    break;
                case control_flow_kind_t::unconditional_branch:
                case control_flow_kind_t::jump:
                    observed_kind = edge_kind_t::jump;
                    break;
                case control_flow_kind_t::call:
                    observed_kind = edge_kind_t::call;
                    add_function(runtime.to, confidence_t::confirmed);
                    {
                        const std::string owner{ function_id(runtime.to) };
                        const std::string target_block{
                            block_id(
                                runtime.to,
                                context_signature(runtime.context_after)
                            )
                        };
                        const std::string membership_id{
                            "membership[" + owner + ":" + target_block + "]"
                        };
                        if (function_block_ids.insert(membership_id).second)
                        {
                            model.function_blocks.push_back({
                                .stable_id = membership_id,
                                .function_id = owner,
                                .block_id = target_block
                            });
                        }
                    }
                    break;
                case control_flow_kind_t::return_:
                    observed_kind = edge_kind_t::return_;
                    break;
                case control_flow_kind_t::interrupt:
                    observed_kind = edge_kind_t::interrupt;
                    break;
                case control_flow_kind_t::linear:
                case control_flow_kind_t::wait:
                case control_flow_kind_t::stop:
                    observed_kind = edge_kind_t::fallthrough;
                    break;
                }
            }
            const auto source_block{
                std::find_if(
                    model.basic_blocks.begin(),
                    model.basic_blocks.end(),
                    [&](const basic_block_fact_t& block)
                    {
                        return block.start.address_space
                                == k_cpu_bus_address_space
                            && block.start.address <= runtime.from
                            && block.end.address > runtime.from;
                    }
                )
            };
            if (source_block != model.basic_blocks.end())
            {
                const std::string id{
                    edge_id(source_block->stable_id, observed_kind, runtime.to)
                };
                auto existing{
                    std::find_if(
                        model.edges.begin(),
                        model.edges.end(),
                        [&](const edge_fact_t& edge)
                        {
                            return edge.stable_id == id;
                        }
                    )
                };
                if (existing == model.edges.end())
                {
                    edge_ids.insert(id);
                    model.edges.push_back({
                        .stable_id = id,
                        .source_block_id = source_block->stable_id,
                        .target_block_id = block_id(
                            runtime.to,
                            context_signature(runtime.context_after)
                        ),
                        .target = cpu_address(runtime.to),
                        .kind = observed_kind,
                        .confidence = confidence_t::confirmed
                    });
                }
                else
                {
                    existing->confidence = confidence_t::confirmed;
                }
                const bool indirect{
                    observed_instruction.status == decode_status_t::complete
                    && (observed_instruction.control_flow
                            == control_flow_kind_t::call
                        || observed_instruction.control_flow
                            == control_flow_kind_t::jump
                        || observed_instruction.control_flow
                            == control_flow_kind_t::unconditional_branch)
                    && !observed_instruction.direct_target.has_value()
                };
                const evidence_kind_t kind{
                    indirect
                        ? evidence_kind_t::runtime_indirect_target
                        : evidence_kind_t::runtime_edge
                };
                const std::string evidence_id{
                    "evidence[" + id + ":"
                    + std::to_string(static_cast<uint32_t>(kind)) + ":"
                    + runtime.session + "]"
                };
                if (evidence_ids.insert(evidence_id).second)
                {
                    model.evidence.push_back({
                        .stable_id = evidence_id,
                        .subject_id = id,
                        .kind = kind,
                        .source = "runtime",
                        .session = runtime.session,
                        .observation_count = runtime.hit_count
                    });
                }
            }
        }
        for (const auto& [key, hits] : coverage)
        {
            model.coverage.push_back({
                .location = cpu_address(key.first),
                .session = key.second,
                .hit_count = hits
            });
        }

        sort_model(model);
        return result;
    }
}
