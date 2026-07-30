//
// Created by Zack Shrout on 7/9/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/SdlAppShell.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/platform/RomLibrary.h"
#include "clover/platform/sdl/SdlAudioPacing.h"
#include "clover/utils/FileSystem.h"
#include "clover/utils/SystemInfo.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>

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
        constexpr SDL_FRect k_video_menu_button{ 152.f, 0.f, 68.f, k_menu_bar_height };
        constexpr float k_file_menu_width{ 240.f };
        constexpr SDL_FRect k_import_rom_menu_item{
            0.f, k_menu_bar_height, k_file_menu_width, 28.f
        };
        constexpr SDL_FRect k_open_library_menu_item{
            0.f, k_menu_bar_height + 28.f, k_file_menu_width, 28.f
        };
        constexpr SDL_FRect k_open_temporary_rom_menu_item{
            0.f, k_menu_bar_height + 56.f, k_file_menu_width, 28.f
        };
        constexpr SDL_FRect k_open_diagnostics_menu_item{
            0.f, k_menu_bar_height + 84.f, k_file_menu_width, 28.f
        };
        constexpr SDL_FRect k_quit_menu_item{
            0.f, k_menu_bar_height + 112.f, k_file_menu_width, 28.f
        };
        constexpr float k_emulation_menu_width{ 176.f };
        constexpr SDL_FRect k_pause_menu_item{
            56.f, k_menu_bar_height, k_emulation_menu_width, 28.f
        };
        constexpr SDL_FRect k_frame_advance_menu_item{
            56.f, k_menu_bar_height + 28.f, k_emulation_menu_width, 28.f
        };
        constexpr SDL_FRect k_reset_menu_item{
            56.f, k_menu_bar_height + 56.f, k_emulation_menu_width, 28.f
        };
        constexpr std::array<SDL_FRect, 5> k_speed_menu_items{
            SDL_FRect{ 56.f, k_menu_bar_height + 84.f, k_emulation_menu_width, 28.f },
            SDL_FRect{ 56.f, k_menu_bar_height + 112.f, k_emulation_menu_width, 28.f },
            SDL_FRect{ 56.f, k_menu_bar_height + 140.f, k_emulation_menu_width, 28.f },
            SDL_FRect{ 56.f, k_menu_bar_height + 168.f, k_emulation_menu_width, 28.f },
            SDL_FRect{ 56.f, k_menu_bar_height + 196.f, k_emulation_menu_width, 28.f }
        };
        constexpr float k_video_menu_width{ 136.f };
        constexpr float k_library_row_y{ 72.f };
        constexpr float k_library_row_height{ 24.f };
        constexpr size_t k_library_visible_rows{ 24u };
        constexpr uint8_t k_file_menu{ 1u };
        constexpr uint8_t k_emulation_menu{ 2u };
        constexpr uint8_t k_video_menu{ 3u };
        constexpr uint8_t k_menu_hit_none{ 0u };
        constexpr uint8_t k_menu_hit_file{ 1u };
        constexpr uint8_t k_menu_hit_emulation{ 2u };
        constexpr uint8_t k_menu_hit_video{ 3u };
        constexpr uint8_t k_menu_hit_import{ 4u };
        constexpr uint8_t k_menu_hit_library{ 5u };
        constexpr uint8_t k_menu_hit_temporary{ 6u };
        constexpr uint8_t k_menu_hit_quit{ 7u };
        constexpr uint8_t k_menu_hit_pause{ 8u };
        constexpr uint8_t k_menu_hit_frame_advance{ 9u };
        constexpr uint8_t k_menu_hit_reset{ 10u };
        constexpr uint8_t k_menu_hit_speed_base{ 11u };
        constexpr uint8_t k_menu_hit_video_base{ 32u };
        constexpr uint8_t k_menu_hit_diagnostics{ 64u };

        [[nodiscard]] bool contains(const SDL_FRect& rect, float x, float y) noexcept
        {
            return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
        }

        [[nodiscard]] uint8_t menu_hit(uint8_t open_menu,
                                       size_t video_plane_count,
                                       float x,
                                       float y) noexcept
        {
            if (contains(k_file_menu_button, x, y))
                return k_menu_hit_file;
            if (contains(k_emulation_menu_button, x, y))
                return k_menu_hit_emulation;
            if (video_plane_count != 0u && contains(k_video_menu_button, x, y))
                return k_menu_hit_video;
            if (open_menu == k_file_menu)
            {
                if (contains(k_import_rom_menu_item, x, y))
                    return k_menu_hit_import;
                if (contains(k_open_library_menu_item, x, y))
                    return k_menu_hit_library;
                if (contains(k_open_temporary_rom_menu_item, x, y))
                    return k_menu_hit_temporary;
                if (contains(k_open_diagnostics_menu_item, x, y))
                    return k_menu_hit_diagnostics;
                if (contains(k_quit_menu_item, x, y))
                    return k_menu_hit_quit;
            }
            if (open_menu == k_emulation_menu)
            {
                if (contains(k_pause_menu_item, x, y))
                    return k_menu_hit_pause;
                if (contains(k_frame_advance_menu_item, x, y))
                    return k_menu_hit_frame_advance;
                if (contains(k_reset_menu_item, x, y))
                    return k_menu_hit_reset;
                for (size_t index{ 0 }; index < k_speed_menu_items.size(); ++index)
                {
                    if (contains(k_speed_menu_items[index], x, y))
                        return static_cast<uint8_t>(k_menu_hit_speed_base + index);
                }
            }
            if (open_menu == k_video_menu)
            {
                for (size_t index{ 0 }; index < video_plane_count; ++index)
                {
                    const SDL_FRect item{
                        152.f,
                        k_menu_bar_height + static_cast<float>(index) * 28.f,
                        k_video_menu_width,
                        28.f
                    };
                    if (contains(item, x, y))
                        return static_cast<uint8_t>(k_menu_hit_video_base + index);
                }
            }
            return k_menu_hit_none;
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

        enum class pending_rom_source_t : uint8_t
        {
            none,
            import,
            temporary,
            library
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

        struct save_write_result_t
        {
            bool succeeded{ false };
            std::string error{};
        };

        struct duration_stats_t
        {
            void observe(uint64_t duration_ns) noexcept
            {
                ++count;
                total_ns += duration_ns;
                maximum_ns = std::max(maximum_ns, duration_ns);
                above_1_ms += duration_ns > 1'000'000u ? 1u : 0u;
                above_5_ms += duration_ns > 5'000'000u ? 1u : 0u;
                above_10_ms += duration_ns > 10'000'000u ? 1u : 0u;
                above_16_ms += duration_ns > 16'666'667u ? 1u : 0u;
                above_33_ms += duration_ns > 33'333'333u ? 1u : 0u;
                above_50_ms += duration_ns > 50'000'000u ? 1u : 0u;
            }

            uint64_t count{ 0 };
            uint64_t total_ns{ 0 };
            uint64_t maximum_ns{ 0 };
            uint64_t above_1_ms{ 0 };
            uint64_t above_5_ms{ 0 };
            uint64_t above_10_ms{ 0 };
            uint64_t above_16_ms{ 0 };
            uint64_t above_33_ms{ 0 };
            uint64_t above_50_ms{ 0 };
        };

        struct integer_stats_t
        {
            void observe(int value) noexcept
            {
                if (value < 0)
                    return;
                ++count;
                total += static_cast<uint64_t>(value);
                minimum = minimum < 0 ? value : std::min(minimum, value);
                maximum = std::max(maximum, value);
            }

            uint64_t count{ 0 };
            uint64_t total{ 0 };
            int minimum{ -1 };
            int maximum{ -1 };
        };

        void append_duration_stats(std::ostream& output,
                                   std::string_view prefix,
                                   const duration_stats_t& stats)
        {
            const double average_ms{
                stats.count == 0u
                    ? 0.0
                    : static_cast<double>(stats.total_ns)
                        / static_cast<double>(stats.count)
                        / 1'000'000.0
            };
            output << prefix << "_count=" << stats.count << '\n'
                   << prefix << "_total_ms="
                   << static_cast<double>(stats.total_ns) / 1'000'000.0 << '\n'
                   << prefix << "_average_ms=" << average_ms << '\n'
                   << prefix << "_maximum_ms="
                   << static_cast<double>(stats.maximum_ns) / 1'000'000.0 << '\n'
                   << prefix << "_above_1ms=" << stats.above_1_ms << '\n'
                   << prefix << "_above_5ms=" << stats.above_5_ms << '\n'
                   << prefix << "_above_10ms=" << stats.above_10_ms << '\n'
                   << prefix << "_above_16ms=" << stats.above_16_ms << '\n'
                   << prefix << "_above_33ms=" << stats.above_33_ms << '\n'
                   << prefix << "_above_50ms=" << stats.above_50_ms << '\n';
        }

        void append_integer_stats(std::ostream& output,
                                  std::string_view prefix,
                                  const integer_stats_t& stats)
        {
            const double average{
                stats.count == 0u
                    ? 0.0
                    : static_cast<double>(stats.total) / static_cast<double>(stats.count)
            };
            output << prefix << "_samples=" << stats.count << '\n'
                   << prefix << "_minimum=" << stats.minimum << '\n'
                   << prefix << "_average=" << average << '\n'
                   << prefix << "_maximum=" << stats.maximum << '\n';
        }

        [[nodiscard]] const char* power_state_name(SDL_PowerState state) noexcept
        {
            switch (state)
            {
            case SDL_POWERSTATE_ERROR:
                return "error";
            case SDL_POWERSTATE_UNKNOWN:
                return "unknown";
            case SDL_POWERSTATE_ON_BATTERY:
                return "on_battery";
            case SDL_POWERSTATE_NO_BATTERY:
                return "no_battery";
            case SDL_POWERSTATE_CHARGING:
                return "charging";
            case SDL_POWERSTATE_CHARGED:
                return "charged";
            }
            return "unknown";
        }

        class async_save_writer_t
        {
        public:
            [[nodiscard]] bool poll(frontend::emulator_core_t& core) noexcept
            {
                if (!_write.valid()
                    || _write.wait_for(std::chrono::seconds{ 0 }) != std::future_status::ready)
                {
                    return true;
                }
                return finish(core);
            }

            [[nodiscard]] bool schedule(frontend::emulator_core_t& core,
                                        const std::filesystem::path& save_path) noexcept
            {
                if (!poll(core))
                    return false;
                if (_write.valid()
                    || core.persistent_memory().empty()
                    || !core.persistent_memory_dirty())
                {
                    return true;
                }

                _snapshot.assign(core.persistent_memory().begin(),
                                 core.persistent_memory().end());
                const std::vector<std::byte> write_data{ _snapshot };
                _save_path = save_path;
                try
                {
                    _write = std::async(
                        std::launch::async,
                        [save_path, write_data]() noexcept
                        {
                            save_write_result_t result{};
                            result.succeeded = utils::write_binary_file_atomic(
                                save_path,
                                write_data,
                                result.error
                            );
                            return result;
                        }
                    );
                }
                catch (const std::exception& exception)
                {
                    std::fprintf(stderr,
                                 "Unable to start save RAM write: %s\n",
                                 exception.what());
                    _snapshot.clear();
                    _save_path.clear();
                    return false;
                }
                return true;
            }

            [[nodiscard]] bool flush(frontend::emulator_core_t& core,
                                     const std::filesystem::path& save_path) noexcept
            {
                if (_write.valid() && !finish(core))
                    return false;
                if (!schedule(core, save_path))
                    return false;
                return !_write.valid() || finish(core);
            }

        private:
            [[nodiscard]] bool finish(frontend::emulator_core_t& core) noexcept
            {
                save_write_result_t result{};
                try
                {
                    result = _write.get();
                }
                catch (const std::exception& exception)
                {
                    result.error = exception.what();
                }

                if (!result.succeeded)
                {
                    std::fprintf(stderr, "Unable to save RAM: %s\n", result.error.c_str());
                    _snapshot.clear();
                    _save_path.clear();
                    return false;
                }

                const std::span<const std::byte> current{ core.persistent_memory() };
                if (current.size() == _snapshot.size()
                    && std::equal(current.begin(), current.end(), _snapshot.begin()))
                {
                    core.mark_persistent_memory_clean();
                }
                std::printf("Saved save RAM: %s (%zu bytes)\n",
                            _save_path.string().c_str(),
                            _snapshot.size());
                _snapshot.clear();
                _save_path.clear();
                return true;
            }

            std::future<save_write_result_t> _write{};
            std::vector<std::byte> _snapshot{};
            std::filesystem::path _save_path{};
        };

        class diagnostic_log_writer_t
        {
        public:
            diagnostic_log_writer_t(std::filesystem::path path,
                                    std::filesystem::path timeline_path,
                                    std::string timeline_header)
                : _path{ std::move(path) },
                  _timeline_path{ std::move(timeline_path) },
                  _timeline_header{ std::move(timeline_header) },
                  _thread{ [this] { write_loop(); } }
            {
            }

            diagnostic_log_writer_t(const diagnostic_log_writer_t&) = delete;
            diagnostic_log_writer_t& operator=(const diagnostic_log_writer_t&) = delete;

            ~diagnostic_log_writer_t()
            {
                finish();
            }

            void submit(std::string contents, std::string timeline_line = {})
            {
                {
                    const std::scoped_lock lock{ _mutex };
                    _pending.push_back({
                        std::move(contents),
                        std::move(timeline_line)
                    });
                }
                _condition.notify_one();
            }

            void finish() noexcept
            {
                {
                    const std::scoped_lock lock{ _mutex };
                    _stopping = true;
                }
                _condition.notify_one();
                if (_thread.joinable())
                    _thread.join();
            }

        private:
            struct write_request_t
            {
                std::string contents{};
                std::string timeline_line{};
            };

            void write_loop() noexcept
            {
                std::error_code filesystem_error{};
                std::filesystem::create_directories(
                    _timeline_path.parent_path(),
                    filesystem_error
                );
                std::ofstream timeline{};
                if (!filesystem_error)
                {
                    timeline.open(
                        _timeline_path,
                        std::ios::binary | std::ios::trunc
                    );
                    if (timeline)
                    {
                        timeline << _timeline_header << '\n';
                        timeline.flush();
                    }
                }
                if (filesystem_error || !timeline)
                {
                    std::fprintf(
                        stderr,
                        "Unable to open diagnostics timeline: %s\n",
                        filesystem_error
                            ? filesystem_error.message().c_str()
                            : utils::path_to_utf8(_timeline_path).c_str()
                    );
                }

                for (;;)
                {
                    write_request_t request{};
                    {
                        std::unique_lock lock{ _mutex };
                        _condition.wait(lock, [this] {
                            return !_pending.empty() || _stopping;
                        });
                        if (_pending.empty() && _stopping)
                            return;
                        request = std::move(_pending.front());
                        _pending.pop_front();
                    }

                    std::string error{};
                    const std::span<const char> characters{ request.contents };
                    if (!utils::write_binary_file_atomic(
                            _path,
                            std::as_bytes(characters),
                            error))
                    {
                        std::fprintf(stderr,
                                     "Unable to write diagnostics log: %s\n",
                                     error.c_str());
                    }
                    if (timeline && !request.timeline_line.empty())
                    {
                        timeline << request.timeline_line << '\n';
                        timeline.flush();
                        if (!timeline)
                        {
                            std::fprintf(
                                stderr,
                                "Unable to write diagnostics timeline: %s\n",
                                utils::path_to_utf8(_timeline_path).c_str()
                            );
                            timeline.close();
                        }
                    }
                }
            }

            std::filesystem::path _path{};
            std::filesystem::path _timeline_path{};
            std::string _timeline_header{};
            std::mutex _mutex{};
            std::condition_variable _condition{};
            std::deque<write_request_t> _pending{};
            bool _stopping{ false };
            std::thread _thread{};
        };

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
                                          std::span<const std::byte> initial_save_ram,
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
                _save_ram_size = initial_save_ram.size();
                _save_ram_crc32 = crc32(initial_save_ram);
                _sample_rate = audio_format.sample_rate_hz;
                _channels = audio_format.channels;
                if (_sample_rate == 0u || _channels == 0u)
                    return false;

                _audio.open(_directory / "audio.wav", std::ios::binary | std::ios::trunc);
                _frames.open(_directory / "frames.csv", std::ios::binary | std::ios::trunc);
                if (!_audio || !_frames)
                    return false;

                if (!initial_save_ram.empty())
                {
                    std::ofstream save_output{
                        _directory / "initial_save_ram.srm", std::ios::binary | std::ios::trunc
                    };
                    if (!save_output)
                        return false;
                    save_output.write(reinterpret_cast<const char*>(initial_save_ram.data()),
                                      static_cast<std::streamsize>(initial_save_ram.size_bytes()));
                    if (!save_output)
                        return false;
                }

                write_wav_header(0u);
                _frames << "frame,joypad1,joypad2,first_audio_sample,sample_count,discontinuity,marker,"
                           "host_interval_ns,audio_queued_before_bytes,audio_queued_after_bytes,"
                           "audio_started,core_run_ns,present_ns,audio_queue_ns\n";
                _active = true;
                return true;
            }

            void record_frame(uint64_t frame,
                              uint16_t joypad_state_1,
                              uint16_t joypad_state_2,
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

                _joypad_states[0].push_back(joypad_state_1);
                _joypad_states[1].push_back(joypad_state_2);
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
                        << std::hex << std::setw(4) << std::setfill('0') << joypad_state_1 << ','
                        << std::setw(4) << std::setfill('0') << joypad_state_2
                        << std::dec << ','
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
                write_input_script("joypad1.script", _joypad_states[0]);
                write_input_script("joypad2.script", _joypad_states[1]);
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

            void write_input_script(std::string_view name,
                                    const std::vector<uint16_t>& states) const
            {
                std::ofstream output{ _directory / name, std::ios::binary | std::ios::trunc };
                bool first_entry{ true };
                size_t index{ 0 };
                while (index < states.size())
                {
                    const uint16_t state{ states[index] };
                    size_t end{ index };
                    while (end + 1u < states.size() && states[end + 1u] == state)
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
                output << "format=clover-capture-v5\n"
                       << "system=snes\n"
                       << "frame_numbering=first-run-frame-is-1\n"
                       << "joypad_encoding=snes-serial-16-msb-first\n"
                       << "rom_path=" << _rom_path << '\n'
                       << "rom_size=" << _rom_size << '\n'
                       << "rom_crc32=" << std::hex << std::setw(8) << std::setfill('0') << _rom_crc32 << std::dec << '\n'
                       << "initial_save_ram=" << (_save_ram_size == 0u ? "none" : "initial_save_ram.srm") << '\n'
                       << "initial_save_ram_size=" << _save_ram_size << '\n'
                       << "initial_save_ram_crc32=" << std::hex << std::setw(8) << std::setfill('0')
                       << _save_ram_crc32 << std::dec << '\n'
                       << "frames=" << _joypad_states[0].size() << '\n'
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
            std::array<std::vector<uint16_t>, 2> _joypad_states{};
            std::vector<uint64_t> _markers{};
            size_t _rom_size{ 0 };
            uint32_t _rom_crc32{ 0 };
            size_t _save_ram_size{ 0 };
            uint32_t _save_ram_crc32{ 0 };
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
        _audio_underruns = 0;
        _audio_feed_requests.store(0u, std::memory_order_relaxed);
        _audio_feed_requested_bytes.store(0u, std::memory_order_relaxed);
        _audio_feed_maximum_request_bytes.store(0u, std::memory_order_relaxed);
        _window = window;
        _display = display;
        _audio_input_sample_rate_hz = audio_format.sample_rate_hz;
        _audio_input_channels = audio_format.channels;
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
                                                      &audio_stream_get_callback,
                                                      this);
            if (_audio_stream == nullptr)
                std::fprintf(stderr, "Audio disabled: %s\n", SDL_GetError());
            else
            {
                const SDL_AudioDeviceID device{ SDL_GetAudioStreamDevice(_audio_stream) };
                SDL_AudioSpec device_spec{};
                int device_sample_frames{ 0 };
                static_cast<void>(SDL_GetAudioDeviceFormat(device,
                                                           &device_spec,
                                                           &device_sample_frames));
                std::printf("Clover SDL audio device: driver=%s device=%s "
                            "format=0x%x channels=%d rate=%d sample_frames=%d\n",
                            SDL_GetCurrentAudioDriver() != nullptr
                                ? SDL_GetCurrentAudioDriver()
                                : "unknown",
                            SDL_GetAudioDeviceName(device) != nullptr
                                ? SDL_GetAudioDeviceName(device)
                                : "unknown",
                            static_cast<unsigned int>(device_spec.format),
                            static_cast<int>(device_spec.channels),
                            device_spec.freq,
                            device_sample_frames);
            }
        }

        std::printf("Clover SDL video: renderer=%s\n",
                    SDL_GetRendererName(_renderer) != nullptr
                        ? SDL_GetRendererName(_renderer)
                        : "unknown");
        open_first_available_gamepad();
        refresh_gamepad_state();
        return true;
    }

    void sdl_presentation_t::shutdown() noexcept
    {
        for (size_t port{ 0 }; port < _gamepads.size(); ++port)
            close_gamepad(port);
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
        _audio_input_sample_rate_hz = 0u;
        _audio_input_channels = 0u;
        _display = {};
        _test_pattern.clear();
        _window = nullptr;
    }

    void sdl_presentation_t::handle_event(const SDL_Event& event) noexcept
    {
        if (_rom_library_visible
            && (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP))
        {
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat)
            {
                if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                    _rom_library_visible = false;
                else if (!_rom_library_names.empty() && event.key.scancode == SDL_SCANCODE_UP)
                {
                    if (_rom_library_selected > 0u)
                        --_rom_library_selected;
                    if (_rom_library_selected < _rom_library_scroll)
                        _rom_library_scroll = _rom_library_selected;
                }
                else if (!_rom_library_names.empty() && event.key.scancode == SDL_SCANCODE_DOWN)
                {
                    _rom_library_selected = std::min(_rom_library_selected + 1u,
                                                     _rom_library_names.size() - 1u);
                    if (_rom_library_selected >= _rom_library_scroll + k_library_visible_rows)
                        _rom_library_scroll = _rom_library_selected - k_library_visible_rows + 1u;
                }
                else if (!_rom_library_names.empty()
                         && event.key.scancode == SDL_SCANCODE_RETURN)
                {
                    _rom_library_selection = _rom_library_selected;
                    _rom_library_visible = false;
                }
            }
            return;
        }

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
                    _open_library_requested = true;
                else if (menu_shortcut && event.key.scancode == SDL_SCANCODE_R)
                    _reset_requested = true;
                else if (menu_shortcut && event.key.scancode == SDL_SCANCODE_Q)
                    _quit_requested = true;
                else if (!menu_shortcut && event.key.scancode == SDL_SCANCODE_LSHIFT)
                    _pause_requested = true;
                else if (!menu_shortcut && event.key.scancode == SDL_SCANCODE_PERIOD)
                    _frame_advance_requested = true;
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
        {
            float x{ event.motion.x };
            float y{ event.motion.y };
            if (_renderer != nullptr)
                static_cast<void>(SDL_RenderCoordinatesFromWindow(_renderer, x, y, &x, &y));
            _hovered_menu_item = menu_hit(_open_menu, _video_plane_names.size(), x, y);
            if (_rom_library_visible && x >= 20.f && x < 620.f && y >= k_library_row_y)
            {
                const size_t row{
                    static_cast<size_t>((y - k_library_row_y) / k_library_row_height)
                };
                const size_t index{ _rom_library_scroll + row };
                if (row < k_library_visible_rows && index < _rom_library_names.size())
                    _rom_library_selected = index;
            }
            break;
        }
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                float x{ event.button.x };
                float y{ event.button.y };
                if (_renderer != nullptr)
                    static_cast<void>(SDL_RenderCoordinatesFromWindow(_renderer, x, y, &x, &y));

                _hovered_menu_item = menu_hit(_open_menu, _video_plane_names.size(), x, y);
                _pressed_menu_item = _hovered_menu_item;
                if (_rom_library_visible && x >= 20.f && x < 620.f && y >= k_library_row_y)
                {
                    const size_t row{ static_cast<size_t>((y - k_library_row_y) / k_library_row_height) };
                    const size_t index{ _rom_library_scroll + row };
                    if (row < k_library_visible_rows && index < _rom_library_names.size())
                    {
                        _rom_library_selected = index;
                        if (event.button.clicks >= 2)
                        {
                            _rom_library_selection = index;
                            _rom_library_visible = false;
                        }
                    }
                }
                else if (_pressed_menu_item == k_menu_hit_none)
                {
                    _open_menu = 0u;
                }
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT)
            {
                float x{ event.button.x };
                float y{ event.button.y };
                if (_renderer != nullptr)
                    static_cast<void>(SDL_RenderCoordinatesFromWindow(_renderer, x, y, &x, &y));
                const uint8_t released_item{
                    menu_hit(_open_menu, _video_plane_names.size(), x, y)
                };
                if (released_item == _pressed_menu_item)
                {
                    switch (released_item)
                    {
                    case k_menu_hit_file:
                        _rom_library_visible = false;
                        _open_menu = _open_menu == k_file_menu ? 0u : k_file_menu;
                        break;
                    case k_menu_hit_emulation:
                        _rom_library_visible = false;
                        _open_menu = _open_menu == k_emulation_menu ? 0u : k_emulation_menu;
                        break;
                    case k_menu_hit_video:
                        _rom_library_visible = false;
                        _open_menu = _open_menu == k_video_menu ? 0u : k_video_menu;
                        break;
                    case k_menu_hit_import:
                        _import_rom_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_library:
                        _open_library_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_temporary:
                        _open_temporary_rom_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_diagnostics:
                        _open_diagnostics_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_quit:
                        _quit_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_pause:
                        _pause_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_frame_advance:
                        _frame_advance_requested = true;
                        _open_menu = 0u;
                        break;
                    case k_menu_hit_reset:
                        _reset_requested = true;
                        _open_menu = 0u;
                        break;
                    default:
                        if (released_item >= k_menu_hit_speed_base
                            && released_item < k_menu_hit_speed_base + k_speed_menu_items.size())
                        {
                            _requested_speed_selection = released_item - k_menu_hit_speed_base;
                            _open_menu = 0u;
                        }
                        else if (released_item >= k_menu_hit_video_base
                                 && released_item
                                     < k_menu_hit_video_base + _video_plane_names.size())
                        {
                            _video_plane_selection = released_item - k_menu_hit_video_base;
                            _open_menu = 0u;
                        }
                        break;
                    }
                }
                _pressed_menu_item = k_menu_hit_none;
                _hovered_menu_item = menu_hit(_open_menu, _video_plane_names.size(), x, y);
            }
            break;
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            _hovered_menu_item = k_menu_hit_none;
            break;
        case SDL_EVENT_GAMEPAD_ADDED:
            open_first_available_gamepad();
            break;
        case SDL_EVENT_GAMEPAD_REMOVED:
            for (size_t port{ 0 }; port < _gamepads.size(); ++port)
            {
                if (_gamepads[port] != nullptr && event.gdevice.which == _gamepad_ids[port])
                {
                    close_gamepad(port);
                    open_first_available_gamepad();
                    break;
                }
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
            || frame.width > _display.framebuffer_width
            || frame.height > _display.framebuffer_height)
        {
            return;
        }

        const SDL_Rect update_rect{
            0, 0, static_cast<int>(frame.width), static_cast<int>(frame.height)
        };
        static_cast<void>(SDL_UpdateTexture(_texture,
                                            &update_rect,
                                            frame.pixels,
                                            static_cast<int>(frame.pitch_bytes)));
        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 0u, 0u, 0u, SDL_ALPHA_OPAQUE));
        static_cast<void>(SDL_RenderClear(_renderer));
        const SDL_FRect destination{ presentation_rect() };
        const SDL_FRect source{
            0.f, 0.f, static_cast<float>(frame.width), static_cast<float>(frame.height)
        };
        static_cast<void>(SDL_RenderTexture(_renderer, _texture, &source, &destination));
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

        const auto set_interaction_color{ [this](uint8_t item, bool selected) noexcept
        {
            if (_pressed_menu_item == item)
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 46u, 82u, 128u,
                                                         SDL_ALPHA_OPAQUE));
            else if (_hovered_menu_item == item || selected)
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 74u, 71u, 68u,
                                                         SDL_ALPHA_OPAQUE));
            else
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 54u, 52u, 50u,
                                                         SDL_ALPHA_OPAQUE));
        } };
        const auto draw_menu_background{ [this, &set_interaction_color](
            const SDL_FRect& rect,
            uint8_t item,
            bool selected = false
        ) noexcept
        {
            set_interaction_color(item, selected);
            static_cast<void>(SDL_RenderFillRect(_renderer, &rect));
        } };

        draw_menu_background(k_file_menu_button,
                             k_menu_hit_file,
                             _open_menu == k_file_menu);
        draw_menu_background(k_emulation_menu_button,
                             k_menu_hit_emulation,
                             _open_menu == k_emulation_menu);
        if (!_video_plane_names.empty())
        {
            draw_menu_background(k_video_menu_button,
                                 k_menu_hit_video,
                                 _open_menu == k_video_menu);
        }

        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u, SDL_ALPHA_OPAQUE));
        static_cast<void>(SDL_RenderDebugText(_renderer, 12.f, 10.f, "File"));
        static_cast<void>(SDL_RenderDebugText(_renderer, 68.f, 10.f, "Emulation"));
        if (!_video_plane_names.empty())
            static_cast<void>(SDL_RenderDebugText(_renderer, 164.f, 10.f, "Video"));

        if (_open_menu == k_emulation_menu)
        {
            static constexpr std::array<const char*, 5> speed_labels{
                "Speed: 0.5x", "Speed: 1x", "Speed: 2x", "Speed: 4x", "Speed: Unlimited"
            };
            const std::string pause_label{ _paused ? "[x] Pause" : "[ ] Pause" };
            const auto draw_item{ [this, &draw_menu_background](
                const SDL_FRect& item,
                uint8_t hit,
                const char* item_label
            ) noexcept
            {
                draw_menu_background(item, hit);
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u,
                                                         SDL_ALPHA_OPAQUE));
                static_cast<void>(SDL_RenderDebugText(_renderer,
                                                      item.x + 12.f,
                                                      item.y + 10.f,
                                                      item_label));
            } };
            draw_item(k_pause_menu_item, k_menu_hit_pause, pause_label.c_str());
            draw_item(k_frame_advance_menu_item, k_menu_hit_frame_advance, "Frame Advance");
            draw_item(k_reset_menu_item, k_menu_hit_reset, "Reset");
            for (size_t index{ 0 }; index < k_speed_menu_items.size(); ++index)
            {
                std::string speed_label{ index == _speed_selection ? "(*) " : "( ) " };
                speed_label += speed_labels[index];
                draw_item(k_speed_menu_items[index],
                          static_cast<uint8_t>(k_menu_hit_speed_base + index),
                          speed_label.c_str());
            }
        }

        if (_open_menu == k_file_menu)
        {
            static constexpr std::array<const char*, 5> labels{
                "Import ROM to Library...",
                "Open ROM Library...",
                "Open ROM Temporarily...",
                "Open Diagnostics Folder",
                "Quit"
            };
            static constexpr std::array<SDL_FRect, 5> items{
                k_import_rom_menu_item,
                k_open_library_menu_item,
                k_open_temporary_rom_menu_item,
                k_open_diagnostics_menu_item,
                k_quit_menu_item
            };
            static constexpr std::array<uint8_t, 5> hits{
                k_menu_hit_import,
                k_menu_hit_library,
                k_menu_hit_temporary,
                k_menu_hit_diagnostics,
                k_menu_hit_quit
            };
            for (size_t index{ 0 }; index < items.size(); ++index)
            {
                draw_menu_background(items[index], hits[index]);
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u, SDL_ALPHA_OPAQUE));
                static_cast<void>(SDL_RenderDebugText(_renderer,
                                                      items[index].x + 12.f,
                                                      items[index].y + 10.f,
                                                      labels[index]));
            }
        }

        if (_open_menu == k_video_menu)
        {
            for (size_t index{ 0 }; index < _video_plane_names.size(); ++index)
            {
                const SDL_FRect item{
                    152.f,
                    k_menu_bar_height + static_cast<float>(index) * 28.f,
                    k_video_menu_width,
                    28.f
                };
                const uint8_t hit{ static_cast<uint8_t>(k_menu_hit_video_base + index) };
                draw_menu_background(item, hit);
                static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u,
                                                         SDL_ALPHA_OPAQUE));
                std::string item_label{ _video_plane_enabled[index] ? "[x] " : "[ ] " };
                item_label += _video_plane_names[index];
                static_cast<void>(SDL_RenderDebugText(_renderer,
                                                      item.x + 12.f,
                                                      item.y + 10.f,
                                                      item_label.c_str()));
            }
        }

        if (_rom_library_visible)
        {
            const SDL_FRect panel{ 20.f, 44.f, std::min(static_cast<float>(output_width) - 40.f, 600.f),
                                   std::min(static_cast<float>(output_height) - 64.f, 640.f) };
            static_cast<void>(SDL_SetRenderDrawColor(_renderer, 24u, 24u, 24u, SDL_ALPHA_OPAQUE));
            static_cast<void>(SDL_RenderFillRect(_renderer, &panel));
            static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u, SDL_ALPHA_OPAQUE));
            static_cast<void>(SDL_RenderDebugText(_renderer, 32.f, 54.f, "ROM Library"));
            if (_rom_library_names.empty())
            {
                static_cast<void>(SDL_RenderDebugText(_renderer, 32.f, k_library_row_y,
                                                      "No ROMs imported yet. Press Esc to close."));
            }
            else
            {
                const size_t end{ std::min(_rom_library_scroll + k_library_visible_rows,
                                           _rom_library_names.size()) };
                for (size_t index{ _rom_library_scroll }; index < end; ++index)
                {
                    const float y{ k_library_row_y
                        + static_cast<float>(index - _rom_library_scroll) * k_library_row_height };
                    if (index == _rom_library_selected)
                    {
                        const SDL_FRect selection{ 28.f, y - 6.f, panel.w - 16.f, 20.f };
                        static_cast<void>(SDL_SetRenderDrawColor(_renderer, 60u, 88u, 132u,
                                                                 SDL_ALPHA_OPAQUE));
                        static_cast<void>(SDL_RenderFillRect(_renderer, &selection));
                    }
                    static_cast<void>(SDL_SetRenderDrawColor(_renderer, 236u, 234u, 231u,
                                                             SDL_ALPHA_OPAQUE));
                    std::string label{ _rom_library_names[index] };
                    if (label.size() > 66u)
                        label.resize(66u);
                    static_cast<void>(SDL_RenderDebugText(_renderer, 32.f, y, label.c_str()));
                }
                static_cast<void>(SDL_RenderDebugText(_renderer, 32.f, panel.y + panel.h - 18.f,
                                                      "Up/Down + Enter, double-click, Esc to close"));
            }
        }
    }

    void sdl_presentation_t::queue_audio(const frontend::audio_frame_view_t& audio) noexcept
    {
        if (audio.discontinuity)
            std::fprintf(stderr, "Core audio discontinuity: output frame exceeded its buffer\n");
        if (_audio_stream == nullptr || audio.interleaved_samples.empty())
            return;

        _audio_queued_bytes_before_put = SDL_GetAudioStreamQueued(_audio_stream);
        if (_audio_started
            && sdl_audio_pacing::queue_is_empty(_audio_queued_bytes_before_put))
            ++_audio_underruns;

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
                >= sdl_audio_pacing::initial_queue_bytes(audio.sample_rate_hz, audio.channels))
        {
            if (SDL_ResumeAudioDevice(SDL_GetAudioStreamDevice(_audio_stream)))
                _audio_started = true;
        }
    }

    void SDLCALL sdl_presentation_t::audio_stream_get_callback(void* userdata,
                                                               SDL_AudioStream*,
                                                               int additional_amount,
                                                               int) noexcept
    {
        if (additional_amount <= 0 || userdata == nullptr)
            return;
        auto& presentation{ *static_cast<sdl_presentation_t*>(userdata) };
        presentation._audio_feed_requests.fetch_add(1u, std::memory_order_relaxed);
        presentation._audio_feed_requested_bytes.fetch_add(
            static_cast<uint64_t>(additional_amount),
            std::memory_order_relaxed
        );
        uint64_t maximum{
            presentation._audio_feed_maximum_request_bytes.load(std::memory_order_relaxed)
        };
        while (maximum < static_cast<uint64_t>(additional_amount)
               && !presentation._audio_feed_maximum_request_bytes.compare_exchange_weak(
                   maximum,
                   static_cast<uint64_t>(additional_amount),
                   std::memory_order_relaxed))
        {
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

    const frontend::gamepad_state_t& sdl_presentation_t::gamepad_state(size_t port) const noexcept
    {
        return _inputs[port < _inputs.size() ? port : 0u];
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

    bool sdl_presentation_t::consume_import_rom_request() noexcept
    {
        const bool requested{ _import_rom_requested };
        _import_rom_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_open_library_request() noexcept
    {
        const bool requested{ _open_library_requested };
        _open_library_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_open_temporary_rom_request() noexcept
    {
        const bool requested{ _open_temporary_rom_requested };
        _open_temporary_rom_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_open_diagnostics_request() noexcept
    {
        const bool requested{ _open_diagnostics_requested };
        _open_diagnostics_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_quit_request() noexcept
    {
        const bool requested{ _quit_requested };
        _quit_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_pause_request() noexcept
    {
        const bool requested{ _pause_requested };
        _pause_requested = false;
        return requested;
    }

    bool sdl_presentation_t::consume_frame_advance_request() noexcept
    {
        const bool requested{ _frame_advance_requested };
        _frame_advance_requested = false;
        return requested;
    }

    std::optional<size_t> sdl_presentation_t::consume_speed_selection() noexcept
    {
        const std::optional<size_t> selection{ _requested_speed_selection };
        _requested_speed_selection.reset();
        return selection;
    }

    std::optional<size_t> sdl_presentation_t::consume_video_plane_selection() noexcept
    {
        const std::optional<size_t> selection{ _video_plane_selection };
        _video_plane_selection.reset();
        return selection;
    }

    void sdl_presentation_t::set_paused(bool paused) noexcept
    {
        _paused = paused;
    }

    void sdl_presentation_t::set_speed_selection(size_t index) noexcept
    {
        if (index < k_speed_menu_items.size())
            _speed_selection = index;
    }

    void sdl_presentation_t::set_video_planes(
        std::span<const frontend::video_plane_descriptor_t> planes)
    {
        _video_plane_names.clear();
        _video_plane_enabled.clear();
        _video_plane_names.reserve(planes.size());
        _video_plane_enabled.reserve(planes.size());
        for (const frontend::video_plane_descriptor_t& plane : planes)
        {
            _video_plane_names.emplace_back(plane.label);
            _video_plane_enabled.push_back(plane.enabled);
        }
        if (_video_plane_names.empty() && _open_menu == k_video_menu)
            _open_menu = 0u;
    }

    void sdl_presentation_t::show_rom_library(std::vector<std::string> display_names) noexcept
    {
        _key_states.fill(false);
        refresh_gamepad_state();
        _rom_library_names = std::move(display_names);
        _rom_library_selected = 0u;
        _rom_library_scroll = 0u;
        _rom_library_selection.reset();
        _rom_library_visible = true;
        _open_menu = 0u;
    }

    std::optional<size_t> sdl_presentation_t::consume_library_selection() noexcept
    {
        const std::optional<size_t> selection{ _rom_library_selection };
        _rom_library_selection.reset();
        return selection;
    }

    bool sdl_presentation_t::rom_library_visible() const noexcept
    {
        return _rom_library_visible;
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

    uint64_t sdl_presentation_t::audio_underruns() const noexcept
    {
        return _audio_underruns;
    }

    uint64_t sdl_presentation_t::audio_feed_requests() const noexcept
    {
        return _audio_feed_requests.load(std::memory_order_relaxed);
    }

    uint64_t sdl_presentation_t::audio_feed_requested_bytes() const noexcept
    {
        return _audio_feed_requested_bytes.load(std::memory_order_relaxed);
    }

    uint64_t sdl_presentation_t::audio_feed_maximum_request_bytes() const noexcept
    {
        return _audio_feed_maximum_request_bytes.load(std::memory_order_relaxed);
    }

    std::string sdl_presentation_t::diagnostic_environment() const
    {
        std::ostringstream output{};
        int power_seconds{ -1 };
        int power_percent{ -1 };
        const SDL_PowerState power_state{
            SDL_GetPowerInfo(&power_seconds, &power_percent)
        };
        output << "platform=" << SDL_GetPlatform() << '\n'
               << "sdl_revision=" << SDL_GetRevision() << '\n'
               << "cpu_brand=" << utils::cpu_brand() << '\n'
               << "logical_cpu_cores=" << SDL_GetNumLogicalCPUCores() << '\n'
               << "system_ram_mib=" << SDL_GetSystemRAM() << '\n'
               << "cpu_cache_line_bytes=" << SDL_GetCPUCacheLineSize() << '\n'
               << "cpu_has_sse2=" << (SDL_HasSSE2() ? 1 : 0) << '\n'
               << "cpu_has_avx=" << (SDL_HasAVX() ? 1 : 0) << '\n'
               << "cpu_has_avx2=" << (SDL_HasAVX2() ? 1 : 0) << '\n'
               << "power_state=" << power_state_name(power_state) << '\n'
               << "power_percent=" << power_percent << '\n'
               << "power_seconds=" << power_seconds << '\n'
               << "video_renderer="
               << (_renderer != nullptr && SDL_GetRendererName(_renderer) != nullptr
                       ? SDL_GetRendererName(_renderer)
                       : "unavailable")
               << '\n'
               << "window_display_scale="
               << (_window != nullptr ? SDL_GetWindowDisplayScale(_window) : 0.f) << '\n'
               << "audio_driver="
               << (SDL_GetCurrentAudioDriver() != nullptr
                       ? SDL_GetCurrentAudioDriver()
                       : "unavailable")
               << '\n'
               << "audio_input_channels=" << static_cast<int>(_audio_input_channels) << '\n'
               << "audio_input_rate_hz=" << _audio_input_sample_rate_hz << '\n';
        if (_window != nullptr)
        {
            const SDL_DisplayID display_id{ SDL_GetDisplayForWindow(_window) };
            if (const SDL_DisplayMode* const mode{ SDL_GetCurrentDisplayMode(display_id) })
            {
                output << "display_width=" << mode->w << '\n'
                       << "display_height=" << mode->h << '\n'
                       << "display_refresh_hz=" << mode->refresh_rate << '\n';
            }
        }
        if (_renderer != nullptr)
        {
            int output_width{ 0 };
            int output_height{ 0 };
            static_cast<void>(SDL_GetCurrentRenderOutputSize(_renderer,
                                                             &output_width,
                                                             &output_height));
            output << "renderer_output_width=" << output_width << '\n'
                   << "renderer_output_height=" << output_height << '\n';
        }
        if (_audio_stream != nullptr)
        {
            const SDL_AudioDeviceID device{ SDL_GetAudioStreamDevice(_audio_stream) };
            SDL_AudioSpec spec{};
            int sample_frames{ 0 };
            static_cast<void>(SDL_GetAudioDeviceFormat(device, &spec, &sample_frames));
            output << "audio_device="
                   << (SDL_GetAudioDeviceName(device) != nullptr
                           ? SDL_GetAudioDeviceName(device)
                           : "unavailable")
                   << '\n'
                   << "audio_device_format=0x" << std::hex
                   << static_cast<unsigned int>(spec.format) << std::dec << '\n'
                   << "audio_device_channels=" << static_cast<int>(spec.channels) << '\n'
                   << "audio_device_rate_hz=" << spec.freq << '\n'
                   << "audio_device_sample_frames=" << sample_frames << '\n';
        }
        return output.str();
    }

    bool sdl_presentation_t::key_pressed(SDL_Scancode scancode) const noexcept
    {
        return scancode >= 0
            && scancode < SDL_SCANCODE_COUNT
            && _key_states[static_cast<size_t>(scancode)];
    }

    bool sdl_presentation_t::physical_control_pressed(frontend::gamepad_button_t button, size_t port) const noexcept
    {
        using button_t = frontend::gamepad_button_t;
        switch (button)
        {
        case button_t::dpad_up:
            return (port == 0u && key_pressed(SDL_SCANCODE_UP))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_UP, port)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTY, false, port);
        case button_t::dpad_down:
            return (port == 0u && key_pressed(SDL_SCANCODE_DOWN))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_DOWN, port)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTY, true, port);
        case button_t::dpad_left:
            return (port == 0u && key_pressed(SDL_SCANCODE_LEFT))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_LEFT, port)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTX, false, port);
        case button_t::dpad_right:
            return (port == 0u && key_pressed(SDL_SCANCODE_RIGHT))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_DPAD_RIGHT, port)
                || gamepad_axis_pressed(SDL_GAMEPAD_AXIS_LEFTX, true, port);
        case button_t::face_south:
            return (port == 0u && key_pressed(SDL_SCANCODE_X))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_SOUTH, port);
        case button_t::face_east:
            return (port == 0u && key_pressed(SDL_SCANCODE_C))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_EAST, port);
        case button_t::face_west:
            return (port == 0u && key_pressed(SDL_SCANCODE_S))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_WEST, port);
        case button_t::face_north:
            return (port == 0u && key_pressed(SDL_SCANCODE_D))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_NORTH, port);
        case button_t::left_shoulder:
            return (port == 0u && key_pressed(SDL_SCANCODE_F))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, port);
        case button_t::right_shoulder:
            return (port == 0u && key_pressed(SDL_SCANCODE_V))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, port);
        case button_t::back:
            return (port == 0u && key_pressed(SDL_SCANCODE_SPACE))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_BACK, port);
        case button_t::start:
            return (port == 0u && key_pressed(SDL_SCANCODE_RETURN))
                || gamepad_button_pressed(SDL_GAMEPAD_BUTTON_START, port);
        }
        return false;
    }

    bool sdl_presentation_t::gamepad_button_pressed(SDL_GamepadButton button, size_t port) const noexcept
    {
        return port < _gamepads.size()
            && _gamepads[port] != nullptr
            && SDL_GetGamepadButton(_gamepads[port], button);
    }

    bool sdl_presentation_t::gamepad_axis_pressed(SDL_GamepadAxis axis, bool positive, size_t port) const noexcept
    {
        if (port >= _gamepads.size() || _gamepads[port] == nullptr)
            return false;
        const int16_t value{ SDL_GetGamepadAxis(_gamepads[port], axis) };
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

    bool sdl_presentation_t::open_gamepad(SDL_JoystickID joystick_id, size_t port) noexcept
    {
        if (port >= _gamepads.size() || _gamepads[port] != nullptr || !SDL_IsGamepad(joystick_id))
            return false;
        SDL_Gamepad* const gamepad{ SDL_OpenGamepad(joystick_id) };
        if (gamepad == nullptr)
            return false;

        _gamepads[port] = gamepad;
        _gamepad_ids[port] = joystick_id;
        return true;
    }

    void sdl_presentation_t::open_first_available_gamepad() noexcept
    {
        int gamepad_count{ 0 };
        SDL_JoystickID* const gamepad_ids{ SDL_GetGamepads(&gamepad_count) };
        if (gamepad_ids == nullptr)
            return;
        for (size_t port{ 0 }; port < _gamepads.size(); ++port)
        {
            if (_gamepads[port] != nullptr)
                continue;
            for (int index{ 0 }; index < gamepad_count; ++index)
            {
                const SDL_JoystickID candidate{ gamepad_ids[index] };
                bool already_open{ false };
                for (size_t open_port{ 0 }; open_port < _gamepads.size(); ++open_port)
                {
                    already_open = already_open
                        || (_gamepads[open_port] != nullptr && _gamepad_ids[open_port] == candidate);
                }
                if (!already_open && open_gamepad(candidate, port))
                    break;
            }
        }
        SDL_free(gamepad_ids);
    }

    void sdl_presentation_t::close_gamepad(size_t port) noexcept
    {
        if (port >= _gamepads.size())
            return;
        if (_gamepads[port] != nullptr)
        {
            SDL_CloseGamepad(_gamepads[port]);
            _gamepads[port] = nullptr;
        }
        _gamepad_ids[port] = 0;
    }

    void sdl_presentation_t::refresh_gamepad_state() noexcept
    {
        for (size_t port{ 0 }; port < _inputs.size(); ++port)
        {
            for (uint8_t raw{ 0 }; raw <= static_cast<uint8_t>(frontend::gamepad_button_t::start); ++raw)
            {
                const auto button{ static_cast<frontend::gamepad_button_t>(raw) };
                _inputs[port].set(button, physical_control_pressed(button, port));
            }
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

        std::printf("Clover build: compiler=%s platform_toolset=%s\n",
                    CLOVER_BUILD_COMPILER,
                    CLOVER_BUILD_PLATFORM_TOOLSET);
        static_cast<void>(SDL_SetAppMetadata("Clover", CLOVER_VERSION, "com.bunnysoft.clover"));
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD))
        {
            std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
            return 1;
        }

        const Uint32 first_rom_dialog_event_type{ SDL_RegisterEvents(2) };
        if (first_rom_dialog_event_type == 0u)
        {
            std::fprintf(stderr, "Unable to register ROM dialog event: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        const Uint32 import_rom_dialog_event_type{ first_rom_dialog_event_type };
        const Uint32 temporary_rom_dialog_event_type{ first_rom_dialog_event_type + 1u };

        char* const preference_path{ SDL_GetPrefPath("BunnySoft", "Clover") };
        if (preference_path == nullptr)
        {
            std::fprintf(stderr, "Unable to locate Clover application data: %s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }
        const std::filesystem::path data_root{ preference_path };
        SDL_free(preference_path);
        rom_library_t rom_library{};
        std::string library_error{};
        if (!rom_library.initialize(data_root, library_error))
        {
            std::fprintf(stderr, "Unable to initialize ROM library: %s\n", library_error.c_str());
            SDL_Quit();
            return 1;
        }
        std::printf("Clover application data: %s\n", data_root.string().c_str());

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
            media = utils::read_binary_file(rom_path);
            if (media.empty() || !core->load_media(media))
            {
                std::fprintf(stderr, "Unable to load media: %s\n", rom_path.string().c_str());
                SDL_Quit();
                return 1;
            }
            const std::string content_hash{
                rom_library.identify(frontend::system_id_t::snes, media)
            };
            save_path = rom_library.save_path(frontend::system_id_t::snes, content_hash);
            if (!core->persistent_memory().empty()
                && !rom_library.migrate_sibling_save(rom_path,
                                                     save_path,
                                                     core->persistent_memory().size(),
                                                     library_error))
            {
                std::fprintf(stderr, "Unable to migrate save RAM: %s\n", library_error.c_str());
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
        if (frontend::video_plane_control_t* const planes{ core->video_plane_control() })
            presentation.set_video_planes(planes->video_planes());
        if (media_loaded)
            presentation.present(core->video_frame());
        else
            presentation.present_test_pattern();

        const std::filesystem::path diagnostic_log_path{
            data_root / "logs" / "clover-latest.log"
        };
        const std::filesystem::path diagnostic_timeline_path{
            data_root / "logs" / "clover-performance.csv"
        };
        diagnostic_log_writer_t diagnostic_log{
            diagnostic_log_path,
            diagnostic_timeline_path,
            "interval,start_seconds,end_seconds,start_frame,end_frame,frames,"
            "wall_fps,active_fps,power_state,power_percent,"
            "core_average_ms,core_p50_ms,core_p95_ms,core_p99_ms,core_maximum_ms,"
            "core_above_budget,active_loop_average_ms,active_loop_maximum_ms,"
            "presentation_average_ms,presentation_maximum_ms,"
            "event_processing_average_ms,event_processing_maximum_ms,"
            "scheduler_lateness_count,scheduler_lateness_average_ms,"
            "scheduler_lateness_maximum_ms,scheduler_lateness_above_budget,"
            "completed_presentations,skipped_presentations,deadline_resets,"
            "audio_queue_before_minimum,audio_queue_before_average,"
            "audio_queue_after_minimum,audio_queue_after_average,"
            "audio_underruns,audio_feed_requests,audio_feed_requested_bytes,"
            "audio_discontinuities,marker_count,marker_first_frame,marker_last_frame"
        };
        const std::string diagnostic_environment{
            presentation.diagnostic_environment()
        };
        std::printf("Clover diagnostics log: %s\n",
                    utils::path_to_utf8(diagnostic_log_path).c_str());
        std::printf("Clover performance timeline: %s\n",
                    utils::path_to_utf8(diagnostic_timeline_path).c_str());

        capture_session_t capture{};
        if (!command_line.capture_path.empty()
            && !capture.initialize(std::filesystem::path{ command_line.capture_path },
                                   rom_path.string(),
                                   media,
                                   core->persistent_memory(),
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
        duration_stats_t core_run_stats{};
        duration_stats_t audio_queue_stats{};
        duration_stats_t presentation_stats{};
        duration_stats_t active_loop_stats{};
        duration_stats_t event_processing_stats{};
        duration_stats_t scheduler_sleep_stats{};
        duration_stats_t scheduler_oversleep_stats{};
        duration_stats_t scheduler_lateness_stats{};
        duration_stats_t save_schedule_stats{};
        duration_stats_t save_flush_stats{};
        duration_stats_t diagnostics_submit_stats{};
        duration_stats_t dialog_idle_stats{};
        duration_stats_t library_idle_stats{};
        duration_stats_t no_media_idle_stats{};
        duration_stats_t paused_idle_stats{};
        integer_stats_t audio_queue_before_stats{};
        integer_stats_t audio_queue_after_stats{};
        duration_stats_t interval_core_run_stats{};
        duration_stats_t interval_presentation_stats{};
        duration_stats_t interval_active_loop_stats{};
        duration_stats_t interval_event_processing_stats{};
        duration_stats_t interval_scheduler_lateness_stats{};
        integer_stats_t interval_audio_queue_before_stats{};
        integer_stats_t interval_audio_queue_after_stats{};
        std::vector<uint64_t> interval_core_samples_ns{};
        interval_core_samples_ns.reserve(512u);
        uint64_t skipped_presentations{ 0 };
        uint64_t completed_presentations{ 0 };
        uint64_t scheduler_deadline_resets{ 0 };
        uint64_t normal_speed_frames{ 0 };
        async_save_writer_t save_writer{};
        uint64_t next_diagnostic_log_ns{ run_start_ns };
        uint64_t interval_index{ 0 };
        uint64_t interval_start_ns{ run_start_ns };
        uint64_t interval_start_frame{ 0 };
        uint64_t interval_completed_presentations{ 0 };
        uint64_t interval_skipped_presentations{ 0 };
        uint64_t interval_scheduler_deadline_resets{ 0 };
        uint64_t interval_audio_underruns_start{ presentation.audio_underruns() };
        uint64_t interval_audio_feed_requests_start{ presentation.audio_feed_requests() };
        uint64_t interval_audio_feed_requested_bytes_start{
            presentation.audio_feed_requested_bytes()
        };
        uint64_t interval_audio_discontinuities{ 0 };
        uint64_t interval_marker_count{ 0 };
        uint64_t interval_marker_first_frame{ 0 };
        uint64_t interval_marker_last_frame{ 0 };
        const auto diagnostic_snapshot{
            [&](bool final) -> std::string
            {
                const uint64_t now_ns{ SDL_GetTicksNS() };
                const double elapsed{
                    static_cast<double>(now_ns - run_start_ns)
                        / static_cast<double>(k_nanoseconds_per_second)
                };
                const double active_seconds{
                    static_cast<double>(active_loop_stats.total_ns)
                        / static_cast<double>(k_nanoseconds_per_second)
                };
                int power_seconds{ -1 };
                int power_percent{ -1 };
                const SDL_PowerState power_state{
                    SDL_GetPowerInfo(&power_seconds, &power_percent)
                };
                std::ostringstream output{};
                output << std::fixed << std::setprecision(3);
                output << "Clover diagnostics\n"
                       << "status=" << (final ? "final" : "running") << '\n'
                       << "diagnostics_schema=3\n"
                       << "version=" << CLOVER_VERSION << '\n'
                       << "build_revision=" << CLOVER_BUILD_REVISION << '\n'
                       << "compiler=" << CLOVER_BUILD_COMPILER << '\n'
                       << "platform_toolset=" << CLOVER_BUILD_PLATFORM_TOOLSET << '\n'
                       << "application_data=" << utils::path_to_utf8(data_root) << '\n'
                       << "rom=" << (media_loaded
                               ? utils::path_to_utf8(rom_path.filename())
                               : "none")
                       << '\n'
                       << "performance_timeline="
                       << utils::path_to_utf8(diagnostic_timeline_path.filename())
                       << '\n'
                       << diagnostic_environment
                       << "current_power_state=" << power_state_name(power_state) << '\n'
                       << "current_power_percent=" << power_percent << '\n'
                       << "current_power_seconds=" << power_seconds << '\n'
                       << "frames=" << frames_run << '\n'
                       << "normal_speed_frames=" << normal_speed_frames << '\n'
                       << "elapsed_seconds=" << elapsed << '\n'
                       << "active_emulation_seconds=" << active_seconds << '\n'
                       << "effective_fps="
                       << (elapsed > 0.0 ? static_cast<double>(frames_run) / elapsed : 0.0)
                       << '\n'
                       << "active_emulation_fps="
                       << (active_seconds > 0.0
                               ? static_cast<double>(frames_run) / active_seconds
                               : 0.0)
                       << '\n'
                       << "target_fps=" << display.nominal_refresh_hz << '\n'
                       << "completed_presentations=" << completed_presentations << '\n'
                       << "skipped_presentations=" << skipped_presentations << '\n'
                       << "scheduler_deadline_resets=" << scheduler_deadline_resets << '\n';
                append_duration_stats(output, "core_run", core_run_stats);
                append_duration_stats(output, "audio_queue_call", audio_queue_stats);
                append_duration_stats(output, "presentation", presentation_stats);
                append_duration_stats(output, "active_loop", active_loop_stats);
                append_duration_stats(output, "event_processing", event_processing_stats);
                append_duration_stats(output, "scheduler_sleep", scheduler_sleep_stats);
                append_duration_stats(output, "scheduler_oversleep", scheduler_oversleep_stats);
                append_duration_stats(output, "scheduler_lateness", scheduler_lateness_stats);
                append_duration_stats(output, "save_schedule", save_schedule_stats);
                append_duration_stats(output, "save_flush", save_flush_stats);
                append_duration_stats(output, "diagnostics_submit", diagnostics_submit_stats);
                append_duration_stats(output, "dialog_idle", dialog_idle_stats);
                append_duration_stats(output, "library_idle", library_idle_stats);
                append_duration_stats(output, "no_media_idle", no_media_idle_stats);
                append_duration_stats(output, "paused_idle", paused_idle_stats);
                append_integer_stats(output, "audio_queue_before_bytes",
                                     audio_queue_before_stats);
                append_integer_stats(output, "audio_queue_after_bytes",
                                     audio_queue_after_stats);
                output
                       << "audio_underruns=" << presentation.audio_underruns() << '\n'
                       << "audio_feed_requests=" << presentation.audio_feed_requests() << '\n'
                       << "audio_feed_requested_bytes="
                       << presentation.audio_feed_requested_bytes() << '\n'
                       << "audio_feed_maximum_request_bytes="
                       << presentation.audio_feed_maximum_request_bytes() << '\n'
                       << "audio_discontinuities=" << audio_discontinuities << '\n';
                return output.str();
            }
        };
        const auto interval_snapshot{
            [&](uint64_t end_ns) -> std::string
            {
                const uint64_t interval_frames{ frames_run - interval_start_frame };
                if (end_ns <= interval_start_ns
                    || (interval_frames == 0u
                        && interval_active_loop_stats.count == 0u
                        && interval_marker_count == 0u))
                {
                    return {};
                }

                std::sort(interval_core_samples_ns.begin(),
                          interval_core_samples_ns.end());
                const auto percentile_ms{
                    [&](uint64_t percentile) -> double
                    {
                        if (interval_core_samples_ns.empty())
                            return 0.0;
                        const size_t index{
                            static_cast<size_t>(
                                (percentile * interval_core_samples_ns.size() + 99u) / 100u
                            ) - 1u
                        };
                        return static_cast<double>(interval_core_samples_ns[index])
                            / 1'000'000.0;
                    }
                };
                const auto duration_average_ms{
                    [](const duration_stats_t& stats) -> double
                    {
                        return stats.count == 0u
                            ? 0.0
                            : static_cast<double>(stats.total_ns)
                                / static_cast<double>(stats.count)
                                / 1'000'000.0;
                    }
                };
                const auto integer_average{
                    [](const integer_stats_t& stats) -> double
                    {
                        return stats.count == 0u
                            ? 0.0
                            : static_cast<double>(stats.total)
                                / static_cast<double>(stats.count);
                    }
                };
                const double wall_seconds{
                    static_cast<double>(end_ns - interval_start_ns)
                        / static_cast<double>(k_nanoseconds_per_second)
                };
                const double active_seconds{
                    static_cast<double>(interval_active_loop_stats.total_ns)
                        / static_cast<double>(k_nanoseconds_per_second)
                };
                int power_seconds{ -1 };
                int power_percent{ -1 };
                const SDL_PowerState power_state{
                    SDL_GetPowerInfo(&power_seconds, &power_percent)
                };
                const uint64_t audio_underruns{
                    presentation.audio_underruns() - interval_audio_underruns_start
                };
                const uint64_t audio_feed_requests{
                    presentation.audio_feed_requests() - interval_audio_feed_requests_start
                };
                const uint64_t audio_feed_requested_bytes{
                    presentation.audio_feed_requested_bytes()
                        - interval_audio_feed_requested_bytes_start
                };

                std::ostringstream output{};
                output << std::fixed << std::setprecision(3)
                       << interval_index << ','
                       << static_cast<double>(interval_start_ns - run_start_ns)
                            / static_cast<double>(k_nanoseconds_per_second) << ','
                       << static_cast<double>(end_ns - run_start_ns)
                            / static_cast<double>(k_nanoseconds_per_second) << ','
                       << interval_start_frame << ','
                       << frames_run << ','
                       << interval_frames << ','
                       << (wall_seconds > 0.0
                               ? static_cast<double>(interval_frames) / wall_seconds
                               : 0.0) << ','
                       << (active_seconds > 0.0
                               ? static_cast<double>(interval_frames) / active_seconds
                               : 0.0) << ','
                       << power_state_name(power_state) << ','
                       << power_percent << ','
                       << duration_average_ms(interval_core_run_stats) << ','
                       << percentile_ms(50u) << ','
                       << percentile_ms(95u) << ','
                       << percentile_ms(99u) << ','
                       << static_cast<double>(interval_core_run_stats.maximum_ns)
                            / 1'000'000.0 << ','
                       << interval_core_run_stats.above_16_ms << ','
                       << duration_average_ms(interval_active_loop_stats) << ','
                       << static_cast<double>(interval_active_loop_stats.maximum_ns)
                            / 1'000'000.0 << ','
                       << duration_average_ms(interval_presentation_stats) << ','
                       << static_cast<double>(interval_presentation_stats.maximum_ns)
                            / 1'000'000.0 << ','
                       << duration_average_ms(interval_event_processing_stats) << ','
                       << static_cast<double>(interval_event_processing_stats.maximum_ns)
                            / 1'000'000.0 << ','
                       << interval_scheduler_lateness_stats.count << ','
                       << duration_average_ms(interval_scheduler_lateness_stats) << ','
                       << static_cast<double>(interval_scheduler_lateness_stats.maximum_ns)
                            / 1'000'000.0 << ','
                       << interval_scheduler_lateness_stats.above_16_ms << ','
                       << interval_completed_presentations << ','
                       << interval_skipped_presentations << ','
                       << interval_scheduler_deadline_resets << ','
                       << interval_audio_queue_before_stats.minimum << ','
                       << integer_average(interval_audio_queue_before_stats) << ','
                       << interval_audio_queue_after_stats.minimum << ','
                       << integer_average(interval_audio_queue_after_stats) << ','
                       << audio_underruns << ','
                       << audio_feed_requests << ','
                       << audio_feed_requested_bytes << ','
                       << interval_audio_discontinuities << ','
                       << interval_marker_count << ','
                       << interval_marker_first_frame << ','
                       << interval_marker_last_frame;
                return output.str();
            }
        };
        const auto reset_interval{
            [&](uint64_t start_ns)
            {
                ++interval_index;
                interval_start_ns = start_ns;
                interval_start_frame = frames_run;
                interval_core_run_stats = {};
                interval_presentation_stats = {};
                interval_active_loop_stats = {};
                interval_event_processing_stats = {};
                interval_scheduler_lateness_stats = {};
                interval_audio_queue_before_stats = {};
                interval_audio_queue_after_stats = {};
                interval_core_samples_ns.clear();
                interval_completed_presentations = 0;
                interval_skipped_presentations = 0;
                interval_scheduler_deadline_resets = 0;
                interval_audio_underruns_start = presentation.audio_underruns();
                interval_audio_feed_requests_start = presentation.audio_feed_requests();
                interval_audio_feed_requested_bytes_start =
                    presentation.audio_feed_requested_bytes();
                interval_audio_discontinuities = 0;
                interval_marker_count = 0;
                interval_marker_first_frame = 0;
                interval_marker_last_frame = 0;
            }
        };
        bool paused{ false };
        bool frame_advance_pending{ false };
        size_t speed_selection{ 1u };
        bool rom_dialog_active{ false };
        uint64_t window_focus_restore_ns{ 0u };
        uint8_t window_focus_restore_attempts{ 0u };
        std::filesystem::path selected_rom_path{};
        std::string selected_rom_hash{};
        std::string selected_rom_display_name{};
        pending_rom_source_t selected_rom_source{ pending_rom_source_t::none };
        std::vector<rom_library_entry_t> library_entries{};
        while (running && (frame_limit == 0u || frames_run < frame_limit))
        {
            if (media_loaded)
                static_cast<void>(save_writer.poll(*core));
            const uint64_t frame_start_ns{ SDL_GetTicksNS() };
            if (frame_start_ns >= next_diagnostic_log_ns)
            {
                const uint64_t submit_start_ns{ SDL_GetTicksNS() };
                diagnostic_log.submit(
                    diagnostic_snapshot(false),
                    interval_snapshot(frame_start_ns)
                );
                reset_interval(frame_start_ns);
                diagnostics_submit_stats.observe(SDL_GetTicksNS() - submit_start_ns);
                next_diagnostic_log_ns = frame_start_ns + 5u * k_nanoseconds_per_second;
            }
            const uint64_t event_processing_start_ns{ SDL_GetTicksNS() };
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
                if (event.type == import_rom_dialog_event_type
                    || event.type == temporary_rom_dialog_event_type)
                {
                    rom_dialog_active = false;
                    window_focus_restore_ns = SDL_GetTicksNS() + k_dialog_focus_retry_ns;
                    window_focus_restore_attempts = 0u;
                    if (event.user.code == 0 && event.user.data1 != nullptr)
                    {
                        selected_rom_path = utils::path_from_utf8(
                            static_cast<const char*>(event.user.data1)
                        );
                        selected_rom_source = event.type == import_rom_dialog_event_type
                            ? pending_rom_source_t::import
                            : pending_rom_source_t::temporary;
                    }
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
            const uint64_t event_processing_duration_ns{
                SDL_GetTicksNS() - event_processing_start_ns
            };
            event_processing_stats.observe(event_processing_duration_ns);
            interval_event_processing_stats.observe(event_processing_duration_ns);
            if (!running)
                break;

            const bool reset_requested{ presentation.consume_reset_request() };
            const bool pause_requested{ presentation.consume_pause_request() };
            const bool frame_advance_requested{ presentation.consume_frame_advance_request() };
            const std::optional<size_t> requested_speed{
                presentation.consume_speed_selection()
            };
            const std::optional<size_t> requested_video_plane{
                presentation.consume_video_plane_selection()
            };
            const bool import_rom_requested{ presentation.consume_import_rom_request() };
            const bool open_library_requested{ presentation.consume_open_library_request() };
            const bool open_temporary_rom_requested{
                presentation.consume_open_temporary_rom_request()
            };
            if (presentation.consume_open_diagnostics_request())
            {
                const std::string url{
                    utils::path_to_file_url(diagnostic_log_path.parent_path())
                };
                if (!SDL_OpenURL(url.c_str()))
                    std::fprintf(stderr,
                                 "Unable to open diagnostics folder: %s\n",
                                 SDL_GetError());
            }
            if (presentation.consume_quit_request())
            {
                running = false;
                break;
            }
            const bool media_change_requested{
                import_rom_requested || open_library_requested || open_temporary_rom_requested
            };
            const bool runtime_override_requested{
                pause_requested || frame_advance_requested || requested_speed.has_value()
                    || requested_video_plane.has_value()
            };
            if ((reset_requested || media_change_requested || runtime_override_requested)
                && capture.active())
            {
                std::fprintf(stderr,
                             "Reset, ROM loading, and runtime overrides are disabled during "
                             "a deterministic capture\n");
            }
            else
            {
                if (pause_requested)
                {
                    paused = !paused;
                    frame_advance_pending = false;
                    presentation.set_paused(paused);
                    presentation.reset_audio();
                    next_frame_deadline_ns = SDL_GetTicksNS();
                    previous_frame_start_ns = next_frame_deadline_ns;
                }
                if (frame_advance_requested)
                {
                    paused = true;
                    frame_advance_pending = media_loaded;
                    presentation.set_paused(true);
                    presentation.reset_audio();
                    next_frame_deadline_ns = SDL_GetTicksNS();
                    previous_frame_start_ns = next_frame_deadline_ns;
                }
                if (requested_speed.has_value()
                    && *requested_speed < k_speed_menu_items.size())
                {
                    static constexpr std::array<const char*, 5> speed_names{
                        "0.5x", "1x", "2x", "4x", "Unlimited"
                    };
                    speed_selection = *requested_speed;
                    presentation.set_speed_selection(speed_selection);
                    presentation.reset_audio();
                    next_frame_deadline_ns = SDL_GetTicksNS();
                    previous_frame_start_ns = next_frame_deadline_ns;
                    std::printf("Emulation speed: %s\n", speed_names[speed_selection]);
                }
                if (requested_video_plane.has_value())
                {
                    if (frontend::video_plane_control_t* const planes{
                            core->video_plane_control()
                        };
                        planes != nullptr)
                    {
                        const std::span<const frontend::video_plane_descriptor_t> descriptors{
                            planes->video_planes()
                        };
                        if (*requested_video_plane < descriptors.size())
                        {
                            const frontend::video_plane_descriptor_t descriptor{
                                descriptors[*requested_video_plane]
                            };
                            static_cast<void>(planes->set_video_plane_enabled(
                                descriptor.id,
                                !descriptor.enabled
                            ));
                            presentation.set_video_planes(planes->video_planes());
                        }
                    }
                }
                if (reset_requested)
                {
                    if (!media_loaded)
                    {
                        std::fprintf(stderr, "Reset ignored: no ROM is loaded\n");
                    }
                    else
                    {
                        static_cast<void>(save_writer.flush(*core, save_path));
                        core->reset();
                        presentation.reset_audio();
                        next_frame_deadline_ns = SDL_GetTicksNS();
                        previous_frame_start_ns = next_frame_deadline_ns;
                        std::printf("Emulator reset\n");
                    }
                }
                if ((import_rom_requested || open_temporary_rom_requested) && !rom_dialog_active)
                {
                    static constexpr std::array<SDL_DialogFileFilter, 2> filters{
                        SDL_DialogFileFilter{ "SNES ROMs", "sfc;smc" },
                        SDL_DialogFileFilter{ "All files", "*" }
                    };
                    rom_dialog_active = true;
                    window_focus_restore_ns = 0u;
                    SDL_ShowOpenFileDialog(rom_dialog_callback,
                                           reinterpret_cast<void*>(
                                               static_cast<uintptr_t>(
                                                   import_rom_requested
                                                       ? import_rom_dialog_event_type
                                                       : temporary_rom_dialog_event_type
                                               )
                                           ),
                                           window,
                                           filters.data(),
                                           static_cast<int>(filters.size()),
                                           nullptr,
                                           false);
                }
                if (open_library_requested)
                {
                    library_error.clear();
                    library_entries = rom_library.entries(frontend::system_id_t::snes,
                                                          library_error);
                    if (!library_error.empty())
                    {
                        std::fprintf(stderr, "Unable to read ROM library: %s\n",
                                     library_error.c_str());
                    }
                    else
                    {
                        std::vector<std::string> names{};
                        names.reserve(library_entries.size());
                        for (const rom_library_entry_t& entry : library_entries)
                            names.push_back(entry.display_name);
                        presentation.show_rom_library(std::move(names));
                    }
                }
            }

            if (const std::optional<size_t> selection{ presentation.consume_library_selection() };
                selection.has_value() && *selection < library_entries.size())
            {
                selected_rom_path = library_entries[*selection].rom_path;
                selected_rom_hash = library_entries[*selection].content_hash;
                selected_rom_display_name = library_entries[*selection].display_name;
                selected_rom_source = pending_rom_source_t::library;
            }

            if (!selected_rom_path.empty())
            {
                const std::filesystem::path requested_path{ std::move(selected_rom_path) };
                selected_rom_path.clear();
                std::vector<std::byte> requested_media{
                    utils::read_binary_file(requested_path)
                };
                auto replacement{ frontend::create_emulator_core(frontend::system_id_t::snes) };
                std::filesystem::path active_requested_path{ requested_path };
                std::string active_display_name{
                    selected_rom_display_name.empty()
                        ? utils::path_to_utf8(requested_path.stem())
                        : std::move(selected_rom_display_name)
                };
                selected_rom_display_name.clear();
                std::string requested_hash{ std::move(selected_rom_hash) };
                selected_rom_hash.clear();
                const pending_rom_source_t requested_source{ selected_rom_source };
                selected_rom_source = pending_rom_source_t::none;
                if (requested_source == pending_rom_source_t::library
                    && rom_library.identify(frontend::system_id_t::snes, requested_media)
                        != requested_hash)
                {
                    std::fprintf(stderr, "Library ROM failed its SHA-256 identity check: %s\n",
                                 requested_path.string().c_str());
                    requested_media.clear();
                }
                if (!replacement
                    || requested_media.empty()
                    || !replacement->load_media(requested_media))
                {
                    std::fprintf(stderr, "Unable to load media: %s\n", requested_path.string().c_str());
                }
                else
                {
                    if (requested_source == pending_rom_source_t::import)
                    {
                        library_error.clear();
                        const rom_import_result_t imported{
                            rom_library.import_rom(frontend::system_id_t::snes,
                                                   requested_path,
                                                   requested_media,
                                                   library_error)
                        };
                        if (!library_error.empty())
                        {
                            std::fprintf(stderr, "Unable to import ROM: %s\n", library_error.c_str());
                            replacement.reset();
                        }
                        else
                        {
                            active_requested_path = imported.entry.rom_path;
                            active_display_name = imported.entry.display_name;
                            requested_hash = imported.entry.content_hash;
                            std::printf("%s ROM in library: %s\n",
                                        imported.newly_imported ? "Imported" : "Found existing",
                                        active_requested_path.string().c_str());
                        }
                    }
                    if (replacement && requested_hash.empty())
                        requested_hash = rom_library.identify(frontend::system_id_t::snes,
                                                              requested_media);
                    const std::filesystem::path requested_save_path{
                        rom_library.save_path(frontend::system_id_t::snes, requested_hash)
                    };
                    if (replacement
                        && requested_source != pending_rom_source_t::library
                        && !replacement->persistent_memory().empty())
                    {
                        library_error.clear();
                        if (!rom_library.migrate_sibling_save(requested_path,
                                                             requested_save_path,
                                                             replacement->persistent_memory().size(),
                                                             library_error))
                        {
                            std::fprintf(stderr, "Unable to migrate save RAM: %s\n",
                                         library_error.c_str());
                            replacement.reset();
                        }
                    }
                    if (replacement && !load_persistent_memory(*replacement, requested_save_path))
                    {
                        std::fprintf(stderr,
                                     "Keeping current ROM because its replacement save could not be loaded\n");
                        replacement.reset();
                    }
                    if (replacement
                        && media_loaded
                        && !save_writer.flush(*core, save_path))
                    {
                        std::fprintf(stderr,
                                     "Keeping current ROM because its save RAM could not be written\n");
                        replacement.reset();
                    }
                    if (replacement)
                    {
                        replacement->power_on();
                        core = std::move(replacement);
                        rom_path = active_requested_path;
                        save_path = requested_save_path;
                        media_loaded = true;
                        const std::string window_title{ "Clover — " + active_display_name };
                        static_cast<void>(SDL_SetWindowTitle(window, window_title.c_str()));
                        if (frontend::video_plane_control_t* const planes{
                                core->video_plane_control()
                            })
                        {
                            presentation.set_video_planes(planes->video_planes());
                        }
                        else
                        {
                            presentation.set_video_planes({});
                        }
                        presentation.reset_audio();
                        presentation.present(core->video_frame());
                        next_frame_deadline_ns = SDL_GetTicksNS();
                        previous_frame_start_ns = next_frame_deadline_ns;
                        std::printf("Loaded ROM: %s\n", rom_path.string().c_str());
                    }
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
                dialog_idle_stats.observe(SDL_GetTicksNS() - frame_start_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            if (presentation.rom_library_visible())
            {
                if (media_loaded)
                    presentation.present(core->video_frame());
                else
                    presentation.present_test_pattern();
                SDL_Delay(10u);
                library_idle_stats.observe(SDL_GetTicksNS() - frame_start_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            if (!media_loaded)
            {
                presentation.present_test_pattern();
                SDL_Delay(10u);
                no_media_idle_stats.observe(SDL_GetTicksNS() - frame_start_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            if (paused && !frame_advance_pending)
            {
                presentation.present(core->video_frame());
                SDL_Delay(10u);
                paused_idle_stats.observe(SDL_GetTicksNS() - frame_start_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            const bool advancing_paused_frame{ paused && frame_advance_pending };
            const uint64_t active_loop_start_ns{ frame_start_ns };
            frame_advance_pending = false;
            const frontend::gamepad_state_t gamepad_state{ presentation.gamepad_state(0u) };
            const frontend::gamepad_state_t gamepad_state_2{ presentation.gamepad_state(1u) };
            const bool capture_marker{ presentation.consume_capture_marker() };
            if (capture_marker)
            {
                const uint64_t marker_frame{ frames_run + 1u };
                ++interval_marker_count;
                if (interval_marker_first_frame == 0u)
                    interval_marker_first_frame = marker_frame;
                interval_marker_last_frame = marker_frame;
            }
            core->set_gamepad_state(0u, gamepad_state);
            core->set_gamepad_state(1u, gamepad_state_2);
            static constexpr std::array<size_t, 5> frames_per_presentation{ 1u, 1u, 2u, 4u, 8u };
            const size_t batch_size{
                advancing_paused_frame ? 1u : frames_per_presentation[speed_selection]
            };
            for (size_t batch_index{ 0u };
                 batch_index < batch_size && (frame_limit == 0u || frames_run < frame_limit);
                 ++batch_index)
            {
                const uint64_t core_run_start_ns{ SDL_GetTicksNS() };
                core->run_frame();
                const uint64_t core_run_end_ns{ SDL_GetTicksNS() };
                ++frames_run;
                if (!paused && speed_selection == 1u)
                    ++normal_speed_frames;
                const frontend::audio_frame_view_t audio{ core->audio_frame() };
                const bool batch_complete{
                    batch_index + 1u == batch_size
                        || (frame_limit != 0u && frames_run >= frame_limit)
                };
                uint64_t audio_queue_end_ns{ core_run_end_ns };
                uint64_t present_end_ns{ core_run_end_ns };
                if (batch_complete)
                {
                    if (!paused && speed_selection == 1u)
                        presentation.queue_audio(audio);
                    audio_queue_end_ns = SDL_GetTicksNS();

                    const bool presentation_is_late{
                        !advancing_paused_frame
                            && speed_selection == 1u
                            && core_run_end_ns > next_frame_deadline_ns
                            && core_run_end_ns - next_frame_deadline_ns
                                > target_frame_duration_ns * 2u
                    };
                    if (presentation_is_late)
                    {
                        ++skipped_presentations;
                        ++interval_skipped_presentations;
                        present_end_ns = audio_queue_end_ns;
                    }
                    else
                    {
                        presentation.present(core->video_frame());
                        present_end_ns = SDL_GetTicksNS();
                        ++completed_presentations;
                        ++interval_completed_presentations;
                    }
                }
                const uint64_t core_run_duration_ns{
                    core_run_end_ns - core_run_start_ns
                };
                core_run_stats.observe(core_run_duration_ns);
                interval_core_run_stats.observe(core_run_duration_ns);
                interval_core_samples_ns.push_back(core_run_duration_ns);
                if (batch_complete && !paused && speed_selection == 1u)
                    audio_queue_stats.observe(audio_queue_end_ns - core_run_end_ns);
                if (batch_complete && present_end_ns > audio_queue_end_ns)
                {
                    presentation_stats.observe(present_end_ns - audio_queue_end_ns);
                    interval_presentation_stats.observe(
                        present_end_ns - audio_queue_end_ns
                    );
                }
                if (batch_complete && !paused && speed_selection == 1u)
                {
                    audio_queue_before_stats.observe(
                        presentation.audio_queued_bytes_before_put()
                    );
                    audio_queue_after_stats.observe(
                        presentation.audio_queued_bytes_after_put()
                    );
                    interval_audio_queue_before_stats.observe(
                        presentation.audio_queued_bytes_before_put()
                    );
                    interval_audio_queue_after_stats.observe(
                        presentation.audio_queued_bytes_after_put()
                    );
                }
                if (capture.active())
                {
                    capture.record_frame(frames_run,
                                         frontend::snes_joypad_state(gamepad_state),
                                         frontend::snes_joypad_state(gamepad_state_2),
                                         audio,
                                         core->video_frame(),
                                         capture_marker,
                                         host_interval_ns,
                                         presentation.audio_queued_bytes_before_put(),
                                         presentation.audio_queued_bytes_after_put(),
                                         presentation.audio_started(),
                                         core_run_end_ns - core_run_start_ns,
                                         present_end_ns - audio_queue_end_ns,
                                         audio_queue_end_ns - core_run_end_ns);
                }
                audio_sample_values += audio.interleaved_samples.size();
                if (audio.discontinuity)
                {
                    ++audio_discontinuities;
                    ++interval_audio_discontinuities;
                }
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
                {
                    const uint64_t save_start_ns{ SDL_GetTicksNS() };
                    static_cast<void>(save_writer.schedule(*core, save_path));
                    save_schedule_stats.observe(SDL_GetTicksNS() - save_start_ns);
                }
            }

            if (advancing_paused_frame)
            {
                const uint64_t active_loop_duration_ns{
                    SDL_GetTicksNS() - active_loop_start_ns
                };
                active_loop_stats.observe(active_loop_duration_ns);
                interval_active_loop_stats.observe(active_loop_duration_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                previous_frame_start_ns = next_frame_deadline_ns;
                continue;
            }

            static constexpr std::array<double, 4> speed_multipliers{ 0.5, 1.0, 2.0, 4.0 };
            if (speed_selection >= speed_multipliers.size())
            {
                const uint64_t active_loop_duration_ns{
                    SDL_GetTicksNS() - active_loop_start_ns
                };
                active_loop_stats.observe(active_loop_duration_ns);
                interval_active_loop_stats.observe(active_loop_duration_ns);
                next_frame_deadline_ns = SDL_GetTicksNS();
                continue;
            }
            const uint64_t nominal_paced_batch_duration_ns{ static_cast<uint64_t>(std::llround(
                static_cast<double>(target_frame_duration_ns)
                    * static_cast<double>(batch_size)
                    / speed_multipliers[speed_selection]
            )) };
            const int64_t audio_adjustment_ns{
                speed_selection == 1u && presentation.audio_started()
                    ? sdl_audio_pacing::adjustment_ns(
                        presentation.audio_queued_bytes_after_put(),
                        core->audio_frame().sample_rate_hz,
                        core->audio_frame().channels)
                    : 0
            };
            const uint64_t paced_batch_duration_ns{ static_cast<uint64_t>(std::max<int64_t>(
                1,
                static_cast<int64_t>(nominal_paced_batch_duration_ns) + audio_adjustment_ns
            )) };
            next_frame_deadline_ns += paced_batch_duration_ns;
            const uint64_t current_ticks_ns{ SDL_GetTicksNS() };
            if (next_frame_deadline_ns > current_ticks_ns)
            {
                const uint64_t requested_sleep_ns{
                    next_frame_deadline_ns - current_ticks_ns
                };
                SDL_DelayPrecise(requested_sleep_ns);
                const uint64_t actual_sleep_ns{ SDL_GetTicksNS() - current_ticks_ns };
                scheduler_sleep_stats.observe(actual_sleep_ns);
                if (actual_sleep_ns > requested_sleep_ns)
                    scheduler_oversleep_stats.observe(actual_sleep_ns - requested_sleep_ns);
            }
            else
            {
                const uint64_t lateness_ns{ current_ticks_ns - next_frame_deadline_ns };
                scheduler_lateness_stats.observe(lateness_ns);
                interval_scheduler_lateness_stats.observe(lateness_ns);
                if (lateness_ns > paced_batch_duration_ns * 3u)
                {
                    next_frame_deadline_ns = current_ticks_ns;
                    ++scheduler_deadline_resets;
                    ++interval_scheduler_deadline_resets;
                }
            }
            const uint64_t active_loop_duration_ns{
                SDL_GetTicksNS() - active_loop_start_ns
            };
            active_loop_stats.observe(active_loop_duration_ns);
            interval_active_loop_stats.observe(active_loop_duration_ns);
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
        std::printf("Clover SDL timing: max_core_ms=%.3f max_present_ms=%.3f "
                    "skipped_presentations=%llu\n",
                    static_cast<double>(core_run_stats.maximum_ns) / 1'000'000.0,
                    static_cast<double>(presentation_stats.maximum_ns) / 1'000'000.0,
                    static_cast<unsigned long long>(skipped_presentations));
        std::printf("Clover SDL audio: max_values_per_frame=%zu discontinuities=%llu "
                    "queue_min_bytes=%d queue_max_bytes=%d underruns=%llu "
                    "feed_requests=%llu\n",
                    max_audio_sample_values_per_frame,
                    static_cast<unsigned long long>(audio_discontinuities),
                    audio_queue_after_stats.minimum,
                    audio_queue_after_stats.maximum,
                    static_cast<unsigned long long>(presentation.audio_underruns()),
                    static_cast<unsigned long long>(presentation.audio_feed_requests()));
        const uint64_t save_flush_start_ns{ SDL_GetTicksNS() };
        const bool save_flushed{
            !media_loaded || save_writer.flush(*core, save_path)
        };
        save_flush_stats.observe(SDL_GetTicksNS() - save_flush_start_ns);
        const uint64_t final_submit_start_ns{ SDL_GetTicksNS() };
        diagnostic_log.submit(
            diagnostic_snapshot(true),
            interval_snapshot(final_submit_start_ns)
        );
        diagnostics_submit_stats.observe(SDL_GetTicksNS() - final_submit_start_ns);
        diagnostic_log.finish();
        capture.finalize();
        presentation.shutdown();
        SDL_DestroyWindow(window);
        SDL_Quit();
        return save_flushed ? 0 : 1;
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

        const std::vector<std::byte> data{ utils::read_binary_file(save_path) };
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

}
