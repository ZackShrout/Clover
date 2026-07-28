//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/utils/FileSystem.h"
#include "clover/utils/SystemInfo.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>

int main()
{
    if (clover::utils::cpu_brand().empty())
        return 11;

    if (clover::utils::path_to_file_url(
            clover::utils::path_from_utf8("/tmp/Clover logs"))
        != "file:///tmp/Clover%20logs")
    {
        return 10;
    }

    const std::filesystem::path directory{
        std::filesystem::temp_directory_path()
            / std::filesystem::path{ u8"clover-file-system-tést" }
    };
    std::error_code cleanup_error{};
    std::filesystem::remove_all(directory, cleanup_error);

    const std::filesystem::path path{ directory / std::filesystem::path{ u8"savé.srm" } };
    const std::array first{ std::byte{ 0x01 }, std::byte{ 0x02 } };
    const std::array replacement{
        std::byte{ 0x10 },
        std::byte{ 0x20 },
        std::byte{ 0x30 }
    };
    std::string error{};
    if (!clover::utils::write_binary_file_atomic(path, first, error)
        || !clover::utils::write_binary_file_atomic(path, replacement, error))
    {
        std::fprintf(stderr, "%s\n", error.c_str());
        return 1;
    }

    const std::vector<std::byte> loaded{ clover::utils::read_binary_file(path) };
    if (loaded.size() != replacement.size()
        || !std::equal(loaded.begin(), loaded.end(), replacement.begin()))
    {
        std::fprintf(stderr, "Atomic replacement returned the wrong bytes\n");
        return 1;
    }

    const std::string encoded{ clover::utils::path_to_utf8(path) };
    if (clover::utils::path_from_utf8(encoded) != path)
    {
        std::fprintf(stderr, "UTF-8 path conversion did not round-trip\n");
        return 1;
    }

    std::filesystem::remove_all(directory, cleanup_error);
    return 0;
}
