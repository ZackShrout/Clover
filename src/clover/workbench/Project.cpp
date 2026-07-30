//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/Project.h"

#include "clover/analysis/snes/HardwareSymbols.h"
#include "clover/frontend/MediaIdentity.h"
#include "clover/utils/FileSystem.h"

#include <algorithm>
#include <array>
#include <limits>
#include <system_error>

#include <sqlite3.h>

namespace
{
    using namespace clover::workbench;

    class statement_t
    {
    public:
        statement_t() = default;
        ~statement_t()
        {
            if (_statement != nullptr)
                sqlite3_finalize(_statement);
        }

        statement_t(const statement_t&) = delete;
        statement_t& operator=(const statement_t&) = delete;

        [[nodiscard]] bool prepare(sqlite3* database,
                                   const char* sql,
                                   std::string& error)
        {
            if (sqlite3_prepare_v2(database, sql, -1, &_statement, nullptr) == SQLITE_OK)
                return true;
            error = sqlite3_errmsg(database);
            return false;
        }

        [[nodiscard]] sqlite3_stmt* get() const noexcept
        {
            return _statement;
        }

    private:
        sqlite3_stmt* _statement{ nullptr };
    };

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

    [[nodiscard]] const char* system_name(clover::frontend::system_id_t system) noexcept
    {
        switch (system)
        {
        case clover::frontend::system_id_t::snes:
            return "snes";
        }
        return "unknown";
    }

    [[nodiscard]] int layer_value(fact_layer_t layer) noexcept
    {
        return static_cast<int>(layer);
    }

    [[nodiscard]] fact_layer_t fact_layer(int value) noexcept
    {
        if (value == layer_value(fact_layer_t::imported))
            return fact_layer_t::imported;
        if (value == layer_value(fact_layer_t::derived))
            return fact_layer_t::derived;
        return fact_layer_t::user;
    }

    [[nodiscard]] bool bind_text(sqlite3_stmt* statement,
                                 int index,
                                 std::string_view value,
                                 std::string& error)
    {
        if (sqlite3_bind_text(
                statement,
                index,
                value.empty() ? "" : value.data(),
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT
            ) == SQLITE_OK)
        {
            return true;
        }
        error = sqlite3_errmsg(sqlite3_db_handle(statement));
        return false;
    }

    [[nodiscard]] bool step_done(sqlite3_stmt* statement, std::string& error)
    {
        if (sqlite3_step(statement) == SQLITE_DONE)
            return true;
        error = sqlite3_errmsg(sqlite3_db_handle(statement));
        return false;
    }

    [[nodiscard]] std::string column_text(sqlite3_stmt* statement, int column)
    {
        const unsigned char* const text{ sqlite3_column_text(statement, column) };
        if (text == nullptr)
            return {};
        return reinterpret_cast<const char*>(text);
    }

    [[nodiscard]] bool valid_location(const address_key_t& location) noexcept
    {
        return !location.address_space.empty()
            && location.address <= static_cast<uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()
            );
    }

    [[nodiscard]] bool valid_model(const clover::analysis::program_model_t& model,
                                   std::string& error)
    {
        for (const clover::analysis::instruction_fact_t& fact : model.instructions)
        {
            if (fact.stable_id.empty() || !valid_location(fact.location)
                || fact.context.empty() || fact.encoded_size == 0u
                || fact.encoded_size > 4u)
            {
                error = "Invalid analysis instruction";
                return false;
            }
        }
        for (const clover::analysis::basic_block_fact_t& fact : model.basic_blocks)
        {
            if (fact.stable_id.empty() || !valid_location(fact.start)
                || !valid_location(fact.end) || fact.context.empty())
            {
                error = "Invalid analysis basic block";
                return false;
            }
        }
        for (const clover::analysis::function_fact_t& fact : model.functions)
        {
            if (fact.stable_id.empty() || !valid_location(fact.entry))
            {
                error = "Invalid analysis function";
                return false;
            }
        }
        for (const clover::analysis::function_block_fact_t& fact
             : model.function_blocks)
        {
            if (fact.stable_id.empty() || fact.function_id.empty()
                || fact.block_id.empty())
            {
                error = "Invalid analysis function membership";
                return false;
            }
        }
        for (const clover::analysis::edge_fact_t& fact : model.edges)
        {
            if (fact.stable_id.empty() || fact.source_block_id.empty()
                || (fact.target.has_value() && !valid_location(*fact.target)))
            {
                error = "Invalid analysis edge";
                return false;
            }
        }
        for (const clover::analysis::cross_reference_fact_t& fact
             : model.cross_references)
        {
            if (fact.stable_id.empty() || !valid_location(fact.source)
                || !valid_location(fact.target))
            {
                error = "Invalid analysis cross-reference";
                return false;
            }
        }
        for (const clover::analysis::evidence_fact_t& fact : model.evidence)
        {
            if (fact.stable_id.empty() || fact.subject_id.empty()
                || fact.observation_count == 0u
                || fact.observation_count > static_cast<uint64_t>(
                    std::numeric_limits<sqlite3_int64>::max()
                ))
            {
                error = "Invalid analysis evidence";
                return false;
            }
        }
        for (const clover::analysis::conflict_fact_t& fact : model.conflicts)
        {
            if (fact.stable_id.empty() || !valid_location(fact.location))
            {
                error = "Invalid analysis conflict";
                return false;
            }
        }
        for (const clover::analysis::coverage_fact_t& fact : model.coverage)
        {
            if (!valid_location(fact.location) || fact.session.empty()
                || fact.hit_count == 0u
                || fact.hit_count > static_cast<uint64_t>(
                    std::numeric_limits<sqlite3_int64>::max()
                ))
            {
                error = "Invalid analysis coverage";
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] bool begin_transaction(sqlite3* database, std::string& error)
    {
        return execute(database, "BEGIN IMMEDIATE;", error);
    }

    [[nodiscard]] bool commit_transaction(sqlite3* database, std::string& error)
    {
        return execute(database, "COMMIT;", error);
    }

    void rollback_transaction(sqlite3* database) noexcept
    {
        char* raw_error{ nullptr };
        static_cast<void>(sqlite3_exec(
            database,
            "ROLLBACK;",
            nullptr,
            nullptr,
            &raw_error
        ));
        sqlite3_free(raw_error);
    }

    [[nodiscard]] bool read_user_version(sqlite3* database,
                                         uint32_t& version,
                                         std::string& error)
    {
        statement_t statement{};
        if (!statement.prepare(database, "PRAGMA user_version;", error))
            return false;
        if (sqlite3_step(statement.get()) != SQLITE_ROW)
        {
            error = sqlite3_errmsg(database);
            return false;
        }
        version = static_cast<uint32_t>(sqlite3_column_int(statement.get(), 0));
        return true;
    }

    constexpr const char* k_schema_v1{
        "CREATE TABLE project("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "system TEXT NOT NULL,"
        "canonical_media_sha256 TEXT NOT NULL,"
        "canonical_media_size INTEGER NOT NULL,"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE labels("
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,text TEXT NOT NULL,"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '',analysis_generation INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(address_space,address,layer,source));"
        "CREATE TABLE comments("
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,text TEXT NOT NULL,"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '',analysis_generation INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(address_space,address,layer,source));"
        "CREATE TABLE bookmarks("
        "id INTEGER PRIMARY KEY,address_space TEXT NOT NULL,address INTEGER NOT NULL,"
        "name TEXT NOT NULL,created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(address_space,address,name));"
        "CREATE TABLE classifications("
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,length INTEGER NOT NULL CHECK(length>0),"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 1),"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '',analysis_generation INTEGER NOT NULL DEFAULT 0,"
        "updated_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "PRIMARY KEY(address_space,address,layer,source));"
        "CREATE TABLE symbols("
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,name TEXT NOT NULL,"
        "description TEXT NOT NULL DEFAULT '',"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '',analysis_generation INTEGER NOT NULL DEFAULT 0,"
        "PRIMARY KEY(address_space,address,name,layer,source));"
    };

    constexpr const char* k_schema_v2{
        "CREATE TABLE analysis_state("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "analyzer_version TEXT NOT NULL,decoder_version TEXT NOT NULL,"
        "generation INTEGER NOT NULL CHECK(generation>=0));"
        "INSERT INTO analysis_state(singleton,analyzer_version,decoder_version,generation)"
        " VALUES(1,'clover-analyzer-1','wdc65c816-decoder-1',0);"
        "CREATE TABLE navigation_history("
        "sequence INTEGER PRIMARY KEY AUTOINCREMENT,"
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,view TEXT NOT NULL,"
        "visited_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
        "CREATE TABLE navigation_state("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "cursor_sequence INTEGER REFERENCES navigation_history(sequence) ON DELETE SET NULL);"
        "INSERT INTO navigation_state(singleton,cursor_sequence) VALUES(1,NULL);"
        "CREATE INDEX labels_location ON labels(address_space,address);"
        "CREATE INDEX comments_location ON comments(address_space,address);"
        "CREATE INDEX classifications_location ON classifications(address_space,address);"
        "CREATE INDEX symbols_location ON symbols(address_space,address);"
    };

    constexpr const char* k_schema_v3{
        "CREATE TABLE debug_breakpoints("
        "id INTEGER PRIMARY KEY,address_space TEXT NOT NULL,address INTEGER NOT NULL,"
        "enabled INTEGER NOT NULL CHECK(enabled IN(0,1)),"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(address_space,address));"
        "CREATE TABLE debug_watchpoints("
        "id INTEGER PRIMARY KEY,address_space TEXT NOT NULL,address INTEGER NOT NULL,"
        "length INTEGER NOT NULL CHECK(length>0),"
        "access INTEGER NOT NULL CHECK(access BETWEEN 1 AND 3),"
        "enabled INTEGER NOT NULL CHECK(enabled IN(0,1)),"
        "created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "UNIQUE(address_space,address,length,access));"
        "CREATE INDEX debug_breakpoints_location"
        " ON debug_breakpoints(address_space,address);"
        "CREATE INDEX debug_watchpoints_location"
        " ON debug_watchpoints(address_space,address);"
    };

    constexpr const char* k_schema_v4{
        "CREATE TABLE analysis_generations("
        "generation INTEGER PRIMARY KEY CHECK(generation>=0),"
        "analyzer_version TEXT NOT NULL,decoder_version TEXT NOT NULL,"
        "input_fingerprint TEXT NOT NULL DEFAULT '',"
        "published_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP);"
        "INSERT INTO analysis_generations("
        "generation,analyzer_version,decoder_version,input_fingerprint)"
        " SELECT generation,analyzer_version,decoder_version,'' FROM analysis_state;"
        "CREATE TABLE analysis_instructions("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,address_space TEXT NOT NULL,address INTEGER NOT NULL,"
        "context TEXT NOT NULL,opcode INTEGER NOT NULL CHECK(opcode BETWEEN 0 AND 255),"
        "encoded_size INTEGER NOT NULL CHECK(encoded_size BETWEEN 1 AND 4),"
        "code_identity INTEGER NOT NULL CHECK(code_identity BETWEEN 0 AND 2),"
        "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 4),"
        "PRIMARY KEY(generation,stable_id),"
        "UNIQUE(generation,address_space,address,context));"
        "CREATE TABLE analysis_basic_blocks("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,start_space TEXT NOT NULL,start_address INTEGER NOT NULL,"
        "end_space TEXT NOT NULL,end_address INTEGER NOT NULL,context TEXT NOT NULL,"
        "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 4),"
        "PRIMARY KEY(generation,stable_id));"
        "CREATE TABLE analysis_functions("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,entry_space TEXT NOT NULL,entry_address INTEGER NOT NULL,"
        "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 4),"
        "PRIMARY KEY(generation,stable_id),"
        "UNIQUE(generation,entry_space,entry_address));"
        "CREATE TABLE analysis_function_blocks("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,function_id TEXT NOT NULL,block_id TEXT NOT NULL,"
        "PRIMARY KEY(generation,stable_id),"
        "UNIQUE(generation,function_id,block_id),"
        "FOREIGN KEY(generation,function_id)"
        " REFERENCES analysis_functions(generation,stable_id) ON DELETE CASCADE,"
        "FOREIGN KEY(generation,block_id)"
        " REFERENCES analysis_basic_blocks(generation,stable_id) ON DELETE CASCADE);"
        "CREATE TABLE analysis_edges("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,source_block_id TEXT NOT NULL,"
        "target_block_id TEXT,target_space TEXT,target_address INTEGER,"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 6),"
        "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 4),"
        "CHECK((target_space IS NULL)=(target_address IS NULL)),"
        "PRIMARY KEY(generation,stable_id),"
        "FOREIGN KEY(generation,source_block_id)"
        " REFERENCES analysis_basic_blocks(generation,stable_id) ON DELETE CASCADE);"
        "CREATE TABLE analysis_cross_references("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,source_space TEXT NOT NULL,source_address INTEGER NOT NULL,"
        "target_space TEXT NOT NULL,target_address INTEGER NOT NULL,"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 4),"
        "confidence INTEGER NOT NULL CHECK(confidence BETWEEN 0 AND 4),"
        "PRIMARY KEY(generation,stable_id));"
        "CREATE TABLE analysis_evidence("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,subject_id TEXT NOT NULL,"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 6),"
        "source TEXT NOT NULL,session TEXT NOT NULL,"
        "observation_count INTEGER NOT NULL CHECK(observation_count>0),"
        "PRIMARY KEY(generation,stable_id));"
        "CREATE TABLE analysis_conflicts("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "stable_id TEXT NOT NULL,address_space TEXT NOT NULL,address INTEGER NOT NULL,"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 7),detail TEXT NOT NULL,"
        "PRIMARY KEY(generation,stable_id));"
        "CREATE TABLE analysis_coverage("
        "generation INTEGER NOT NULL REFERENCES analysis_generations(generation)"
        " ON DELETE CASCADE,"
        "address_space TEXT NOT NULL,address INTEGER NOT NULL,session TEXT NOT NULL,"
        "hit_count INTEGER NOT NULL CHECK(hit_count>0),"
        "PRIMARY KEY(generation,address_space,address,session));"
        "CREATE INDEX analysis_instructions_location"
        " ON analysis_instructions(generation,address_space,address);"
        "CREATE INDEX analysis_blocks_location"
        " ON analysis_basic_blocks(generation,start_space,start_address);"
        "CREATE INDEX analysis_functions_location"
        " ON analysis_functions(generation,entry_space,entry_address);"
        "CREATE INDEX analysis_xrefs_source"
        " ON analysis_cross_references(generation,source_space,source_address);"
        "CREATE INDEX analysis_xrefs_target"
        " ON analysis_cross_references(generation,target_space,target_address);"
        "CREATE INDEX analysis_conflicts_location"
        " ON analysis_conflicts(generation,address_space,address);"
        "CREATE INDEX analysis_coverage_location"
        " ON analysis_coverage(generation,address_space,address);"
    };

    constexpr const char* k_schema_v5{
        "CREATE TABLE typed_data_types("
        "stable_id TEXT PRIMARY KEY,name TEXT NOT NULL,"
        "kind INTEGER NOT NULL CHECK(kind BETWEEN 0 AND 7),"
        "byte_size INTEGER NOT NULL CHECK(byte_size>0),"
        "byte_order INTEGER NOT NULL CHECK(byte_order BETWEEN 0 AND 1),"
        "element_type_id TEXT REFERENCES typed_data_types(stable_id)"
        " DEFERRABLE INITIALLY DEFERRED,"
        "element_count INTEGER NOT NULL DEFAULT 0 CHECK(element_count>=0),"
        "pointer_address_space TEXT NOT NULL DEFAULT '',"
        "encoding TEXT NOT NULL DEFAULT '',"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '');"
        "CREATE TABLE typed_data_members("
        "owner_type_id TEXT NOT NULL REFERENCES typed_data_types(stable_id)"
        " ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL CHECK(ordinal>=0),stable_id TEXT NOT NULL,"
        "name TEXT NOT NULL,type_id TEXT NOT NULL REFERENCES typed_data_types(stable_id)"
        " DEFERRABLE INITIALLY DEFERRED,"
        "byte_offset INTEGER NOT NULL CHECK(byte_offset>=0),"
        "bit_offset INTEGER NOT NULL CHECK(bit_offset BETWEEN 0 AND 63),"
        "bit_width INTEGER NOT NULL CHECK(bit_width BETWEEN 0 AND 64),"
        "PRIMARY KEY(owner_type_id,stable_id),"
        "UNIQUE(owner_type_id,ordinal));"
        "CREATE TABLE typed_data_values("
        "owner_type_id TEXT NOT NULL REFERENCES typed_data_types(stable_id)"
        " ON DELETE CASCADE,"
        "ordinal INTEGER NOT NULL CHECK(ordinal>=0),name TEXT NOT NULL,"
        "value INTEGER NOT NULL,"
        "PRIMARY KEY(owner_type_id,name),"
        "UNIQUE(owner_type_id,ordinal));"
        "CREATE TABLE typed_data_objects("
        "stable_id TEXT PRIMARY KEY,address_space TEXT NOT NULL,"
        "address INTEGER NOT NULL,type_id TEXT NOT NULL"
        " REFERENCES typed_data_types(stable_id),"
        "name TEXT NOT NULL DEFAULT '',"
        "layer INTEGER NOT NULL CHECK(layer BETWEEN 0 AND 2),"
        "source TEXT NOT NULL DEFAULT '');"
        "CREATE INDEX typed_data_objects_location"
        " ON typed_data_objects(address_space,address);"
        "INSERT INTO typed_data_types("
        "stable_id,name,kind,byte_size,byte_order,layer,source) VALUES"
        "('clover.u8','Unsigned byte',0,1,0,1,'clover.builtin-types.v1'),"
        "('clover.u16le','Unsigned word',0,2,0,1,'clover.builtin-types.v1'),"
        "('clover.u24le','Unsigned long address',0,3,0,1,'clover.builtin-types.v1'),"
        "('clover.s8','Signed byte',1,1,0,1,'clover.builtin-types.v1');"
    };
}

namespace clover::workbench
{
    project_t::~project_t()
    {
        close();
    }

    bool project_t::open(const std::filesystem::path& projects_root,
                         frontend::system_id_t system,
                         std::span<const std::byte> media,
                         std::string& error)
    {
        close();
        error.clear();
        const std::span<const std::byte> canonical{
            frontend::canonical_media(system, media)
        };
        if (canonical.empty())
        {
            error = "Cannot create a Workbench project for empty media";
            return false;
        }

        _identity = {
            .system = system,
            .canonical_media_sha256 = frontend::media_identity(system, media),
            .canonical_media_size = canonical.size()
        };
        if (_identity.canonical_media_sha256.empty())
        {
            error = "Unable to identify canonical media";
            close();
            return false;
        }
        _path = path_for(
            projects_root,
            system,
            _identity.canonical_media_sha256
        );

        std::error_code filesystem_error{};
        std::filesystem::create_directories(_path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "Unable to create Workbench project directory: "
                + filesystem_error.message();
            close();
            return false;
        }

        const std::string path_utf8{ utils::path_to_utf8(_path) };
        if (sqlite3_open_v2(
                path_utf8.c_str(),
                &_database,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
                nullptr
            ) != SQLITE_OK)
        {
            error = _database != nullptr
                ? sqlite3_errmsg(_database)
                : "Unable to open Workbench project";
            close();
            return false;
        }
        static_cast<void>(sqlite3_busy_timeout(_database, 5000));
        if (!execute(_database, "PRAGMA foreign_keys=ON;", error)
            || !migrate(error)
            || !establish_identity(error))
        {
            close();
            return false;
        }
        return true;
    }

    void project_t::close() noexcept
    {
        if (_database != nullptr)
        {
            sqlite3_close(_database);
            _database = nullptr;
        }
        _identity = {};
        _path.clear();
    }

    bool project_t::is_open() const noexcept
    {
        return _database != nullptr;
    }

    const project_identity_t& project_t::identity() const noexcept
    {
        return _identity;
    }

    const std::filesystem::path& project_t::path() const noexcept
    {
        return _path;
    }

    std::filesystem::path project_t::path_for(
        const std::filesystem::path& projects_root,
        frontend::system_id_t system,
        std::string_view canonical_media_sha256
    )
    {
        return projects_root
            / system_name(system)
            / std::string{ canonical_media_sha256 }
            / "project.sqlite3";
    }

    bool project_t::migrate(std::string& error)
    {
        uint32_t version{};
        if (!read_user_version(_database, version, error))
            return false;
        if (version > k_project_schema_version)
        {
            error = "Workbench project schema is newer than this build";
            return false;
        }
        if (version == k_project_schema_version)
            return true;
        if (!begin_transaction(_database, error))
            return false;

        bool success{ true };
        if (version == 0)
            success = execute(_database, k_schema_v1, error);
        if (success && version <= 1)
            success = execute(_database, k_schema_v2, error);
        if (success && version <= 2)
            success = execute(_database, k_schema_v3, error);
        if (success && version <= 3)
            success = execute(_database, k_schema_v4, error);
        if (success && version <= 4)
            success = execute(_database, k_schema_v5, error);
        if (success)
            success = execute(_database, "PRAGMA user_version=5;", error);
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    bool project_t::establish_identity(std::string& error)
    {
        if (!begin_transaction(_database, error))
            return false;
        statement_t insert{};
        static constexpr const char* insert_sql{
            "INSERT OR IGNORE INTO project("
            "singleton,system,canonical_media_sha256,canonical_media_size)"
            " VALUES(1,?,?,?);"
        };
        bool success{ insert.prepare(_database, insert_sql, error) };
        if (success)
            success = bind_text(insert.get(), 1, system_name(_identity.system), error);
        if (success)
        {
            success = bind_text(
                insert.get(),
                2,
                _identity.canonical_media_sha256,
                error
            );
        }
        if (success)
        {
            sqlite3_bind_int64(
                insert.get(),
                3,
                static_cast<sqlite3_int64>(_identity.canonical_media_size)
            );
            success = sqlite3_step(insert.get()) == SQLITE_DONE;
            if (!success)
                error = sqlite3_errmsg(_database);
        }

        statement_t query{};
        if (success)
        {
            success = query.prepare(
                _database,
                "SELECT system,canonical_media_sha256,canonical_media_size"
                " FROM project WHERE singleton=1;",
                error
            );
        }
        if (success)
        {
            success = sqlite3_step(query.get()) == SQLITE_ROW;
            if (!success)
                error = "Workbench project identity is missing";
        }
        if (success)
        {
            const bool matches{
                column_text(query.get(), 0) == system_name(_identity.system)
                && column_text(query.get(), 1) == _identity.canonical_media_sha256
                && static_cast<uint64_t>(sqlite3_column_int64(query.get(), 2))
                    == _identity.canonical_media_size
            };
            if (!matches)
            {
                error = "Workbench project belongs to different canonical media";
                success = false;
            }
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    bool project_t::upsert_named_fact(const char* table,
                                      const address_key_t& location,
                                      std::string_view text,
                                      fact_layer_t layer,
                                      std::string_view source,
                                      uint64_t generation,
                                      std::string& error)
    {
        if (_database == nullptr || !valid_location(location) || text.empty())
        {
            error = "Invalid Workbench fact";
            return false;
        }
        const std::string sql{
            "INSERT INTO " + std::string{ table }
            + "(address_space,address,text,layer,source,analysis_generation)"
              " VALUES(?,?,?,?,?,?)"
              " ON CONFLICT(address_space,address,layer,source) DO UPDATE SET"
              " text=excluded.text,analysis_generation=excluded.analysis_generation,"
              " updated_at=CURRENT_TIMESTAMP;"
        };
        statement_t statement{};
        if (!statement.prepare(_database, sql.c_str(), error)
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        if (!bind_text(statement.get(), 3, text, error))
            return false;
        sqlite3_bind_int(statement.get(), 4, layer_value(layer));
        if (!bind_text(statement.get(), 5, source, error))
            return false;
        sqlite3_bind_int64(
            statement.get(),
            6,
            static_cast<sqlite3_int64>(generation)
        );
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::set_label(const address_key_t& location,
                              std::string_view label,
                              std::string& error)
    {
        return upsert_named_fact(
            "labels",
            location,
            label,
            fact_layer_t::user,
            "",
            0,
            error
        );
    }

    bool project_t::set_comment(const address_key_t& location,
                                std::string_view comment,
                                std::string& error)
    {
        return upsert_named_fact(
            "comments",
            location,
            comment,
            fact_layer_t::user,
            "",
            0,
            error
        );
    }

    bool project_t::set_derived_label(const address_key_t& location,
                                      std::string_view label,
                                      std::string_view source,
                                      std::string& error)
    {
        const uint64_t generation{ analysis_generation(error) };
        if (!error.empty())
            return false;
        return upsert_named_fact(
            "labels",
            location,
            label,
            fact_layer_t::derived,
            source,
            generation,
            error
        );
    }

    bool project_t::add_bookmark(const address_key_t& location,
                                 std::string_view name,
                                 std::string& error)
    {
        if (_database == nullptr || !valid_location(location) || name.empty())
        {
            error = "Invalid Workbench bookmark";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "INSERT OR IGNORE INTO bookmarks(address_space,address,name)"
                " VALUES(?,?,?);",
                error
            )
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        if (!bind_text(statement.get(), 3, name, error))
            return false;
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::remove_bookmark(int64_t id, std::string& error)
    {
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(_database, "DELETE FROM bookmarks WHERE id=?;", error))
            return false;
        sqlite3_bind_int64(statement.get(), 1, id);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::set_classification(const address_key_t& location,
                                       uint64_t length,
                                       classification_kind_t kind,
                                       std::string& error)
    {
        if (_database == nullptr || !valid_location(location) || length == 0
            || length > static_cast<uint64_t>(std::numeric_limits<sqlite3_int64>::max()))
        {
            error = "Invalid Workbench classification";
            return false;
        }
        statement_t statement{};
        static constexpr const char* sql{
            "INSERT INTO classifications("
            "address_space,address,length,kind,layer,source,analysis_generation)"
            " VALUES(?,?,?,?,0,'',0)"
            " ON CONFLICT(address_space,address,layer,source) DO UPDATE SET"
            " length=excluded.length,kind=excluded.kind,updated_at=CURRENT_TIMESTAMP;"
        };
        if (!statement.prepare(_database, sql, error)
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        sqlite3_bind_int64(statement.get(), 3, static_cast<sqlite3_int64>(length));
        sqlite3_bind_int(statement.get(), 4, static_cast<int>(kind));
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::import_snes_hardware_symbols(std::string& error)
    {
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return false;
        }
        if (!begin_transaction(_database, error))
            return false;
        statement_t statement{};
        static constexpr const char* sql{
            "INSERT OR REPLACE INTO symbols("
            "address_space,address,name,description,layer,source,analysis_generation)"
            " VALUES('snes.cpu-bus',?,?,?,1,'clover.snes.hardware.v1',0);"
        };
        bool success{ statement.prepare(_database, sql, error) };
        for (uint32_t address{ 0x2000u }; success && address <= 0x43ffu; ++address)
        {
            const std::optional<analysis::snes::hardware_symbol_t> symbol{
                analysis::snes::hardware_symbol(address)
            };
            if (!symbol.has_value())
                continue;
            sqlite3_reset(statement.get());
            sqlite3_clear_bindings(statement.get());
            sqlite3_bind_int64(statement.get(), 1, address);
            success = bind_text(statement.get(), 2, symbol->name, error)
                && bind_text(statement.get(), 3, symbol->description, error)
                && sqlite3_step(statement.get()) == SQLITE_DONE;
            if (!success && error.empty())
                error = sqlite3_errmsg(_database);
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    std::vector<named_fact_t> project_t::named_facts(
        const char* table,
        std::string& error
    ) const
    {
        std::vector<named_fact_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        const std::string sql{
            "SELECT address_space,address,text,layer,source,analysis_generation FROM "
            + std::string{ table }
            + " ORDER BY address_space,address,layer,source;"
        };
        statement_t statement{};
        if (!statement.prepare(_database, sql.c_str(), error))
            return result;
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .location = {
                    .address_space = column_text(statement.get(), 0),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 1)
                    )
                },
                .text = column_text(statement.get(), 2),
                .layer = fact_layer(sqlite3_column_int(statement.get(), 3)),
                .source = column_text(statement.get(), 4),
                .analysis_generation = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 5)
                )
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::vector<named_fact_t> project_t::labels(std::string& error) const
    {
        return named_facts("labels", error);
    }

    std::vector<named_fact_t> project_t::comments(std::string& error) const
    {
        return named_facts("comments", error);
    }

    std::vector<bookmark_t> project_t::bookmarks(std::string& error) const
    {
        std::vector<bookmark_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT id,address_space,address,name FROM bookmarks ORDER BY id;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .id = sqlite3_column_int64(statement.get(), 0),
                .location = {
                    .address_space = column_text(statement.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 2)
                    )
                },
                .name = column_text(statement.get(), 3)
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::vector<classification_t> project_t::classifications(
        std::string& error
    ) const
    {
        std::vector<classification_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT address_space,address,length,kind,layer,source,analysis_generation"
                " FROM classifications ORDER BY address_space,address,layer,source;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .location = {
                    .address_space = column_text(statement.get(), 0),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 1)
                    )
                },
                .length = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 2)
                ),
                .kind = static_cast<classification_kind_t>(
                    sqlite3_column_int(statement.get(), 3)
                ),
                .layer = fact_layer(sqlite3_column_int(statement.get(), 4)),
                .source = column_text(statement.get(), 5),
                .analysis_generation = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 6)
                )
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::vector<symbol_t> project_t::symbols(std::string& error) const
    {
        std::vector<symbol_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT address_space,address,name,description,layer,source,analysis_generation"
                " FROM symbols ORDER BY address_space,address,name;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .location = {
                    .address_space = column_text(statement.get(), 0),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 1)
                    )
                },
                .name = column_text(statement.get(), 2),
                .description = column_text(statement.get(), 3),
                .layer = fact_layer(sqlite3_column_int(statement.get(), 4)),
                .source = column_text(statement.get(), 5),
                .analysis_generation = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 6)
                )
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::vector<project_data_type_t> project_t::data_types(
        std::string& error
    ) const
    {
        std::vector<project_data_type_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t type_statement{};
        if (!type_statement.prepare(
                _database,
                "SELECT stable_id,name,kind,byte_size,byte_order,"
                "element_type_id,element_count,pointer_address_space,encoding,"
                "layer,source FROM typed_data_types ORDER BY stable_id;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(type_statement.get())) == SQLITE_ROW)
        {
            analysis::data_type_t definition{
                .stable_id = column_text(type_statement.get(), 0),
                .name = column_text(type_statement.get(), 1),
                .kind = static_cast<analysis::data_type_kind_t>(
                    sqlite3_column_int(type_statement.get(), 2)
                ),
                .byte_size = static_cast<uint64_t>(
                    sqlite3_column_int64(type_statement.get(), 3)
                ),
                .byte_order = static_cast<analysis::byte_order_t>(
                    sqlite3_column_int(type_statement.get(), 4)
                ),
                .element_count = static_cast<uint64_t>(
                    sqlite3_column_int64(type_statement.get(), 6)
                ),
                .pointer_address_space = column_text(type_statement.get(), 7),
                .encoding = column_text(type_statement.get(), 8)
            };
            if (sqlite3_column_type(type_statement.get(), 5) != SQLITE_NULL)
                definition.element_type_id = column_text(type_statement.get(), 5);
            result.push_back({
                .definition = std::move(definition),
                .layer = fact_layer(sqlite3_column_int(type_statement.get(), 9)),
                .source = column_text(type_statement.get(), 10)
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return {};
        }

        statement_t member_statement{};
        if (!member_statement.prepare(
                _database,
                "SELECT owner_type_id,stable_id,name,type_id,byte_offset,"
                "bit_offset,bit_width FROM typed_data_members"
                " ORDER BY owner_type_id,ordinal;",
                error
            ))
        {
            return {};
        }
        while ((step_result = sqlite3_step(member_statement.get())) == SQLITE_ROW)
        {
            const std::string owner{ column_text(member_statement.get(), 0) };
            const auto found{
                std::find_if(
                    result.begin(),
                    result.end(),
                    [&owner](const project_data_type_t& type)
                    {
                        return type.definition.stable_id == owner;
                    }
                )
            };
            if (found == result.end())
            {
                error = "Typed-data member has no owner";
                return {};
            }
            found->definition.members.push_back({
                .stable_id = column_text(member_statement.get(), 1),
                .name = column_text(member_statement.get(), 2),
                .type_id = column_text(member_statement.get(), 3),
                .byte_offset = static_cast<uint64_t>(
                    sqlite3_column_int64(member_statement.get(), 4)
                ),
                .bit_offset = static_cast<uint8_t>(
                    sqlite3_column_int(member_statement.get(), 5)
                ),
                .bit_width = static_cast<uint8_t>(
                    sqlite3_column_int(member_statement.get(), 6)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return {};
        }

        statement_t value_statement{};
        if (!value_statement.prepare(
                _database,
                "SELECT owner_type_id,name,value FROM typed_data_values"
                " ORDER BY owner_type_id,ordinal;",
                error
            ))
        {
            return {};
        }
        while ((step_result = sqlite3_step(value_statement.get())) == SQLITE_ROW)
        {
            const std::string owner{ column_text(value_statement.get(), 0) };
            const auto found{
                std::find_if(
                    result.begin(),
                    result.end(),
                    [&owner](const project_data_type_t& type)
                    {
                        return type.definition.stable_id == owner;
                    }
                )
            };
            if (found == result.end())
            {
                error = "Typed-data value has no owner";
                return {};
            }
            found->definition.values.push_back({
                .name = column_text(value_statement.get(), 1),
                .value = sqlite3_column_int64(value_statement.get(), 2)
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::vector<project_typed_object_t> project_t::typed_objects(
        std::string& error
    ) const
    {
        std::vector<project_typed_object_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT stable_id,address_space,address,type_id,name,layer,source"
                " FROM typed_data_objects"
                " ORDER BY address_space,address,stable_id;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .object = {
                    .stable_id = column_text(statement.get(), 0),
                    .location = {
                        .address_space = column_text(statement.get(), 1),
                        .address = static_cast<uint64_t>(
                            sqlite3_column_int64(statement.get(), 2)
                        )
                    },
                    .type_id = column_text(statement.get(), 3),
                    .name = column_text(statement.get(), 4)
                },
                .layer = fact_layer(sqlite3_column_int(statement.get(), 5)),
                .source = column_text(statement.get(), 6)
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    bool project_t::set_data_type(const analysis::data_type_t& definition,
                                  std::string& error)
    {
        error.clear();
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return false;
        }
        std::vector<project_data_type_t> stored_types{ data_types(error) };
        const std::vector<project_typed_object_t> stored_objects{
            typed_objects(error)
        };
        if (!error.empty())
            return false;
        const auto existing{
            std::find_if(
                stored_types.begin(),
                stored_types.end(),
                [&definition](const project_data_type_t& type)
                {
                    return type.definition.stable_id == definition.stable_id;
                }
            )
        };
        if (existing != stored_types.end()
            && existing->layer != fact_layer_t::user)
        {
            error = "Imported typed-data definitions cannot be overwritten";
            return false;
        }
        if (existing == stored_types.end())
        {
            stored_types.push_back({
                .definition = definition,
                .layer = fact_layer_t::user
            });
        }
        else
        {
            existing->definition = definition;
        }
        std::vector<analysis::data_type_t> definitions{};
        for (const project_data_type_t& type : stored_types)
            definitions.push_back(type.definition);
        std::vector<analysis::typed_object_t> objects{};
        for (const project_typed_object_t& object : stored_objects)
            objects.push_back(object.object);
        const analysis::typed_data_validation_t validation{
            analysis::validate_typed_data(definitions, objects)
        };
        if (!validation.valid())
        {
            error = validation.conflicts.front().detail;
            return false;
        }
        if (!begin_transaction(_database, error))
            return false;

        statement_t type_statement{};
        bool success{
            type_statement.prepare(
                _database,
                "INSERT INTO typed_data_types("
                "stable_id,name,kind,byte_size,byte_order,element_type_id,"
                "element_count,pointer_address_space,encoding,layer,source)"
                " VALUES(?,?,?,?,?,?,?,?,?,0,'')"
                " ON CONFLICT(stable_id) DO UPDATE SET"
                " name=excluded.name,kind=excluded.kind,"
                " byte_size=excluded.byte_size,byte_order=excluded.byte_order,"
                " element_type_id=excluded.element_type_id,"
                " element_count=excluded.element_count,"
                " pointer_address_space=excluded.pointer_address_space,"
                " encoding=excluded.encoding;",
                error
            )
            && bind_text(
                type_statement.get(),
                1,
                definition.stable_id,
                error
            )
            && bind_text(type_statement.get(), 2, definition.name, error)
        };
        if (success)
        {
            sqlite3_bind_int(
                type_statement.get(),
                3,
                static_cast<int>(definition.kind)
            );
            sqlite3_bind_int64(
                type_statement.get(),
                4,
                static_cast<sqlite3_int64>(definition.byte_size)
            );
            sqlite3_bind_int(
                type_statement.get(),
                5,
                static_cast<int>(definition.byte_order)
            );
            if (definition.element_type_id.has_value())
            {
                success = bind_text(
                    type_statement.get(),
                    6,
                    *definition.element_type_id,
                    error
                );
            }
            else
            {
                success = sqlite3_bind_null(type_statement.get(), 6) == SQLITE_OK;
            }
            sqlite3_bind_int64(
                type_statement.get(),
                7,
                static_cast<sqlite3_int64>(definition.element_count)
            );
            success = success && bind_text(
                type_statement.get(),
                8,
                definition.pointer_address_space,
                error
            ) && bind_text(
                type_statement.get(),
                9,
                definition.encoding,
                error
            ) && step_done(type_statement.get(), error);
        }

        statement_t delete_members{};
        statement_t delete_values{};
        if (success)
        {
            success = delete_members.prepare(
                _database,
                "DELETE FROM typed_data_members WHERE owner_type_id=?;",
                error
            ) && bind_text(
                delete_members.get(),
                1,
                definition.stable_id,
                error
            ) && step_done(delete_members.get(), error)
                && delete_values.prepare(
                    _database,
                    "DELETE FROM typed_data_values WHERE owner_type_id=?;",
                    error
                ) && bind_text(
                    delete_values.get(),
                    1,
                    definition.stable_id,
                    error
                ) && step_done(delete_values.get(), error);
        }

        statement_t member_statement{};
        if (success && !definition.members.empty())
        {
            success = member_statement.prepare(
                _database,
                "INSERT INTO typed_data_members("
                "owner_type_id,ordinal,stable_id,name,type_id,byte_offset,"
                "bit_offset,bit_width) VALUES(?,?,?,?,?,?,?,?);",
                error
            );
        }
        for (size_t index{}; success && index < definition.members.size(); ++index)
        {
            const analysis::data_type_member_t& member{
                definition.members[index]
            };
            sqlite3_reset(member_statement.get());
            sqlite3_clear_bindings(member_statement.get());
            success = bind_text(
                member_statement.get(),
                1,
                definition.stable_id,
                error
            );
            sqlite3_bind_int64(
                member_statement.get(),
                2,
                static_cast<sqlite3_int64>(index)
            );
            success = success
                && bind_text(member_statement.get(), 3, member.stable_id, error)
                && bind_text(member_statement.get(), 4, member.name, error)
                && bind_text(member_statement.get(), 5, member.type_id, error);
            sqlite3_bind_int64(
                member_statement.get(),
                6,
                static_cast<sqlite3_int64>(member.byte_offset)
            );
            sqlite3_bind_int(member_statement.get(), 7, member.bit_offset);
            sqlite3_bind_int(member_statement.get(), 8, member.bit_width);
            success = success && step_done(member_statement.get(), error);
        }

        statement_t value_statement{};
        if (success && !definition.values.empty())
        {
            success = value_statement.prepare(
                _database,
                "INSERT INTO typed_data_values("
                "owner_type_id,ordinal,name,value) VALUES(?,?,?,?);",
                error
            );
        }
        for (size_t index{}; success && index < definition.values.size(); ++index)
        {
            const analysis::data_type_value_t& value{ definition.values[index] };
            sqlite3_reset(value_statement.get());
            sqlite3_clear_bindings(value_statement.get());
            success = bind_text(
                value_statement.get(),
                1,
                definition.stable_id,
                error
            );
            sqlite3_bind_int64(
                value_statement.get(),
                2,
                static_cast<sqlite3_int64>(index)
            );
            success = success
                && bind_text(value_statement.get(), 3, value.name, error);
            sqlite3_bind_int64(value_statement.get(), 4, value.value);
            success = success && step_done(value_statement.get(), error);
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    bool project_t::set_typed_object(const analysis::typed_object_t& object,
                                     std::string& error)
    {
        error.clear();
        if (_database == nullptr || !valid_location(object.location))
        {
            error = "Invalid typed-data object";
            return false;
        }
        const std::vector<project_data_type_t> stored_types{ data_types(error) };
        std::vector<project_typed_object_t> stored_objects{
            typed_objects(error)
        };
        if (!error.empty())
            return false;
        const auto existing{
            std::find_if(
                stored_objects.begin(),
                stored_objects.end(),
                [&object](const project_typed_object_t& stored)
                {
                    return stored.object.stable_id == object.stable_id;
                }
            )
        };
        if (existing != stored_objects.end()
            && existing->layer != fact_layer_t::user)
        {
            error = "Imported typed-data objects cannot be overwritten";
            return false;
        }
        if (existing == stored_objects.end())
            stored_objects.push_back({ .object = object });
        else
            existing->object = object;
        std::vector<analysis::data_type_t> definitions{};
        for (const project_data_type_t& type : stored_types)
            definitions.push_back(type.definition);
        std::vector<analysis::typed_object_t> objects{};
        for (const project_typed_object_t& stored : stored_objects)
            objects.push_back(stored.object);
        const analysis::typed_data_validation_t validation{
            analysis::validate_typed_data(definitions, objects)
        };
        if (!validation.valid())
        {
            error = validation.conflicts.front().detail;
            return false;
        }

        const auto selected_type{
            std::find_if(
                definitions.begin(),
                definitions.end(),
                [&object](const analysis::data_type_t& type)
                {
                    return type.stable_id == object.type_id;
                }
            )
        };
        if (selected_type == definitions.end()
            || !begin_transaction(_database, error))
        {
            if (error.empty())
                error = "Typed-data object type is unavailable";
            return false;
        }
        statement_t statement{};
        bool success{
            statement.prepare(
                _database,
                "INSERT INTO typed_data_objects("
                "stable_id,address_space,address,type_id,name,layer,source)"
                " VALUES(?,?,?,?,?,0,'')"
                " ON CONFLICT(stable_id) DO UPDATE SET"
                " address_space=excluded.address_space,"
                " address=excluded.address,type_id=excluded.type_id,"
                " name=excluded.name;",
                error
            )
            && bind_text(statement.get(), 1, object.stable_id, error)
            && bind_text(
                statement.get(),
                2,
                object.location.address_space,
                error
            )
        };
        if (success)
        {
            sqlite3_bind_int64(
                statement.get(),
                3,
                static_cast<sqlite3_int64>(object.location.address)
            );
            success = bind_text(statement.get(), 4, object.type_id, error)
                && bind_text(statement.get(), 5, object.name, error)
                && step_done(statement.get(), error);
        }

        statement_t classification{};
        if (success)
        {
            success = classification.prepare(
                _database,
                "INSERT INTO classifications("
                "address_space,address,length,kind,layer,source,"
                "analysis_generation) VALUES(?,?,?,1,0,'',0)"
                " ON CONFLICT(address_space,address,layer,source) DO UPDATE SET"
                " length=excluded.length,kind=excluded.kind,"
                " updated_at=CURRENT_TIMESTAMP;",
                error
            ) && bind_text(
                classification.get(),
                1,
                object.location.address_space,
                error
            );
        }
        if (success)
        {
            sqlite3_bind_int64(
                classification.get(),
                2,
                static_cast<sqlite3_int64>(object.location.address)
            );
            sqlite3_bind_int64(
                classification.get(),
                3,
                static_cast<sqlite3_int64>(selected_type->byte_size)
            );
            success = step_done(classification.get(), error);
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    bool project_t::remove_typed_object(std::string_view stable_id,
                                        std::string& error)
    {
        if (_database == nullptr || stable_id.empty())
        {
            error = "Invalid typed-data object";
            return false;
        }
        statement_t statement{};
        return statement.prepare(
                _database,
                "DELETE FROM typed_data_objects"
                " WHERE stable_id=? AND layer=0;",
                error
            )
            && bind_text(statement.get(), 1, stable_id, error)
            && step_done(statement.get(), error);
    }

    uint64_t project_t::analysis_generation(std::string& error) const
    {
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return 0;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT generation FROM analysis_state WHERE singleton=1;",
                error
            )
            || sqlite3_step(statement.get()) != SQLITE_ROW)
        {
            if (error.empty())
                error = sqlite3_errmsg(_database);
            return 0;
        }
        return static_cast<uint64_t>(sqlite3_column_int64(statement.get(), 0));
    }

    bool project_t::begin_analysis_generation(
        std::string_view analyzer_version,
        std::string_view decoder_version,
        std::string& error
    )
    {
        uint64_t generation{};
        return publish_analysis(
            {},
            analyzer_version,
            decoder_version,
            {},
            generation,
            error
        );
    }

    bool project_t::publish_analysis(
        const analysis::program_model_t& model,
        std::string_view analyzer_version,
        std::string_view decoder_version,
        std::string_view input_fingerprint,
        uint64_t& generation,
        std::string& error
    )
    {
        generation = 0u;
        error.clear();
        if (_database == nullptr || analyzer_version.empty()
            || decoder_version.empty() || !valid_model(model, error))
        {
            if (error.empty())
                error = "Invalid analysis generation";
            return false;
        }
        if (!begin_transaction(_database, error))
            return false;

        bool success{ true };
        statement_t next_generation{};
        if (!next_generation.prepare(
                _database,
                "SELECT COALESCE(MAX(generation),-1)+1"
                " FROM analysis_generations;",
                error
            )
            || sqlite3_step(next_generation.get()) != SQLITE_ROW)
        {
            if (error.empty())
                error = sqlite3_errmsg(_database);
            success = false;
        }
        if (success)
        {
            generation = static_cast<uint64_t>(
                sqlite3_column_int64(next_generation.get(), 0)
            );
        }

        statement_t insert_generation{};
        if (success)
        {
            success = insert_generation.prepare(
                _database,
                "INSERT INTO analysis_generations("
                "generation,analyzer_version,decoder_version,input_fingerprint)"
                " VALUES(?,?,?,?);",
                error
            );
        }
        if (success)
        {
            sqlite3_bind_int64(
                insert_generation.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(
                insert_generation.get(),
                2,
                analyzer_version,
                error
            ) && bind_text(
                insert_generation.get(),
                3,
                decoder_version,
                error
            ) && bind_text(
                insert_generation.get(),
                4,
                input_fingerprint,
                error
            ) && step_done(insert_generation.get(), error);
        }

        statement_t instruction{};
        if (success)
        {
            success = instruction.prepare(
                _database,
                "INSERT INTO analysis_instructions("
                "generation,stable_id,address_space,address,context,opcode,"
                "encoded_size,code_identity,confidence)"
                " VALUES(?,?,?,?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::instruction_fact_t& fact : model.instructions)
        {
            if (!success)
                break;
            sqlite3_reset(instruction.get());
            sqlite3_clear_bindings(instruction.get());
            sqlite3_bind_int64(
                instruction.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(instruction.get(), 2, fact.stable_id, error)
                && bind_text(
                    instruction.get(),
                    3,
                    fact.location.address_space,
                    error
                );
            sqlite3_bind_int64(
                instruction.get(),
                4,
                static_cast<sqlite3_int64>(fact.location.address)
            );
            success = success
                && bind_text(instruction.get(), 5, fact.context, error);
            sqlite3_bind_int(instruction.get(), 6, fact.opcode);
            sqlite3_bind_int(instruction.get(), 7, fact.encoded_size);
            sqlite3_bind_int(
                instruction.get(),
                8,
                static_cast<int>(fact.code_identity)
            );
            sqlite3_bind_int(
                instruction.get(),
                9,
                static_cast<int>(fact.confidence)
            );
            success = success && step_done(instruction.get(), error);
        }

        statement_t block{};
        if (success)
        {
            success = block.prepare(
                _database,
                "INSERT INTO analysis_basic_blocks("
                "generation,stable_id,start_space,start_address,end_space,"
                "end_address,context,confidence) VALUES(?,?,?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::basic_block_fact_t& fact : model.basic_blocks)
        {
            if (!success)
                break;
            sqlite3_reset(block.get());
            sqlite3_clear_bindings(block.get());
            sqlite3_bind_int64(
                block.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(block.get(), 2, fact.stable_id, error)
                && bind_text(block.get(), 3, fact.start.address_space, error);
            sqlite3_bind_int64(
                block.get(),
                4,
                static_cast<sqlite3_int64>(fact.start.address)
            );
            success = success
                && bind_text(block.get(), 5, fact.end.address_space, error);
            sqlite3_bind_int64(
                block.get(),
                6,
                static_cast<sqlite3_int64>(fact.end.address)
            );
            success = success && bind_text(block.get(), 7, fact.context, error);
            sqlite3_bind_int(
                block.get(),
                8,
                static_cast<int>(fact.confidence)
            );
            success = success && step_done(block.get(), error);
        }

        statement_t function{};
        if (success)
        {
            success = function.prepare(
                _database,
                "INSERT INTO analysis_functions("
                "generation,stable_id,entry_space,entry_address,confidence)"
                " VALUES(?,?,?,?,?);",
                error
            );
        }
        for (const analysis::function_fact_t& fact : model.functions)
        {
            if (!success)
                break;
            sqlite3_reset(function.get());
            sqlite3_clear_bindings(function.get());
            sqlite3_bind_int64(
                function.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(function.get(), 2, fact.stable_id, error)
                && bind_text(function.get(), 3, fact.entry.address_space, error);
            sqlite3_bind_int64(
                function.get(),
                4,
                static_cast<sqlite3_int64>(fact.entry.address)
            );
            sqlite3_bind_int(
                function.get(),
                5,
                static_cast<int>(fact.confidence)
            );
            success = success && step_done(function.get(), error);
        }

        statement_t function_block{};
        if (success)
        {
            success = function_block.prepare(
                _database,
                "INSERT INTO analysis_function_blocks("
                "generation,stable_id,function_id,block_id)"
                " VALUES(?,?,?,?);",
                error
            );
        }
        for (const analysis::function_block_fact_t& fact
             : model.function_blocks)
        {
            if (!success)
                break;
            sqlite3_reset(function_block.get());
            sqlite3_clear_bindings(function_block.get());
            sqlite3_bind_int64(
                function_block.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(
                function_block.get(),
                2,
                fact.stable_id,
                error
            ) && bind_text(
                function_block.get(),
                3,
                fact.function_id,
                error
            ) && bind_text(
                function_block.get(),
                4,
                fact.block_id,
                error
            ) && step_done(function_block.get(), error);
        }

        statement_t edge{};
        if (success)
        {
            success = edge.prepare(
                _database,
                "INSERT INTO analysis_edges("
                "generation,stable_id,source_block_id,target_block_id,"
                "target_space,target_address,kind,confidence)"
                " VALUES(?,?,?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::edge_fact_t& fact : model.edges)
        {
            if (!success)
                break;
            sqlite3_reset(edge.get());
            sqlite3_clear_bindings(edge.get());
            sqlite3_bind_int64(
                edge.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(edge.get(), 2, fact.stable_id, error)
                && bind_text(edge.get(), 3, fact.source_block_id, error);
            if (fact.target_block_id.has_value())
            {
                success = success
                    && bind_text(edge.get(), 4, *fact.target_block_id, error);
            }
            else
            {
                sqlite3_bind_null(edge.get(), 4);
            }
            if (fact.target.has_value())
            {
                success = success && bind_text(
                    edge.get(),
                    5,
                    fact.target->address_space,
                    error
                );
                sqlite3_bind_int64(
                    edge.get(),
                    6,
                    static_cast<sqlite3_int64>(fact.target->address)
                );
            }
            else
            {
                sqlite3_bind_null(edge.get(), 5);
                sqlite3_bind_null(edge.get(), 6);
            }
            sqlite3_bind_int(edge.get(), 7, static_cast<int>(fact.kind));
            sqlite3_bind_int(
                edge.get(),
                8,
                static_cast<int>(fact.confidence)
            );
            success = success && step_done(edge.get(), error);
        }

        statement_t reference{};
        if (success)
        {
            success = reference.prepare(
                _database,
                "INSERT INTO analysis_cross_references("
                "generation,stable_id,source_space,source_address,target_space,"
                "target_address,kind,confidence) VALUES(?,?,?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::cross_reference_fact_t& fact
             : model.cross_references)
        {
            if (!success)
                break;
            sqlite3_reset(reference.get());
            sqlite3_clear_bindings(reference.get());
            sqlite3_bind_int64(
                reference.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(reference.get(), 2, fact.stable_id, error)
                && bind_text(
                    reference.get(),
                    3,
                    fact.source.address_space,
                    error
                );
            sqlite3_bind_int64(
                reference.get(),
                4,
                static_cast<sqlite3_int64>(fact.source.address)
            );
            success = success && bind_text(
                reference.get(),
                5,
                fact.target.address_space,
                error
            );
            sqlite3_bind_int64(
                reference.get(),
                6,
                static_cast<sqlite3_int64>(fact.target.address)
            );
            sqlite3_bind_int(reference.get(), 7, static_cast<int>(fact.kind));
            sqlite3_bind_int(
                reference.get(),
                8,
                static_cast<int>(fact.confidence)
            );
            success = success && step_done(reference.get(), error);
        }

        statement_t evidence{};
        if (success)
        {
            success = evidence.prepare(
                _database,
                "INSERT INTO analysis_evidence("
                "generation,stable_id,subject_id,kind,source,session,"
                "observation_count) VALUES(?,?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::evidence_fact_t& fact : model.evidence)
        {
            if (!success)
                break;
            sqlite3_reset(evidence.get());
            sqlite3_clear_bindings(evidence.get());
            sqlite3_bind_int64(
                evidence.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(evidence.get(), 2, fact.stable_id, error)
                && bind_text(evidence.get(), 3, fact.subject_id, error);
            sqlite3_bind_int(evidence.get(), 4, static_cast<int>(fact.kind));
            success = success
                && bind_text(evidence.get(), 5, fact.source, error)
                && bind_text(evidence.get(), 6, fact.session, error);
            sqlite3_bind_int64(
                evidence.get(),
                7,
                static_cast<sqlite3_int64>(fact.observation_count)
            );
            success = success && step_done(evidence.get(), error);
        }

        statement_t conflict{};
        if (success)
        {
            success = conflict.prepare(
                _database,
                "INSERT INTO analysis_conflicts("
                "generation,stable_id,address_space,address,kind,detail)"
                " VALUES(?,?,?,?,?,?);",
                error
            );
        }
        for (const analysis::conflict_fact_t& fact : model.conflicts)
        {
            if (!success)
                break;
            sqlite3_reset(conflict.get());
            sqlite3_clear_bindings(conflict.get());
            sqlite3_bind_int64(
                conflict.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(conflict.get(), 2, fact.stable_id, error)
                && bind_text(
                    conflict.get(),
                    3,
                    fact.location.address_space,
                    error
                );
            sqlite3_bind_int64(
                conflict.get(),
                4,
                static_cast<sqlite3_int64>(fact.location.address)
            );
            sqlite3_bind_int(conflict.get(), 5, static_cast<int>(fact.kind));
            success = success && bind_text(
                conflict.get(),
                6,
                fact.detail,
                error
            ) && step_done(conflict.get(), error);
        }

        statement_t coverage{};
        if (success)
        {
            success = coverage.prepare(
                _database,
                "INSERT INTO analysis_coverage("
                "generation,address_space,address,session,hit_count)"
                " VALUES(?,?,?,?,?);",
                error
            );
        }
        for (const analysis::coverage_fact_t& fact : model.coverage)
        {
            if (!success)
                break;
            sqlite3_reset(coverage.get());
            sqlite3_clear_bindings(coverage.get());
            sqlite3_bind_int64(
                coverage.get(),
                1,
                static_cast<sqlite3_int64>(generation)
            );
            success = bind_text(
                coverage.get(),
                2,
                fact.location.address_space,
                error
            );
            sqlite3_bind_int64(
                coverage.get(),
                3,
                static_cast<sqlite3_int64>(fact.location.address)
            );
            success = success
                && bind_text(coverage.get(), 4, fact.session, error);
            sqlite3_bind_int64(
                coverage.get(),
                5,
                static_cast<sqlite3_int64>(fact.hit_count)
            );
            success = success && step_done(coverage.get(), error);
        }

        statement_t publish{};
        if (success)
        {
            success = publish.prepare(
                _database,
                "UPDATE analysis_state SET analyzer_version=?,decoder_version=?,"
                "generation=? WHERE singleton=1;",
                error
            );
        }
        if (success)
        {
            success = bind_text(publish.get(), 1, analyzer_version, error)
                && bind_text(publish.get(), 2, decoder_version, error);
            sqlite3_bind_int64(
                publish.get(),
                3,
                static_cast<sqlite3_int64>(generation)
            );
            success = success && step_done(publish.get(), error);
        }
        if (success)
        {
            success =
                execute(_database, "DELETE FROM labels WHERE layer=2;", error)
                && execute(_database, "DELETE FROM comments WHERE layer=2;", error)
                && execute(
                    _database,
                    "DELETE FROM classifications WHERE layer=2;",
                    error
                )
                && execute(_database, "DELETE FROM symbols WHERE layer=2;", error);
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
        {
            rollback_transaction(_database);
            generation = 0u;
        }
        return success;
    }

    std::vector<analysis_generation_t> project_t::analysis_generations(
        std::string& error
    ) const
    {
        std::vector<analysis_generation_t> result{};
        error.clear();
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT g.generation,g.analyzer_version,g.decoder_version,"
                "g.input_fingerprint,g.generation=s.generation"
                " FROM analysis_generations g CROSS JOIN analysis_state s"
                " WHERE s.singleton=1 ORDER BY g.generation;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .generation = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 0)
                ),
                .analyzer_version = column_text(statement.get(), 1),
                .decoder_version = column_text(statement.get(), 2),
                .input_fingerprint = column_text(statement.get(), 3),
                .current = sqlite3_column_int(statement.get(), 4) != 0
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    std::optional<analysis::program_model_t> project_t::current_analysis(
        std::string& error
    ) const
    {
        error.clear();
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return std::nullopt;
        }
        const uint64_t generation{ analysis_generation(error) };
        if (!error.empty())
            return std::nullopt;
        return analysis(generation, error);
    }

    std::optional<analysis::program_model_t> project_t::analysis(
        uint64_t generation,
        std::string& error
    ) const
    {
        error.clear();
        if (_database == nullptr
            || generation > static_cast<uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()
            ))
        {
            error = "Invalid analysis generation";
            return std::nullopt;
        }
        statement_t exists{};
        if (!exists.prepare(
                _database,
                "SELECT 1 FROM analysis_generations WHERE generation=?;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            exists.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        if (sqlite3_step(exists.get()) != SQLITE_ROW)
        {
            error = "Analysis generation does not exist";
            return std::nullopt;
        }
        analysis::program_model_t model{};
        int step_result{};

        statement_t instruction{};
        if (!instruction.prepare(
                _database,
                "SELECT stable_id,address_space,address,context,opcode,"
                "encoded_size,code_identity,confidence FROM analysis_instructions"
                " WHERE generation=? ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            instruction.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(instruction.get())) == SQLITE_ROW)
        {
            model.instructions.push_back({
                .stable_id = column_text(instruction.get(), 0),
                .location = {
                    .address_space = column_text(instruction.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(instruction.get(), 2)
                    )
                },
                .context = column_text(instruction.get(), 3),
                .opcode = static_cast<uint8_t>(
                    sqlite3_column_int(instruction.get(), 4)
                ),
                .encoded_size = static_cast<uint8_t>(
                    sqlite3_column_int(instruction.get(), 5)
                ),
                .code_identity = static_cast<analysis::code_identity_t>(
                    sqlite3_column_int(instruction.get(), 6)
                ),
                .confidence = static_cast<analysis::confidence_t>(
                    sqlite3_column_int(instruction.get(), 7)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t block{};
        if (!block.prepare(
                _database,
                "SELECT stable_id,start_space,start_address,end_space,"
                "end_address,context,confidence FROM analysis_basic_blocks"
                " WHERE generation=? ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            block.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(block.get())) == SQLITE_ROW)
        {
            model.basic_blocks.push_back({
                .stable_id = column_text(block.get(), 0),
                .start = {
                    .address_space = column_text(block.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(block.get(), 2)
                    )
                },
                .end = {
                    .address_space = column_text(block.get(), 3),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(block.get(), 4)
                    )
                },
                .context = column_text(block.get(), 5),
                .confidence = static_cast<analysis::confidence_t>(
                    sqlite3_column_int(block.get(), 6)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t function{};
        if (!function.prepare(
                _database,
                "SELECT stable_id,entry_space,entry_address,confidence"
                " FROM analysis_functions WHERE generation=?"
                " ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            function.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(function.get())) == SQLITE_ROW)
        {
            model.functions.push_back({
                .stable_id = column_text(function.get(), 0),
                .entry = {
                    .address_space = column_text(function.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(function.get(), 2)
                    )
                },
                .confidence = static_cast<analysis::confidence_t>(
                    sqlite3_column_int(function.get(), 3)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t function_block{};
        if (!function_block.prepare(
                _database,
                "SELECT stable_id,function_id,block_id"
                " FROM analysis_function_blocks WHERE generation=?"
                " ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            function_block.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(function_block.get())) == SQLITE_ROW)
        {
            model.function_blocks.push_back({
                .stable_id = column_text(function_block.get(), 0),
                .function_id = column_text(function_block.get(), 1),
                .block_id = column_text(function_block.get(), 2)
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t edge{};
        if (!edge.prepare(
                _database,
                "SELECT stable_id,source_block_id,target_block_id,target_space,"
                "target_address,kind,confidence FROM analysis_edges"
                " WHERE generation=? ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            edge.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(edge.get())) == SQLITE_ROW)
        {
            analysis::edge_fact_t fact{
                .stable_id = column_text(edge.get(), 0),
                .source_block_id = column_text(edge.get(), 1),
                .kind = static_cast<analysis::edge_kind_t>(
                    sqlite3_column_int(edge.get(), 5)
                ),
                .confidence = static_cast<analysis::confidence_t>(
                    sqlite3_column_int(edge.get(), 6)
                )
            };
            if (sqlite3_column_type(edge.get(), 2) != SQLITE_NULL)
                fact.target_block_id = column_text(edge.get(), 2);
            if (sqlite3_column_type(edge.get(), 3) != SQLITE_NULL)
            {
                fact.target = analysis::address_t{
                    .address_space = column_text(edge.get(), 3),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(edge.get(), 4)
                    )
                };
            }
            model.edges.push_back(std::move(fact));
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t reference{};
        if (!reference.prepare(
                _database,
                "SELECT stable_id,source_space,source_address,target_space,"
                "target_address,kind,confidence FROM analysis_cross_references"
                " WHERE generation=? ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            reference.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(reference.get())) == SQLITE_ROW)
        {
            model.cross_references.push_back({
                .stable_id = column_text(reference.get(), 0),
                .source = {
                    .address_space = column_text(reference.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(reference.get(), 2)
                    )
                },
                .target = {
                    .address_space = column_text(reference.get(), 3),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(reference.get(), 4)
                    )
                },
                .kind = static_cast<analysis::cross_reference_kind_t>(
                    sqlite3_column_int(reference.get(), 5)
                ),
                .confidence = static_cast<analysis::confidence_t>(
                    sqlite3_column_int(reference.get(), 6)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t evidence{};
        if (!evidence.prepare(
                _database,
                "SELECT stable_id,subject_id,kind,source,session,"
                "observation_count FROM analysis_evidence"
                " WHERE generation=? ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            evidence.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(evidence.get())) == SQLITE_ROW)
        {
            model.evidence.push_back({
                .stable_id = column_text(evidence.get(), 0),
                .subject_id = column_text(evidence.get(), 1),
                .kind = static_cast<analysis::evidence_kind_t>(
                    sqlite3_column_int(evidence.get(), 2)
                ),
                .source = column_text(evidence.get(), 3),
                .session = column_text(evidence.get(), 4),
                .observation_count = static_cast<uint64_t>(
                    sqlite3_column_int64(evidence.get(), 5)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t conflict{};
        if (!conflict.prepare(
                _database,
                "SELECT stable_id,address_space,address,kind,detail"
                " FROM analysis_conflicts WHERE generation=?"
                " ORDER BY stable_id;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            conflict.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(conflict.get())) == SQLITE_ROW)
        {
            model.conflicts.push_back({
                .stable_id = column_text(conflict.get(), 0),
                .location = {
                    .address_space = column_text(conflict.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(conflict.get(), 2)
                    )
                },
                .kind = static_cast<analysis::conflict_kind_t>(
                    sqlite3_column_int(conflict.get(), 3)
                ),
                .detail = column_text(conflict.get(), 4)
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        statement_t coverage{};
        if (!coverage.prepare(
                _database,
                "SELECT address_space,address,session,hit_count"
                " FROM analysis_coverage WHERE generation=?"
                " ORDER BY address_space,address,session;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(
            coverage.get(),
            1,
            static_cast<sqlite3_int64>(generation)
        );
        while ((step_result = sqlite3_step(coverage.get())) == SQLITE_ROW)
        {
            model.coverage.push_back({
                .location = {
                    .address_space = column_text(coverage.get(), 0),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(coverage.get(), 1)
                    )
                },
                .session = column_text(coverage.get(), 2),
                .hit_count = static_cast<uint64_t>(
                    sqlite3_column_int64(coverage.get(), 3)
                )
            });
        }
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }

        return model;
    }

    bool project_t::record_navigation(const address_key_t& location,
                                      std::string_view view,
                                      std::string& error)
    {
        if (_database == nullptr || !valid_location(location) || view.empty())
        {
            error = "Invalid navigation entry";
            return false;
        }
        if (!begin_transaction(_database, error))
            return false;
        bool success{
            execute(
                _database,
                "DELETE FROM navigation_history WHERE sequence>("
                "SELECT COALESCE(cursor_sequence,0) FROM navigation_state WHERE singleton=1);",
                error
            )
        };
        statement_t insert{};
        if (success)
        {
            success = insert.prepare(
                _database,
                "INSERT INTO navigation_history(address_space,address,view) VALUES(?,?,?);",
                error
            );
        }
        if (success)
            success = bind_text(insert.get(), 1, location.address_space, error);
        if (success)
        {
            sqlite3_bind_int64(
                insert.get(),
                2,
                static_cast<sqlite3_int64>(location.address)
            );
            success = bind_text(insert.get(), 3, view, error);
        }
        if (success)
        {
            success = sqlite3_step(insert.get()) == SQLITE_DONE;
            if (!success)
                error = sqlite3_errmsg(_database);
        }
        if (success)
        {
            success = execute(
                _database,
                "UPDATE navigation_state SET cursor_sequence=last_insert_rowid()"
                " WHERE singleton=1;"
                "DELETE FROM navigation_history WHERE sequence NOT IN("
                "SELECT sequence FROM navigation_history ORDER BY sequence DESC LIMIT 512);",
                error
            );
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
    }

    std::optional<navigation_entry_t> project_t::current_navigation(
        std::string& error
    ) const
    {
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return std::nullopt;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT h.sequence,h.address_space,h.address,h.view"
                " FROM navigation_state s JOIN navigation_history h"
                " ON h.sequence=s.cursor_sequence WHERE s.singleton=1;",
                error
            ))
        {
            return std::nullopt;
        }
        const int step_result{ sqlite3_step(statement.get()) };
        if (step_result == SQLITE_DONE)
            return std::nullopt;
        if (step_result != SQLITE_ROW)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }
        return navigation_entry_t{
            .sequence = sqlite3_column_int64(statement.get(), 0),
            .location = {
                .address_space = column_text(statement.get(), 1),
                .address = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 2)
                )
            },
            .view = column_text(statement.get(), 3)
        };
    }

    std::optional<navigation_entry_t> project_t::move_navigation(
        bool forward,
        std::string& error
    )
    {
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return std::nullopt;
        }
        const char* const sql{
            forward
                ? "SELECT sequence,address_space,address,view FROM navigation_history"
                  " WHERE sequence>(SELECT COALESCE(cursor_sequence,0) FROM navigation_state"
                  " WHERE singleton=1) ORDER BY sequence ASC LIMIT 1;"
                : "SELECT sequence,address_space,address,view FROM navigation_history"
                  " WHERE sequence<(SELECT COALESCE(cursor_sequence,0) FROM navigation_state"
                  " WHERE singleton=1) ORDER BY sequence DESC LIMIT 1;"
        };
        statement_t query{};
        if (!query.prepare(_database, sql, error))
            return std::nullopt;
        const int step_result{ sqlite3_step(query.get()) };
        if (step_result == SQLITE_DONE)
            return current_navigation(error);
        if (step_result != SQLITE_ROW)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }
        navigation_entry_t entry{
            .sequence = sqlite3_column_int64(query.get(), 0),
            .location = {
                .address_space = column_text(query.get(), 1),
                .address = static_cast<uint64_t>(
                    sqlite3_column_int64(query.get(), 2)
                )
            },
            .view = column_text(query.get(), 3)
        };
        statement_t update{};
        if (!update.prepare(
                _database,
                "UPDATE navigation_state SET cursor_sequence=? WHERE singleton=1;",
                error
            ))
        {
            return std::nullopt;
        }
        sqlite3_bind_int64(update.get(), 1, entry.sequence);
        if (sqlite3_step(update.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return std::nullopt;
        }
        return entry;
    }

    std::optional<navigation_entry_t> project_t::navigate_back(std::string& error)
    {
        return move_navigation(false, error);
    }

    std::optional<navigation_entry_t> project_t::navigate_forward(std::string& error)
    {
        return move_navigation(true, error);
    }

    std::vector<navigation_entry_t> project_t::navigation_history(
        std::string& error
    ) const
    {
        std::vector<navigation_entry_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT sequence,address_space,address,view"
                " FROM navigation_history ORDER BY sequence;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .sequence = sqlite3_column_int64(statement.get(), 0),
                .location = {
                    .address_space = column_text(statement.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 2)
                    )
                },
                .view = column_text(statement.get(), 3)
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    bool project_t::set_debug_breakpoint(const address_key_t& location,
                                         bool enabled,
                                         std::string& error)
    {
        if (_database == nullptr || !valid_location(location))
        {
            error = "Invalid debugger breakpoint";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "INSERT INTO debug_breakpoints(address_space,address,enabled)"
                " VALUES(?,?,?)"
                " ON CONFLICT(address_space,address) DO UPDATE SET"
                " enabled=excluded.enabled;",
                error
            )
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        sqlite3_bind_int(statement.get(), 3, enabled ? 1 : 0);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::remove_debug_breakpoint(const address_key_t& location,
                                            std::string& error)
    {
        if (_database == nullptr || !valid_location(location))
        {
            error = "Invalid debugger breakpoint";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "DELETE FROM debug_breakpoints"
                " WHERE address_space=? AND address=?;",
                error
            )
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    std::vector<project_breakpoint_t> project_t::debug_breakpoints(
        std::string& error
    ) const
    {
        std::vector<project_breakpoint_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT id,address_space,address,enabled"
                " FROM debug_breakpoints ORDER BY id;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .id = sqlite3_column_int64(statement.get(), 0),
                .location = {
                    .address_space = column_text(statement.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 2)
                    )
                },
                .enabled = sqlite3_column_int(statement.get(), 3) != 0
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }

    bool project_t::set_debug_watchpoint(
        const address_key_t& location,
        uint64_t length,
        project_watch_access_t access,
        bool enabled,
        std::string& error
    )
    {
        const int access_value{ static_cast<int>(access) };
        if (_database == nullptr || !valid_location(location) || length == 0u
            || length > static_cast<uint64_t>(
                std::numeric_limits<sqlite3_int64>::max()
            )
            || access_value < static_cast<int>(project_watch_access_t::read)
            || access_value
                > static_cast<int>(project_watch_access_t::read_write))
        {
            error = "Invalid debugger watchpoint";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "INSERT INTO debug_watchpoints("
                "address_space,address,length,access,enabled) VALUES(?,?,?,?,?)"
                " ON CONFLICT(address_space,address,length,access) DO UPDATE SET"
                " enabled=excluded.enabled;",
                error
            )
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        sqlite3_bind_int64(statement.get(), 3, static_cast<sqlite3_int64>(length));
        sqlite3_bind_int(statement.get(), 4, access_value);
        sqlite3_bind_int(statement.get(), 5, enabled ? 1 : 0);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    bool project_t::remove_debug_watchpoint(
        const address_key_t& location,
        uint64_t length,
        project_watch_access_t access,
        std::string& error
    )
    {
        const int access_value{ static_cast<int>(access) };
        if (_database == nullptr || !valid_location(location) || length == 0u
            || access_value < static_cast<int>(project_watch_access_t::read)
            || access_value
                > static_cast<int>(project_watch_access_t::read_write))
        {
            error = "Invalid debugger watchpoint";
            return false;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "DELETE FROM debug_watchpoints WHERE"
                " address_space=? AND address=? AND length=? AND access=?;",
                error
            )
            || !bind_text(statement.get(), 1, location.address_space, error))
        {
            return false;
        }
        sqlite3_bind_int64(
            statement.get(),
            2,
            static_cast<sqlite3_int64>(location.address)
        );
        sqlite3_bind_int64(statement.get(), 3, static_cast<sqlite3_int64>(length));
        sqlite3_bind_int(statement.get(), 4, access_value);
        if (sqlite3_step(statement.get()) != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return false;
        }
        return true;
    }

    std::vector<project_watchpoint_t> project_t::debug_watchpoints(
        std::string& error
    ) const
    {
        std::vector<project_watchpoint_t> result{};
        if (_database == nullptr)
        {
            error = "Workbench project is not open";
            return result;
        }
        statement_t statement{};
        if (!statement.prepare(
                _database,
                "SELECT id,address_space,address,length,access,enabled"
                " FROM debug_watchpoints ORDER BY id;",
                error
            ))
        {
            return result;
        }
        int step_result{};
        while ((step_result = sqlite3_step(statement.get())) == SQLITE_ROW)
        {
            result.push_back({
                .id = sqlite3_column_int64(statement.get(), 0),
                .location = {
                    .address_space = column_text(statement.get(), 1),
                    .address = static_cast<uint64_t>(
                        sqlite3_column_int64(statement.get(), 2)
                    )
                },
                .length = static_cast<uint64_t>(
                    sqlite3_column_int64(statement.get(), 3)
                ),
                .access = static_cast<project_watch_access_t>(
                    sqlite3_column_int(statement.get(), 4)
                ),
                .enabled = sqlite3_column_int(statement.get(), 5) != 0
            });
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        return result;
    }
}
