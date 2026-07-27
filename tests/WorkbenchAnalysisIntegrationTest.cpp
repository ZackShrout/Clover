//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/HybridAnalyzer.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/workbench/Project.h"
#include "clover/workbench/SnesDebugger.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_rom()
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0xeau });
        const uint8_t program[]{
            0xa9u, 0x42u,             // $8000 LDA #$42
            0x20u, 0x10u, 0x80u,      // $8002 JSR $8010
            0xd0u, 0x01u,             // $8005 BNE $8008
            0xeau,                     // $8007 NOP
            0x80u, 0xf6u               // $8008 BRA $8000
        };
        for (size_t index{ 0 }; index < std::size(program); ++index)
            rom[index] = static_cast<std::byte>(program[index]);
        rom[0x10u] = std::byte{ 0xe8u }; // $8010 INX
        rom[0x11u] = std::byte{ 0x60u }; // $8011 RTS

        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x02u };
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint, const std::string& error = {})
    {
        std::fprintf(
            stderr,
            "WorkbenchAnalysisIntegrationTest failed at %s%s%s\n",
            checkpoint,
            error.empty() ? "" : ": ",
            error.c_str()
        );
        return 1;
    }
}

int main()
{
    using namespace clover;

    const std::vector<std::byte> rom{ make_rom() };
    std::unique_ptr<frontend::emulator_core_t> emulator{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    if (emulator == nullptr || !emulator->load_media(rom))
        return fail("load");
    frontend::debug_target_t* const target{ emulator->debug_target() };
    if (target == nullptr)
        return fail("target");
    const analysis::snes::debug_target_byte_source_t source{
        *target,
        frontend::snes_debug::k_cpu_bus_space,
        frontend::snes_debug::k_canonical_media_space
    };

    emulator->power_on();
    workbench::snes_debugger_t debugger{};
    std::string error{};
    if (!debugger.initialize(*target, error, "integration-session")
        || !debugger.resume(error)
        || debugger.pump(64u, error) != 64u
        || debugger.runtime_edges().empty())
    {
        return fail("runtime_capture", error);
    }

    analysis::snes::hybrid_analysis_options_t options{};
    options.seeds = analysis::snes::default_snes_vector_seeds(source);
    options.runtime_edges = debugger.runtime_edges();
    options.seeds.push_back({
        .address = 0x008030u,
        .context = {},
        .kind = analysis::snes::analysis_seed_kind_t::user,
        .source = "conflicting-user-entry"
    });
    options.classifications.push_back({
        .address = 0x008030u,
        .length = 1u,
        .code = false
    });
    const analysis::snes::hybrid_analysis_result_t analyzed{
        analysis::snes::analyze_program(source, options)
    };
    if (analyzed.limit_reached || analyzed.model.coverage.empty()
        || analyzed.model.functions.size() < 2u
        || analyzed.model.function_blocks.empty()
        || analyzed.model.cross_references.empty()
        || analyzed.model.evidence.empty()
        || analyzed.model.conflicts.empty())
    {
        return fail("hybrid_model");
    }
    if (!std::ranges::any_of(
            analyzed.model.edges,
            [](const analysis::edge_fact_t& edge)
            {
                return edge.kind == analysis::edge_kind_t::call
                    && edge.target.has_value()
                    && edge.target->address == 0x008010u;
            }
        ))
    {
        return fail("call_graph");
    }

    const analysis::coverage_fact_t& covered{
        analyzed.model.coverage.front()
    };
    const auto instruction{
        std::find_if(
            analyzed.model.instructions.begin(),
            analyzed.model.instructions.end(),
            [&covered](const analysis::instruction_fact_t& fact)
            {
                return fact.location == covered.location;
            }
        )
    };
    const auto block{
        std::find_if(
            analyzed.model.basic_blocks.begin(),
            analyzed.model.basic_blocks.end(),
            [&covered](const analysis::basic_block_fact_t& fact)
            {
                return fact.start.address <= covered.location.address
                    && fact.end.address > covered.location.address;
            }
        )
    };
    const bool has_membership{
        block != analyzed.model.basic_blocks.end()
        && std::ranges::any_of(
            analyzed.model.function_blocks,
            [&block](const analysis::function_block_fact_t& membership)
            {
                return membership.block_id == block->stable_id;
            }
        )
    };
    const bool has_runtime_evidence{
        instruction != analyzed.model.instructions.end()
        && std::ranges::any_of(
            analyzed.model.evidence,
            [&instruction](const analysis::evidence_fact_t& evidence)
            {
                return evidence.subject_id == instruction->stable_id
                    && evidence.session == "integration-session";
            }
        )
    };
    if (instruction == analyzed.model.instructions.end()
        || block == analyzed.model.basic_blocks.end()
        || instruction->code_identity
            != analysis::code_identity_t::canonical_media
        || !has_membership || !has_runtime_evidence)
    {
        return fail(
            "executed_instruction_navigation",
            "covered=$" + std::to_string(covered.location.address)
                + " instruction="
                + (instruction == analyzed.model.instructions.end() ? "no" : "yes")
                + " block="
                + (block == analyzed.model.basic_blocks.end() ? "no" : "yes")
                + " membership=" + (has_membership ? "yes" : "no")
                + " runtime-evidence="
                + (has_runtime_evidence ? "yes" : "no")
        );
    }

    const uint64_t nonce{
        static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        )
    };
    const std::filesystem::path root{
        std::filesystem::temp_directory_path()
        / ("clover-analysis-integration-" + std::to_string(nonce))
    };
    struct cleanup_t
    {
        std::filesystem::path path{};
        ~cleanup_t()
        {
            std::error_code ignored{};
            std::filesystem::remove_all(path, ignored);
        }
    } cleanup{ root };

    workbench::project_t project{};
    if (!project.open(root, frontend::system_id_t::snes, rom, error))
        return fail("project_open", error);
    uint64_t generation{};
    if (!project.publish_analysis(
            analyzed.model,
            workbench::k_analyzer_version,
            workbench::k_decoder_version,
            analysis::snes::hybrid_analysis_fingerprint(
                project.identity().canonical_media_sha256,
                options
            ),
            generation,
            error
        ))
    {
        return fail("publish", error);
    }
    const auto restored{ project.current_analysis(error) };
    const auto prior{ project.analysis(0u, error) };
    if (!error.empty() || generation != 1u || !restored.has_value()
        || !prior.has_value()
        || restored->instructions.size() != analyzed.model.instructions.size()
        || restored->function_blocks.size()
            != analyzed.model.function_blocks.size()
        || restored->coverage.size() != analyzed.model.coverage.size())
    {
        return fail("durable_generation", error);
    }

    std::printf(
        "Workbench analysis integration passed: execution -> coverage -> "
        "instruction -> block -> function, graph, evidence, conflicts, "
        "and atomic persistence\n"
    );
    return 0;
}
