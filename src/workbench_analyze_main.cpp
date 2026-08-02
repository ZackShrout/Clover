//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/HybridAnalyzer.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/utils/FileSystem.h"
#include "clover/workbench/Project.h"
#include "clover/workbench/snes/SnesAnalysisServices.h"
#include "clover/workbench/snes/SnesDebugger.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

int main(int argc, char** argv)
{
    using namespace clover;

    if (argc < 2)
    {
        std::fprintf(
            stderr,
            "Usage: clover_workbench_analyze <rom> "
            "[--project-root PATH] [--run-instructions COUNT] "
            "[--fast-run-instructions COUNT]\n"
        );
        return 2;
    }
    std::filesystem::path project_root{};
    uint64_t run_instructions{};
    bool fast_run{};
    for (int index{ 2 }; index < argc; ++index)
    {
        if (index + 1 >= argc)
        {
            std::fprintf(stderr, "Missing value for %s\n", argv[index]);
            return 2;
        }
        const std::string_view option{ argv[index++] };
        const std::string_view value{ argv[index] };
        if (option == "--project-root")
        {
            project_root = utils::path_from_utf8(value);
        }
        else if (option == "--run-instructions"
                 || option == "--fast-run-instructions")
        {
            const auto parsed{
                std::from_chars(
                    value.data(),
                    value.data() + value.size(),
                    run_instructions
                )
            };
            if (parsed.ec != std::errc{}
                || parsed.ptr != value.data() + value.size())
            {
                std::fprintf(stderr, "Invalid instruction count.\n");
                return 2;
            }
            fast_run = option == "--fast-run-instructions";
        }
        else
        {
            std::fprintf(
                stderr,
                "Unknown option: %.*s\n",
                static_cast<int>(option.size()),
                option.data()
            );
            return 2;
        }
    }

    const std::vector<std::byte> media{
        utils::read_binary_file(utils::path_from_utf8(argv[1]))
    };
    if (media.empty())
    {
        std::fprintf(stderr, "Unable to read the selected ROM.\n");
        return 1;
    }
    std::unique_ptr<frontend::emulator_core_t> core{
        frontend::create_emulator_core(frontend::system_id_t::snes)
    };
    if (core == nullptr || !core->load_media(media))
    {
        std::fprintf(stderr, "ROM was not recognized as supported SNES media.\n");
        return 1;
    }
    frontend::debug_target_t* const target{ core->debug_target() };
    if (target == nullptr)
    {
        std::fprintf(stderr, "SNES debug target is unavailable.\n");
        return 1;
    }
    const analysis::snes::debug_target_byte_source_t source{
        *target,
        frontend::snes_debug::k_cpu_bus_space,
        frontend::snes_debug::k_canonical_media_space
    };
    analysis::snes::hybrid_analysis_options_t options{};
    options.seeds = analysis::snes::default_snes_vector_seeds(source);
    workbench::snes::snes_debugger_t debugger{};
    std::string error{};
    if (run_instructions != 0u)
    {
        core->power_on();
        if (!debugger.initialize(*target, error, "headless-smoke"))
        {
            std::fprintf(stderr, "Unable to attach debugger: %s\n", error.c_str());
            return 1;
        }
        if (!debugger.resume(error))
        {
            std::fprintf(stderr, "Unable to run debugger: %s\n", error.c_str());
            return 1;
        }
        uint64_t remaining{ run_instructions };
        while (remaining != 0u
               && debugger.run_state()
                    == workbench::debugger_run_state_t::running)
        {
            const size_t budget{
                static_cast<size_t>(std::min<uint64_t>(remaining, 10000u))
            };
            const size_t executed{
                fast_run
                    ? debugger.pump_fast(budget, error)
                    : debugger.pump(budget, error)
            };
            if (!error.empty() || executed == 0u)
                break;
            remaining -= executed;
        }
        if (!error.empty())
        {
            std::fprintf(stderr, "Debugger execution failed: %s\n", error.c_str());
            return 1;
        }
        workbench::live_processor_state_t live{};
        if (!debugger.live_state(live, error))
        {
            std::fprintf(stderr, "Unable to inspect final debugger state: %s\n",
                         error.c_str());
            return 1;
        }
        std::printf(
            "Run: %llu instructions, PC=$%02llX:%04llX",
            static_cast<unsigned long long>(run_instructions - remaining),
            static_cast<unsigned long long>(
                (live.instruction_address.value >> 16u) & 0xffu
            ),
            static_cast<unsigned long long>(
                live.instruction_address.value & 0xffffu
            )
        );
        std::printf("\n");
        const auto* const tile_diagnostics{
            dynamic_cast<const frontend::snes::tile_layer_diagnostics_t*>(core.get())
        };
        if (tile_diagnostics == nullptr)
        {
            std::fprintf(stderr, "SNES tile-layer diagnostics are unavailable.\n");
            return 1;
        }
        std::array<frontend::snes::tile_layer_state_t, 4> layers{};
        const size_t layer_count{
            tile_diagnostics->inspect_tile_layers(layers)
        };
        for (size_t index{}; index < layer_count; ++index)
        {
            const frontend::snes::tile_layer_state_t& layer{ layers[index] };
            std::printf(
                "  %.*s: active=%u map=$%04llX tiles=$%04llX "
                "format=%u geometry=%ux%u/%upx\n",
                static_cast<int>(layer.label.size()),
                layer.label.data(),
                layer.active ? 1u : 0u,
                static_cast<unsigned long long>(layer.tile_map.value),
                static_cast<unsigned long long>(layer.tile_graphics.value),
                static_cast<unsigned>(layer.format),
                layer.width_tiles,
                layer.height_tiles,
                layer.tile_size
            );
        }
        options.runtime_edges = debugger.runtime_edges();
    }

    workbench::project_t project{};
    if (!project_root.empty())
    {
        if (!project.open(
                project_root,
                frontend::system_id_t::snes,
                media,
                error
            ))
        {
            std::fprintf(stderr, "Unable to open Workbench project: %s\n", error.c_str());
            return 1;
        }
        for (const workbench::classification_t& classification
             : project.classifications(error))
        {
            if (classification.location.address_space != "snes.cpu-bus"
                || classification.layer != workbench::fact_layer_t::user
                || classification.length > UINT32_MAX)
            {
                continue;
            }
            options.classifications.push_back({
                .address = static_cast<uint32_t>(
                    classification.location.address
                ),
                .length = static_cast<uint32_t>(classification.length),
                .code = classification.kind
                    == workbench::classification_kind_t::code
            });
            if (classification.kind == workbench::classification_kind_t::code)
            {
                const uint32_t address{
                    static_cast<uint32_t>(classification.location.address)
                };
                const bool already_seeded{
                    std::ranges::any_of(
                        options.seeds,
                        [address](const analysis::snes::analysis_seed_t& seed)
                        {
                            return seed.address == address;
                        }
                    )
                };
                if (!already_seeded)
                {
                    options.seeds.push_back({
                        .address = address,
                        .context = {},
                        .kind = analysis::snes::analysis_seed_kind_t::user,
                        .source = "user-code-classification"
                    });
                }
            }
        }
        if (!error.empty())
        {
            std::fprintf(stderr, "Unable to read project facts: %s\n", error.c_str());
            return 1;
        }
    }

    const analysis::snes::hybrid_analysis_result_t result{
        analysis::snes::analyze_program(source, options)
    };
    if (result.limit_reached)
    {
        std::fprintf(stderr, "Analysis limit reached; no generation published.\n");
        return 1;
    }

    uint64_t generation{};
    if (project.is_open())
    {
        const std::string fingerprint{
            analysis::snes::hybrid_analysis_fingerprint(
                project.identity().canonical_media_sha256,
                options
            )
        };
        if (!project.publish_analysis(
                result.model,
                workbench::snes::k_snes_analyzer_version,
                workbench::snes::k_snes_decoder_version,
                fingerprint,
                generation,
                error
            ))
        {
            std::fprintf(stderr, "Unable to publish analysis: %s\n", error.c_str());
            return 1;
        }
        const auto persisted{ project.current_analysis(error) };
        if (!persisted.has_value())
        {
            std::fprintf(
                stderr,
                "Unable to reload published analysis: %s\n",
                error.c_str()
            );
            return 1;
        }
        if (persisted->instructions.size() != result.model.instructions.size()
            || persisted->basic_blocks.size()
                != result.model.basic_blocks.size()
            || persisted->functions.size() != result.model.functions.size()
            || persisted->function_blocks.size()
                != result.model.function_blocks.size()
            || persisted->edges.size() != result.model.edges.size()
            || persisted->cross_references.size()
                != result.model.cross_references.size()
            || persisted->evidence.size() != result.model.evidence.size()
            || persisted->conflicts.size() != result.model.conflicts.size()
            || persisted->coverage.size() != result.model.coverage.size())
        {
            std::fprintf(
                stderr,
                "Published analysis failed persistence verification.\n"
            );
            return 1;
        }
    }

    std::printf(
        "Analysis%s%s: %zu instructions, %zu blocks, %zu functions, "
        "%zu edges, %zu xrefs, %zu conflicts, %zu covered addresses"
        " (%zu runtime edges, %llu dropped)\n",
        project.is_open() ? " generation " : "",
        project.is_open() ? std::to_string(generation).c_str() : "",
        result.model.instructions.size(),
        result.model.basic_blocks.size(),
        result.model.functions.size(),
        result.model.edges.size(),
        result.model.cross_references.size(),
        result.model.conflicts.size(),
        result.model.coverage.size(),
        options.runtime_edges.size(),
        static_cast<unsigned long long>(debugger.dropped_runtime_edges())
    );
    return 0;
}
