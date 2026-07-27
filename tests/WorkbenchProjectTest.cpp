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
        || !project.import_snes_hardware_symbols(error))
    {
        return fail("write_facts", error);
    }

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
        || initial_classifications.size() != 1u
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
        || project.classifications(error).size() != 1u)
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
        || project.classifications(error).size() != 1u
        || project.symbols(error).size() != initial_symbols.size()
        || project.analysis_generation(error) != 1u)
    {
        return fail("derived_invalidation_preserves_authored_facts", error);
    }

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
        "invalidation, symbols, and navigation\n"
    );
    return 0;
}
