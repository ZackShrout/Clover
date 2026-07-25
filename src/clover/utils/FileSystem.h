//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace clover::utils
{
    // Returns a UTF-8 representation suitable for cross-platform C APIs such
    // as SDL and SQLite. std::filesystem::path::string() is not UTF-8 on
    // Windows and must not be used for those APIs.
    [[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);
    [[nodiscard]] std::filesystem::path path_from_utf8(std::string_view path);

    [[nodiscard]] std::vector<std::byte> read_binary_file(
        const std::filesystem::path& path
    ) noexcept;

    // Writes through a sibling temporary file and atomically replaces the
    // destination. Windows and POSIX require different replacement calls.
    [[nodiscard]] bool write_binary_file_atomic(
        const std::filesystem::path& path,
        std::span<const std::byte> data,
        std::string& error
    ) noexcept;
}
