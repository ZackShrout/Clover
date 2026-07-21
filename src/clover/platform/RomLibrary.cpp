//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/RomLibrary.h"

#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <sstream>
#include <system_error>

#include <sqlite3.h>

namespace clover::platform
{
    namespace
    {
        constexpr std::array<uint32_t, 64> k_sha256_round_constants{
            0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u,
            0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
            0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
            0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
            0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu,
            0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
            0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u,
            0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
            0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
            0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
            0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u,
            0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
            0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u,
            0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
            0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
            0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
        };

        [[nodiscard]] std::array<uint8_t, 32> sha256(std::span<const std::byte> data) noexcept
        {
            std::array<uint32_t, 8> state{
                0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
            };
            const uint64_t bit_count{ static_cast<uint64_t>(data.size()) * 8u };
            const size_t padded_size{ ((data.size() + 9u + 63u) / 64u) * 64u };
            std::vector<uint8_t> padded(padded_size, 0u);
            for (size_t index{ 0 }; index < data.size(); ++index)
                padded[index] = static_cast<uint8_t>(data[index]);
            padded[data.size()] = 0x80u;
            for (uint8_t index{ 0 }; index < 8u; ++index)
                padded[padded_size - 1u - index] = static_cast<uint8_t>(bit_count >> (index * 8u));

            for (size_t block{ 0 }; block < padded.size(); block += 64u)
            {
                std::array<uint32_t, 64> words{};
                for (size_t index{ 0 }; index < 16u; ++index)
                {
                    const size_t offset{ block + index * 4u };
                    words[index] = (static_cast<uint32_t>(padded[offset]) << 24u)
                        | (static_cast<uint32_t>(padded[offset + 1u]) << 16u)
                        | (static_cast<uint32_t>(padded[offset + 2u]) << 8u)
                        | padded[offset + 3u];
                }
                for (size_t index{ 16u }; index < words.size(); ++index)
                {
                    const uint32_t s0{ std::rotr(words[index - 15u], 7)
                        ^ std::rotr(words[index - 15u], 18) ^ (words[index - 15u] >> 3u) };
                    const uint32_t s1{ std::rotr(words[index - 2u], 17)
                        ^ std::rotr(words[index - 2u], 19) ^ (words[index - 2u] >> 10u) };
                    words[index] = words[index - 16u] + s0 + words[index - 7u] + s1;
                }

                uint32_t a{ state[0] };
                uint32_t b{ state[1] };
                uint32_t c{ state[2] };
                uint32_t d{ state[3] };
                uint32_t e{ state[4] };
                uint32_t f{ state[5] };
                uint32_t g{ state[6] };
                uint32_t h{ state[7] };
                for (size_t index{ 0 }; index < words.size(); ++index)
                {
                    const uint32_t sum1{ std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25) };
                    const uint32_t choice{ (e & f) ^ (~e & g) };
                    const uint32_t temporary1{
                        h + sum1 + choice + k_sha256_round_constants[index] + words[index]
                    };
                    const uint32_t sum0{ std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22) };
                    const uint32_t majority{ (a & b) ^ (a & c) ^ (b & c) };
                    const uint32_t temporary2{ sum0 + majority };
                    h = g;
                    g = f;
                    f = e;
                    e = d + temporary1;
                    d = c;
                    c = b;
                    b = a;
                    a = temporary1 + temporary2;
                }
                state[0] += a;
                state[1] += b;
                state[2] += c;
                state[3] += d;
                state[4] += e;
                state[5] += f;
                state[6] += g;
                state[7] += h;
            }

            std::array<uint8_t, 32> result{};
            for (size_t index{ 0 }; index < state.size(); ++index)
            {
                result[index * 4u] = static_cast<uint8_t>(state[index] >> 24u);
                result[index * 4u + 1u] = static_cast<uint8_t>(state[index] >> 16u);
                result[index * 4u + 2u] = static_cast<uint8_t>(state[index] >> 8u);
                result[index * 4u + 3u] = static_cast<uint8_t>(state[index]);
            }
            return result;
        }

        [[nodiscard]] std::string sha256_hex(std::span<const std::byte> data)
        {
            const std::array<uint8_t, 32> digest{ sha256(data) };
            std::ostringstream output{};
            output << std::hex << std::setfill('0');
            for (const uint8_t byte : digest)
                output << std::setw(2) << static_cast<unsigned>(byte);
            return output.str();
        }

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
        if (sqlite3_open(database_path.string().c_str(), &_database) != SQLITE_OK)
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
        const std::span<const std::byte> canonical{ canonical_media(system, media) };
        const std::string hash{ sha256_hex(canonical) };
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
        const std::string source_filename{ source_path.filename().string() };
        const std::string display_name{ source_path.stem().string() };
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
        return sha256_hex(canonical_media(system, media));
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

    std::span<const std::byte> rom_library_t::canonical_media(frontend::system_id_t system,
                                                             std::span<const std::byte> media) noexcept
    {
        if (system == frontend::system_id_t::snes
            && media.size() >= 512u
            && (media.size() % 1024u) == 512u)
        {
            return media.subspan(512u);
        }
        return media;
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
        std::error_code filesystem_error{};
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "Unable to create directory for " + path.string() + ": "
                + filesystem_error.message();
            return false;
        }
        std::filesystem::path temporary{ path };
        temporary += ".tmp";
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output)
        {
            error = "Unable to write temporary file: " + temporary.string();
            return false;
        }
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            std::error_code cleanup_error{};
            std::filesystem::remove(temporary, cleanup_error);
            error = "Unable to replace " + path.string() + ": " + filesystem_error.message();
            return false;
        }
        return true;
    }
}
