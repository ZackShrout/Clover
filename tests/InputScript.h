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
    struct joypad_input_range_t
    {
        uint64_t first_frame{ 0 };
        uint64_t last_frame{ 0 };
        uint16_t state{ 0 };
    };

    class joypad_input_script_t
    {
    public:
        [[nodiscard]] static joypad_input_script_t from_environment() noexcept
        {
            joypad_input_script_t result{};
            std::string file_contents{};
            const char* raw{ std::getenv("CLOVER_JOYPAD1_SCRIPT") };
            if (const char* const path = std::getenv("CLOVER_JOYPAD1_SCRIPT_FILE"); path != nullptr)
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
