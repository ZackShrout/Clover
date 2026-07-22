//
// Created by Zack Shrout on 7/20/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

namespace clover::test
{
    class frame_event_script_t
    {
    public:
        [[nodiscard]] static frame_event_script_t from_environment(const char* name) noexcept
        {
            frame_event_script_t result{};
            const char* const raw{ std::getenv(name) };
            if (raw == nullptr || *raw == '\0')
                return result;

            std::string_view remaining{ raw };
            while (!remaining.empty())
            {
                const size_t separator{ remaining.find(',') };
                const std::string_view entry{ remaining.substr(0, separator) };
                uint64_t frame{ 0 };
                if (!parse_unsigned(entry, frame) || frame == 0u)
                {
                    result._valid = false;
                    return result;
                }
                result._frames.push_back(frame);
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1u);
            }
            return result;
        }

        [[nodiscard]] bool valid() const noexcept { return _valid; }

        [[nodiscard]] bool contains(uint64_t frame) const noexcept
        {
            for (const uint64_t event_frame : _frames)
            {
                if (event_frame == frame)
                    return true;
            }
            return false;
        }

    private:
        [[nodiscard]] static bool parse_unsigned(std::string_view raw, uint64_t& result) noexcept
        {
            if (raw.empty())
                return false;

            result = 0;
            for (const char character : raw)
            {
                if (character < '0' || character > '9')
                    return false;
                const uint8_t digit{ static_cast<uint8_t>(character - '0') };
                if (result > (UINT64_MAX - digit) / 10u)
                    return false;
                result = result * 10u + digit;
            }
            return true;
        }

        std::vector<uint64_t> _frames{};
        bool _valid{ true };
    };

    struct joypad_input_range_t
    {
        uint64_t first_frame{ 0 };
        uint64_t last_frame{ 0 };
        uint16_t state{ 0 };
    };

    class joypad_input_script_t
    {
    public:
        [[nodiscard]] static joypad_input_script_t from_environment(
            const char* script_name = "CLOVER_JOYPAD1_SCRIPT",
            const char* script_file_name = "CLOVER_JOYPAD1_SCRIPT_FILE") noexcept
        {
            joypad_input_script_t result{};
            std::string file_contents{};
            const char* raw{ std::getenv(script_name) };
            if (const char* const path = std::getenv(script_file_name);
                path != nullptr && *path != '\0')
            {
                std::ifstream input{ path, std::ios::binary };
                if (!input)
                {
                    result._valid = false;
                    return result;
                }
                file_contents.assign(std::istreambuf_iterator<char>{ input },
                                     std::istreambuf_iterator<char>{});
                while (!file_contents.empty()
                    && (file_contents.back() == '\n' || file_contents.back() == '\r'))
                {
                    file_contents.pop_back();
                }
                raw = file_contents.c_str();
            }
            if (raw == nullptr || *raw == '\0')
                return result;

            std::string_view remaining{ raw };
            while (!remaining.empty())
            {
                const size_t separator{ remaining.find(',') };
                const std::string_view entry{ remaining.substr(0, separator) };
                const size_t dash{ entry.find('-') };
                const size_t equals{ entry.find('=') };
                if (dash == std::string_view::npos || equals == std::string_view::npos || dash >= equals)
                {
                    result._valid = false;
                    return result;
                }

                uint64_t first_frame{ 0 };
                uint64_t last_frame{ 0 };
                uint64_t state{ 0 };
                if (!parse_unsigned(entry.substr(0, dash), 10u, first_frame)
                    || !parse_unsigned(entry.substr(dash + 1u, equals - dash - 1u), 10u, last_frame)
                    || !parse_unsigned(entry.substr(equals + 1u), 16u, state)
                    || first_frame == 0u
                    || last_frame < first_frame
                    || state > 0xffffu)
                {
                    result._valid = false;
                    return result;
                }

                result._ranges.push_back({
                    .first_frame = first_frame,
                    .last_frame = last_frame,
                    .state = static_cast<uint16_t>(state)
                });
                if (separator == std::string_view::npos)
                    break;
                remaining.remove_prefix(separator + 1u);
            }
            return result;
        }

        [[nodiscard]] bool valid() const noexcept { return _valid; }

        [[nodiscard]] uint16_t state_for_frame(uint64_t frame) const noexcept
        {
            uint16_t state{ 0 };
            for (const joypad_input_range_t& range : _ranges)
            {
                if (frame >= range.first_frame && frame <= range.last_frame)
                    state = static_cast<uint16_t>(state | range.state);
            }
            return state;
        }

    private:
        [[nodiscard]] static bool parse_unsigned(std::string_view raw,
                                                 uint8_t base,
                                                 uint64_t& result) noexcept
        {
            if (raw.empty())
                return false;

            result = 0;
            for (const char character : raw)
            {
                uint8_t digit{ 0 };
                if (character >= '0' && character <= '9')
                    digit = static_cast<uint8_t>(character - '0');
                else if (base == 16u && character >= 'a' && character <= 'f')
                    digit = static_cast<uint8_t>(character - 'a' + 10);
                else if (base == 16u && character >= 'A' && character <= 'F')
                    digit = static_cast<uint8_t>(character - 'A' + 10);
                else
                    return false;

                if (digit >= base || result > (UINT64_MAX - digit) / base)
                    return false;
                result = result * base + digit;
            }
            return true;
        }

        std::vector<joypad_input_range_t> _ranges{};
        bool _valid{ true };
    };
}
