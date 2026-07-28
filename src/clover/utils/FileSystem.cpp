//
// Created by Zack Shrout on 7/25/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/utils/FileSystem.h"

#include <cstring>
#include <fstream>
#include <iterator>
#include <system_error>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace clover::utils
{
    std::string path_to_utf8(const std::filesystem::path& path)
    {
        const std::u8string encoded{ path.generic_u8string() };
        return {
            reinterpret_cast<const char*>(encoded.data()),
            encoded.size()
        };
    }

    std::filesystem::path path_from_utf8(std::string_view path)
    {
        const std::u8string encoded{
            reinterpret_cast<const char8_t*>(path.data()),
            path.size()
        };
        return std::filesystem::path{ encoded };
    }

    std::string path_to_file_url(const std::filesystem::path& path)
    {
        const std::string encoded_path{ path_to_utf8(path) };
        std::string result{ "file://" };
        if (encoded_path.empty() || encoded_path.front() != '/')
            result.push_back('/');

        static constexpr char hex[]{ "0123456789ABCDEF" };
        for (const unsigned char character : encoded_path)
        {
            const bool unreserved{
                (character >= 'a' && character <= 'z')
                    || (character >= 'A' && character <= 'Z')
                    || (character >= '0' && character <= '9')
                    || character == '-' || character == '.' || character == '_'
                    || character == '~' || character == '/' || character == ':'
            };
            if (unreserved)
            {
                result.push_back(static_cast<char>(character));
            }
            else
            {
                result.push_back('%');
                result.push_back(hex[character >> 4u]);
                result.push_back(hex[character & 0x0fu]);
            }
        }
        return result;
    }

    std::vector<std::byte> read_binary_file(const std::filesystem::path& path) noexcept
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return {};

        const std::vector<char> raw{
            std::istreambuf_iterator<char>{ input },
            std::istreambuf_iterator<char>{}
        };
        std::vector<std::byte> bytes(raw.size());
        if (!raw.empty())
            std::memcpy(bytes.data(), raw.data(), raw.size());
        return bytes;
    }

    bool write_binary_file_atomic(const std::filesystem::path& path,
                                  std::span<const std::byte> data,
                                  std::string& error) noexcept
    {
        std::error_code filesystem_error{};
        std::filesystem::create_directories(path.parent_path(), filesystem_error);
        if (filesystem_error)
        {
            error = "Unable to create directory for " + path_to_utf8(path)
                + ": " + filesystem_error.message();
            return false;
        }

        std::filesystem::path temporary{ path };
        temporary += ".tmp";
        std::ofstream output{ temporary, std::ios::binary | std::ios::trunc };
        if (!output)
        {
            error = "Unable to open temporary file: " + path_to_utf8(temporary);
            return false;
        }
        output.write(reinterpret_cast<const char*>(data.data()),
                     static_cast<std::streamsize>(data.size()));
        output.close();
        if (!output)
        {
            error = "Unable to write temporary file: " + path_to_utf8(temporary);
            return false;
        }

#if defined(_WIN32)
        if (!MoveFileExW(temporary.c_str(),
                         path.c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            const DWORD native_error{ GetLastError() };
            std::error_code cleanup_error{};
            std::filesystem::remove(temporary, cleanup_error);
            error = "Unable to replace " + path_to_utf8(path)
                + ": Windows error " + std::to_string(native_error);
            return false;
        }
#else
        std::filesystem::rename(temporary, path, filesystem_error);
        if (filesystem_error)
        {
            std::error_code cleanup_error{};
            std::filesystem::remove(temporary, cleanup_error);
            error = "Unable to replace " + path_to_utf8(path)
                + ": " + filesystem_error.message();
            return false;
        }
#endif
        return true;
    }
}
