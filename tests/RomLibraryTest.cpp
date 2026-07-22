//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/RomLibrary.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] bool write_bytes(const std::filesystem::path& path,
                                   std::span<const std::byte> bytes)
    {
        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        output.write(reinterpret_cast<const char*>(bytes.data()),
                     static_cast<std::streamsize>(bytes.size()));
        return static_cast<bool>(output);
    }
}

int main()
{
    using clover::frontend::system_id_t;
    using clover::platform::rom_library_t;

    const auto unique{ std::chrono::steady_clock::now().time_since_epoch().count() };
    const std::filesystem::path root{
        std::filesystem::temp_directory_path() / ("clover-rom-library-test-" + std::to_string(unique))
    };
    std::error_code filesystem_error{};
    std::filesystem::create_directories(root, filesystem_error);
    if (filesystem_error)
        return 1;

    rom_library_t library{};
    std::string error{};
    if (!library.initialize(root / "data", error))
        return 2;

    const std::array<std::byte, 3> abc{
        std::byte{ 'a' }, std::byte{ 'b' }, std::byte{ 'c' }
    };
    if (library.identify(system_id_t::snes, abc)
        != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad")
    {
        return 3;
    }

    std::vector<std::byte> canonical(1024u);
    for (size_t index{ 0 }; index < canonical.size(); ++index)
        canonical[index] = static_cast<std::byte>(index & 0xffu);
    std::vector<std::byte> headered(512u, std::byte{ 0x5a });
    headered.insert(headered.end(), canonical.begin(), canonical.end());
    const std::filesystem::path first_source{ root / "Example Game.sfc" };
    const std::filesystem::path second_source{ root / "Renamed Game.smc" };
    if (!write_bytes(first_source, canonical) || !write_bytes(second_source, headered))
        return 4;

    const auto first{ library.import_rom(system_id_t::snes, first_source, canonical, error) };
    if (!error.empty() || !first.newly_imported || !std::filesystem::exists(first.entry.rom_path))
        return 5;
    const auto second{ library.import_rom(system_id_t::snes, second_source, headered, error) };
    if (!error.empty() || second.newly_imported
        || second.entry.content_hash != first.entry.content_hash)
    {
        return 6;
    }
    const auto entries{ library.entries(system_id_t::snes, error) };
    if (!error.empty() || entries.size() != 1u || entries.front().display_name != "Renamed Game")
        return 7;
    if (std::filesystem::file_size(entries.front().rom_path) != canonical.size())
        return 8;

    const std::filesystem::path sibling_save{ root / "Example Game.srm" };
    const std::vector<std::byte> save(32u, std::byte{ 0xa5 });
    if (!write_bytes(sibling_save, save))
        return 9;
    const std::filesystem::path central_save{
        library.save_path(system_id_t::snes, first.entry.content_hash)
    };
    if (!library.migrate_sibling_save(first_source, central_save, save.size(), error)
        || !error.empty() || std::filesystem::file_size(central_save) != save.size())
    {
        return 10;
    }

    const std::vector<std::byte> newer_sibling(save.size(), std::byte{ 0x3c });
    if (!write_bytes(sibling_save, newer_sibling)
        || !library.migrate_sibling_save(first_source, central_save, save.size(), error))
    {
        return 11;
    }
    std::ifstream central_input{ central_save, std::ios::binary };
    const std::vector<char> central_bytes{
        std::istreambuf_iterator<char>{ central_input }, std::istreambuf_iterator<char>{}
    };
    if (central_bytes.empty() || static_cast<unsigned char>(central_bytes.front()) != 0xa5u)
        return 12;

    library.shutdown();
    if (!library.initialize(root / "data", error)
        || library.entries(system_id_t::snes, error).size() != 1u)
    {
        return 13;
    }
    library.shutdown();
    std::filesystem::remove_all(root, filesystem_error);
    return filesystem_error ? 14 : 0;
}
