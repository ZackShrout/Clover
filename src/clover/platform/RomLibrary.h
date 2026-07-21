//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/EmulatorCore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace clover::platform
{
    struct rom_library_entry_t
    {
        int64_t id{ 0 };
        frontend::system_id_t system{ frontend::system_id_t::snes };
        std::string content_hash{};
        std::string display_name{};
        std::string original_filename{};
        std::filesystem::path rom_path{};
        uint64_t size_bytes{ 0u };
    };

    struct rom_import_result_t
    {
        rom_library_entry_t entry{};
        bool newly_imported{ false };
    };

    class rom_library_t
    {
    public:
        rom_library_t() = default;
        ~rom_library_t();

        rom_library_t(const rom_library_t&) = delete;
        rom_library_t& operator=(const rom_library_t&) = delete;

        [[nodiscard]] bool initialize(const std::filesystem::path& data_root,
                                      std::string& error) noexcept;
        void shutdown() noexcept;

        [[nodiscard]] rom_import_result_t import_rom(frontend::system_id_t system,
                                                     const std::filesystem::path& source_path,
                                                     std::span<const std::byte> media,
                                                     std::string& error) noexcept;
        [[nodiscard]] std::vector<rom_library_entry_t> entries(frontend::system_id_t system,
                                                               std::string& error) const noexcept;

        [[nodiscard]] std::string identify(frontend::system_id_t system,
                                           std::span<const std::byte> media) const noexcept;
        [[nodiscard]] std::filesystem::path save_path(frontend::system_id_t system,
                                                      std::string_view content_hash) const;
        [[nodiscard]] bool migrate_sibling_save(const std::filesystem::path& rom_path,
                                                const std::filesystem::path& central_save_path,
                                                size_t expected_size,
                                                std::string& error) const noexcept;

        [[nodiscard]] const std::filesystem::path& data_root() const noexcept;

    private:
        [[nodiscard]] static std::span<const std::byte> canonical_media(
            frontend::system_id_t system,
            std::span<const std::byte> media
        ) noexcept;
        [[nodiscard]] static const char* system_name(frontend::system_id_t system) noexcept;
        [[nodiscard]] bool write_atomic(const std::filesystem::path& path,
                                        std::span<const std::byte> data,
                                        std::string& error) const noexcept;

        sqlite3* _database{ nullptr };
        std::filesystem::path _data_root{};
    };
}
