//
// Created by Zack Shrout on 8/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/snes/SnesAnalysisServices.h"

#include "clover/analysis/snes/Formatter.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/snes/SnesDebugger.h"
#include "clover/workbench/snes/SnesInstructionServices.h"

#include <iomanip>
#include <limits>
#include <sstream>

namespace
{
    [[nodiscard]] std::optional<uint32_t> vector_entry(
        const clover::analysis::snes::debug_target_byte_source_t& source,
        uint32_t vector_address
    )
    {
        const auto low{ source.inspect(vector_address) };
        const auto high{ source.inspect(
            clover::analysis::snes::advance_program_address(
                vector_address,
                1u
            )
        ) };
        using clover::analysis::snes::byte_inspection_status_t;
        if (low.status != byte_inspection_status_t::available
            || high.status != byte_inspection_status_t::available)
        {
            return std::nullopt;
        }
        return static_cast<uint32_t>(low.value)
            | (static_cast<uint32_t>(high.value) << 8u);
    }

    [[nodiscard]] std::string formatted_bytes(
        const clover::analysis::snes::decoded_instruction_t& instruction
    )
    {
        std::ostringstream stream{};
        stream << std::uppercase << std::hex << std::setfill('0');
        for (uint8_t index{}; index < instruction.encoded_size; ++index)
        {
            if (index != 0u)
            {
                stream << ' ';
            }
            stream << std::setw(2)
                   << static_cast<unsigned>(instruction.bytes[index]);
        }
        return stream.str();
    }
}

namespace clover::workbench::snes
{
    snes_analysis_services_t::snes_analysis_services_t(
        const frontend::debug_target_t& target
    ) noexcept
        : _source{
            target,
            frontend::snes_debug::k_cpu_bus_space,
            frontend::snes_debug::k_canonical_media_space
        }
    {
    }

    frontend::address_space_id_t
        snes_analysis_services_t::instruction_address_space() const noexcept
    {
        return frontend::snes_debug::k_cpu_bus_space;
    }

    std::string_view
        snes_analysis_services_t::instruction_address_space_name() const noexcept
    {
        return analysis::snes::k_cpu_bus_address_space;
    }

    std::optional<uint64_t> snes_analysis_services_t::default_entry() const
    {
        return vector_entry(_source, 0x00fffcu);
    }

    disassembly_listing_t snes_analysis_services_t::build_listing(
            uint64_t address,
            size_t maximum_instructions,
            const live_processor_state_t& state
        ) const
    {
        const analysis::snes::static_listing_result_t listing{
            analysis::snes::build_static_listing(
            _source,
            {
                .start_address = static_cast<uint32_t>(address),
                .maximum_instructions = maximum_instructions,
                .maximum_bytes = 512u,
                .context = decode_context(state)
            }
        ) };
        disassembly_listing_t result{};
        result.next_address = listing.next_address;
        result.instructions.reserve(listing.instructions.size());
        for (const analysis::snes::decoded_instruction_t& instruction
             : listing.instructions)
        {
            result.instructions.push_back({
                .address = instruction.address,
                .encoded_size = instruction.encoded_size,
                .direct_target = instruction.direct_target,
                .formatted_bytes = formatted_bytes(instruction),
                .formatted_instruction =
                    analysis::snes::format_instruction(instruction)
            });
        }
        return result;
    }

    bool snes_analysis_services_t::analyze_and_publish(
        project_t& project,
        std::span<const classification_t> classifications,
        const debugger_t& debugger,
        analysis_publication_t& publication,
        std::string& error
    ) const
    {
        error.clear();
        const auto* snes_debugger{
            dynamic_cast<const snes_debugger_t*>(&debugger)
        };
        if (snes_debugger == nullptr)
        {
            error = "SNES analysis requires SNES debugger evidence";
            return false;
        }
        analysis::snes::hybrid_analysis_options_t options{};
        options.seeds = analysis::snes::default_snes_vector_seeds(_source);
        const uint32_t reset_address{
            static_cast<uint32_t>(default_entry().value_or(0x008000u))
        };
        for (const classification_t& classification : classifications)
        {
            if (classification.location.address_space
                    != analysis::snes::k_cpu_bus_address_space
                || classification.layer != fact_layer_t::user
                || classification.length
                    > std::numeric_limits<uint32_t>::max())
            {
                continue;
            }
            options.classifications.push_back({
                .address = static_cast<uint32_t>(
                    classification.location.address
                ),
                .length = static_cast<uint32_t>(classification.length),
                .code = classification.kind == classification_kind_t::code
            });
            if (classification.kind == classification_kind_t::code
                && classification.location.address != reset_address)
            {
                options.seeds.push_back({
                    .address = static_cast<uint32_t>(
                        classification.location.address
                    ),
                    .context = {},
                    .kind = analysis::snes::analysis_seed_kind_t::user,
                    .source = "user-code-classification"
                });
            }
        }
        const auto& runtime_edges{ snes_debugger->runtime_edges() };
        options.runtime_edges.assign(runtime_edges.begin(), runtime_edges.end());
        const analysis::snes::hybrid_analysis_result_t analyzed{
            analysis::snes::analyze_program(_source, options)
        };
        if (analyzed.limit_reached)
        {
            error = "Analysis limit reached; previous generation preserved";
            return false;
        }
        const std::string fingerprint{
            analysis::snes::hybrid_analysis_fingerprint(
                project.identity().canonical_media_sha256,
                options
            )
        };
        uint64_t generation{};
        if (!project.publish_analysis(
                analyzed.model,
                k_snes_analyzer_version,
                k_snes_decoder_version,
                fingerprint,
                generation,
                error
            ))
        {
            return false;
        }
        publication.model = analyzed.model;
        publication.generation = generation;
        publication.status = "Published analysis generation "
            + std::to_string(generation) + ": "
            + std::to_string(publication.model.functions.size())
            + " functions, "
            + std::to_string(publication.model.basic_blocks.size())
            + " blocks, "
            + std::to_string(publication.model.conflicts.size())
            + " conflicts";
        return true;
    }
}
