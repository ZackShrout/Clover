//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/MediaIdentity.h"
#include "clover/utils/FileSystem.h"
#include "clover/workbench/Project.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <sqlite3.h>

namespace
{
    [[nodiscard]] int fail(const char* checkpoint, const std::string& detail = {})
    {
        std::fprintf(
            stderr,
            "WorkbenchProjectTest failed at %s%s%s\n",
            checkpoint,
            detail.empty() ? "" : ": ",
            detail.c_str()
        );
        return 1;
    }

    [[nodiscard]] bool execute(sqlite3* database,
                               const char* sql,
                               std::string& error)
    {
        char* raw_error{ nullptr };
        if (sqlite3_exec(database, sql, nullptr, nullptr, &raw_error) == SQLITE_OK)
            return true;
        error = raw_error != nullptr ? raw_error : sqlite3_errmsg(database);
        sqlite3_free(raw_error);
        return false;
    }

    [[nodiscard]] uint32_t user_version(const std::filesystem::path& path)
    {
        sqlite3* database{};
        const std::string path_utf8{ clover::utils::path_to_utf8(path) };
        if (sqlite3_open(path_utf8.c_str(), &database) != SQLITE_OK)
        {
            if (database != nullptr)
                sqlite3_close(database);
            return UINT32_MAX;
        }
        sqlite3_stmt* statement{};
        uint32_t result{ UINT32_MAX };
        if (sqlite3_prepare_v2(database, "PRAGMA user_version;", -1, &statement, nullptr)
                == SQLITE_OK
            && sqlite3_step(statement) == SQLITE_ROW)
        {
            result = static_cast<uint32_t>(sqlite3_column_int(statement, 0));
        }
        sqlite3_finalize(statement);
        sqlite3_close(database);
        return result;
    }

    [[nodiscard]] bool has_layer(
        const std::vector<clover::workbench::named_fact_t>& facts,
        clover::workbench::fact_layer_t layer
    )
    {
        for (const auto& fact : facts)
        {
            if (fact.layer == layer)
                return true;
        }
        return false;
    }
}

int main()
{
    using namespace clover;
    using namespace clover::workbench;

    const uint64_t nonce{
        static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()
        )
    };
    const std::filesystem::path root{
        std::filesystem::temp_directory_path()
        / ("clover-workbench-project-test-" + std::to_string(nonce))
    };
    struct cleanup_t
    {
        std::filesystem::path path{};
        ~cleanup_t()
        {
            std::error_code error{};
            std::filesystem::remove_all(path, error);
        }
    } cleanup{ root };

    std::vector<std::byte> media(0x8000u, std::byte{ 0 });
    media[0] = std::byte{ 0x78u };
    media[1] = std::byte{ 0xeau };
    std::string error{};
    project_t project{};
    if (!project.open(root, frontend::system_id_t::snes, media, error))
        return fail("create", error);

    const std::string expected_hash{
        frontend::media_identity(frontend::system_id_t::snes, media)
    };
    const std::filesystem::path expected_path{
        project_t::path_for(root, frontend::system_id_t::snes, expected_hash)
    };
    if (project.identity().canonical_media_sha256 != expected_hash
        || project.identity().canonical_media_size != media.size()
        || project.path() != expected_path
        || user_version(expected_path) != k_project_schema_version)
    {
        return fail("identity_and_schema");
    }

    const address_key_t reset_entry{ "snes.cpu-bus", 0x008000u };
    if (!project.set_label(reset_entry, "Reset_Entry", error)
        || !project.set_comment(reset_entry, "Console reset entry point", error)
        || !project.add_bookmark(reset_entry, "Startup", error)
        || !project.set_classification(
            reset_entry,
            16u,
            classification_kind_t::code,
            error
        )
        || !project.set_derived_label(
            { "snes.cpu-bus", 0x008100u },
            "sub_008100",
            "recursive-descent",
            error
        )
        || !project.import_snes_hardware_symbols(error)
        || !project.set_debug_breakpoint(reset_entry, true, error)
        || !project.set_debug_watchpoint(
            { "snes.cpu-bus", 0x002100u },
            1u,
            project_watch_access_t::write,
            true,
            error
        ))
    {
        return fail("write_facts", error);
    }

    const analysis::data_type_t actor_state{
        .stable_id = "user.actor-state",
        .name = "Actor state",
        .kind = analysis::data_type_kind_t::enumeration,
        .byte_size = 1u,
        .values = {
            { .name = "idle", .value = 0 },
            { .name = "walking", .value = 1 }
        }
    };
    const analysis::data_type_t actor_flags{
        .stable_id = "user.actor-flags",
        .name = "Actor flags",
        .kind = analysis::data_type_kind_t::bitfield,
        .byte_size = 1u,
        .values = {
            { .name = "visible", .value = 1 },
            { .name = "hostile", .value = 4 }
        }
    };
    const analysis::data_type_t actor_name{
        .stable_id = "user.actor-name",
        .name = "Actor name",
        .kind = analysis::data_type_kind_t::string,
        .byte_size = 8u,
        .encoding = "ascii"
    };
    const analysis::data_type_t actor{
        .stable_id = "user.actor",
        .name = "Actor",
        .kind = analysis::data_type_kind_t::structure,
        .byte_size = 12u,
        .members = {
            {
                .stable_id = "user.actor.hp",
                .name = "hp",
                .type_id = "clover.u16le",
                .byte_offset = 0u
            },
            {
                .stable_id = "user.actor.state",
                .name = "state",
                .type_id = "user.actor-state",
                .byte_offset = 2u
            },
            {
                .stable_id = "user.actor.flags",
                .name = "flags",
                .type_id = "user.actor-flags",
                .byte_offset = 3u
            },
            {
                .stable_id = "user.actor.name",
                .name = "name",
                .type_id = "user.actor-name",
                .byte_offset = 4u
            }
        }
    };
    const analysis::data_type_t actor_pointer{
        .stable_id = "user.actor-pointer",
        .name = "Actor pointer",
        .kind = analysis::data_type_kind_t::pointer,
        .byte_size = 2u,
        .element_type_id = "user.actor",
        .pointer_address_space = "snes.cpu-bus"
    };
    if (!project.set_data_type(actor_state, error)
        || !project.set_data_type(actor_flags, error)
        || !project.set_data_type(actor_name, error)
        || !project.set_data_type(actor, error)
        || !project.set_data_type(actor_pointer, error)
        || !project.set_typed_object(
            {
                .stable_id = "object.actor",
                .location = { "snes.cpu-bus", 0x7e1000u },
                .type_id = "user.actor",
                .name = "Actor zero"
            },
            error
        )
        || !project.set_typed_object(
            {
                .stable_id = "object.actor-pointer",
                .location = { "snes.cpu-bus", 0x7e1020u },
                .type_id = "user.actor-pointer",
                .name = "Current actor"
            },
            error
        ))
    {
        return fail("write_typed_data", error);
    }
    const auto initial_types{ project.data_types(error) };
    const auto initial_objects{ project.typed_objects(error) };
    const auto loaded_actor{
        std::find_if(
            initial_types.begin(),
            initial_types.end(),
            [](const project_data_type_t& type)
            {
                return type.definition.stable_id == "user.actor";
            }
        )
    };
    if (!error.empty() || initial_types.size() != 9u
        || initial_objects.size() != 2u || loaded_actor == initial_types.end()
        || loaded_actor->definition.members.size() != 4u)
    {
        return fail("typed_data_round_trip", error);
    }
    error.clear();

    const analysis::palette_asset_t cgram_palette{
        .stable_id = "palette.cgram",
        .name = "Live CGRAM",
        .location = { "snes.cgram", 0u },
        .color_count = 256u
    };
    const analysis::palette_asset_t actor_palette{
        .stable_id = "palette.actor",
        .name = "Actor palette",
        .location = { "snes.cpu-bus", 0x7e1100u },
        .color_count = 16u
    };
    if (!project.set_palette(cgram_palette, error)
        || !project.set_palette(actor_palette, error))
    {
        return fail("write_palettes", error);
    }
    const auto initial_palettes{ project.palettes(error) };
    if (!error.empty() || initial_palettes.size() != 2u
        || initial_palettes.front().asset.location.address_space
            != "snes.cgram"
        || initial_palettes.front().asset.color_count != 256u
        || initial_palettes.back().asset.color_count != 16u)
    {
        return fail("palette_round_trip", error);
    }
    error.clear();
    analysis::palette_asset_t misaligned_palette{ actor_palette };
    misaligned_palette.stable_id = "palette.invalid";
    misaligned_palette.location.address += 1u;
    if (project.set_palette(misaligned_palette, error)
        || error.empty() || project.palettes(error).size() != 2u)
    {
        return fail("invalid_palette_rejected_atomically", error);
    }
    error.clear();

    const analysis::tile_asset_t vram_tiles{
        .stable_id = "tiles.vram",
        .name = "Live VRAM tiles",
        .location = { "snes.vram", 0u },
        .tile_count = 8u,
        .format = analysis::tile_format_t::snes_4bpp,
        .palette_id = "palette.cgram",
        .palette_base = 0u
    };
    const analysis::tile_asset_t actor_tiles{
        .stable_id = "tiles.actor",
        .name = "Actor tiles",
        .location = { "snes.cpu-bus", 0x7e1200u },
        .tile_count = 4u,
        .format = analysis::tile_format_t::snes_2bpp,
        .palette_id = "palette.actor",
        .palette_base = 0u
    };
    if (!project.set_tile_asset(vram_tiles, error)
        || !project.set_tile_asset(actor_tiles, error))
    {
        return fail("write_tile_assets", error);
    }
    const auto initial_tiles{ project.tile_assets(error) };
    if (!error.empty() || initial_tiles.size() != 2u
        || initial_tiles.front().asset.palette_id != "palette.actor"
        || initial_tiles.back().asset.palette_id != "palette.cgram")
    {
        return fail("tile_asset_round_trip", error);
    }
    error.clear();
    analysis::tile_asset_t misaligned_tiles{ actor_tiles };
    misaligned_tiles.stable_id = "tiles.invalid";
    misaligned_tiles.location.address += 1u;
    if (project.set_tile_asset(misaligned_tiles, error)
        || error.empty() || project.tile_assets(error).size() != 2u)
    {
        return fail("invalid_tile_asset_rejected_atomically", error);
    }
    error.clear();

    if (project.set_typed_object(
            {
                .stable_id = "object.overlap",
                .location = { "snes.cpu-bus", 0x7e1001u },
                .type_id = "clover.u16le"
            },
            error
        )
        || error.empty() || project.typed_objects(error).size() != 2u)
    {
        return fail("typed_object_overlap_rejected_atomically", error);
    }
    error.clear();

    const std::vector<named_fact_t> initial_labels{ project.labels(error) };
    const std::vector<named_fact_t> initial_comments{ project.comments(error) };
    const std::vector<bookmark_t> initial_bookmarks{ project.bookmarks(error) };
    const std::vector<classification_t> initial_classifications{
        project.classifications(error)
    };
    const std::vector<symbol_t> initial_symbols{ project.symbols(error) };
    if (!error.empty()
        || initial_labels.size() != 2u
        || !has_layer(initial_labels, fact_layer_t::user)
        || !has_layer(initial_labels, fact_layer_t::derived)
        || initial_comments.size() != 1u
        || initial_bookmarks.size() != 1u
        || initial_classifications.size() != 5u
        || initial_symbols.size() < 90u
        || initial_symbols.front().layer != fact_layer_t::imported)
    {
        return fail("fact_layers", error);
    }

    if (!project.record_navigation(reset_entry, "disassembly", error)
        || !project.record_navigation(
            { "snes.cpu-bus", 0x008010u },
            "disassembly",
            error
        )
        || !project.record_navigation(
            { "snes.cpu-bus", 0x008020u },
            "disassembly",
            error
        ))
    {
        return fail("record_navigation", error);
    }
    const std::optional<navigation_entry_t> back{ project.navigate_back(error) };
    if (!back.has_value() || back->location.address != 0x008010u)
        return fail("navigate_back", error);
    if (!project.record_navigation(
            { "snes.cpu-bus", 0x008018u },
            "hex",
            error
        ))
    {
        return fail("branch_navigation", error);
    }
    const std::vector<navigation_entry_t> history{
        project.navigation_history(error)
    };
    if (!error.empty()
        || history.size() != 3u
        || history.back().location.address != 0x008018u)
    {
        return fail("navigation_history", error);
    }
    const std::optional<navigation_entry_t> forward{
        project.navigate_forward(error)
    };
    if (!forward.has_value() || forward->location.address != 0x008018u)
        return fail("navigate_forward_at_tip", error);

    project.close();
    if (!project.open(root, frontend::system_id_t::snes, media, error))
        return fail("reopen", error);
    if (project.labels(error).size() != 2u
        || project.comments(error).size() != 1u
        || project.bookmarks(error).size() != 1u
        || project.classifications(error).size() != 5u
        || project.data_types(error).size() != 9u
        || project.typed_objects(error).size() != 2u
        || project.palettes(error).size() != 2u
        || project.tile_assets(error).size() != 2u
        || project.debug_breakpoints(error).size() != 1u
        || project.debug_watchpoints(error).size() != 1u
        || project.debug_watchpoints(error).front().access
            != project_watch_access_t::write)
    {
        return fail("durable_user_knowledge", error);
    }

    if (!project.begin_analysis_generation(
            "clover-analyzer-2",
            "wdc65c816-decoder-2",
            error
        ))
    {
        return fail("begin_analysis_generation", error);
    }
    const std::vector<named_fact_t> regenerated_labels{ project.labels(error) };
    if (!error.empty()
        || regenerated_labels.size() != 1u
        || regenerated_labels.front().layer != fact_layer_t::user
        || project.comments(error).size() != 1u
        || project.classifications(error).size() != 5u
        || project.data_types(error).size() != 9u
        || project.typed_objects(error).size() != 2u
        || project.palettes(error).size() != 2u
        || project.tile_assets(error).size() != 2u
        || project.symbols(error).size() != initial_symbols.size()
        || project.analysis_generation(error) != 1u)
    {
        return fail("derived_invalidation_preserves_authored_facts", error);
    }

    analysis::program_model_t model{};
    model.instructions.push_back({
        .stable_id = "instruction@008000[E=1;M=1;X=1;D=0000;DB=00]",
        .location = reset_entry,
        .context = "E=1;M=1;X=1;D=0000;DB=00",
        .opcode = 0x78u,
        .encoded_size = 1u,
        .code_identity = analysis::code_identity_t::canonical_media,
        .confidence = analysis::confidence_t::confirmed
    });
    model.basic_blocks.push_back({
        .stable_id = "block@008000[E=1;M=1;X=1;D=0000;DB=00]",
        .start = reset_entry,
        .end = { "snes.cpu-bus", 0x008001u },
        .context = "E=1;M=1;X=1;D=0000;DB=00",
        .confidence = analysis::confidence_t::confirmed
    });
    model.functions.push_back({
        .stable_id = "function@008000",
        .entry = reset_entry,
        .confidence = analysis::confidence_t::confirmed
    });
    model.function_blocks.push_back({
        .stable_id = "membership-reset",
        .function_id = "function@008000",
        .block_id = "block@008000[E=1;M=1;X=1;D=0000;DB=00]"
    });
    model.edges.push_back({
        .stable_id = "edge-return",
        .source_block_id = "block@008000[E=1;M=1;X=1;D=0000;DB=00]",
        .kind = analysis::edge_kind_t::return_,
        .confidence = analysis::confidence_t::strongly_inferred
    });
    model.cross_references.push_back({
        .stable_id = "xref-reset",
        .source = reset_entry,
        .target = { "snes.cpu-bus", 0x008010u },
        .kind = analysis::cross_reference_kind_t::code,
        .confidence = analysis::confidence_t::strongly_inferred
    });
    model.evidence.push_back({
        .stable_id = "evidence-reset",
        .subject_id = model.instructions.front().stable_id,
        .kind = analysis::evidence_kind_t::vector,
        .source = "reset",
        .observation_count = 1u
    });
    model.conflicts.push_back({
        .stable_id = "conflict-indirect",
        .location = { "snes.cpu-bus", 0x008020u },
        .kind = analysis::conflict_kind_t::unresolved_transfer,
        .detail = "Synthetic unresolved transfer"
    });
    model.coverage.push_back({
        .location = reset_entry,
        .session = "test-session",
        .hit_count = 4u
    });
    uint64_t published_generation{};
    if (!project.publish_analysis(
            model,
            k_analyzer_version,
            k_decoder_version,
            "fixture-input",
            published_generation,
            error
        )
        || published_generation != 2u)
    {
        return fail("publish_analysis", error);
    }
    const auto loaded_model{ project.current_analysis(error) };
    const auto prior_model{ project.analysis(1u, error) };
    const auto generations{ project.analysis_generations(error) };
    if (!error.empty() || !loaded_model.has_value() || !prior_model.has_value()
        || !prior_model->instructions.empty()
        || loaded_model->instructions.size() != 1u
        || loaded_model->instructions.front().code_identity
            != analysis::code_identity_t::canonical_media
        || loaded_model->basic_blocks.size() != 1u
        || loaded_model->functions.size() != 1u
        || loaded_model->function_blocks.size() != 1u
        || loaded_model->edges.size() != 1u
        || loaded_model->cross_references.size() != 1u
        || loaded_model->evidence.size() != 1u
        || loaded_model->conflicts.size() != 1u
        || loaded_model->coverage.size() != 1u
        || loaded_model->coverage.front().hit_count != 4u
        || generations.size() != 3u
        || !generations.back().current
        || generations.back().input_fingerprint != "fixture-input")
    {
        return fail("load_published_analysis", error);
    }

    analysis::program_model_t invalid_model{ model };
    invalid_model.instructions.push_back(model.instructions.front());
    uint64_t rejected_generation{};
    error.clear();
    if (project.publish_analysis(
            invalid_model,
            k_analyzer_version,
            k_decoder_version,
            "invalid",
            rejected_generation,
            error
        )
        || error.empty() || rejected_generation != 0u
        || project.analysis_generation(error) != published_generation
        || project.analysis_generations(error).size() != generations.size()
        || project.labels(error).size() != 1u)
    {
        return fail("failed_publication_preserves_current_generation", error);
    }
    error.clear();

    std::vector<std::byte> headered(512u + media.size(), std::byte{ 0x55u });
    std::copy(media.begin(), media.end(), headered.begin() + 512);
    project_t headered_project{};
    if (!headered_project.open(
            root,
            frontend::system_id_t::snes,
            headered,
            error
        )
        || headered_project.path() != expected_path)
    {
        return fail("canonical_header_identity", error);
    }
    headered_project.close();

    std::vector<std::byte> other_media{ media };
    other_media[2] = std::byte{ 0x42u };
    project_t other_project{};
    if (!other_project.open(
            root,
            frontend::system_id_t::snes,
            other_media,
            error
        )
        || other_project.path() == expected_path)
    {
        return fail("media_isolation", error);
    }
    other_project.close();

    project.close();
    sqlite3* migration_database{};
    const std::string expected_path_utf8{ utils::path_to_utf8(expected_path) };
    if (sqlite3_open(expected_path_utf8.c_str(), &migration_database) != SQLITE_OK)
        return fail("open_migration_fixture");
    const char* downgrade_sql{
        "DROP TABLE tile_assets;"
        "DROP TABLE palette_assets;"
        "DROP TABLE typed_data_objects;"
        "DROP TABLE typed_data_values;"
        "DROP TABLE typed_data_members;"
        "DROP TABLE typed_data_types;"
        "DROP TABLE analysis_coverage;"
        "DROP TABLE analysis_conflicts;"
        "DROP TABLE analysis_evidence;"
        "DROP TABLE analysis_cross_references;"
        "DROP TABLE analysis_edges;"
        "DROP TABLE analysis_function_blocks;"
        "DROP TABLE analysis_functions;"
        "DROP TABLE analysis_basic_blocks;"
        "DROP TABLE analysis_instructions;"
        "DROP TABLE analysis_generations;"
        "DROP TABLE debug_watchpoints;"
        "DROP TABLE debug_breakpoints;"
        "DROP TABLE navigation_state;"
        "DROP TABLE navigation_history;"
        "DROP TABLE analysis_state;"
        "DROP INDEX labels_location;"
        "DROP INDEX comments_location;"
        "DROP INDEX classifications_location;"
        "DROP INDEX symbols_location;"
        "PRAGMA user_version=1;"
    };
    if (!execute(migration_database, downgrade_sql, error))
    {
        sqlite3_close(migration_database);
        return fail("downgrade_fixture", error);
    }
    sqlite3_close(migration_database);
    if (!project.open(root, frontend::system_id_t::snes, media, error)
        || user_version(expected_path) != k_project_schema_version
        || project.labels(error).size() != 1u)
    {
        return fail("successful_transactional_migration", error);
    }

    project.close();
    if (sqlite3_open(expected_path_utf8.c_str(), &migration_database) != SQLITE_OK)
        return fail("open_failed_migration_fixture");
    if (!execute(
            migration_database,
            downgrade_sql,
            error
        )
        || !execute(
            migration_database,
            "CREATE VIEW analysis_state AS SELECT 1 AS singleton;"
            "CREATE TABLE migration_sentinel(value INTEGER);"
            "INSERT INTO migration_sentinel(value) VALUES(42);",
            error
        ))
    {
        sqlite3_close(migration_database);
        return fail("failed_migration_fixture", error);
    }
    sqlite3_close(migration_database);
    error.clear();
    if (project.open(root, frontend::system_id_t::snes, media, error)
        || error.empty()
        || user_version(expected_path) != 1u)
    {
        return fail("migration_failure_rolls_back", error);
    }

    if (sqlite3_open(expected_path_utf8.c_str(), &migration_database) != SQLITE_OK)
        return fail("verify_migration_rollback");
    sqlite3_stmt* sentinel{};
    const bool sentinel_ok{
        sqlite3_prepare_v2(
            migration_database,
            "SELECT value FROM migration_sentinel;",
            -1,
            &sentinel,
            nullptr
        ) == SQLITE_OK
        && sqlite3_step(sentinel) == SQLITE_ROW
        && sqlite3_column_int(sentinel, 0) == 42
    };
    sqlite3_finalize(sentinel);
    sqlite3_close(migration_database);
    if (!sentinel_ok)
        return fail("migration_rollback_preserves_database");

    std::printf(
        "Workbench project tests passed: identity, migrations, facts, "
        "typed data, palettes, tiles, invalidation, symbols, debugger points, "
        "and navigation\n"
    );
    return 0;
}
