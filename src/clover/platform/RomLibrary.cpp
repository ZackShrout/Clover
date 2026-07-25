//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/RomLibrary.h"

#include "clover/frontend/MediaIdentity.h"
#include "clover/utils/FileSystem.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#include <sqlite3.h>

namespace clover::platform
{
    namespace
    {
        [[nodiscard]] bool execute(sqlite3* database, const char* sql, std::string& error) noexcept
        {
            char* raw_error{ nullptr };
            if (sqlite3_exec(database, sql, nullptr, nullptr, &raw_error) == SQLITE_OK)
                return true;
            error = raw_error != nullptr ? raw_error : sqlite3_errmsg(database);
            sqlite3_free(raw_error);
            return false;
        }
    }

    rom_library_t::~rom_library_t()
    {
        shutdown();
    }

    bool rom_library_t::initialize(const std::filesystem::path& data_root,
                                   std::string& error) noexcept
    {
        shutdown();
        std::error_code filesystem_error{};
        std::filesystem::create_directories(data_root / "library" / "roms", filesystem_error);
        if (filesystem_error)
        {
            error = "Unable to create Clover data directory: " + filesystem_error.message();
            return false;
        }
        _data_root = data_root;
        const std::filesystem::path database_path{ data_root / "library" / "library.sqlite3" };
        const std::string database_path_utf8{ utils::path_to_utf8(database_path) };
        if (sqlite3_open(database_path_utf8.c_str(), &_database) != SQLITE_OK)
        {
            error = _database != nullptr ? sqlite3_errmsg(_database) : "Unable to open SQLite database";
            shutdown();
            return false;
        }
        static constexpr const char* schema{
            "PRAGMA foreign_keys=ON;"
            "CREATE TABLE IF NOT EXISTS roms("
            "id INTEGER PRIMARY KEY,"
            "system TEXT NOT NULL,"
            "content_hash TEXT NOT NULL,"
            "display_name TEXT NOT NULL,"
            "original_filename TEXT NOT NULL,"
            "relative_path TEXT NOT NULL,"
            "size_bytes INTEGER NOT NULL,"
            "imported_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP,"
            "UNIQUE(system, content_hash));"
            "CREATE INDEX IF NOT EXISTS roms_system_name ON roms(system, display_name COLLATE NOCASE);"
        };
        if (!execute(_database, schema, error))
        {
            shutdown();
            return false;
        }
        return true;
    }

    void rom_library_t::shutdown() noexcept
    {
        if (_database != nullptr)
        {
            sqlite3_close(_database);
            _database = nullptr;
        }
        _data_root.clear();
    }

    rom_import_result_t rom_library_t::import_rom(frontend::system_id_t system,
                                                  const std::filesystem::path& source_path,
                                                  std::span<const std::byte> media,
                                                  std::string& error) noexcept
    {
        rom_import_result_t result{};
        if (_database == nullptr || media.empty())
        {
            error = "ROM library is not initialized or the selected ROM is empty";
            return result;
        }
        const std::span<const std::byte> canonical{ frontend::canonical_media(system, media) };
        const std::string hash{ frontend::media_identity(system, media) };
        const std::filesystem::path relative_path{
            std::filesystem::path{ "roms" } / system_name(system) / (hash + ".sfc")
        };
        const std::filesystem::path destination{ _data_root / "library" / relative_path };
        std::error_code filesystem_error{};
        const bool destination_exists{ std::filesystem::exists(destination, filesystem_error) };
        if (filesystem_error)
        {
            error = "Unable to inspect ROM library destination: " + filesystem_error.message();
            return result;
        }
        const bool newly_imported{ !destination_exists };
        if (newly_imported && !write_atomic(destination, canonical, error))
            return result;
        if (!newly_imported)
        {
            std::ifstream existing{ destination, std::ios::binary };
            const std::vector<char> raw{
                std::istreambuf_iterator<char>{ existing }, std::istreambuf_iterator<char>{}
            };
            if (raw.size() != canonical.size()
                || std::memcmp(raw.data(), canonical.data(), canonical.size()) != 0)
            {
                error = "Existing hash-named ROM file does not match imported media: "
                    + destination.string();
                return result;
            }
        }

        sqlite3_stmt* statement{ nullptr };
        static constexpr const char* insert_sql{
            "INSERT INTO roms(system,content_hash,display_name,original_filename,relative_path,size_bytes)"
            " VALUES(?,?,?,?,?,?)"
            " ON CONFLICT(system,content_hash) DO UPDATE SET"
            " display_name=excluded.display_name,original_filename=excluded.original_filename;"
        };
        if (sqlite3_prepare_v2(_database, insert_sql, -1, &statement, nullptr) != SQLITE_OK)
        {
            error = sqlite3_errmsg(_database);
            return result;
        }
        const std::string source_filename{ utils::path_to_utf8(source_path.filename()) };
        const std::string display_name{ utils::path_to_utf8(source_path.stem()) };
        const std::string relative_string{ relative_path.generic_string() };
        sqlite3_bind_text(statement, 1, system_name(system), -1, SQLITE_STATIC);
        sqlite3_bind_text(statement, 2, hash.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 3, display_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 4, source_filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(statement, 5, relative_string.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(statement, 6, static_cast<sqlite3_int64>(canonical.size()));
        const int step_result{ sqlite3_step(statement) };
        sqlite3_finalize(statement);
        if (step_result != SQLITE_DONE)
        {
            error = sqlite3_errmsg(_database);
            return result;
        }

        std::vector<rom_library_entry_t> current_entries{ entries(system, error) };
        for (rom_library_entry_t& entry : current_entries)
        {
            if (entry.content_hash == hash)
            {
                result.entry = std::move(entry);
                result.newly_imported = newly_imported;
                return result;
            }
        }
        if (error.empty())
            error = "Imported ROM was not found in the library index";
        return result;
    }

    std::vector<rom_library_entry_t> rom_library_t::entries(frontend::system_id_t system,
                                                            std::string& error) const noexcept
    {
        std::vector<rom_library_entry_t> result{};
        if (_database == nullptr)
        {
            error = "ROM library is not initialized";
            return result;
        }
        sqlite3_stmt* statement{ nullptr };
        static constexpr const char* query{
            "SELECT id,content_hash,display_name,original_filename,relative_path,size_bytes"
            " FROM roms WHERE system=? ORDER BY display_name COLLATE NOCASE, id;"
        };
        if (sqlite3_prepare_v2(_database, query, -1, &statement, nullptr) != SQLITE_OK)
        {
            error = sqlite3_errmsg(_database);
            return result;
        }
        sqlite3_bind_text(statement, 1, system_name(system), -1, SQLITE_STATIC);
        int step_result{ SQLITE_OK };
        while ((step_result = sqlite3_step(statement)) == SQLITE_ROW)
        {
            rom_library_entry_t entry{};
            entry.id = sqlite3_column_int64(statement, 0);
            entry.system = system;
            entry.content_hash = reinterpret_cast<const char*>(sqlite3_column_text(statement, 1));
            entry.display_name = reinterpret_cast<const char*>(sqlite3_column_text(statement, 2));
            entry.original_filename = reinterpret_cast<const char*>(sqlite3_column_text(statement, 3));
            entry.rom_path = _data_root / "library"
                / reinterpret_cast<const char*>(sqlite3_column_text(statement, 4));
            entry.size_bytes = static_cast<uint64_t>(sqlite3_column_int64(statement, 5));
            result.push_back(std::move(entry));
        }
        if (step_result != SQLITE_DONE)
            error = sqlite3_errmsg(_database);
        sqlite3_finalize(statement);
        return result;
    }

    std::string rom_library_t::identify(frontend::system_id_t system,
                                        std::span<const std::byte> media) const noexcept
    {
        return frontend::media_identity(system, media);
    }

    std::filesystem::path rom_library_t::save_path(frontend::system_id_t system,
                                                   std::string_view content_hash) const
    {
        return _data_root / "saves" / system_name(system) / content_hash / "battery.srm";
    }

    bool rom_library_t::migrate_sibling_save(const std::filesystem::path& rom_path,
                                             const std::filesystem::path& central_save_path,
                                             size_t expected_size,
                                             std::string& error) const noexcept
    {
        std::error_code filesystem_error{};
        const bool central_save_exists{
            std::filesystem::exists(central_save_path, filesystem_error)
        };
        if (filesystem_error)
        {
            error = "Unable to inspect central save: " + filesystem_error.message();
            return false;
        }
        if (central_save_exists)
            return true;
        std::filesystem::path sibling_path{ rom_path };
        sibling_path.replace_extension(".srm");
        if (!std::filesystem::exists(sibling_path, filesystem_error))
        {
            if (filesystem_error)
                error = "Unable to inspect sibling save: " + filesystem_error.message();
            return !filesystem_error;
        }
        std::ifstream input{ sibling_path, std::ios::binary };
        const std::vector<char> raw{
            std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{}
        };
        if (raw.size() != expected_size)
        {
            error = "Sibling save has the wrong size: " + sibling_path.string();
            return false;
        }
        std::vector<std::byte> bytes(raw.size());
        std::memcpy(bytes.data(), raw.data(), raw.size());
        return write_atomic(central_save_path, bytes, error);
    }

    const std::filesystem::path& rom_library_t::data_root() const noexcept
    {
        return _data_root;
    }

    const char* rom_library_t::system_name(frontend::system_id_t system) noexcept
    {
        switch (system)
        {
        case frontend::system_id_t::snes:
            return "snes";
        }
        return "unknown";
    }

    bool rom_library_t::write_atomic(const std::filesystem::path& path,
                                     std::span<const std::byte> data,
                                     std::string& error) const noexcept
    {
        return utils::write_binary_file_atomic(path, data, error);
    }
}
