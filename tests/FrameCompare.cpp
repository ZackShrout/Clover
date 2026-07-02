//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace
{
    struct ppm_image_t
    {
        uint32_t width{ 0 };
        uint32_t height{ 0 };
        std::vector<uint8_t> pixels{};
    };

    struct compare_summary_t
    {
        uint64_t differing_pixels{ 0 };
        uint64_t differing_channels{ 0 };
        uint8_t max_channel_delta{ 0 };
        uint64_t total_channel_delta{ 0 };
        uint32_t first_diff_x{ 0 };
        uint32_t first_diff_y{ 0 };
        bool has_difference{ false };
    };

    void print_usage(const char* executable)
    {
        std::fprintf(stderr,
                     "Usage: %s <expected.ppm> <actual.ppm>\n"
                     "Example: %s bsnes-dumps/frame_120.ppm clover-dumps/frame_120.ppm\n",
                     executable,
                     executable);
    }

    [[nodiscard]] bool read_token(std::ifstream& input, std::string& token)
    {
        token.clear();

        char ch{};
        while (input.get(ch))
        {
            if (ch == '#')
            {
                input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                continue;
            }

            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                continue;

            token.push_back(ch);
            break;
        }

        if (token.empty())
            return false;

        while (input.get(ch))
        {
            if (ch == '#')
            {
                input.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
                break;
            }

            if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n')
                break;

            token.push_back(ch);
        }

        return true;
    }

    [[nodiscard]] bool read_ppm(const std::filesystem::path& path, ppm_image_t& image)
    {
        std::ifstream input{ path, std::ios::binary };
        if (!input)
            return false;

        std::string token{};
        if (!read_token(input, token) || token != "P6")
            return false;
        if (!read_token(input, token))
            return false;
        image.width = static_cast<uint32_t>(std::stoul(token));
        if (!read_token(input, token))
            return false;
        image.height = static_cast<uint32_t>(std::stoul(token));
        if (!read_token(input, token))
            return false;

        const unsigned long max_value{ std::stoul(token) };
        if (max_value != 255)
            return false;

        image.pixels.resize(static_cast<size_t>(image.width) * image.height * 3u);
        input.read(reinterpret_cast<char*>(image.pixels.data()), static_cast<std::streamsize>(image.pixels.size()));
        return input.good() || input.eof();
    }

    [[nodiscard]] compare_summary_t compare_images(const ppm_image_t& expected, const ppm_image_t& actual)
    {
        compare_summary_t summary{};
        const size_t pixel_count{ static_cast<size_t>(expected.width) * expected.height };
        for (size_t pixel_index{ 0 }; pixel_index < pixel_count; ++pixel_index)
        {
            bool pixel_differs{ false };
            for (size_t channel{ 0 }; channel < 3u; ++channel)
            {
                const size_t byte_index{ pixel_index * 3u + channel };
                const uint8_t expected_value{ expected.pixels[byte_index] };
                const uint8_t actual_value{ actual.pixels[byte_index] };
                if (expected_value == actual_value)
                    continue;

                const uint8_t delta{
                    static_cast<uint8_t>(expected_value > actual_value
                                             ? expected_value - actual_value
                                             : actual_value - expected_value)
                };
                summary.differing_channels += 1u;
                summary.total_channel_delta += delta;
                summary.max_channel_delta = std::max(summary.max_channel_delta, delta);
                pixel_differs = true;
            }

            if (!pixel_differs)
                continue;

            if (!summary.has_difference)
            {
                summary.has_difference = true;
                summary.first_diff_x = static_cast<uint32_t>(pixel_index % expected.width);
                summary.first_diff_y = static_cast<uint32_t>(pixel_index / expected.width);
            }

            summary.differing_pixels += 1u;
        }

        return summary;
    }
}

int main(int argc, char** argv)
{
    if (argc != 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    const std::filesystem::path expected_path{ argv[1] };
    const std::filesystem::path actual_path{ argv[2] };

    ppm_image_t expected{};
    ppm_image_t actual{};
    if (!read_ppm(expected_path, expected))
    {
        std::fprintf(stderr, "Failed to read PPM: %s\n", expected_path.string().c_str());
        return 1;
    }
    if (!read_ppm(actual_path, actual))
    {
        std::fprintf(stderr, "Failed to read PPM: %s\n", actual_path.string().c_str());
        return 1;
    }

    if (expected.width != actual.width || expected.height != actual.height)
    {
        std::fprintf(stderr,
                     "Frame size mismatch: expected=%ux%u actual=%ux%u\n",
                     expected.width,
                     expected.height,
                     actual.width,
                     actual.height);
        return 1;
    }

    const compare_summary_t summary{ compare_images(expected, actual) };
    std::printf("Frame compare: size=%ux%u differing_pixels=%llu differing_channels=%llu max_channel_delta=%u average_channel_delta=%.4f\n",
                expected.width,
                expected.height,
                static_cast<unsigned long long>(summary.differing_pixels),
                static_cast<unsigned long long>(summary.differing_channels),
                summary.max_channel_delta,
                summary.differing_channels == 0
                    ? 0.0
                    : static_cast<double>(summary.total_channel_delta) / static_cast<double>(summary.differing_channels));
    if (summary.has_difference)
    {
        std::printf("First difference: x=%u y=%u\n", summary.first_diff_x, summary.first_diff_y);
        return 2;
    }

    std::printf("Frames match exactly.\n");
    return 0;
}
