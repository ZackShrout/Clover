//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    using retro_environment_t = bool (*)(unsigned, void*);
    using retro_video_refresh_t = void (*)(const void*, unsigned, unsigned, size_t);
    using retro_audio_sample_t = void (*)(int16_t, int16_t);
    using retro_audio_sample_batch_t = size_t (*)(const int16_t*, size_t);
    using retro_input_poll_t = void (*)();
    using retro_input_state_t = int16_t (*)(unsigned, unsigned, unsigned, unsigned);

    struct retro_game_info
    {
        const char* path;
        const void* data;
        size_t size;
        const char* meta;
    };

    struct retro_variable
    {
        const char* key;
        const char* value;
    };

    struct retro_system_timing
    {
        double fps;
        double sample_rate;
    };

    struct retro_game_geometry
    {
        unsigned base_width;
        unsigned base_height;
        unsigned max_width;
        unsigned max_height;
        float aspect_ratio;
    };

    struct retro_system_av_info
    {
        retro_game_geometry geometry;
        retro_system_timing timing;
    };

    enum : unsigned
    {
        RETRO_ENVIRONMENT_SET_PIXEL_FORMAT = 10,
        RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS = 11,
        RETRO_ENVIRONMENT_SET_KEYBOARD_CALLBACK = 12,
        RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE = 13,
        RETRO_ENVIRONMENT_SET_HW_RENDER = 14,
        RETRO_ENVIRONMENT_GET_VARIABLE = 15,
        RETRO_ENVIRONMENT_SET_VARIABLES = 16,
        RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE = 17,
        RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME = 18,
        RETRO_ENVIRONMENT_GET_LOG_INTERFACE = 27,
        RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY = 9,
        RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO = 34,
        RETRO_ENVIRONMENT_SET_CONTROLLER_INFO = 35,
        RETRO_ENVIRONMENT_SET_GEOMETRY = 37,
        RETRO_ENVIRONMENT_GET_FASTFORWARDING = 49
    };

    enum : unsigned
    {
        RETRO_PIXEL_FORMAT_0RGB1555 = 0,
        RETRO_PIXEL_FORMAT_XRGB8888 = 1,
        RETRO_PIXEL_FORMAT_RGB565 = 2
    };

    using retro_init_t = void (*)();
    using retro_deinit_t = void (*)();
    using retro_set_environment_t = void (*)(retro_environment_t);
    using retro_set_video_refresh_t = void (*)(retro_video_refresh_t);
    using retro_set_audio_sample_t_fn = void (*)(retro_audio_sample_t);
    using retro_set_audio_sample_batch_t_fn = void (*)(retro_audio_sample_batch_t);
    using retro_set_input_poll_t_fn = void (*)(retro_input_poll_t);
    using retro_set_input_state_t_fn = void (*)(retro_input_state_t);
    using retro_get_system_av_info_t = void (*)(retro_system_av_info*);
    using retro_load_game_t = bool (*)(const retro_game_info*);
    using retro_run_t = void (*)();
    using retro_unload_game_t = void (*)();
    using retro_reset_t = void (*)();

    struct frame_capture_t
    {
        std::vector<uint32_t> pixels{};
        unsigned width{ 0 };
        unsigned height{ 0 };
        size_t pitch_bytes{ 0 };
        uint64_t frame_index{ 0 };
    };

    struct libretro_state_t
    {
        std::unordered_map<std::string, std::string> variables{
            { "bsnes_aspect_ratio", "8:7" },
            { "bsnes_blur_emulation", "OFF" },
            { "bsnes_hotfixes", "OFF" },
            { "bsnes_entropy", "Low" },
            { "bsnes_ppu_fast", "OFF" },
            { "bsnes_ppu_deinterlace", "OFF" },
            { "bsnes_ppu_no_sprite_limit", "OFF" },
            { "bsnes_ppu_no_vram_blocking", "OFF" },
            { "bsnes_ppu_show_overscan", "ON" },
            { "bsnes_mode7_scale", "1x" },
            { "bsnes_mode7_perspective", "OFF" },
            { "bsnes_mode7_supersample", "OFF" },
            { "bsnes_mode7_mosaic", "OFF" },
            { "bsnes_dsp_fast", "OFF" },
            { "bsnes_dsp_cubic", "OFF" },
            { "bsnes_dsp_echo_shadow", "OFF" },
            { "bsnes_coprocessor_delayed_sync", "OFF" },
            { "bsnes_coprocessor_prefer_hle", "OFF" },
            { "bsnes_run_ahead_frames", "OFF" },
            { "bsnes_video_filter", "None" }
        };
        std::filesystem::path system_directory{ std::filesystem::current_path() };
        std::string system_directory_string{ system_directory.string() };
        frame_capture_t latest_frame{};
        retro_system_av_info geometry{};
        unsigned pixel_format{ RETRO_PIXEL_FORMAT_XRGB8888 };
    };

    libretro_state_t* g_state{ nullptr };

    void print_usage(const char* executable)
    {
        std::fprintf(stderr,
                     "Usage: %s <rom-path> [frames] [dump-dir] [dump-count] [dump-start-frame] [core-path]\n"
                     "Example: %s roms/local/Super\\ Mario\\ World\\ \\(USA\\).sfc 125 bsnes-dumps 3 120 /Users/zshrout/dev/bsnes/bsnes/out/bsnes_libretro.dylib\n",
                     executable,
                     executable);
    }

    [[nodiscard]] bool write_frame_ppm(const std::filesystem::path& path, const frame_capture_t& frame)
    {
        if (frame.pixels.empty() || frame.width == 0 || frame.height == 0)
            return false;

        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;

        output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
        for (uint32_t pixel : frame.pixels)
        {
            const unsigned char red{ static_cast<unsigned char>((pixel >> 16u) & 0xffu) };
            const unsigned char green{ static_cast<unsigned char>((pixel >> 8u) & 0xffu) };
            const unsigned char blue{ static_cast<unsigned char>(pixel & 0xffu) };
            output.write(reinterpret_cast<const char*>(&red), 1);
            output.write(reinterpret_cast<const char*>(&green), 1);
            output.write(reinterpret_cast<const char*>(&blue), 1);
        }

        return static_cast<bool>(output);
    }

    [[nodiscard]] bool can_losslessly_downsample_2x(const frame_capture_t& frame)
    {
        if (frame.pixels.empty() || (frame.width % 2u) != 0u || (frame.height % 2u) != 0u)
            return false;

        for (unsigned y{ 0 }; y < frame.height; y += 2u)
        {
            for (unsigned x{ 0 }; x < frame.width; x += 2u)
            {
                const size_t top_left_index{ static_cast<size_t>(y) * frame.width + x };
                const uint32_t top_left{ frame.pixels[top_left_index] };
                const uint32_t top_right{ frame.pixels[top_left_index + 1u] };
                const uint32_t bottom_left{ frame.pixels[top_left_index + frame.width] };
                const uint32_t bottom_right{ frame.pixels[top_left_index + frame.width + 1u] };
                if (!(top_left == top_right && top_left == bottom_left && top_left == bottom_right))
                    return false;
            }
        }

        return true;
    }

    [[nodiscard]] frame_capture_t maybe_normalize_frame(frame_capture_t frame)
    {
        if (!can_losslessly_downsample_2x(frame))
            return frame;

        frame_capture_t normalized{};
        normalized.width = frame.width / 2u;
        normalized.height = frame.height / 2u;
        normalized.pitch_bytes = static_cast<size_t>(normalized.width) * sizeof(uint32_t);
        normalized.frame_index = frame.frame_index;
        normalized.pixels.resize(static_cast<size_t>(normalized.width) * normalized.height);

        for (unsigned y{ 0 }; y < normalized.height; ++y)
        {
            for (unsigned x{ 0 }; x < normalized.width; ++x)
            {
                normalized.pixels[static_cast<size_t>(y) * normalized.width + x] =
                    frame.pixels[static_cast<size_t>(y * 2u) * frame.width + (x * 2u)];
            }
        }

        return normalized;
    }

    [[nodiscard]] void* load_symbol(void* handle, const char* symbol_name)
    {
        if (void* symbol{ dlsym(handle, symbol_name) })
            return symbol;

        std::fprintf(stderr, "Missing libretro symbol: %s\n", symbol_name);
        return nullptr;
    }

    bool environment_callback(unsigned command, void* data)
    {
        if (!g_state)
            return false;

        switch (command)
        {
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            g_state->pixel_format = *static_cast<const unsigned*>(data);
            return g_state->pixel_format == RETRO_PIXEL_FORMAT_XRGB8888;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
        {
            auto* variable{ static_cast<retro_variable*>(data) };
            if (!variable || !variable->key)
                return false;

            const auto found{ g_state->variables.find(variable->key) };
            if (found == g_state->variables.end())
                return false;

            variable->value = found->second.c_str();
            return true;
        }
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = g_state->system_directory_string.c_str();
            return true;
        case RETRO_ENVIRONMENT_SET_GEOMETRY:
            g_state->geometry = *static_cast<const retro_system_av_info*>(data);
            return true;
        case RETRO_ENVIRONMENT_GET_FASTFORWARDING:
            *static_cast<bool*>(data) = false;
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            return false;
        default:
            return false;
        }
    }

    void video_refresh_callback(const void* data, unsigned width, unsigned height, size_t pitch)
    {
        if (!g_state || !data || width == 0 || height == 0)
            return;

        const auto* bytes{ static_cast<const uint8_t*>(data) };
        g_state->latest_frame.width = width;
        g_state->latest_frame.height = height;
        g_state->latest_frame.pitch_bytes = pitch;
        g_state->latest_frame.pixels.resize(static_cast<size_t>(width) * static_cast<size_t>(height));

        for (unsigned y{ 0 }; y < height; ++y)
        {
            const auto* row{ reinterpret_cast<const uint32_t*>(bytes + static_cast<size_t>(y) * pitch) };
            auto* out{ g_state->latest_frame.pixels.data() + static_cast<size_t>(y) * width };
            std::memcpy(out, row, static_cast<size_t>(width) * sizeof(uint32_t));
        }

        ++g_state->latest_frame.frame_index;
        g_state->latest_frame = maybe_normalize_frame(std::move(g_state->latest_frame));
    }

    void audio_sample_callback(int16_t, int16_t)
    {
    }

    size_t audio_sample_batch_callback(const int16_t*, size_t frames)
    {
        return frames;
    }

    void input_poll_callback()
    {
    }

    int16_t input_state_callback(unsigned, unsigned, unsigned, unsigned)
    {
        return 0;
    }
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 7)
    {
        print_usage(argv[0]);
        return 1;
    }

    const std::filesystem::path rom_path{ argv[1] };
    uint64_t target_frames{ 125 };
    if (argc >= 3)
    {
        target_frames = std::strtoull(argv[2], nullptr, 10);
        if (target_frames == 0)
            target_frames = 1;
    }

    std::filesystem::path dump_directory{};
    if (argc >= 4)
        dump_directory = argv[3];

    uint64_t dump_count{ 0 };
    if (argc >= 5)
        dump_count = std::strtoull(argv[4], nullptr, 10);

    uint64_t dump_start_frame{ 1 };
    if (argc >= 6)
    {
        dump_start_frame = std::strtoull(argv[5], nullptr, 10);
        if (dump_start_frame == 0)
            dump_start_frame = 1;
    }

    std::filesystem::path core_path{ "/Users/zshrout/dev/bsnes/bsnes/out/bsnes_libretro.dylib" };
    if (argc >= 7)
        core_path = argv[6];

    if (!std::filesystem::exists(rom_path))
    {
        std::fprintf(stderr, "ROM not found: %s\n", rom_path.string().c_str());
        return 1;
    }

    if (!std::filesystem::exists(core_path))
    {
        std::fprintf(stderr, "bsnes core not found: %s\n", core_path.string().c_str());
        return 1;
    }

    const bool dump_frames{ !dump_directory.empty() && dump_count > 0 };
    if (dump_frames)
    {
        std::error_code error{};
        std::filesystem::create_directories(dump_directory, error);
        if (error)
        {
            std::fprintf(stderr, "Failed to create dump directory: %s\n", dump_directory.string().c_str());
            return 1;
        }
    }

    std::filesystem::path trace_path{};
    if (dump_frames && dump_count == 1 && dump_start_frame == target_frames)
    {
        trace_path = dump_directory / ("trace_frame_" + std::to_string(target_frames) + ".txt");
        const std::string trace_frame{ std::to_string(target_frames) };
        const std::string trace_file{ trace_path.string() };
        setenv("CLOVER_BSNES_TRACE_FRAME", trace_frame.c_str(), 1);
        setenv("CLOVER_BSNES_TRACE_FILE", trace_file.c_str(), 1);
    }

    libretro_state_t state{};
    g_state = &state;

    void* handle{ dlopen(core_path.c_str(), RTLD_LAZY) };
    if (!handle)
    {
        std::fprintf(stderr, "Failed to load bsnes core: %s\n", dlerror());
        return 1;
    }

    const auto retro_init{ reinterpret_cast<retro_init_t>(load_symbol(handle, "retro_init")) };
    const auto retro_deinit{ reinterpret_cast<retro_deinit_t>(load_symbol(handle, "retro_deinit")) };
    const auto retro_set_environment{ reinterpret_cast<retro_set_environment_t>(load_symbol(handle, "retro_set_environment")) };
    const auto retro_set_video_refresh{ reinterpret_cast<retro_set_video_refresh_t>(load_symbol(handle, "retro_set_video_refresh")) };
    const auto retro_set_audio_sample{ reinterpret_cast<retro_set_audio_sample_t_fn>(load_symbol(handle, "retro_set_audio_sample")) };
    const auto retro_set_audio_sample_batch{ reinterpret_cast<retro_set_audio_sample_batch_t_fn>(load_symbol(handle, "retro_set_audio_sample_batch")) };
    const auto retro_set_input_poll{ reinterpret_cast<retro_set_input_poll_t_fn>(load_symbol(handle, "retro_set_input_poll")) };
    const auto retro_set_input_state{ reinterpret_cast<retro_set_input_state_t_fn>(load_symbol(handle, "retro_set_input_state")) };
    const auto retro_get_system_av_info{ reinterpret_cast<retro_get_system_av_info_t>(load_symbol(handle, "retro_get_system_av_info")) };
    const auto retro_load_game{ reinterpret_cast<retro_load_game_t>(load_symbol(handle, "retro_load_game")) };
    const auto retro_run{ reinterpret_cast<retro_run_t>(load_symbol(handle, "retro_run")) };
    const auto retro_unload_game{ reinterpret_cast<retro_unload_game_t>(load_symbol(handle, "retro_unload_game")) };
    const auto retro_reset{ reinterpret_cast<retro_reset_t>(load_symbol(handle, "retro_reset")) };
    if (!retro_init || !retro_deinit || !retro_set_environment || !retro_set_video_refresh || !retro_set_audio_sample
        || !retro_set_audio_sample_batch || !retro_set_input_poll || !retro_set_input_state || !retro_get_system_av_info
        || !retro_load_game || !retro_run || !retro_unload_game || !retro_reset)
    {
        dlclose(handle);
        return 1;
    }

    retro_set_environment(environment_callback);
    retro_set_video_refresh(video_refresh_callback);
    retro_set_audio_sample(audio_sample_callback);
    retro_set_audio_sample_batch(audio_sample_batch_callback);
    retro_set_input_poll(input_poll_callback);
    retro_set_input_state(input_state_callback);
    retro_init();

    const retro_game_info game_info{
        .path = rom_path.c_str(),
        .data = nullptr,
        .size = 0,
        .meta = nullptr
    };
    if (!retro_load_game(&game_info))
    {
        std::fprintf(stderr, "bsnes failed to load ROM: %s\n", rom_path.string().c_str());
        retro_deinit();
        dlclose(handle);
        return 1;
    }

    retro_reset();
    retro_get_system_av_info(&state.geometry);

    uint64_t dumped_frames{ 0 };
    for (uint64_t frame{ 0 }; frame < target_frames; ++frame)
    {
        retro_run();
        const uint64_t frame_number{ frame + 1 };
        if (dump_frames && frame_number >= dump_start_frame && dumped_frames < dump_count)
        {
            const std::filesystem::path frame_path{
                dump_directory / ("frame_" + std::to_string(frame_number) + ".ppm")
            };
            if (!write_frame_ppm(frame_path, state.latest_frame))
            {
                std::fprintf(stderr, "Failed to write bsnes frame dump: %s\n", frame_path.string().c_str());
                retro_unload_game();
                retro_deinit();
                dlclose(handle);
                return 1;
            }
            ++dumped_frames;
        }
    }

    std::printf("bsnes run: frames_completed=%llu width=%u height=%u pitch=%zu pixel_format=%u fps=%.6f\n",
                static_cast<unsigned long long>(target_frames),
                state.latest_frame.width,
                state.latest_frame.height,
                state.latest_frame.pitch_bytes,
                state.pixel_format,
                state.geometry.timing.fps);
    if (dump_frames)
    {
        std::printf("Frame dumps: directory=%s dumped=%llu start_frame=%llu\n",
                    dump_directory.string().c_str(),
                    static_cast<unsigned long long>(dumped_frames),
                    static_cast<unsigned long long>(dump_start_frame));
        if (!trace_path.empty())
            std::printf("Trace dump: %s\n", trace_path.string().c_str());
    }

    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    return 0;
}
