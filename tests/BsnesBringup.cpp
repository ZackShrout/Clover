//
// Created by Zack Shrout on 6/30/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "InputScript.h"

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
        RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY = 31,
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
    using retro_serialize_size_t = size_t (*)();
    using retro_serialize_t = bool (*)(void*, size_t);
    using retro_unserialize_t = bool (*)(const void*, size_t);
    using retro_get_memory_data_t = void* (*)(unsigned);
    using retro_get_memory_size_t = size_t (*)(unsigned);

    constexpr unsigned k_retro_memory_system_ram{ 2u };

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
            { "bsnes_entropy", "None" },
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
        std::filesystem::path save_directory{};
        std::string save_directory_string{};
        frame_capture_t latest_frame{};
        retro_system_av_info geometry{};
        unsigned pixel_format{ RETRO_PIXEL_FORMAT_XRGB8888 };
    };

    libretro_state_t* g_state{ nullptr };
    std::array<clover::test::joypad_input_script_t, 2> g_input_scripts{};
    uint64_t g_input_frame{ 0 };
    bool g_capture_audio{ false };
    std::vector<int16_t> g_audio_samples{};

    struct temporary_directory_t
    {
        std::filesystem::path path{};

        ~temporary_directory_t()
        {
            if (path.empty())
                return;
            std::error_code ignored{};
            std::filesystem::remove_all(path, ignored);
        }
    };

    [[nodiscard]] bool stage_save_ram(const std::filesystem::path& source,
                                      const std::filesystem::path& rom_path,
                                      temporary_directory_t& stage,
                                      std::string& error_message)
    {
        if (!std::filesystem::is_regular_file(source))
        {
            error_message = "Save RAM file not found: " + source.string();
            return false;
        }

        const auto nonce{ std::chrono::steady_clock::now().time_since_epoch().count() };
        for (uint32_t attempt{ 0 }; attempt < 100u && stage.path.empty(); ++attempt)
        {
            const std::filesystem::path candidate{
                std::filesystem::temp_directory_path()
                    / ("clover-bsnes-save-" + std::to_string(nonce) + "-" + std::to_string(attempt))
            };
            std::error_code directory_error{};
            if (std::filesystem::create_directory(candidate, directory_error))
                stage.path = candidate;
        }
        if (stage.path.empty())
        {
            error_message = "Unable to create a private bsnes save directory";
            return false;
        }

        const std::filesystem::path destination{ stage.path / (rom_path.stem().string() + ".srm") };
        std::error_code copy_error{};
        std::filesystem::copy_file(source,
                                   destination,
                                   std::filesystem::copy_options::none,
                                   copy_error);
        if (copy_error)
        {
            error_message = "Unable to stage save RAM: " + copy_error.message();
            return false;
        }
        return true;
    }

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

    [[nodiscard]] bool write_binary_file(const std::filesystem::path& path,
                                         const void* data,
                                         size_t size)
    {
        if (data == nullptr)
            return false;
        std::ofstream output{ path, std::ios::binary };
        if (!output)
            return false;
        output.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
        return static_cast<bool>(output);
    }

    [[nodiscard]] bool write_audio_wav(const std::filesystem::path& path,
                                       const std::vector<int16_t>& samples,
                                       uint32_t sample_rate)
    {
        const size_t sample_bytes{ samples.size() * sizeof(int16_t) };
        if ((samples.size() & 1u) != 0u || sample_bytes > 0xffff'ffffu - 36u)
            return false;

        std::ofstream output{ path, std::ios::binary | std::ios::trunc };
        if (!output)
            return false;

        const auto write_u16 = [&output](uint16_t value) {
            const std::array<uint8_t, 2> bytes{
                static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8u)
            };
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        const auto write_u32 = [&output](uint32_t value) {
            const std::array<uint8_t, 4> bytes{
                static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8u),
                static_cast<uint8_t>(value >> 16u), static_cast<uint8_t>(value >> 24u)
            };
            output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        };
        const uint32_t data_bytes{ static_cast<uint32_t>(sample_bytes) };
        output.write("RIFF", 4);
        write_u32(36u + data_bytes);
        output.write("WAVEfmt ", 8);
        write_u32(16u);
        write_u16(1u);
        write_u16(2u);
        write_u32(sample_rate);
        write_u32(sample_rate * 4u);
        write_u16(4u);
        write_u16(16u);
        output.write("data", 4);
        write_u32(data_bytes);
        output.write(reinterpret_cast<const char*>(samples.data()), data_bytes);
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

    struct serializer_cursor_t
    {
        std::vector<uint8_t>& bytes;
        size_t offset{ 0 };

        void skip(size_t size) noexcept
        {
            offset += size;
        }

        void write_u8(uint8_t value) noexcept
        {
            bytes[offset++] = value;
        }

        void write_u16(uint16_t value) noexcept
        {
            bytes[offset++] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset++] = static_cast<uint8_t>((value >> 8u) & 0xffu);
        }

        void write_u32(uint32_t value) noexcept
        {
            bytes[offset++] = static_cast<uint8_t>(value & 0xffu);
            bytes[offset++] = static_cast<uint8_t>((value >> 8u) & 0xffu);
            bytes[offset++] = static_cast<uint8_t>((value >> 16u) & 0xffu);
            bytes[offset++] = static_cast<uint8_t>((value >> 24u) & 0xffu);
        }

        void write_u64(uint64_t value) noexcept
        {
            for (unsigned shift{ 0 }; shift < 64u; shift += 8u)
                bytes[offset++] = static_cast<uint8_t>((value >> shift) & 0xffu);
        }

        void patch_wram(size_t wram_base, uint32_t address, uint8_t value) noexcept
        {
            bytes[wram_base + address] = value;
        }
    };

    [[nodiscard]] bool apply_hirq_cli_seeded_patch(std::vector<uint8_t>& state)
    {
        serializer_cursor_t cursor{ state };

        constexpr size_t k_header_size{ 4u + 4u + 16u + 512u + 1u + 1u };
        constexpr size_t k_random_size{ 4u + 8u + 8u };
        constexpr size_t k_cartridge_ram_size{ 0u };
        constexpr size_t k_wram_size{ 128u * 1024u };

        cursor.skip(k_header_size + k_random_size + k_cartridge_ram_size);

        cursor.write_u32(0x000000u);  // PC
        cursor.write_u16(0x0000u);    // A
        cursor.write_u16(0x0000u);    // X
        cursor.write_u16(0x0000u);    // Y
        cursor.write_u16(0x0000u);    // Z
        cursor.write_u16(0x01ffu);    // S
        cursor.write_u16(0x0000u);    // D
        cursor.write_u8(0x00u);       // B

        cursor.write_u8(0u);  // C
        cursor.write_u8(0u);  // Z
        cursor.write_u8(1u);  // I
        cursor.write_u8(0u);  // D
        cursor.write_u8(1u);  // X
        cursor.write_u8(1u);  // M
        cursor.write_u8(0u);  // V
        cursor.write_u8(0u);  // N

        cursor.write_u8(1u);  // E
        cursor.write_u8(0u);  // IRQ pin
        cursor.write_u8(0u);  // WAI
        cursor.write_u8(0u);  // STP

        cursor.write_u16(0xfffcu);    // vector
        cursor.write_u32(0x000000u);  // MAR
        cursor.write_u8(0x00u);       // MDR
        cursor.write_u32(0x000000u);  // U
        cursor.write_u32(0x000000u);  // V
        cursor.write_u32(0x000000u);  // W

        cursor.skip(4u + 8u);         // Thread
        cursor.write_u8(0u);          // interlace
        cursor.write_u8(0u);          // field
        cursor.write_u32(262u);       // vperiod
        cursor.write_u32(1364u);      // hperiod
        cursor.write_u32(0u);         // vcounter
        cursor.write_u32(0u);         // hcounter
        cursor.write_u32(262u);       // last.vperiod
        cursor.write_u32(1364u);      // last.hperiod

        const size_t wram_base{ cursor.offset };
        cursor.patch_wram(wram_base, 0x0000u, 0x58u);
        for (uint32_t address{ 0x0001u }; address <= 0x00007fu; ++address)
            cursor.patch_wram(wram_base, address, 0xeau);
        cursor.patch_wram(wram_base, 0x1234u, 0x40u);
        cursor.skip(k_wram_size);

        cursor.skip(4u);               // version
        cursor.skip(4u);               // counter.cpu
        cursor.skip(4u);               // counter.dma
        cursor.skip(4u);               // status.clockCount
        cursor.write_u8(1u);           // status.irqLock

        cursor.skip(4u);               // dramRefreshPosition
        cursor.skip(4u);               // dramRefresh
        cursor.skip(4u);               // hdmaSetupPosition
        cursor.skip(1u);               // hdmaSetupTriggered
        cursor.skip(4u);               // hdmaPosition
        cursor.skip(1u);               // hdmaTriggered

        cursor.write_u8(0u);           // nmiValid
        cursor.write_u8(0u);           // nmiLine
        cursor.write_u8(0u);           // nmiTransition
        cursor.write_u8(0u);           // nmiPending
        cursor.write_u8(0u);           // nmiHold

        cursor.write_u8(0u);           // irqValid
        cursor.write_u8(0u);           // irqLine
        cursor.write_u8(0u);           // irqTransition
        cursor.write_u8(0u);           // irqPending
        cursor.write_u8(0u);           // irqHold

        cursor.write_u8(0u);           // resetPending
        cursor.write_u8(0u);           // interruptPending
        cursor.write_u8(0u);           // dmaActive
        cursor.write_u8(0u);           // dmaPending
        cursor.write_u8(0u);           // hdmaPending
        cursor.write_u8(0u);           // hdmaMode

        cursor.skip(4u);               // autoJoypadCounter
        cursor.skip(1u);               // autoJoypadPort1
        cursor.skip(1u);               // autoJoypadPort2
        cursor.write_u8(0u);           // cpuLatch
        cursor.write_u8(0u);           // autoJoypadLatch

        cursor.skip(4u);               // io.wramAddress
        cursor.write_u8(1u);           // hirqEnable
        cursor.write_u8(0u);           // virqEnable
        cursor.write_u8(1u);           // irqEnable
        cursor.write_u8(0u);           // nmiEnable
        cursor.write_u8(0u);           // autoJoypadPoll

        cursor.skip(1u);               // pio
        cursor.skip(1u);               // wrmpya
        cursor.skip(1u);               // wrmpyb
        cursor.skip(2u);               // wrdiva
        cursor.skip(1u);               // wrdivb
        cursor.write_u16(0x0044u);     // htime = ($0010 + 1) << 2
        cursor.write_u16(0x01ffu);     // vtime

        return cursor.offset <= state.size();
    }

    struct bsnes_cpu_summary_t
    {
        uint32_t pc{ 0 };
        uint16_t a{ 0 };
        uint16_t x{ 0 };
        uint16_t y{ 0 };
        uint16_t sp{ 0 };
        uint8_t p{ 0 };
        uint8_t emulation{ 0 };
        uint16_t scanline{ 0 };
        uint16_t dot{ 0 };
        uint8_t irq_lock{ 0 };
        uint8_t irq_line{ 0 };
        uint8_t irq_transition{ 0 };
        uint8_t irq_pending{ 0 };
        uint8_t interrupt_pending{ 0 };
        uint8_t hirq_enable{ 0 };
        uint8_t virq_enable{ 0 };
        uint8_t irq_enable{ 0 };
        uint16_t htime{ 0 };
        uint16_t vtime{ 0 };
        uint8_t stp{ 0 };
    };

    [[nodiscard]] std::vector<uint8_t> capture_state_blob(const retro_serialize_size_t retro_serialize_size,
                                                          const retro_serialize_t retro_serialize);
    void print_hirq_seed_probe(const char* label, const std::vector<uint8_t>& state);
    void dump_blob_window(const char* label,
                          const std::vector<uint8_t>& state,
                          size_t start,
                          size_t count);

    struct serializer_reader_t
    {
        const std::vector<uint8_t>& bytes;
        size_t offset{ 0 };

        void skip(size_t size) noexcept
        {
            offset += size;
        }

        [[nodiscard]] uint8_t read_u8() noexcept
        {
            return bytes[offset++];
        }

        [[nodiscard]] uint16_t read_u16() noexcept
        {
            const uint16_t value = static_cast<uint16_t>(
                static_cast<uint16_t>(bytes[offset])
                | static_cast<uint16_t>(static_cast<uint16_t>(bytes[offset + 1u]) << 8u)
            );
            offset += 2u;
            return value;
        }

        [[nodiscard]] uint32_t read_u32() noexcept
        {
            const uint32_t value{
                static_cast<uint32_t>(bytes[offset])
                | (static_cast<uint32_t>(bytes[offset + 1u]) << 8u)
                | (static_cast<uint32_t>(bytes[offset + 2u]) << 16u)
                | (static_cast<uint32_t>(bytes[offset + 3u]) << 24u)
            };
            offset += 4u;
            return value;
        }

        [[nodiscard]] uint64_t read_u64() noexcept
        {
            uint64_t value{ 0 };
            for (unsigned shift{ 0 }; shift < 64u; shift += 8u)
                value |= static_cast<uint64_t>(bytes[offset++]) << shift;
            return value;
        }
    };

    [[nodiscard]] bsnes_cpu_summary_t summarize_state(const std::vector<uint8_t>& state)
    {
        serializer_reader_t cursor{ state };
        bsnes_cpu_summary_t summary{};

        constexpr size_t k_header_size{ 4u + 4u + 16u + 512u + 1u + 1u };
        constexpr size_t k_random_size{ 4u + 8u + 8u };
        constexpr size_t k_cartridge_ram_size{ 0u };
        constexpr size_t k_wram_size{ 128u * 1024u };

        cursor.skip(k_header_size + k_random_size + k_cartridge_ram_size);

        summary.pc = cursor.read_u32();
        summary.a = cursor.read_u16();
        summary.x = cursor.read_u16();
        summary.y = cursor.read_u16();
        cursor.skip(2u);               // Z
        summary.sp = cursor.read_u16();
        cursor.skip(2u);               // D
        cursor.skip(1u);               // B
        const uint8_t c{ cursor.read_u8() };
        const uint8_t z{ cursor.read_u8() };
        const uint8_t i{ cursor.read_u8() };
        const uint8_t d{ cursor.read_u8() };
        const uint8_t xf{ cursor.read_u8() };
        const uint8_t mf{ cursor.read_u8() };
        const uint8_t v{ cursor.read_u8() };
        const uint8_t n{ cursor.read_u8() };
        summary.p = static_cast<uint8_t>(
            (c << 0u) | (z << 1u) | (i << 2u) | (d << 3u)
            | (xf << 4u) | (mf << 5u) | (v << 6u) | (n << 7u)
        );
        summary.emulation = cursor.read_u8();
        cursor.skip(1u);               // IRQ pin
        cursor.skip(1u);               // WAI
        summary.stp = cursor.read_u8();
        cursor.skip(2u + 4u + 1u + 4u + 4u + 4u);

        cursor.skip(4u + 8u);         // Thread
        cursor.skip(1u);              // interlace
        cursor.skip(1u);              // field
        cursor.skip(4u);              // vperiod
        cursor.skip(4u);              // hperiod
        summary.scanline = static_cast<uint16_t>(cursor.read_u32());
        summary.dot = static_cast<uint16_t>(cursor.read_u32());
        cursor.skip(4u + 4u);         // last periods

        cursor.skip(k_wram_size);
        cursor.skip(4u + 4u + 4u + 4u);
        summary.irq_lock = cursor.read_u8();
        cursor.skip(4u + 4u + 4u + 1u + 4u + 1u);
        cursor.skip(1u + 1u + 1u + 1u + 1u);
        summary.irq_line = cursor.read_u8();
        summary.irq_transition = cursor.read_u8();
        summary.irq_pending = cursor.read_u8();
        cursor.skip(1u);
        cursor.skip(1u);
        summary.interrupt_pending = cursor.read_u8();
        cursor.skip(1u + 1u + 1u + 1u + 4u + 1u + 1u + 1u + 1u);
        cursor.skip(4u);
        summary.hirq_enable = cursor.read_u8();
        summary.virq_enable = cursor.read_u8();
        summary.irq_enable = cursor.read_u8();
        cursor.skip(1u + 1u + 1u + 2u + 1u);
        summary.htime = cursor.read_u16();
        summary.vtime = cursor.read_u16();
        return summary;
    }

    void print_state_summary(const char* label, const std::vector<uint8_t>& state)
    {
        const bsnes_cpu_summary_t summary{ summarize_state(state) };
        std::fprintf(stderr,
                     "%s: scanline=%u dot=%u PB:%02x PC:%04x A:%04x X:%04x Y:%04x SP:%04x P:%02x E:%u stp=%u irq_lock=%u irq_line=%u irq_transition=%u irq_pending=%u interrupt_pending=%u hirq=%u virq=%u irq_en=%u htime=%u vtime=%u\n",
                     label,
                     summary.scanline,
                     summary.dot,
                     static_cast<unsigned>((summary.pc >> 16u) & 0xffu),
                     static_cast<unsigned>(summary.pc & 0xffffu),
                     summary.a,
                     summary.x,
                     summary.y,
                     summary.sp,
                     summary.p,
                     summary.emulation,
                     summary.stp,
                     summary.irq_lock,
                     summary.irq_line,
                     summary.irq_transition,
                     summary.irq_pending,
                     summary.interrupt_pending,
                     summary.hirq_enable,
                     summary.virq_enable,
                     summary.irq_enable,
                     summary.htime,
                     summary.vtime);
    }

    [[nodiscard]] bool maybe_apply_state_patch(const retro_serialize_size_t retro_serialize_size,
                                               const retro_serialize_t retro_serialize,
                                               const retro_unserialize_t retro_unserialize)
    {
        const char* patch_raw{ std::getenv("CLOVER_BSNES_STATE_PATCH") };
        if (patch_raw == nullptr || *patch_raw == '\0')
            return true;

        const std::string_view patch_name{ patch_raw };
        if (patch_name != "hirq_cli_seeded")
        {
            std::fprintf(stderr, "Unknown bsnes state patch: %s\n", patch_raw);
            return false;
        }

        const size_t state_size{ retro_serialize_size() };
        if (state_size == 0)
        {
            std::fprintf(stderr, "bsnes returned empty serialize size for state patch\n");
            return false;
        }

        std::vector<uint8_t> state(state_size);
        if (!retro_serialize(state.data(), state.size()))
        {
            std::fprintf(stderr, "bsnes serialize failed before state patch\n");
            return false;
        }

        if (!apply_hirq_cli_seeded_patch(state))
        {
            std::fprintf(stderr, "bsnes state patch failed: malformed serializer walk\n");
            return false;
        }

        print_hirq_seed_probe("bsnes patched blob", state);
        dump_blob_window("bsnes patched flags bytes",
                         state,
                         575u,
                         8u);
        dump_blob_window("bsnes patched CPU I/O bytes",
                         state,
                         131775u,
                         18u);

        if (!retro_unserialize(state.data(), state.size()))
        {
            std::fprintf(stderr, "bsnes unserialize failed after state patch\n");
            return false;
        }

        const std::vector<uint8_t> seeded_state{ capture_state_blob(retro_serialize_size, retro_serialize) };
        if (!seeded_state.empty())
        {
            print_hirq_seed_probe("bsnes post-unserialize state", seeded_state);
            dump_blob_window("bsnes post-unserialize flags bytes",
                             seeded_state,
                             575u,
                             8u);
            dump_blob_window("bsnes post-unserialize CPU I/O bytes",
                             seeded_state,
                             131775u,
                             18u);
        }

        std::printf("Applied bsnes state patch: %s\n", patch_raw);
        return true;
    }

    [[nodiscard]] std::vector<uint8_t> capture_state_blob(const retro_serialize_size_t retro_serialize_size,
                                                          const retro_serialize_t retro_serialize)
    {
        const size_t state_size{ retro_serialize_size() };
        if (state_size == 0)
            return {};

        std::vector<uint8_t> state(state_size);
        if (!retro_serialize(state.data(), state.size()))
            return {};
        return state;
    }

    [[nodiscard]] uint8_t read_blob_u8(const std::vector<uint8_t>& blob, size_t offset)
    {
        return offset < blob.size() ? blob[offset] : 0u;
    }

    [[nodiscard]] uint16_t read_blob_u16(const std::vector<uint8_t>& blob, size_t offset)
    {
        if (offset + 1u >= blob.size())
            return 0u;
        return static_cast<uint16_t>(
            static_cast<uint16_t>(blob[offset])
            | static_cast<uint16_t>(static_cast<uint16_t>(blob[offset + 1u]) << 8u)
        );
    }

    [[nodiscard]] uint32_t read_blob_u32(const std::vector<uint8_t>& blob, size_t offset)
    {
        if (offset + 3u >= blob.size())
            return 0u;
        return static_cast<uint32_t>(blob[offset])
            | (static_cast<uint32_t>(blob[offset + 1u]) << 8u)
            | (static_cast<uint32_t>(blob[offset + 2u]) << 16u)
            | (static_cast<uint32_t>(blob[offset + 3u]) << 24u);
    }

    [[nodiscard]] uint8_t read_blob_p(const std::vector<uint8_t>& blob, size_t offset)
    {
        return static_cast<uint8_t>(
            (read_blob_u8(blob, offset + 0u) << 0u)
            | (read_blob_u8(blob, offset + 1u) << 1u)
            | (read_blob_u8(blob, offset + 2u) << 2u)
            | (read_blob_u8(blob, offset + 3u) << 3u)
            | (read_blob_u8(blob, offset + 4u) << 4u)
            | (read_blob_u8(blob, offset + 5u) << 5u)
            | (read_blob_u8(blob, offset + 6u) << 6u)
            | (read_blob_u8(blob, offset + 7u) << 7u)
        );
    }

    void dump_blob_window(const char* label,
                          const std::vector<uint8_t>& state,
                          size_t start,
                          size_t count)
    {
        std::fprintf(stderr, "%s", label);
        for (size_t index{ 0 }; index < count; ++index)
        {
            const size_t offset{ start + index };
            if (offset >= state.size())
                break;
            std::fprintf(stderr,
                         "%s%zu:%02x",
                         index == 0 ? " " : " ",
                         offset,
                         static_cast<unsigned>(state[offset]));
        }
        std::fprintf(stderr, "\n");
    }

    void print_hirq_seed_probe(const char* label, const std::vector<uint8_t>& state)
    {
        constexpr size_t k_pc_offset{ 558u };
        constexpr size_t k_p_offset{ 575u };
        constexpr size_t k_emulation_offset{ 583u };
        constexpr size_t k_vcounter_offset{ 628u };
        constexpr size_t k_hcounter_offset{ 632u };
        constexpr size_t k_irq_lock_offset{ 131732u };
        constexpr size_t k_counter_cpu_offset{ 131720u };
        constexpr size_t k_clock_count_offset{ 131728u };
        constexpr size_t k_irq_line_offset{ 131757u };
        constexpr size_t k_irq_transition_offset{ 131758u };
        constexpr size_t k_irq_pending_offset{ 131759u };
        constexpr size_t k_interrupt_pending_offset{ 131762u };
        constexpr size_t k_wram_address_offset{ 131775u };
        constexpr size_t k_hirq_enable_offset{ 131779u };
        constexpr size_t k_virq_enable_offset{ 131780u };
        constexpr size_t k_irq_enable_offset{ 131781u };
        constexpr size_t k_nmi_enable_offset{ 131782u };
        constexpr size_t k_autojoy_offset{ 131783u };
        constexpr size_t k_pio_offset{ 131784u };
        constexpr size_t k_htime_offset{ 131790u };
        constexpr size_t k_vtime_offset{ 131792u };

        std::fprintf(stderr,
                     "%s: PB:%02x PC:%04x P:%02x E:%u scanline=%u dot=%u cpu_counter=%u clock_count=%u irq_lock=%u irq_line=%u irq_transition=%u irq_pending=%u interrupt_pending=%u wmadd=%05x hirq=%u virq=%u irq_en=%u nmi_en=%u autojoy=%u pio=%02x htime=%u vtime=%u\n",
                     label,
                     static_cast<unsigned>((read_blob_u32(state, k_pc_offset) >> 16u) & 0xffu),
                     static_cast<unsigned>(read_blob_u32(state, k_pc_offset) & 0xffffu),
                     static_cast<unsigned>(read_blob_p(state, k_p_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_emulation_offset)),
                     static_cast<unsigned>(read_blob_u32(state, k_vcounter_offset)),
                     static_cast<unsigned>(read_blob_u32(state, k_hcounter_offset)),
                     static_cast<unsigned>(read_blob_u32(state, k_counter_cpu_offset)),
                     static_cast<unsigned>(read_blob_u32(state, k_clock_count_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_irq_lock_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_irq_line_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_irq_transition_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_irq_pending_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_interrupt_pending_offset)),
                     static_cast<unsigned>(read_blob_u32(state, k_wram_address_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_hirq_enable_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_virq_enable_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_irq_enable_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_nmi_enable_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_autojoy_offset)),
                     static_cast<unsigned>(read_blob_u8(state, k_pio_offset)),
                     static_cast<unsigned>(read_blob_u16(state, k_htime_offset)),
                     static_cast<unsigned>(read_blob_u16(state, k_vtime_offset)));
    }

    void dump_state_diff(const std::vector<uint8_t>& before,
                         const std::vector<uint8_t>& after,
                         size_t max_differences = 128u)
    {
        size_t printed{ 0 };
        for (size_t index{ 0 }; index < before.size() && index < after.size(); ++index)
        {
            if (before[index] == after[index])
                continue;

            std::fprintf(stderr,
                         "state-diff offset=%zu before=%02x after=%02x\n",
                         index,
                         static_cast<unsigned>(before[index]),
                         static_cast<unsigned>(after[index]));
            if (++printed >= max_differences)
                break;
        }

        if (printed == 0)
            std::fprintf(stderr, "state-diff: no differences\n");
    }

    void configure_reference_entropy(libretro_state_t& state)
    {
        const char* entropy_raw{ std::getenv("CLOVER_BSNES_ENTROPY") };
        if (entropy_raw == nullptr)
            return;

        const std::string_view entropy{ entropy_raw };
        if (entropy == "None" || entropy == "Low" || entropy == "High")
            state.variables["bsnes_entropy"] = std::string(entropy);
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
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            if (g_state->save_directory_string.empty())
                return false;
            *static_cast<const char**>(data) = g_state->save_directory_string.c_str();
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

    void audio_sample_callback(int16_t left, int16_t right)
    {
        if (!g_capture_audio)
            return;
        g_audio_samples.push_back(left);
        g_audio_samples.push_back(right);
    }

    size_t audio_sample_batch_callback(const int16_t* samples, size_t frames)
    {
        if (g_capture_audio && samples != nullptr)
            g_audio_samples.insert(g_audio_samples.end(), samples, samples + frames * 2u);
        return frames;
    }

    void input_poll_callback()
    {
    }

    int16_t input_state_callback(unsigned port, unsigned device, unsigned index, unsigned id)
    {
        constexpr unsigned k_retro_device_joypad{ 1u };
        if (port >= g_input_scripts.size() || device != k_retro_device_joypad || index != 0u || id > 11u)
            return 0;

        const uint16_t state{ g_input_scripts[port].state_for_frame(g_input_frame) };
        return static_cast<int16_t>((state >> (15u - id)) & 0x01u);
    }
}

int main(int argc, char** argv)
{
    g_capture_audio = std::getenv("CLOVER_BSNES_AUDIO_FILE") != nullptr;
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

    temporary_directory_t save_stage{};
    if (const char* const save_ram_path{ std::getenv("CLOVER_SAVE_RAM_FILE") };
        save_ram_path != nullptr && *save_ram_path != '\0')
    {
        std::string save_error{};
        if (!stage_save_ram(save_ram_path, rom_path, save_stage, save_error))
        {
            std::fprintf(stderr, "%s\n", save_error.c_str());
            return 1;
        }
        std::printf("Staged save RAM for bsnes: %s\n", save_ram_path);
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
    if (!save_stage.path.empty())
    {
        state.save_directory = save_stage.path;
        state.save_directory_string = state.save_directory.string();
    }
    configure_reference_entropy(state);
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
    const auto retro_serialize_size{ reinterpret_cast<retro_serialize_size_t>(load_symbol(handle, "retro_serialize_size")) };
    const auto retro_serialize{ reinterpret_cast<retro_serialize_t>(load_symbol(handle, "retro_serialize")) };
    const auto retro_unserialize{ reinterpret_cast<retro_unserialize_t>(load_symbol(handle, "retro_unserialize")) };
    const auto retro_get_memory_data{
        reinterpret_cast<retro_get_memory_data_t>(load_symbol(handle, "retro_get_memory_data"))
    };
    const auto retro_get_memory_size{
        reinterpret_cast<retro_get_memory_size_t>(load_symbol(handle, "retro_get_memory_size"))
    };
    if (!retro_init || !retro_deinit || !retro_set_environment || !retro_set_video_refresh || !retro_set_audio_sample
        || !retro_set_audio_sample_batch || !retro_set_input_poll || !retro_set_input_state || !retro_get_system_av_info
        || !retro_load_game || !retro_run || !retro_unload_game || !retro_reset
        || !retro_serialize_size || !retro_serialize || !retro_unserialize
        || !retro_get_memory_data || !retro_get_memory_size)
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
    g_input_scripts[0] = clover::test::joypad_input_script_t::from_environment(
        "CLOVER_JOYPAD1_SCRIPT", "CLOVER_JOYPAD1_SCRIPT_FILE");
    g_input_scripts[1] = clover::test::joypad_input_script_t::from_environment(
        "CLOVER_JOYPAD2_SCRIPT", "CLOVER_JOYPAD2_SCRIPT_FILE");
    if (!g_input_scripts[0].valid() || !g_input_scripts[1].valid())
    {
        std::fprintf(stderr,
                     "Invalid CLOVER_JOYPADn_SCRIPT or CLOVER_JOYPADn_SCRIPT_FILE; "
                     "expected start-end=hhhh[,start-end=hhhh...]\n");
        retro_unload_game();
        retro_deinit();
        dlclose(handle);
        return 1;
    }
    const clover::test::frame_event_script_t reset_script{
        clover::test::frame_event_script_t::from_environment("CLOVER_RESET_FRAMES")
    };
    if (!reset_script.valid())
    {
        std::fprintf(stderr,
                     "Invalid CLOVER_RESET_FRAMES; expected frame[,frame...]\n");
        retro_unload_game();
        retro_deinit();
        dlclose(handle);
        return 1;
    }
    const bool dump_state_diff_enabled{ std::getenv("CLOVER_BSNES_STATE_DIFF") != nullptr };
    std::vector<uint8_t> state_before{};
    if (dump_state_diff_enabled)
        state_before = capture_state_blob(retro_serialize_size, retro_serialize);
    if (!state_before.empty())
        print_hirq_seed_probe("bsnes pre-run state", state_before);
    if (!maybe_apply_state_patch(retro_serialize_size, retro_serialize, retro_unserialize))
    {
        retro_unload_game();
        retro_deinit();
        dlclose(handle);
        return 1;
    }
    retro_get_system_av_info(&state.geometry);

    uint64_t dumped_frames{ 0 };
    const bool dump_state{ std::getenv("CLOVER_DUMP_BSNES_STATE") != nullptr };
    for (uint64_t frame{ 0 }; frame < target_frames; ++frame)
    {
        g_input_frame = frame + 1u;
        if (reset_script.contains(g_input_frame))
            retro_reset();
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
            if (dump_state)
            {
                const std::vector<uint8_t> serialized_state{
                    capture_state_blob(retro_serialize_size, retro_serialize)
                };
                const std::filesystem::path state_path{
                    dump_directory / ("frame_" + std::to_string(frame_number) + ".state.bin")
                };
                if (!write_binary_file(state_path, serialized_state.data(), serialized_state.size()))
                {
                    std::fprintf(stderr, "Failed to write bsnes state dump: %s\n", state_path.string().c_str());
                    retro_unload_game();
                    retro_deinit();
                    dlclose(handle);
                    return 1;
                }
            }
            std::printf("Dumped frame_%llu from video_callback_frame=%llu\n",
                        static_cast<unsigned long long>(frame_number),
                        static_cast<unsigned long long>(state.latest_frame.frame_index));
            ++dumped_frames;
        }
    }

    if (const char* address_raw{ std::getenv("CLOVER_BSNES_WRAM_ADDR") };
        address_raw != nullptr && *address_raw != '\0')
    {
        const size_t memory_size{ retro_get_memory_size(k_retro_memory_system_ram) };
        const auto* memory{
            static_cast<const uint8_t*>(retro_get_memory_data(k_retro_memory_system_ram))
        };
        const size_t address{
            static_cast<size_t>(std::strtoull(address_raw, nullptr, 0))
        };
        size_t count{ 0x20u };
        if (const char* count_raw{ std::getenv("CLOVER_BSNES_WRAM_COUNT") };
            count_raw != nullptr && *count_raw != '\0')
        {
            count = static_cast<size_t>(std::strtoull(count_raw, nullptr, 0));
        }

        if (memory != nullptr && address < memory_size)
        {
            count = std::min(count, memory_size - address);
            std::printf("bsnes WRAM %05zx:", address);
            for (size_t index{ 0 }; index < count; ++index)
            {
                if ((index % 16u) == 0u)
                    std::printf("\n  %05zx:", address + index);
                std::printf(" %02x", memory[address + index]);
            }
            std::printf("\n");
        }
    }

    if (const char* const path_raw{ std::getenv("CLOVER_DUMP_BSNES_WRAM_FILE") };
        path_raw != nullptr && *path_raw != '\0')
    {
        const size_t memory_size{ retro_get_memory_size(k_retro_memory_system_ram) };
        const auto* const memory{
            static_cast<const uint8_t*>(retro_get_memory_data(k_retro_memory_system_ram))
        };
        bool wrote_wram{ false };
        size_t written_size{ memory_size };
        if (memory != nullptr && memory_size != 0u)
            wrote_wram = write_binary_file(path_raw, memory, memory_size);
        else
        {
            // This bsnes libretro target deliberately leaves retro_get_memory_* unimplemented.
            // WRAM follows cartridge RAM in the serialized state; callers tracing a cartridge
            // with save RAM provide its manifest size explicitly.
            const std::vector<uint8_t> serialized_state{
                capture_state_blob(retro_serialize_size, retro_serialize)
            };
            size_t cartridge_ram_size{ 0u };
            if (const char* const size_raw{ std::getenv("CLOVER_BSNES_CARTRIDGE_RAM_SIZE") };
                size_raw != nullptr && *size_raw != '\0')
            {
                cartridge_ram_size = static_cast<size_t>(std::strtoull(size_raw, nullptr, 0));
            }
            const size_t wram_offset{ 644u + cartridge_ram_size };
            constexpr size_t k_wram_size{ 128u * 1024u };
            if (serialized_state.size() >= wram_offset + k_wram_size)
            {
                wrote_wram = write_binary_file(path_raw,
                                               serialized_state.data() + wram_offset,
                                               k_wram_size);
                written_size = k_wram_size;
            }
        }
        if (!wrote_wram)
        {
            std::fprintf(stderr, "Failed to write bsnes WRAM dump: %s\n", path_raw);
            retro_unload_game();
            retro_deinit();
            dlclose(handle);
            return 1;
        }
        std::printf("bsnes WRAM dump: path=%s bytes=%zu\n", path_raw, written_size);
    }

    if (dump_state_diff_enabled)
    {
        const std::vector<uint8_t> state_after{ capture_state_blob(retro_serialize_size, retro_serialize) };
        if (!state_after.empty())
            print_hirq_seed_probe("bsnes post-run state", state_after);
        if (state_before.empty() || state_after.empty())
        {
            std::fprintf(stderr, "state-diff: failed to capture serialized state\n");
        }
        else
        {
            dump_state_diff(state_before, state_after);
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

    if (const char* audio_path{ std::getenv("CLOVER_BSNES_AUDIO_FILE") };
        audio_path != nullptr && *audio_path != '\0')
    {
        const uint32_t sample_rate{ static_cast<uint32_t>(state.geometry.timing.sample_rate + 0.5) };
        if (!write_audio_wav(audio_path, g_audio_samples, sample_rate))
        {
            std::fprintf(stderr, "Failed to write bsnes audio dump: %s\n", audio_path);
            retro_unload_game();
            retro_deinit();
            dlclose(handle);
            return 1;
        }
        std::printf("Audio dump: %s sample_frames=%zu sample_rate=%u\n",
                    audio_path, g_audio_samples.size() / 2u, sample_rate);
    }

    retro_unload_game();
    retro_deinit();
    dlclose(handle);
    return 0;
}
