//
// Created by Zack Shrout on 7/2/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include <algorithm>
#include <cstdlib>
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

    struct offset_summary_t
    {
        int32_t offset{ 0 };
        uint64_t exact_matches{ 0 };
        uint64_t mismatches{ 0 };
        uint64_t missing_expected{ 0 };
        uint64_t missing_actual{ 0 };
        uint64_t first_mismatch_expected_frame{ 0 };
        uint64_t first_mismatch_actual_frame{ 0 };
        compare_summary_t first_mismatch{};
        bool recorded_first_mismatch{ false };
    };

    void print_usage(const char* executable)
    {
        std::fprintf(stderr,
                     "Usage: %s <expected_dir> <actual_dir> <start_frame> <end_frame> [min_offset] [max_offset]\n"
                     "Example: %s /tmp/bsnes /tmp/clover 84 100 -1 1\n",
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

    [[nodiscard]] std::filesystem::path frame_path(const std::filesystem::path& directory, uint64_t frame)
    {
        return directory / ("frame_" + std::to_string(frame) + ".ppm");
    }

    void print_offset_summary(const offset_summary_t& summary)
    {
        std::printf("Offset %+d: exact=%llu mismatches=%llu missing_expected=%llu missing_actual=%llu\n",
                    summary.offset,
                    static_cast<unsigned long long>(summary.exact_matches),
                    static_cast<unsigned long long>(summary.mismatches),
                    static_cast<unsigned long long>(summary.missing_expected),
                    static_cast<unsigned long long>(summary.missing_actual));

        if (!summary.recorded_first_mismatch)
            return;

        std::printf("  first mismatch: expected_frame=%llu actual_frame=%llu differing_pixels=%llu differing_channels=%llu max_channel_delta=%u first_diff=(%u,%u)\n",
                    static_cast<unsigned long long>(summary.first_mismatch_expected_frame),
                    static_cast<unsigned long long>(summary.first_mismatch_actual_frame),
                    static_cast<unsigned long long>(summary.first_mismatch.differing_pixels),
                    static_cast<unsigned long long>(summary.first_mismatch.differing_channels),
                    summary.first_mismatch.max_channel_delta,
                    summary.first_mismatch.first_diff_x,
                    summary.first_mismatch.first_diff_y);
    }
}

int main(int argc, char** argv)
{
    if (argc < 5 || argc > 7)
    {
        print_usage(argv[0]);
        return 1;
    }

    const std::filesystem::path expected_dir{ argv[1] };
    const std::filesystem::path actual_dir{ argv[2] };
    const uint64_t start_frame{ std::strtoull(argv[3], nullptr, 10) };
    const uint64_t end_frame{ std::strtoull(argv[4], nullptr, 10) };
    if (start_frame == 0 || end_frame < start_frame)
    {
        std::fprintf(stderr, "Invalid frame range: start=%llu end=%llu\n",
                     static_cast<unsigned long long>(start_frame),
                     static_cast<unsigned long long>(end_frame));
        return 1;
    }

    int32_t min_offset{ 0 };
    int32_t max_offset{ 0 };
    if (argc >= 6)
        min_offset = static_cast<int32_t>(std::strtol(argv[5], nullptr, 10));
    if (argc >= 7)
        max_offset = static_cast<int32_t>(std::strtol(argv[6], nullptr, 10));
    else
        max_offset = min_offset;

    if (min_offset > max_offset)
    {
        std::fprintf(stderr, "Invalid offset range: min_offset=%d max_offset=%d\n", min_offset, max_offset);
        return 1;
    }

    int32_t best_offset{ 0 };
    bool have_best{ false };
    offset_summary_t best_summary{};
    bool any_failures{ false };
    for (int32_t offset{ min_offset }; offset <= max_offset; ++offset)
    {
        offset_summary_t summary{};
        summary.offset = offset;

        for (uint64_t expected_frame{ start_frame }; expected_frame <= end_frame; ++expected_frame)
        {
            const int64_t actual_frame_signed{ static_cast<int64_t>(expected_frame) + static_cast<int64_t>(offset) };
            if (actual_frame_signed <= 0)
            {
                ++summary.missing_actual;
                continue;
            }

            const uint64_t actual_frame{ static_cast<uint64_t>(actual_frame_signed) };
            const std::filesystem::path expected_path{ frame_path(expected_dir, expected_frame) };
            const std::filesystem::path actual_path{ frame_path(actual_dir, actual_frame) };

            if (!std::filesystem::exists(expected_path))
            {
                ++summary.missing_expected;
                continue;
            }

            if (!std::filesystem::exists(actual_path))
            {
                ++summary.missing_actual;
                continue;
            }

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
                             "Frame size mismatch: expected_frame=%llu actual_frame=%llu expected=%ux%u actual=%ux%u\n",
                             static_cast<unsigned long long>(expected_frame),
                             static_cast<unsigned long long>(actual_frame),
                             expected.width,
                             expected.height,
                             actual.width,
                             actual.height);
                return 1;
            }

            const compare_summary_t compare{ compare_images(expected, actual) };
            if (!compare.has_difference)
            {
                ++summary.exact_matches;
                continue;
            }

            ++summary.mismatches;
            if (!summary.recorded_first_mismatch)
            {
                summary.recorded_first_mismatch = true;
                summary.first_mismatch_expected_frame = expected_frame;
                summary.first_mismatch_actual_frame = actual_frame;
                summary.first_mismatch = compare;
            }
        }

        print_offset_summary(summary);
        const uint64_t summary_failures{
            summary.mismatches + summary.missing_expected + summary.missing_actual
        };
        if (summary_failures != 0)
            any_failures = true;

        if (!have_best)
        {
            have_best = true;
            best_offset = offset;
            best_summary = summary;
            continue;
        }

        const uint64_t best_failures{
            best_summary.mismatches + best_summary.missing_expected + best_summary.missing_actual
        };
        if (summary_failures < best_failures
            || (summary_failures == best_failures && summary.exact_matches > best_summary.exact_matches))
        {
            best_offset = offset;
            best_summary = summary;
        }
    }

    std::printf("Best offset: %+d\n", best_offset);
    if (!any_failures)
        std::printf("All compared frames matched exactly.\n");

    const uint64_t best_failures{
        best_summary.mismatches + best_summary.missing_expected + best_summary.missing_actual
    };
    return best_failures == 0 ? 0 : 2;
}
