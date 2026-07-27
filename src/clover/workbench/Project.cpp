//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/Project.h"

#include "clover/analysis/snes/HardwareSymbols.h"
#include "clover/frontend/MediaIdentity.h"
#include "clover/utils/FileSystem.h"

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
                value.data(),
                static_cast<int>(value.size()),
                SQLITE_TRANSIENT
            ) == SQLITE_OK)
        {
            return true;
        }
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
        if (success)
            success = execute(_database, "PRAGMA user_version=2;", error);
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
        if (_database == nullptr || analyzer_version.empty() || decoder_version.empty())
        {
            error = "Invalid analysis generation";
            return false;
        }
        if (!begin_transaction(_database, error))
            return false;
        bool success{
            execute(_database, "DELETE FROM labels WHERE layer=2;", error)
            && execute(_database, "DELETE FROM comments WHERE layer=2;", error)
            && execute(_database, "DELETE FROM classifications WHERE layer=2;", error)
            && execute(_database, "DELETE FROM symbols WHERE layer=2;", error)
        };
        statement_t statement{};
        if (success)
        {
            success = statement.prepare(
                _database,
                "UPDATE analysis_state SET analyzer_version=?,decoder_version=?,"
                "generation=generation+1 WHERE singleton=1;",
                error
            );
        }
        if (success)
            success = bind_text(statement.get(), 1, analyzer_version, error);
        if (success)
            success = bind_text(statement.get(), 2, decoder_version, error);
        if (success)
        {
            success = sqlite3_step(statement.get()) == SQLITE_DONE;
            if (!success)
                error = sqlite3_errmsg(_database);
        }
        if (success)
            success = commit_transaction(_database, error);
        if (!success)
            rollback_transaction(_database);
        return success;
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
}
