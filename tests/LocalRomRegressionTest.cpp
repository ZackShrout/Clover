//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/core/snes/Console.h"

#include <algorithm>
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
    struct regression_result_t
    {
        std::filesystem::path rom_path{};
        uint64_t frames_completed{ 0 };
        uint64_t target_frames{ 0 };
        uint64_t steps{ 0 };
        bool terminal_pc_detected{ false };
        bool read_failed{ false };
        bool load_failed{ false };
        bool step_limit_hit{ false };
        bool frame_target_hit{ false };
        bool apu_halted{ false };
        uint8_t apu_last_opcode{ 0 };
        uint8_t terminal_pb{ 0 };
        uint16_t terminal_pc{ 0 };
    };

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

    [[nodiscard]] bool is_supported_rom_path(const std::filesystem::path& path)
    {
        if (!std::filesystem::is_regular_file(path))
            return false;

        const std::string extension{ path.extension().string() };
        return extension == ".sfc" || extension == ".smc";
    }

    [[nodiscard]] std::vector<std::filesystem::path> collect_local_roms(const std::filesystem::path& directory)
    {
        std::vector<std::filesystem::path> roms{};
        if (!std::filesystem::exists(directory))
            return roms;

        for (const auto& entry : std::filesystem::directory_iterator{ directory })
        {
            if (!is_supported_rom_path(entry.path()))
                continue;

            roms.push_back(entry.path());
        }

        std::sort(roms.begin(), roms.end());
        return roms;
    }

    [[nodiscard]] regression_result_t run_regression(const std::filesystem::path& rom_path,
                                                     uint64_t target_frames,
                                                     uint64_t step_limit)
    {
        regression_result_t result{};
        result.rom_path = rom_path;
        result.target_frames = target_frames;

        const std::vector<std::byte> rom_bytes{ read_file_bytes(rom_path) };
        if (rom_bytes.empty())
        {
            result.read_failed = true;
            return result;
        }

        clover::core::console_t console{};
        if (!console.load_cartridge(rom_bytes))
        {
            result.load_failed = true;
            return result;
        }

        console.power_on();

        while (result.steps < step_limit && result.frames_completed < target_frames)
        {
            const clover::core::hardware_step_result_t step{ console.step_hardware() };
            ++result.steps;
            result.frames_completed += step.ppu.frames_completed;

            const clover::core::cpu_state_t& cpu{ console.cpu_state() };
            if (cpu.pb == 0x00u && cpu.pc == 0xffffu)
            {
                result.terminal_pc_detected = true;
                result.terminal_pb = cpu.pb;
                result.terminal_pc = cpu.pc;
                break;
            }
        }

        result.step_limit_hit = result.steps >= step_limit;
        result.frame_target_hit = result.frames_completed >= target_frames;
        const clover::core::apu_state_t apu{ console.apu_state() };
        result.apu_halted = apu.halted;
        result.apu_last_opcode = apu.last_opcode;
        return result;
    }

    void print_result(const regression_result_t& result)
    {
        std::printf("Local ROM regression: rom=%s frames=%llu/%llu steps=%llu terminal_pc=%u apu_halted=%u\n",
                    result.rom_path.string().c_str(),
                    static_cast<unsigned long long>(result.frames_completed),
                    static_cast<unsigned long long>(result.target_frames),
                    static_cast<unsigned long long>(result.steps),
                    result.terminal_pc_detected ? 1u : 0u,
                    result.apu_halted ? 1u : 0u);
    }
}

int main(int argc, char** argv)
{
    std::filesystem::path rom_path{};
    uint64_t target_frames{ 800u };
    uint64_t step_limit{ 20'000'000u };

    if (argc >= 2)
        rom_path = argv[1];

    if (argc >= 3)
        target_frames = std::strtoull(argv[2], nullptr, 10);

    if (argc >= 4)
        step_limit = std::strtoull(argv[3], nullptr, 10);

    std::vector<std::filesystem::path> rom_paths{};
    if (!rom_path.empty())
    {
        if (!std::filesystem::exists(rom_path))
        {
            std::printf("Local ROM regression skipped: missing %s\n", rom_path.string().c_str());
            return 0;
        }

        rom_paths.push_back(rom_path);
    }
    else
    {
        rom_paths = collect_local_roms("roms/local");
        if (rom_paths.empty())
        {
            std::printf("Local ROM regression skipped: no supported ROMs in roms/local\n");
            return 0;
        }
    }

    bool had_failure{ false };
    for (const auto& current_rom_path : rom_paths)
    {
        const regression_result_t result{ run_regression(current_rom_path, target_frames, step_limit) };
        print_result(result);

        if (result.read_failed)
        {
            std::fprintf(stderr, "Local ROM regression failed: unable to read %s\n", current_rom_path.string().c_str());
            had_failure = true;
            continue;
        }

        if (result.load_failed)
        {
            std::fprintf(stderr, "Local ROM regression failed: console rejected %s\n", current_rom_path.string().c_str());
            had_failure = true;
            continue;
        }

        if (result.terminal_pc_detected)
        {
            std::fprintf(stderr,
                         "Local ROM regression failed: terminal PC at PB:%02x PC:%04x for %s\n",
                         result.terminal_pb,
                         result.terminal_pc,
                         current_rom_path.string().c_str());
            had_failure = true;
            continue;
        }

        if (result.apu_halted)
        {
            std::fprintf(stderr,
                         "Local ROM regression failed: APU halted on opcode %02x for %s\n",
                         result.apu_last_opcode,
                         current_rom_path.string().c_str());
            had_failure = true;
            continue;
        }

        if (result.step_limit_hit)
        {
            std::fprintf(stderr,
                         "Local ROM regression failed: step limit hit before frame target for %s\n",
                         current_rom_path.string().c_str());
            had_failure = true;
            continue;
        }

        if (!result.frame_target_hit)
        {
            std::fprintf(stderr,
                         "Local ROM regression failed: only reached %llu frames for %s\n",
                         static_cast<unsigned long long>(result.frames_completed),
                         current_rom_path.string().c_str());
            had_failure = true;
        }
    }

    return had_failure ? 1 : 0;
}
