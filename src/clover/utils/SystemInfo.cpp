//
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/utils/SystemInfo.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string_view>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
#include <cpuid.h>
#endif

#if defined(__APPLE__) && !(defined(__x86_64__) || defined(__i386__))
#include <sys/sysctl.h>
#endif

namespace clover::utils
{
    namespace
    {
        [[nodiscard]] std::string trim(std::string value)
        {
            const auto is_space{
                [](unsigned char character) { return std::isspace(character) != 0; }
            };
            value.erase(value.begin(),
                        std::find_if_not(value.begin(), value.end(), is_space));
            value.erase(std::find_if_not(value.rbegin(), value.rend(), is_space).base(),
                        value.end());
            return value;
        }

        [[nodiscard]] std::string x86_cpu_brand() noexcept
        {
            std::array<char, 49> brand{};
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
            std::array<int, 4> registers{};
            __cpuid(registers.data(), static_cast<int>(0x80000000u));
            if (static_cast<uint32_t>(registers[0]) < 0x80000004u)
                return {};
            for (uint32_t leaf{ 0x80000002u }; leaf <= 0x80000004u; ++leaf)
            {
                __cpuid(registers.data(), static_cast<int>(leaf));
                std::memcpy(brand.data() + (leaf - 0x80000002u) * 16u,
                            registers.data(),
                            16u);
            }
#elif (defined(__x86_64__) || defined(__i386__)) && defined(__GNUC__)
            if (__get_cpuid_max(0x80000000u, nullptr) < 0x80000004u)
                return {};
            for (uint32_t leaf{ 0x80000002u }; leaf <= 0x80000004u; ++leaf)
            {
                std::array<unsigned int, 4> registers{};
                __get_cpuid(leaf,
                            &registers[0],
                            &registers[1],
                            &registers[2],
                            &registers[3]);
                std::memcpy(brand.data() + (leaf - 0x80000002u) * 16u,
                            registers.data(),
                            16u);
            }
#endif
            return trim(brand.data());
        }
    }

    std::string cpu_brand() noexcept
    {
        try
        {
            if (std::string brand{ x86_cpu_brand() }; !brand.empty())
                return brand;

#if defined(__APPLE__) && !(defined(__x86_64__) || defined(__i386__))
            std::array<char, 256> value{};
            size_t size{ value.size() };
            if (sysctlbyname("machdep.cpu.brand_string",
                             value.data(),
                             &size,
                             nullptr,
                             0) == 0)
            {
                return trim(value.data());
            }
            size = value.size();
            if (sysctlbyname("hw.model", value.data(), &size, nullptr, 0) == 0)
                return trim(value.data());
#elif defined(__linux__)
            std::ifstream input{ "/proc/cpuinfo" };
            std::string line{};
            while (std::getline(input, line))
            {
                const size_t separator{ line.find(':') };
                if (separator == std::string::npos)
                    continue;
                const std::string_view name{ line.data(), separator };
                if (name.find("model name") != std::string_view::npos
                    || name.find("Hardware") != std::string_view::npos)
                {
                    return trim(line.substr(separator + 1u));
                }
            }
#endif
        }
        catch (...)
        {
        }
        return "unknown";
    }
}
