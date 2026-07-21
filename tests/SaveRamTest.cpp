//
// Created by Zack Shrout on 7/21/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Cartridge.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> make_lorom_with_save_ram()
    {
        std::vector<std::byte> rom(0x8000u, std::byte{ 0 });
        constexpr size_t header{ 0x7fc0u };
        rom[header + 0x15u] = std::byte{ 0x20u };
        rom[header + 0x16u] = std::byte{ 0x02u };
        rom[header + 0x18u] = std::byte{ 0x03u };
        rom[header + 0x1cu] = std::byte{ 0xcbu };
        rom[header + 0x1du] = std::byte{ 0xedu };
        rom[header + 0x1eu] = std::byte{ 0x34u };
        rom[header + 0x1fu] = std::byte{ 0x12u };
        rom[header + 0x3cu] = std::byte{ 0x00u };
        rom[header + 0x3du] = std::byte{ 0x80u };
        return rom;
    }

    [[nodiscard]] int fail(const char* checkpoint)
    {
        std::fprintf(stderr, "SaveRamTest failed at %s\n", checkpoint);
        return 1;
    }
}

int main()
{
    clover::core::cartridge_t cartridge{};
    const std::vector<std::byte> rom{ make_lorom_with_save_ram() };
    if (!cartridge.load(rom))
        return fail("load_cartridge");

    if (cartridge.persistent_memory().size() != 8u * 1024u)
        return fail("save_ram_size");
    if (cartridge.persistent_memory_dirty())
        return fail("clean_after_load");

    cartridge.write_u8(0x700123u, 0x5au);
    if (cartridge.read_u8(0x700123u) != 0x5au
        || cartridge.read_u8(0xf00123u) != 0x5au
        || !cartridge.persistent_memory_dirty())
    {
        return fail("write_and_mirror");
    }

    cartridge.mark_persistent_memory_clean();
    cartridge.write_u8(0x700123u, 0x5au);
    if (cartridge.persistent_memory_dirty())
        return fail("same_value_stays_clean");

    std::array<std::byte, 8u * 1024u> restored{};
    restored.fill(std::byte{ 0xa5u });
    if (!cartridge.load_persistent_memory(restored)
        || cartridge.read_u8(0x700123u) != 0xa5u
        || cartridge.persistent_memory_dirty())
    {
        return fail("restore_save_ram");
    }

    const std::array<std::byte, 1> wrong_size{ std::byte{ 0x00u } };
    if (cartridge.load_persistent_memory(wrong_size)
        || cartridge.read_u8(0x700123u) != 0xa5u)
    {
        return fail("reject_wrong_size");
    }

    cartridge.reset();
    if (cartridge.read_u8(0x700123u) != 0xa5u)
        return fail("reset_preserves_save_ram");

    return 0;
}
