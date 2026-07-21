//
// Created by Zack Shrout on 7/9/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/SdlAppShell.h"
#include "clover/frontend/SnesEmulatorCore.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>

namespace clover::platform
{
    namespace
    {
        constexpr int16_t k_axis_press_threshold{ 16384 };
        constexpr uint64_t k_nanoseconds_per_second{ 1'000'000'000ull };
        constexpr uint64_t k_dialog_focus_retry_ns{ 50'000'000ull };
        constexpr uint8_t k_dialog_focus_attempts{ 5u };
        constexpr float k_menu_bar_height{ 28.f };
        constexpr SDL_FRect k_file_menu_button{ 0.f, 0.f, 56.f, k_menu_bar_height };
        constexpr SDL_FRect k_emulation_menu_button{ 56.f, 0.f, 96.f, k_menu_bar_height };
        constexpr SDL_FRect k_load_rom_menu_item{ 0.f, k_menu_bar_height, 136.f, 28.f };
        constexpr SDL_FRect k_reset_menu_item{ 56.f, k_menu_bar_height, 96.f, 28.f };
        constexpr uint8_t k_file_menu{ 1u };
        constexpr uint8_t k_emulation_menu{ 2u };

        [[nodiscard]] bool contains(const SDL_FRect& rect, float x, float y) noexcept
        {
            return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
        }

        void SDLCALL rom_dialog_callback(void* userdata,
                                         const char* const* file_list,
                                         int) noexcept
        {
            const Uint32 event_type{ static_cast<Uint32>(reinterpret_cast<uintptr_t>(userdata)) };
            SDL_Event event{};
            event.type = event_type;
            event.user.code = file_list == nullptr ? 2 : (file_list[0] == nullptr ? 1 : 0);
            if (event.user.code == 0)
                event.user.data1 = SDL_strdup(file_list[0]);
            else if (event.user.code == 2)
                event.user.data1 = SDL_strdup(SDL_GetError());
            if (!SDL_PushEvent(&event) && event.user.data1 != nullptr)
                SDL_free(event.user.data1);
        }

        struct command_line_t
        {
            std::string_view rom_path{};
            std::string_view capture_path{};
            uint64_t frame_limit{ 0 };
            bool valid{ true };
        };

        [[nodiscard]] uint32_t crc32(std::span<const std::byte> bytes) noexcept
        {
            uint32_t result{ 0xffffffffu };
            for (const std::byte byte : bytes)
            {
                result ^= static_cast<uint8_t>(byte);
                for (uint8_t bit{ 0 }; bit < 8u; ++bit)
                    result = (result >> 1u) ^ (0xedb88320u & (0u - (result & 1u)));
            }
            return ~result;
        }

        void write_u16_le(std::ostream& output, uint16_t value)
        {
            const std::array<char, 2> bytes{
                static_cast<char>(value & 0xffu),
                static_cast<char>((value >> 8u) & 0xffu)
            };
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }

        void write_u32_le(std::ostream& output, uint32_t value)
        {
            const std::array<char, 4> bytes{
                static_cast<char>(value & 0xffu),
                static_cast<char>((value >> 8u) & 0xffu),
                static_cast<char>((value >> 16u) & 0xffu),
                static_cast<char>((value >> 24u) & 0xffu)
            };
            output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }

        class capture_session_t
        {
        public:
            [[nodiscard]] bool initialize(const std::filesystem::path& directory,
                                          std::string_view rom_path,
                                          std::span<const std::byte> rom,
                                          const frontend::audio_frame_view_t& audio_format) noexcept
            {
                std::error_code error{};
                if (directory.empty() || std::filesystem::exists(directory, error) || error)
                    return false;
                if (!std::filesystem::create_directories(directory, error) || error)
                    return false;

                _directory = directory;
                _rom_path = rom_path;
                _rom_size = rom.size();
                _rom_crc32 = crc32(rom);
                _sample_rate = audio_format.sample_rate_hz;
                _channels = audio_format.channels;
                if (_sample_rate == 0u || _channels == 0u)
                    return false;

                _audio.open(_directory / "audio.wav", std::ios::binary | std::ios::trunc);
                _frames.open(_directory / "frames.csv", std::ios::binary | std::ios::trunc);
                if (!_audio || !_frames)
                    return false;

                write_wav_header(0u);
                _frames << "frame,joypad1,first_audio_sample,sample_count,discontinuity,marker,"
                           "host_interval_ns,audio_queued_before_bytes,audio_queued_after_bytes,"
                           "audio_started,core_run_ns,present_ns,audio_queue_ns\n";
                _active = true;
                return true;
            }

            void record_frame(uint64_t frame,
                              uint16_t joypad_state,
                              const frontend::audio_frame_view_t& audio,
                              const frontend::video_frame_view_t& video,
                              bool marker,
                              uint64_t host_interval_ns,
                              int audio_queued_before_bytes,
                              int audio_queued_after_bytes,
                              bool audio_started,
                              uint64_t core_run_ns,
                              uint64_t present_ns,
                              uint64_t audio_queue_ns) noexcept
            {
                if (!_active)
                    return;

                _joypad_states.push_back(joypad_state);
                const uint64_t first_audio_sample{ _audio_sample_frames };
                const uint64_t sample_values{ audio.interleaved_samples.size() };
                const uint64_t sample_frames{
                    audio.channels == 0u ? 0u : sample_values / audio.channels
                };
                if (!audio.interleaved_samples.empty())
                {
                    _audio.write(reinterpret_cast<const char*>(audio.interleaved_samples.data()),
                                 static_cast<std::streamsize>(audio.interleaved_samples.size_bytes()));
                    _audio_data_bytes += audio.interleaved_samples.size_bytes();
                }
                _audio_sample_frames += sample_frames;
                _discontinuities += audio.discontinuity ? 1u : 0u;

                _frames << frame << ','
                        << std::hex << std::setw(4) << std::setfill('0') << joypad_state << std::dec << ','
                        << first_audio_sample << ',' << sample_frames << ','
                        << (audio.discontinuity ? 1 : 0) << ',' << (marker ? 1 : 0) << ','
                        << host_interval_ns << ',' << audio_queued_before_bytes << ','
                        << audio_queued_after_bytes << ',' << (audio_started ? 1 : 0) << ','
                        << core_run_ns << ',' << present_ns << ',' << audio_queue_ns << '\n';
                if (marker)
                {
                    _markers.push_back(frame);
                    write_marker_frame(frame, video);
                    std::printf("Capture marker: frame=%llu first_audio_sample=%llu\n",
                                static_cast<unsigned long long>(frame),
                                static_cast<unsigned long long>(first_audio_sample));
                }
            }

            void finalize() noexcept
            {
                if (!_active)
                    return;
                _active = false;

                if (_audio_data_bytes <= 0xffffffffu - 36u)
                {
                    _audio.seekp(0, std::ios::beg);
                    write_wav_header(static_cast<uint32_t>(_audio_data_bytes));
                }
                _audio.close();
                _frames.close();
                write_input_script();
                write_manifest();
            }

            [[nodiscard]] bool active() const noexcept { return _active; }

        private:
            void write_wav_header(uint32_t data_bytes)
            {
                _audio.write("RIFF", 4);
                write_u32_le(_audio, 36u + data_bytes);
                _audio.write("WAVEfmt ", 8);
                write_u32_le(_audio, 16u);
                write_u16_le(_audio, 1u);
                write_u16_le(_audio, _channels);
                write_u32_le(_audio, _sample_rate);
                write_u32_le(_audio, _sample_rate * _channels * sizeof(int16_t));
                write_u16_le(_audio, static_cast<uint16_t>(_channels * sizeof(int16_t)));
                write_u16_le(_audio, 16u);
                _audio.write("data", 4);
                write_u32_le(_audio, data_bytes);
            }

            void write_input_script() const
            {
                std::ofstream output{ _directory / "joypad1.script", std::ios::binary | std::ios::trunc };
                bool first_entry{ true };
                size_t index{ 0 };
                while (index < _joypad_states.size())
                {
                    const uint16_t state{ _joypad_states[index] };
                    size_t end{ index };
                    while (end + 1u < _joypad_states.size() && _joypad_states[end + 1u] == state)
                        ++end;
                    if (state != 0u)
                    {
                        if (!first_entry)
                            output << ',';
                        output << (index + 1u) << '-' << (end + 1u) << '='
                               << std::hex << std::setw(4) << std::setfill('0') << state << std::dec;
                        first_entry = false;
                    }
                    index = end + 1u;
                }
                output << '\n';
            }

            void write_manifest() const
            {
                std::ofstream output{ _directory / "manifest.txt", std::ios::binary | std::ios::trunc };
                output << "format=clover-capture-v3\n"
                       << "system=snes\n"
                       << "frame_numbering=first-run-frame-is-1\n"
                       << "joypad_encoding=snes-serial-16-msb-first\n"
                       << "rom_path=" << _rom_path << '\n'
                       << "rom_size=" << _rom_size << '\n'
                       << "rom_crc32=" << std::hex << std::setw(8) << std::setfill('0') << _rom_crc32 << std::dec << '\n'
                       << "frames=" << _joypad_states.size() << '\n'
                       << "sample_rate=" << _sample_rate << '\n'
                       << "channels=" << static_cast<unsigned>(_channels) << '\n'
                       << "audio_sample_frames=" << _audio_sample_frames << '\n'
                       << "audio_discontinuities=" << _discontinuities << '\n'
                       << "markers=";
                for (size_t index{ 0 }; index < _markers.size(); ++index)
                {
                    if (index != 0u)
                        output << ',';
                    output << _markers[index];
                }
                output << '\n';
            }

            void write_marker_frame(uint64_t frame, const frontend::video_frame_view_t& video) const
            {
                if (video.pixels == nullptr || video.format != frontend::pixel_format_t::argb8888)
                    return;
                std::ostringstream name{};
                name << "marker_frame_" << std::setw(8) << std::setfill('0') << frame << ".ppm";
                std::ofstream output{ _directory / name.str(), std::ios::binary | std::ios::trunc };
                if (!output)
                    return;
                output << "P6\n" << video.width << ' ' << video.height << "\n255\n";
                std::vector<char> rgb(static_cast<size_t>(video.width) * video.height * 3u);
                const auto* const source{ static_cast<const uint8_t*>(video.pixels) };
                for (uint32_t y{ 0 }; y < video.height; ++y)
                {
                    const auto* const row{
                        reinterpret_cast<const uint32_t*>(source + static_cast<size_t>(y) * video.pitch_bytes)
                    };
                    for (uint32_t x{ 0 }; x < video.width; ++x)
                    {
                        const uint32_t pixel{ row[x] };
                        const size_t destination{ (static_cast<size_t>(y) * video.width + x) * 3u };
                        rgb[destination] = static_cast<char>((pixel >> 16u) & 0xffu);
                        rgb[destination + 1u] = static_cast<char>((pixel >> 8u) & 0xffu);
                        rgb[destination + 2u] = static_cast<char>(pixel & 0xffu);
                    }
                }
                output.write(rgb.data(), static_cast<std::streamsize>(rgb.size()));
            }

            std::filesystem::path _directory{};
            std::string _rom_path{};
            std::ofstream _audio{};
            std::ofstream _frames{};
            std::vector<uint16_t> _joypad_states{};
            std::vector<uint64_t> _markers{};
            size_t _rom_size{ 0 };
            uint32_t _rom_crc32{ 0 };
            uint32_t _sample_rate{ 0 };
            uint16_t _channels{ 0 };
            uint64_t _audio_data_bytes{ 0 };
            uint64_t _audio_sample_frames{ 0 };
            uint64_t _discontinuities{ 0 };
            bool _active{ false };
        };

        [[nodiscard]] uint64_t frame_duration_ns(const frontend::display_info_t& display) noexcept
        {
            const double refresh_hz{ display.nominal_refresh_hz > 1.0 ? display.nominal_refresh_hz : 60.0 };
            return static_cast<uint64_t>(static_cast<double>(k_nanoseconds_per_second) / refresh_hz);
        }

        [[nodiscard]] command_line_t parse_command_line(int argc, char** argv) noexcept
        {
            command_line_t result{};
            for (int index{ 1 }; index < argc; ++index)
            {
                const std::string_view argument{ argv[index] };
                if (argument == "--frames")
                {
                    if (++index >= argc)
                    {
                        result.valid = false;
                        return result;
                    }

                    const std::string_view raw{ argv[index] };
                    const auto parsed{
                        std::from_chars(raw.data(), raw.data() + raw.size(), result.frame_limit)
                    };
                    if (parsed.ec != std::errc{} || parsed.ptr != raw.data() + raw.size())
                    {
                        result.valid = false;
                        return result;
                    }
                    continue;
                }

                if (argument == "--capture")
                {
                    if (++index >= argc)
                    {
                        result.valid = false;
                        return result;
                    }
                    result.capture_path = argv[index];
                    continue;
                }

                if (!argument.empty() && argument.front() != '-')
                {
                    result.rom_path = argument;
                    continue;
                }

                result.valid = false;
                return result;
            }
            return result;
        }
    }

    bool sdl_presentation_t::initialize(SDL_Window* window,
                                        const frontend::display_info_t& display,
                                        const frontend::audio_frame_view_t& audio_format) noexcept
    {
        _audio_started = false;
        _audio_queued_bytes_before_put = -1;
        _audio_queued_bytes_after_put = -1;
        _audio_empty_queue_observations = 0;
        _window = window;
        _display = display;
        _renderer = SDL_CreateRenderer(window, nullptr);
        if (_renderer == nullptr)
            return false;
        // The core's hardware refresh rate is authoritative. Display vsync can
        // be 60.000 Hz (or any other host rate) and must not slow a 60.098812
        // Hz SNES or starve its exact 32.04 kHz audio stream.
        static_cast<void>(SDL_SetRenderVSync(_renderer, 0));

        _texture = SDL_CreateTexture(_renderer,
                                     SDL_PIXELFORMAT_ARGB8888,
                                     SDL_TEXTUREACCESS_STREAMING,
                                     static_cast<int>(display.framebuffer_width),
                                     static_cast<int>(display.framebuffer_height));
        if (_texture == nullptr)
            return false;

        static_cast<void>(SDL_SetTextureScaleMode(_texture, SDL_SCALEMODE_NEAREST));
        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE));

        _test_pattern.resize(static_cast<size_t>(display.framebuffer_width)
                             * display.framebuffer_height);
        for (uint32_t y{ 0 }; y < display.framebuffer_height; ++y)
        {
            for (uint32_t x{ 0 }; x < display.framebuffer_width; ++x)
            {
                const uint8_t red{ static_cast<uint8_t>(
                    (x * 255u) / std::max(display.framebuffer_width - 1u, 1u)
                ) };
                const uint8_t green{ static_cast<uint8_t>(
                    (y * 255u) / std::max(display.framebuffer_height - 1u, 1u)
                ) };
                const uint8_t blue{
                    static_cast<uint8_t>(((x / 16u) ^ (y / 16u)) ? 0xd0u : 0x30u)
                };
                const bool border{
                    x < 4u || y < 4u
                        || x >= display.framebuffer_width - std::min(display.framebuffer_width, 4u)
                        || y >= display.framebuffer_height - std::min(display.framebuffer_height, 4u)
                };
                _test_pattern[static_cast<size_t>(y) * display.framebuffer_width + x] = border
                    ? 0xffffffffu
                    : 0xff000000u
                        | (static_cast<uint32_t>(red) << 16u)
                        | (static_cast<uint32_t>(green) << 8u)
                        | blue;
            }
        }

        const float window_aspect{
            (static_cast<float>(display.framebuffer_width) * display.pixel_aspect_ratio)
                / (static_cast<float>(display.framebuffer_height)
                    + k_menu_bar_height / 3.f)
        };
        static_cast<void>(SDL_SetWindowAspectRatio(window, window_aspect, window_aspect));

        if (audio_format.sample_rate_hz != 0u && audio_format.channels != 0u)
        {
            const SDL_AudioSpec spec{
                .format = SDL_AUDIO_S16,
                .channels = audio_format.channels,
                .freq = static_cast<int>(audio_format.sample_rate_hz)
            };
            _audio_stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                      &spec,
                                                      nullptr,
                                                      nullptr);
            if (_audio_stream == nullptr)
                std::fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
        }

        open_first_available_gamepad();
        refresh_gamepad_state();
        return true;
    }

    void sdl_presentation_t::shutdown() noexcept
    {
        close_gamepad();
        if (_audio_stream != nullptr)
        {
            SDL_DestroyAudioStream(_audio_stream);
            _audio_stream = nullptr;
        }
        if (_texture != nullptr)
        {
            SDL_DestroyTexture(_texture);
            _texture = nullptr;
        }
        if (_renderer != nullptr)
        {
            SDL_DestroyRenderer(_renderer);
            _renderer = nullptr;
        }

        _audio_started = false;
        _display = {};
        _test_pattern.clear();
        _window = nullptr;
    }

    void sdl_presentation_t::handle_event(const SDL_Event& event) noexcept
    {
        switch (event.type)
        {
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            if (event.key.scancode >= 0 && event.key.scancode < SDL_SCANCODE_COUNT)
                _key_states[static_cast<size_t>(event.key.scancode)] = event.type == SDL_EVENT_KEY_DOWN;
            if (event.type == SDL_EVENT_KEY_DOWN
                && event.key.scancode == SDL_SCANCODE_F8
                && !event.key.repeat)
            {
                _capture_marker_requested = true;
            }
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                const bool menu_shortcut{ (event.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) != 0 };
                if (menu_shortcut && event.key.scancode == SDL_SCANCODE_O)
                    _load_rom_requested = true;
                else if (menu_shortcut && event.key.scancode == SDL_SCANCODE_R)
                    _reset_requested = true;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                float x{ event.button.x };
                float y{ event.button.y };
                if (_renderer != nullptr)
                    static_cast<void>(SDL_RenderCoordinatesFromWindow(_renderer, x, y, &x, &y));

                if (contains(k_file_menu_button, x, y))
                    _open_menu = _open_menu == k_file_menu ? 0u : k_file_menu;
                else if (contains(k_emulation_menu_button, x, y))
                    _open_menu = _open_menu == k_emulation_menu ? 0u : k_emulation_menu;
                else if (_open_menu == k_file_menu && contains(k_load_rom_menu_item, x, y))
                {
                    _load_rom_requested = true;
                    _open_menu = 0u;
                }
                else if (_open_menu == k_emulation_menu && contains(k_reset_menu_item, x, y))
                {
                    _reset_requested = true;
                    _open_menu = 0u;
                }
                else
                {
                    _open_menu = 0u;
                }
            }
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            if (_gamepad == nullptr)
                static_cast<void>(open_gamepad(event.gdevice.which));
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            if (_gamepad != nullptr && event.gdevice.which == _gamepad_id)
            {
                close_gamepad();
                open_first_available_gamepad();
            }
            break;
        default:
            break;
        }
        refresh_gamepad_state();
    }

    void sdl_presentation_t::present(const frontend::video_frame_view_t& frame) noexcept
    {
        if (_renderer == nullptr || _texture == nullptr || frame.pixels == nullptr)
            return;
        if (frame.format != frontend::pixel_format_t::argb8888
            || frame.width != _display.framebuffer_width
            || frame.height != _display.framebuffer_height)
        {
            return;
        }

        static_cast<void>(SDL_UpdateTexture(_texture,
                                            nullptr,
                                            frame.pixels,
                                            static_cast<int>(frame.pitch_bytes)));
        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 0u, 0u, 0u, SDL_ALPHA_OPAQUE));
        static_cast<void>(SDL_RenderClear(_renderer));
        const SDL_FRect destination{ presentation_rect() };
        static_cast<void>(SDL_RenderTexture(_renderer, _texture, nullptr, &destination));
        render_menu();
        SDL_RenderPresent(_renderer);
    }

    void sdl_presentation_t::present_test_pattern() noexcept
    {
        if (_test_pattern.empty())
            return;
        present({
            .pixels = _test_pattern.data(),
            .width = _display.framebuffer_width,
            .height = _display.framebuffer_height,
            .pitch_bytes = static_cast<size_t>(_display.framebuffer_width) * sizeof(uint32_t),
            .format = frontend::pixel_format_t::argb8888
        });
    }

    void sdl_presentation_t::render_menu() noexcept
    {
        if (_renderer == nullptr)
            return;

        int output_width{ 0 };
        int output_height{ 0 };
        if (!SDL_GetRenderOutputSize(_renderer, &output_width, &output_height))
            return;
        static_cast<void>(output_height);

        const SDL_FRect menu_bar{ 0.f, 0.f, static_cast<float>(output_width), k_menu_bar_height };
        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 34u, 32u, 31u, SDL_ALPHA_OPAQUE));
        static_cast<void>(SDL_RenderFillRect(_renderer, &menu_bar));

        const SDL_FRect* selected_button{ nullptr };
        if (_open_menu == k_file_menu)
            selected_button = &k_file_menu_button;
        else if (_open_menu == k_emulation_menu)
            selected_button = &k_emulation_menu_button;
        if (selected_button != nullptr)
        {
            static_cast<void>(SDL_SetRenderDrawColor(_renderer, 74u, 71u, 68u, SDL_ALPHA_OPAQUE));
            static_cast<void>(SDL_RenderFillRect(_renderer, selected_button));
        }

        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u, SDL_ALPHA_OPAQUE));
        static_cast<void>(SDL_RenderDebugText(_renderer, 12.f, 10.f, "File"));
        static_cast<void>(SDL_RenderDebugText(_renderer, 68.f, 10.f, "Emulation"));

        const SDL_FRect* menu_item{ nullptr };
        const char* label{ nullptr };
        if (_open_menu == k_file_menu)
        {
            menu_item = &k_load_rom_menu_item;
            label = "Load ROM...";
        }
        else if (_open_menu == k_emulation_menu)
        {
            menu_item = &k_reset_menu_item;
            label = "Reset";
        }
        if (menu_item != nullptr)
        {
            static_cast<void>(SDL_SetRenderDrawColor(_renderer, 54u, 52u, 50u, SDL_ALPHA_OPAQUE));
            static_cast<void>(SDL_RenderFillRect(_renderer, menu_item));
            static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u, SDL_ALPHA_OPAQUE));
            static_cast<void>(SDL_RenderDebugText(_renderer,
                                                  menu_item->x + 12.f,
                                                  menu_item->y + 10.f,
                                                  label));
        }
    }

    void sdl_presentation_t::queue_audio(const frontend::audio_frame_view_t& audio) noexcept
    {
        if (audio.discontinuity)
            std::fprintf(stderr, "Core audio discontinuity: output frame exceeded its buffer\n");
        if (_audio_stream == nullptr || audio.interleaved_samples.empty())
            return;

        _audio_queued_bytes_before_put = SDL_GetAudioStreamQueued(_audio_stream);
        if (_audio_started && _audio_queued_bytes_before_put == 0)
            ++_audio_empty_queue_observations;

        const int byte_count{
            static_cast<int>(audio.interleaved_samples.size_bytes())
        };
        if (!SDL_PutAudioStreamData(_audio_stream, audio.interleaved_samples.data(), byte_count))
        {
            std::fprintf(stderr, "Audio queue failed: %s\n", SDL_GetError());
            return;
        }
        _audio_queued_bytes_after_put = SDL_GetAudioStreamQueued(_audio_stream);

        // A short initial cushion prevents startup underruns without adding a
        // frame scheduler dependency to the emulator core.
        if (!_audio_started
            && SDL_GetAudioStreamQueued(_audio_stream)
                >= static_cast<int>(audio.sample_rate_hz * audio.channels * sizeof(int16_t) / 20u))
        {
            if (SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(_audio_stream)))
                _audio_started = true;
        }
    }

    void sdl_presentation_t::reset_audio() noexcept
    {
        if (_audio_stream != nullptr)
        {
            static_cast<void>(SDL_PauseAudioDevice(SDL_GetAudioStreamDevice(_audio_stream)));
            static_cast<void>(SDL_ClearAudioStream(_audio_stream));
        }
        _audio_started = false;
        _audio_queued_bytes_before_put = -1;
        _audio_queued_bytes_after_put = -1;
    }

    const frontend::gamepad_state_t& sdl_presentation_t::gamepad_state() const noexcept
    {
        return _input;
    }

    bool sdl_presentation_t::consume_capture_marker() noexcept
    {
        const bool requested{ _capture_marker_requested };
        _capture_marker_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_reset_request() noexcept
    {
        const bool requested{ _reset_requested };
        _reset_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_load_rom_request() noexcept
    {
        const bool requested{ _load_rom_requested };
        _load_rom_requested = false;
        return requested;
    }

    int sdl_presentation_t::audio_queued_bytes_before_put() const noexcept
    {
        return _audio_queued_bytes_before_put;
    }

    int sdl_presentation_t::audio_queued_bytes_after_put() const noexcept
    {
        return _audio_queued_bytes_after_put;
    }

    bool sdl_presentation_t::audio_started() const noexcept
    {
        return _audio_started;
    }

    uint64_t sdl_presentation_t::audio_empty_queue_observations() const noexcept
    {
        return _audio_empty_queue_observations;
    }

    bool sdl_presentation_t::key_pressed(SDL_Scancode scancode) const noexcept
    {
        return scancode >= 0
            && scancode < SDL_SCANCODE_COUNT
            && _key_states[static_cast<size_t>(scancode)];
    }

    bool sdl_presentation_t::physical_control_pressed(frontend::gamepad_button_t button) const noexcept
    {
        using button_t = frontend::gamepad_button_t;
        switch (button)
        {
        case button_t::dpad_up:
            return key_pressed(SDL_SCANCODE_UP)
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_UP)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTY, false);
        case button_t::dpad_down:
            return key_pressed(SDL_SCANCODE_DOWN)
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTY, true);
        case button_t::dpad_left:
            return key_pressed(SDL_SCANCODE_LEFT)
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTX, false);
        case button_t::dpad_right:
            return key_pressed(SDL_SCANCODE_RIGHT)
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTX, true);
        case button_t::face_south:
            return key_pressed(SDL_SCANCODE_X) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_SOUTH);
        case button_t::face_east:
            return key_pressed(SDL_SCANCODE_Z) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_EAST);
        case button_t::face_west:
            return key_pressed(SDL_SCANCODE_A) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_WEST);
        case button_t::face_north:
            return key_pressed(SDL_SCANCODE_S) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_NORTH);
        case button_t::left_shoulder:
            return key_pressed(SDL_SCANCODE_Q) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
        case button_t::right_shoulder:
            return key_pressed(SDL_SCANCODE_W) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
        case button_t::back:
            return key_pressed(SDL_SCANCODE_RSHIFT) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_BACK);
        case button_t::start:
            return key_pressed(SDL_SCANCODE_RETURN) || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_START);
        }
        return false;
    }

    bool sdl_presentation_t::gamepad_button_pressed(SDL_GamepadButton button) const noexcept
    {
        return _gamepad != nullptr && SDL_GetGamepadButton(_gamepad, button);
    }

    bool sdl_presentation_t::gamepad_axis_pressed(SDL_GamepadAxis axis, bool positive) const noexcept
    {
        if (_gamepad == nullptr)
            return false;
        const int16_t value{ SDL_GetGamepadAxis(_gamepad, axis) };
        return positive ? value >= k_axis_press_threshold : value <= -k_axis_press_threshold;
    }

    SDL_FRect sdl_presentation_t::presentation_rect() const noexcept
    {
        int output_width{ 0 };
        int output_height{ 0 };
        if (_renderer == nullptr || !SDL_GetRenderOutputSize(_renderer, &output_width, &output_height))
            return {};

        const float content_width{
            static_cast<float>(_display.framebuffer_width) * _display.pixel_aspect_ratio
        };
        const float content_height{ static_cast<float>(_display.framebuffer_height) };
        const float available_height{
            std::max(0.f, static_cast<float>(output_height) - k_menu_bar_height)
        };
        const float scale{
            std::min(static_cast<float>(output_width) / content_width,
                     available_height / content_height)
        };
        const float width{ content_width * scale };
        const float height{ content_height * scale };
        return {
            (static_cast<float>(output_width) - width) * 0.5f,
            k_menu_bar_height + (available_height - height) * 0.5f,
            width,
            height
        };
    }

    bool sdl_presentation_t::open_gamepad(SDL_JoystickID joystick_id) noexcept
    {
        if (!SDL_IsGamepad(joystick_id))
            return false;
        SDL_Gamepad* const gamepad{ SDL_OpenGamepad(joystick_id) };
        if (gamepad == nullptr)
            return false;

        close_gamepad();
        _gamepad = gamepad;
        _gamepad_id = joystick_id;
        return true;
    }

    void sdl_presentation_t::open_first_available_gamepad() noexcept
    {
        if (_gamepad != nullptr)
            return;

        int gamepad_count{ 0 };
        SDL_JoystickID* const gamepad_ids{ SDL_GetGamepads(&gamepad_count) };
        if (gamepad_ids == nullptr)
            return;
        for (int index{ 0 }; index < gamepad_count; ++index)
        {
            if (open_gamepad(gamepad_ids[index]))
                break;
        }
        SDL_free(gamepad_ids);
    }

    void sdl_presentation_t::close_gamepad() noexcept
    {
        if (_gamepad != nullptr)
        {
            SDL_CloseGamepad(_gamepad);
            _gamepad = nullptr;
        }
        _gamepad_id = 0;
    }

    void sdl_presentation_t::refresh_gamepad_state() noexcept
    {
        for (uint8_t raw{ 0 }; raw <= static_cast<uint8_t>(frontend::gamepad_button_t::start); ++raw)
        {
            const auto button{ static_cast<frontend::gamepad_button_t>(raw) };
            _input.set(button, physical_control_pressed(button));
        }
    }

    int sdl_app_shell_t::run(int argc, char** argv) noexcept
    {
        const command_line_t command_line{ parse_command_line(argc, argv) };
        if (!command_line.valid)
        {
            std::fprintf(stderr, "Usage: %s [rom-path] [--frames count] [--capture new-directory]\n",
                         argc > 0 ? argv[0] : "clover_sdl");
            return 1;
        }

        static_cast<void>(SDL_SetAppMetadata("Clover", "0.1", "com.bunnysoft.clover"));
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        const Uint32 rom_dialog_event_type{ SDL_RegisterEvents(1) };
        if (rom_dialog_event_type == 0u)
        {
            std::fprintf(stderr, "Unable to register ROM dialog event: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        auto core{ frontend::create_emulator_core(frontend::system_id_t::snes) };
        if (!core)
        {
            std::fprintf(stderr, "Unable to create SNES emulator core\n");
            SDL_Quit();
            return 1;
        }

        bool media_loaded{ false };
        std::filesystem::path rom_path{};
        std::filesystem::path save_path{};
        std::vector<std::byte> media{};
        if (!command_line.rom_path.empty())
        {
            rom_path = command_line.rom_path;
            save_path = save_path_for_rom(rom_path);
            media = load_file_bytes(rom_path.string().c_str());
            if (media.empty() || !core->load_media(media))
            {
                std::fprintf(stderr, "Unable to load media: %s\n", rom_path.string().c_str());
                SDL_Quit();
                return 1;
            }
            if (!load_persistent_memory(*core, save_path))
            {
                SDL_Quit();
                return 1;
            }
            core->power_on();
            media_loaded = true;
        }

        if (!command_line.capture_path.empty() && !media_loaded)
        {
            std::fprintf(stderr, "A ROM path is required when using --capture\n");
            SDL_Quit();
            return 1;
        }

        const frontend::display_info_t display{ core->display_info() };
        const std::string initial_window_title{
            media_loaded ? "Clover — " + rom_path.filename().string() : "Clover"
        };
        SDL_Window* const window{
            SDL_CreateWindow(initial_window_title.c_str(),
                             static_cast<int>(std::lround(display.framebuffer_width
                                                          * display.pixel_aspect_ratio * 3.0)),
                             static_cast<int>(display.framebuffer_height * 3u + k_menu_bar_height),
                             SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED)
        };
        if (window == nullptr)
        {
            std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        sdl_presentation_t presentation{};
        if (!presentation.initialize(window, display, core->audio_frame()))
        {
            std::fprintf(stderr, "SDL presentation initialization failed: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        if (media_loaded)
            presentation.present(core->video_frame());
        else
            presentation.present_test_pattern();

        capture_session_t capture{};
        if (!command_line.capture_path.empty()
            && !capture.initialize(std::filesystem::path{ command_line.capture_path },
                                   rom_path.string(),
                                   media,
                                   core->audio_frame()))
        {
            std::fprintf(stderr,
                         "Unable to create capture directory (it must not already exist): %.*s\n",
                         static_cast<int>(command_line.capture_path.size()),
                         command_line.capture_path.data());
            presentation.shutdown();
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
        if (capture.active())
        {
            std::printf("Capture active: %.*s (press F8 to mark the next frame)\n",
                        static_cast<int>(command_line.capture_path.size()),
                        command_line.capture_path.data());
        }

        bool running{ true };
        uint64_t frames_run{ 0 };
        const uint64_t frame_limit{ command_line.frame_limit };
        const uint64_t target_frame_duration_ns{ frame_duration_ns(display) };
        const uint64_t run_start_ns{ SDL_GetTicksNS() };
        uint64_t next_frame_deadline_ns{ run_start_ns };
        uint64_t previous_frame_start_ns{ run_start_ns };
        uint64_t audio_sample_values{ 0 };
        uint64_t audio_discontinuities{ 0 };
        size_t max_audio_sample_values_per_frame{ 0 };
        int32_t audio_peak{ 0 };
        bool rom_dialog_active{ false };
        uint64_t window_focus_restore_ns{ 0u };
        uint8_t window_focus_restore_attempts{ 0u };
        std::filesystem::path selected_rom_path{};
        while (running && (frame_limit == 0u || frames_run < frame_limit))
        {
            const uint64_t frame_start_ns{ SDL_GetTicksNS() };
            const uint64_t host_interval_ns{ frame_start_ns - previous_frame_start_ns };
            previous_frame_start_ns = frame_start_ns;
            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                    break;
                }
                if (event.type == rom_dialog_event_type)
                {
                    rom_dialog_active = false;
                    window_focus_restore_ns = SDL_GetTicksNS() + k_dialog_focus_retry_ns;
                    window_focus_restore_attempts = 0u;
                    if (event.user.code == 0 && event.user.data1 != nullptr)
                        selected_rom_path = static_cast<const char*>(event.user.data1);
                    else if (event.user.code == 2)
                        std::fprintf(stderr,
                                     "ROM dialog failed: %s\n",
                                     event.user.data1 != nullptr
                                         ? static_cast<const char*>(event.user.data1)
                                         : "unknown error");
                    if (event.user.data1 != nullptr)
                        SDL_free(event.user.data1);
                    continue;
                }
                presentation.handle_event(event);
            }
            if (!running)
                break;

            const bool reset_requested{ presentation.consume_reset_request() };
            const bool load_rom_requested{ presentation.consume_load_rom_request() };
            if ((reset_requested || load_rom_requested) && capture.active())
            {
                std::fprintf(stderr,
                             "Reset and ROM loading are disabled during a deterministic capture\n");
            }
            else
            {
                if (reset_requested)
                {
                    if (!media_loaded)
                    {
                        std::fprintf(stderr, "Reset ignored: no ROM is loaded\n");
                    }
                    else
                    {
                        static_cast<void>(flush_persistent_memory(*core, save_path));
                        core->reset();
                        presentation.reset_audio();
                        next_frame_deadline_ns = SDL_GetTicksNS();
                        previous_frame_start_ns = next_frame_deadline_ns;
                        std::printf("Emulator reset\n");
                    }
                }
                if (load_rom_requested && !rom_dialog_active)
                {
                    static constexpr std::array<SDL_DialogFileFilter, 2> filters{
                        SDL_DialogFileFilter{ "SNES ROMs", "sfc;smc" },
                        SDL_DialogFileFilter{ "All files", "*" }
                    };
                    rom_dialog_active = true;
                    window_focus_restore_ns = 0u;
                    SDL_ShowOpenFileDialog(rom_dialog_callback,
                                           reinterpret_cast<void*>(
                                               static_cast<uintptr_t>(rom_dialog_event_type)
                                           ),
                                           window,
                                           filters.data(),
                                           static_cast<int>(filters.size()),
                                           nullptr,
                                           false);
                }
            }

            if (!selected_rom_path.empty())
            {
                const std::filesystem::path requested_path{ std::move(selected_rom_path) };
                selected_rom_path.clear();
                const std::vector<std::byte> requested_media{
                    load_file_bytes(requested_path.string().c_str())
                };
                auto replacement{ frontend::create_emulator_core(frontend::system_id_t::snes) };
                const std::filesystem::path requested_save_path{ save_path_for_rom(requested_path) };
                if (!replacement
                    || requested_media.empty()
                    || !replacement->load_media(requested_media))
                {
                    std::fprintf(stderr, "Unable to load media: %s\n", requested_path.string().c_str());
                }
                else if (!load_persistent_memory(*replacement, requested_save_path))
                {
                    std::fprintf(stderr, "Keeping current ROM because its replacement save could not be loaded\n");
                }
                else if (media_loaded && !flush_persistent_memory(*core, save_path))
                {
                    std::fprintf(stderr, "Keeping current ROM because its save RAM could not be written\n");
                }
                else
                {
                    replacement->power_on();
                    core = std::move(replacement);
                    rom_path = requested_path;
                    save_path = requested_save_path;
                    media_loaded = true;
                    const std::string window_title{ "Clover — " + rom_path.filename().string() };
                    static_cast<void>(SDL_SetWindowTitle(window, window_title.c_str()));
                    presentation.reset_audio();
                    presentation.present(core->video_frame());
                    next_frame_deadline_ns = SDL_GetTicksNS();
                    previous_frame_start_ns = next_frame_deadline_ns;
                    std::printf("Loaded ROM: %s\n", rom_path.string().c_str());
                }
            }

            if (window_focus_restore_ns != 0u
                && SDL_GetTicksNS() >= window_focus_restore_ns)
            {
                static_cast<void>(SDL_RaiseWindow(window));
                ++window_focus_restore_attempts;
                if ((SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0u
                    || window_focus_restore_attempts >= k_dialog_focus_attempts)
                {
                    window_focus_restore_ns = 0u;
                }
                else
                {
                    window_focus_restore_ns = SDL_GetTicksNS() + k_dialog_focus_retry_ns;
                }
            }

            if (rom_dialog_active)
            {
                SDL_Delay(10u);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            if (!media_loaded)
            {
                presentation.present_test_pattern();
                SDL_Delay(10u);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            const frontend::gamepad_state_t gamepad_state{ presentation.gamepad_state() };
            const bool capture_marker{ presentation.consume_capture_marker() };
            core->set_gamepad_state(0u, gamepad_state);
            const uint64_t core_run_start_ns{ SDL_GetTicksNS() };
            core->run_frame();
            const uint64_t core_run_end_ns{ SDL_GetTicksNS() };
            ++frames_run;
            presentation.present(core->video_frame());
            const uint64_t present_end_ns{ SDL_GetTicksNS() };
            const frontend::audio_frame_view_t audio{ core->audio_frame() };
            presentation.queue_audio(audio);
            const uint64_t audio_queue_end_ns{ SDL_GetTicksNS() };
            if (capture.active())
            {
                capture.record_frame(frames_run,
                                     frontend::snes_joypad_state(gamepad_state),
                                     audio,
                                     core->video_frame(),
                                     capture_marker,
                                     host_interval_ns,
                                     presentation.audio_queued_bytes_before_put(),
                                     presentation.audio_queued_bytes_after_put(),
                                     presentation.audio_started(),
                                     core_run_end_ns - core_run_start_ns,
                                     present_end_ns - core_run_end_ns,
                                     audio_queue_end_ns - present_end_ns);
            }
            audio_sample_values += audio.interleaved_samples.size();
            audio_discontinuities += audio.discontinuity ? 1u : 0u;
            max_audio_sample_values_per_frame = std::max(
                max_audio_sample_values_per_frame,
                audio.interleaved_samples.size()
            );
            for (const int16_t sample : audio.interleaved_samples)
            {
                const int32_t magnitude{
                    sample < 0 ? -static_cast<int32_t>(sample) : static_cast<int32_t>(sample)
                };
                audio_peak = std::max(audio_peak, magnitude);
            }
            if ((frames_run % 60u) == 0u)
                static_cast<void>(flush_persistent_memory(*core, save_path));

            next_frame_deadline_ns += target_frame_duration_ns;
            const uint64_t current_ticks_ns{ SDL_GetTicksNS() };
            if (next_frame_deadline_ns > current_ticks_ns)
                SDL_DelayPrecise(next_frame_deadline_ns - current_ticks_ns);
            else if (current_ticks_ns - next_frame_deadline_ns > target_frame_duration_ns * 3u)
                next_frame_deadline_ns = current_ticks_ns;
        }

        const double elapsed_seconds{
            static_cast<double>(SDL_GetTicksNS() - run_start_ns)
                / static_cast<double>(k_nanoseconds_per_second)
        };
        const double effective_fps{
            elapsed_seconds > 0.0 ? static_cast<double>(frames_run) / elapsed_seconds : 0.0
        };
        std::printf("Clover SDL: frames=%llu elapsed=%.3f effective_fps=%.3f "
                    "target_fps=%.6f audio_frames=%llu audio_peak=%d\n",
                    static_cast<unsigned long long>(frames_run),
                    elapsed_seconds,
                    effective_fps,
                    display.nominal_refresh_hz,
                    static_cast<unsigned long long>(
                        audio_sample_values / std::max<uint8_t>(core->audio_frame().channels, 1u)),
                    audio_peak);
        std::printf("Clover SDL audio: max_values_per_frame=%zu discontinuities=%llu "
                    "empty_queue_observations=%llu\n",
                    max_audio_sample_values_per_frame,
                    static_cast<unsigned long long>(audio_discontinuities),
                    static_cast<unsigned long long>(presentation.audio_empty_queue_observations()));
        const bool save_flushed{ !media_loaded || flush_persistent_memory(*core, save_path) };
        capture.finalize();
        presentation.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return save_flushed ? 0 : 1;
    }

    std::vector<std::byte> sdl_app_shell_t::load_file_bytes(const char* path) noexcept
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

    std::filesystem::path sdl_app_shell_t::save_path_for_rom(const std::filesystem::path& rom_path)
    {
        std::filesystem::path result{ rom_path };
        result.replace_extension(".srm");
        return result;
    }

    bool sdl_app_shell_t::load_persistent_memory(frontend::emulator_core_t& core,
                                                  const std::filesystem::path& save_path) noexcept
    {
        const std::span<const std::byte> memory{ core.persistent_memory() };
        if (memory.empty())
            return true;

        std::error_code error{};
        if (!std::filesystem::exists(save_path, error))
        {
            if (error)
            {
                std::fprintf(stderr,
                             "Unable to inspect save RAM file %s: %s\n",
                             save_path.string().c_str(),
                             error.message().c_str());
                return false;
            }
            return true;
        }

        const std::vector<std::byte> data{ load_file_bytes(save_path.string().c_str()) };
        if (data.size() != memory.size() || !core.load_persistent_memory(data))
        {
            std::fprintf(stderr,
                         "Save RAM size mismatch: %s has %zu bytes; ROM requires %zu\n",
                         save_path.string().c_str(),
                         data.size(),
                         memory.size());
            return false;
        }

        std::printf("Loaded save RAM: %s (%zu bytes)\n",
                    save_path.string().c_str(),
                    data.size());
        return true;
    }

    bool sdl_app_shell_t::flush_persistent_memory(frontend::emulator_core_t& core,
                                                   const std::filesystem::path& save_path) noexcept
    {
        const std::span<const std::byte> memory{ core.persistent_memory() };
        if (memory.empty() || !core.persistent_memory_dirty())
            return true;

        std::filesystem::path temporary_path{ save_path };
        temporary_path += ".tmp";
        std::ofstream output{ temporary_path, std::ios::binary | std::ios::trunc };
        if (!output)
        {
            std::fprintf(stderr, "Unable to open temporary save RAM file: %s\n",
                         temporary_path.string().c_str());
            return false;
        }
        output.write(reinterpret_cast<const char*>(memory.data()),
                     static_cast<std::streamsize>(memory.size()));
        output.close();
        if (!output)
        {
            std::fprintf(stderr, "Unable to write temporary save RAM file: %s\n",
                         temporary_path.string().c_str());
            return false;
        }

        std::error_code error{};
        std::filesystem::rename(temporary_path, save_path, error);
        if (error)
        {
            std::fprintf(stderr,
                         "Unable to replace save RAM file %s: %s\n",
                         save_path.string().c_str(),
                         error.message().c_str());
            std::error_code cleanup_error{};
            static_cast<void>(std::filesystem::remove(temporary_path, cleanup_error));
            return false;
        }

        core.mark_persistent_memory_clean();
        std::printf("Saved save RAM: %s (%zu bytes)\n",
                    save_path.string().c_str(),
                    memory.size());
        return true;
    }
}
