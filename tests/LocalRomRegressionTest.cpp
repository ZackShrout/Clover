//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/Console.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
    [[nodiscard]] std::vector<std::byte> read_file_bytes(const std::filesystem::path& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return {};

        const std::vector<char> raw{ std::istreambuf_iterator<char>{ input }, std::istreambuf_iterator<char>{} };
        std::vector<std::byte> bytes(raw.size());
        for (std::size_t index{ 0 }; index < raw.size(); ++index)
            bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(raw[index]));
        return bytes;
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path rom_path{ "roms/local/Super Mario World (USA).sfc" };
    uint64_t target_frames{ 300u };
    uint64_t step_limit{ 10'000'000u };

    if (argc >= 2)
        rom_path = argv[1];

    if (argc >= 3)
        target_frames = std::strtoull(argv[2], nullptr, 10);

    if (argc >= 4)
        step_limit = std::strtoull(argv[3], nullptr, 10);

    if (!std::filesystem::exists(rom_path))
    {
        std::printf("Local ROM regression skipped: missing %s\n", rom_path.string().c_str());
        return 0;
    }

    const std::vector<std::byte> rom_bytes{ read_file_bytes(rom_path) };
    if (rom_bytes.empty())
    {
        std::fprintf(stderr, "Local ROM regression failed: unable to read %s\n", rom_path.string().c_str());
        return 1;
    }

    clover::core::console_t console{};
    if (!console.load_cartridge(rom_bytes))
    {
        std::fprintf(stderr, "Local ROM regression failed: console rejected %s\n", rom_path.string().c_str());
        return 1;
    }

    console.power_on();

    uint64_t frames_completed{ 0 };
    uint64_t steps{ 0 };
    bool terminal_pc_detected{ false };
    while (steps < step_limit && frames_completed < target_frames)
    {
        const clover::core::hardware_step_result_t step{ console.step_hardware() };
        ++steps;
        frames_completed += step.ppu.frame_complete ? 1u : 0u;

        const clover::core::cpu_state_t& cpu{ console.cpu_state() };
        if (cpu.pb == 0x00u && cpu.pc == 0xffffu)
        {
            terminal_pc_detected = true;
            break;
        }
    }

    std::printf("Local ROM regression: rom=%s frames=%llu/%llu steps=%llu terminal_pc=%u\n",
                rom_path.string().c_str(),
                static_cast<unsigned long long>(frames_completed),
                static_cast<unsigned long long>(target_frames),
                static_cast<unsigned long long>(steps),
                terminal_pc_detected ? 1u : 0u);

    if (terminal_pc_detected)
    {
        const clover::core::cpu_state_t& cpu{ console.cpu_state() };
        std::fprintf(stderr, "Local ROM regression failed: terminal PC at PB:%02x PC:%04x\n", cpu.pb, cpu.pc);
        return 1;
    }

    if (steps >= step_limit)
    {
        std::fprintf(stderr, "Local ROM regression failed: step limit hit before frame target\n");
        return 1;
    }

    if (frames_completed < target_frames)
    {
        std::fprintf(stderr, "Local ROM regression failed: only reached %llu frames\n",
                     static_cast<unsigned long long>(frames_completed));
        return 1;
    }

    return 0;
}
