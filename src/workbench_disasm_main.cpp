//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/analysis/snes/Formatter.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using clover::analysis::snes::bit_state_t;

    struct command_line_t
    {
        std::filesystem::path rom_path{};
        uint32_t address{ 0 };
        size_t count{ 64 };
        size_t byte_limit{ 1024 };
        clover::analysis::snes::cpu_decode_context_t context{};
        bool address_set{ false };
        bool json_lines{ false };
    };

    void print_usage()
    {
        std::fprintf(
            stderr,
            "Usage: clover_workbench_disasm <rom> --address BB:AAAA "
            "[--count N] [--bytes N] [--e 0|1|?] [--m 0|1|?] [--x 0|1|?] "
            "[--d HHHH] [--db HH] "
            "[--jsonl]\n"
        );
    }

    [[nodiscard]] bool parse_hex_address(std::string value, uint32_t& address)
    {
        if (!value.empty() && value.front() == '$')
            value.erase(value.begin());
        if (value.starts_with("0x") || value.starts_with("0X"))
            value.erase(0, 2);
        const size_t colon{ value.find(':') };
        if (colon != std::string::npos)
            value.erase(colon, 1);
        if (value.empty() || value.size() > 6)
            return false;

        uint32_t parsed{};
        const auto result{
            std::from_chars(value.data(), value.data() + value.size(), parsed, 16)
        };
        if (result.ec != std::errc{} || result.ptr != value.data() + value.size())
            return false;
        address = parsed;
        return parsed <= 0x00ffffffu;
    }

    [[nodiscard]] bool parse_size(std::string_view value, size_t& parsed)
    {
        const auto result{
            std::from_chars(value.data(), value.data() + value.size(), parsed, 10)
        };
        return result.ec == std::errc{}
            && result.ptr == value.data() + value.size()
            && parsed != 0;
    }

    [[nodiscard]] bool parse_bit(std::string_view value, bit_state_t& state)
    {
        if (value == "0")
        {
            state = bit_state_t::clear;
            return true;
        }
        if (value == "1")
        {
            state = bit_state_t::set;
            return true;
        }
        if (value == "?")
        {
            state = bit_state_t::unknown;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool parse_arguments(int argc, char** argv, command_line_t& command)
    {
        if (argc < 2)
            return false;
        command.rom_path = argv[1];

        for (int index{ 2 }; index < argc; ++index)
        {
            const std::string_view argument{ argv[index] };
            if (argument == "--jsonl")
            {
                command.json_lines = true;
                continue;
            }
            if (index + 1 >= argc)
                return false;
            const std::string_view value{ argv[++index] };
            if (argument == "--address")
            {
                command.address_set = parse_hex_address(std::string{ value }, command.address);
                if (!command.address_set)
                    return false;
            }
            else if (argument == "--count")
            {
                if (!parse_size(value, command.count))
                    return false;
            }
            else if (argument == "--bytes")
            {
                if (!parse_size(value, command.byte_limit))
                    return false;
            }
            else if (argument == "--e")
            {
                if (!parse_bit(value, command.context.emulation))
                    return false;
            }
            else if (argument == "--m")
            {
                if (!parse_bit(value, command.context.accumulator_width))
                    return false;
            }
            else if (argument == "--x")
            {
                if (!parse_bit(value, command.context.index_width))
                    return false;
            }
            else if (argument == "--d")
            {
                uint32_t direct_page{};
                if (!parse_hex_address(std::string{ value }, direct_page)
                    || direct_page > 0xffffu)
                {
                    return false;
                }
                command.context.direct_page = static_cast<uint16_t>(direct_page);
            }
            else if (argument == "--db")
            {
                uint32_t data_bank{};
                if (!parse_hex_address(std::string{ value }, data_bank)
                    || data_bank > 0xffu)
                {
                    return false;
                }
                command.context.data_bank = static_cast<uint8_t>(data_bank);
            }
            else
            {
                return false;
            }
        }
        return command.address_set;
    }

    [[nodiscard]] std::vector<std::byte> read_file(const std::filesystem::path& path)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return {};
        const std::vector<char> raw{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{}
        };
        std::vector<std::byte> bytes(raw.size());
        for (size_t index{ 0 }; index < raw.size(); ++index)
            bytes[index] = static_cast<std::byte>(static_cast<unsigned char>(raw[index]));
        return bytes;
    }

    [[nodiscard]] std::string formatted_address(uint32_t address)
    {
        std::ostringstream output{};
        output << '$' << std::uppercase << std::hex << std::setfill('0')
               << std::setw(2) << ((address >> 16u) & 0xffu)
               << ':' << std::setw(4) << (address & 0xffffu);
        return output.str();
    }

    [[nodiscard]] std::string formatted_bytes(
        const clover::analysis::snes::decoded_instruction_t& instruction
    )
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0');
        for (uint8_t index{ 0 }; index < instruction.byte_count; ++index)
        {
            if (index != 0)
                output << ' ';
            output << std::setw(2) << static_cast<uint32_t>(instruction.bytes[index]);
        }
        return output.str();
    }
}

int main(int argc, char** argv)
{
    command_line_t command{};
    if (!parse_arguments(argc, argv, command))
    {
        print_usage();
        return 2;
    }

    const std::vector<std::byte> media{ read_file(command.rom_path) };
    if (media.empty())
    {
        std::fprintf(stderr, "Unable to read ROM: %s\n", command.rom_path.string().c_str());
        return 1;
    }

    std::unique_ptr<clover::frontend::emulator_core_t> core{
        clover::frontend::create_emulator_core(clover::frontend::system_id_t::snes)
    };
    if (core == nullptr || !core->load_media(media))
    {
        std::fprintf(stderr, "ROM was not recognized as supported SNES media.\n");
        return 1;
    }
    const clover::frontend::debug_target_t* target{ core->debug_target() };
    if (target == nullptr)
    {
        std::fprintf(stderr, "SNES debug target is unavailable.\n");
        return 1;
    }

    const clover::analysis::snes::debug_target_byte_source_t source{
        *target,
        clover::frontend::snes_debug::k_cpu_bus_space,
        clover::frontend::snes_debug::k_canonical_media_space
    };
    const clover::analysis::snes::static_listing_result_t listing{
        clover::analysis::snes::build_static_listing(
            source,
            {
                .start_address = command.address,
                .maximum_instructions = command.count,
                .maximum_bytes = command.byte_limit,
                .context = command.context
            }
        )
    };

    if (!command.json_lines)
    {
        std::printf(
            "; Context E=%.*s M=%.*s X=%.*s\n",
            static_cast<int>(clover::analysis::snes::bit_state_name(
                command.context.emulation
            ).size()),
            clover::analysis::snes::bit_state_name(command.context.emulation).data(),
            static_cast<int>(clover::analysis::snes::bit_state_name(
                command.context.accumulator_width
            ).size()),
            clover::analysis::snes::bit_state_name(command.context.accumulator_width).data(),
            static_cast<int>(clover::analysis::snes::bit_state_name(
                command.context.index_width
            ).size()),
            clover::analysis::snes::bit_state_name(command.context.index_width).data()
        );
        if (command.context.direct_page.has_value()
            || command.context.data_bank.has_value())
        {
            std::printf(
                "; Address context D=%s DB=%s\n",
                command.context.direct_page.has_value()
                    ? formatted_address(*command.context.direct_page).substr(4).c_str()
                    : "?",
                command.context.data_bank.has_value()
                    ? formatted_address(
                        static_cast<uint32_t>(*command.context.data_bank) << 16u
                    ).substr(1, 2).c_str()
                    : "?"
            );
        }
    }

    for (const auto& instruction : listing.instructions)
    {
        if (command.json_lines)
        {
            std::printf(
                "%s\n",
                clover::analysis::snes::format_instruction_json(instruction).c_str()
            );
            continue;
        }

        std::printf(
            "%-8s  %-11s  %s\n",
            formatted_address(instruction.address).c_str(),
            formatted_bytes(instruction).c_str(),
            clover::analysis::snes::format_instruction(instruction).c_str()
        );
    }

    if (listing.stop_reason == clover::analysis::snes::listing_stop_reason_t::ambiguous_context)
    {
        std::fprintf(
            stderr,
            "Listing stopped at context-dependent width; specify --e, --m, and --x.\n"
        );
    }
    else if (listing.stop_reason
        == clover::analysis::snes::listing_stop_reason_t::contradictory_context)
    {
        std::fprintf(
            stderr,
            "Listing stopped because emulation mode requires M=1 and X=1.\n"
        );
    }
    else if (listing.stop_reason == clover::analysis::snes::listing_stop_reason_t::unavailable)
    {
        std::fprintf(stderr, "Listing stopped at unavailable or unmapped memory.\n");
    }
    return 0;
}
