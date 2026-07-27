//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/WorkbenchAppShell.h"

#include "clover/analysis/snes/Formatter.h"
#include "clover/analysis/snes/StaticListing.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/utils/FileSystem.h"
#include "clover/workbench/Project.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using clover::analysis::snes::decoded_instruction_t;
    using clover::workbench::address_key_t;

    constexpr std::string_view k_cpu_address_space{ "snes.cpu-bus" };
    constexpr size_t k_listing_rows{ 38u };

    struct command_line_t
    {
        std::filesystem::path rom_path{};
        std::filesystem::path project_root{};
        uint32_t address{ 0 };
        bool address_set{ false };
        bool valid{ false };
    };

    enum class edit_kind_t : uint8_t
    {
        none,
        label,
        comment
    };

    [[nodiscard]] bool parse_address(std::string value, uint32_t& address)
    {
        if (!value.empty() && value.front() == '$')
            value.erase(value.begin());
        if (value.starts_with("0x") || value.starts_with("0X"))
            value.erase(0, 2);
        if (const size_t colon{ value.find(':') }; colon != std::string::npos)
            value.erase(colon, 1);
        if (value.empty() || value.size() > 6u)
            return false;
        const auto result{
            std::from_chars(value.data(), value.data() + value.size(), address, 16)
        };
        return result.ec == std::errc{}
            && result.ptr == value.data() + value.size()
            && address <= 0x00ffffffu;
    }

    [[nodiscard]] command_line_t parse_arguments(int argc, char** argv)
    {
        command_line_t command{};
        if (argc < 2)
            return command;
        command.rom_path = clover::utils::path_from_utf8(argv[1]);
        command.valid = true;
        for (int index{ 2 }; index < argc; ++index)
        {
            if (index + 1 >= argc)
            {
                command.valid = false;
                break;
            }
            const std::string_view argument{ argv[index++] };
            const std::string value{ argv[index] };
            if (argument == "--address")
            {
                command.address_set = parse_address(value, command.address);
                command.valid = command.valid && command.address_set;
            }
            else if (argument == "--project-root")
            {
                command.project_root = clover::utils::path_from_utf8(value);
            }
            else
            {
                command.valid = false;
            }
        }
        return command;
    }

    [[nodiscard]] std::string formatted_address(uint32_t address)
    {
        std::ostringstream output{};
        output << '$' << std::uppercase << std::hex << std::setfill('0')
               << std::setw(2) << ((address >> 16u) & 0xffu)
               << ':' << std::setw(4) << (address & 0xffffu);
        return output.str();
    }

    [[nodiscard]] std::string formatted_bytes(
        const decoded_instruction_t& instruction
    )
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0');
        for (uint8_t index{ 0 }; index < instruction.byte_count; ++index)
        {
            if (index != 0u)
                output << ' ';
            output << std::setw(2)
                   << static_cast<uint32_t>(instruction.bytes[index]);
        }
        return output.str();
    }

    [[nodiscard]] std::filesystem::path default_project_root()
    {
        char* const preference_path{
            SDL_GetPrefPath("BunnySoft", "CloverWorkbench")
        };
        if (preference_path == nullptr)
            return {};
        const std::filesystem::path result{
            clover::utils::path_from_utf8(preference_path)
        };
        SDL_free(preference_path);
        return result / "projects";
    }

    [[nodiscard]] std::optional<uint32_t> reset_entry(
        const clover::analysis::snes::debug_target_byte_source_t& source
    )
    {
        const auto low{ source.inspect(0x00fffcu) };
        const auto high{ source.inspect(0x00fffdu) };
        using clover::analysis::snes::byte_inspection_status_t;
        if (low.status != byte_inspection_status_t::available
            || high.status != byte_inspection_status_t::available)
        {
            return std::nullopt;
        }
        return static_cast<uint32_t>(low.value)
            | (static_cast<uint32_t>(high.value) << 8u);
    }

    void draw_text(SDL_Renderer* renderer,
                   float x,
                   float y,
                   std::string_view text)
    {
        const std::string owned{ text };
        static_cast<void>(SDL_RenderDebugText(renderer, x, y, owned.c_str()));
    }

    void draw_panel(SDL_Renderer* renderer,
                    const SDL_FRect& rect,
                    Uint8 red,
                    Uint8 green,
                    Uint8 blue)
    {
        static_cast<void>(SDL_SetRenderDrawColor(renderer, red, green, blue, 255));
        static_cast<void>(SDL_RenderFillRect(renderer, &rect));
        static_cast<void>(SDL_SetRenderDrawColor(renderer, 62, 70, 84, 255));
        static_cast<void>(SDL_RenderRect(renderer, &rect));
    }

    template<typename Fact>
    [[nodiscard]] const Fact* fact_at(const std::vector<Fact>& facts,
                                      uint32_t address)
    {
        const auto found{
            std::find_if(
                facts.begin(),
                facts.end(),
                [address](const Fact& fact)
                {
                    return fact.location.address_space == k_cpu_address_space
                        && fact.location.address == address;
                }
            )
        };
        return found == facts.end() ? nullptr : &*found;
    }
}

namespace clover::platform
{
    int workbench_app_shell_t::run(int argc, char** argv)
    {
        const command_line_t command{ parse_arguments(argc, argv) };
        if (!command.valid)
        {
            std::fprintf(
                stderr,
                "Usage: clover_workbench <rom> [--address BB:AAAA] "
                "[--project-root PATH]\n"
            );
            return 2;
        }

        const std::vector<std::byte> media{
            utils::read_binary_file(command.rom_path)
        };
        if (media.empty())
        {
            std::fprintf(stderr, "Unable to read the selected ROM.\n");
            return 1;
        }

        std::unique_ptr<frontend::emulator_core_t> core{
            frontend::create_emulator_core(frontend::system_id_t::snes)
        };
        if (core == nullptr || !core->load_media(media))
        {
            std::fprintf(stderr, "ROM was not recognized as supported SNES media.\n");
            return 1;
        }
        const frontend::debug_target_t* const target{ core->debug_target() };
        if (target == nullptr)
        {
            std::fprintf(stderr, "SNES debug target is unavailable.\n");
            return 1;
        }
        const analysis::snes::debug_target_byte_source_t source{
            *target,
            frontend::snes_debug::k_cpu_bus_space,
            frontend::snes_debug::k_canonical_media_space
        };

        const std::filesystem::path project_root{
            command.project_root.empty()
                ? default_project_root()
                : command.project_root
        };
        if (project_root.empty())
        {
            std::fprintf(stderr, "Unable to locate the Workbench project directory.\n");
            return 1;
        }

        workbench::project_t project{};
        std::string error{};
        if (!project.open(
                project_root,
                frontend::system_id_t::snes,
                media,
                error
            )
            || !project.import_snes_hardware_symbols(error))
        {
            std::fprintf(stderr, "Unable to open Workbench project: %s\n", error.c_str());
            return 1;
        }

        uint32_t listing_address{
            command.address_set
                ? command.address
                : reset_entry(source).value_or(0x008000u)
        };
        if (!project.record_navigation(
                { std::string{ k_cpu_address_space }, listing_address },
                "disassembly",
                error
            ))
        {
            std::fprintf(stderr, "Unable to record navigation: %s\n", error.c_str());
            return 1;
        }

        if (!SDL_Init(SDL_INIT_VIDEO))
        {
            std::fprintf(stderr, "SDL initialization failed: %s\n", SDL_GetError());
            return 1;
        }
        struct sdl_cleanup_t
        {
            SDL_Window* window{};
            SDL_Renderer* renderer{};
            ~sdl_cleanup_t()
            {
                if (renderer != nullptr)
                    SDL_DestroyRenderer(renderer);
                if (window != nullptr)
                    SDL_DestroyWindow(window);
                SDL_Quit();
            }
        } sdl{};
        if (!SDL_CreateWindowAndRenderer(
                "Clover Workbench",
                1440,
                860,
                SDL_WINDOW_RESIZABLE,
                &sdl.window,
                &sdl.renderer
            ))
        {
            std::fprintf(stderr, "Unable to create Workbench window: %s\n", SDL_GetError());
            return 1;
        }

        analysis::snes::cpu_decode_context_t context{
            .emulation = analysis::snes::bit_state_t::set,
            .accumulator_width = analysis::snes::bit_state_t::set,
            .index_width = analysis::snes::bit_state_t::set,
            .direct_page = 0u,
            .data_bank = 0u
        };
        analysis::snes::static_listing_result_t listing{};
        std::vector<workbench::named_fact_t> labels{};
        std::vector<workbench::named_fact_t> comments{};
        std::vector<workbench::bookmark_t> bookmarks{};
        std::vector<workbench::classification_t> classifications{};
        std::vector<workbench::symbol_t> symbols{};
        size_t selected{ 0u };
        bool refresh_listing{ true };
        bool refresh_facts{ true };
        bool running{ true };
        edit_kind_t edit_kind{ edit_kind_t::none };
        std::string edit_buffer{};
        std::string status{
            "Project " + utils::path_to_utf8(project.path())
        };

        const auto selected_address = [&]() -> uint32_t
        {
            if (listing.instructions.empty())
                return listing_address;
            return listing.instructions[
                std::min(selected, listing.instructions.size() - 1u)
            ].address;
        };
        const auto navigate = [&](uint32_t address)
        {
            listing_address = address & 0x00ffffffu;
            selected = 0u;
            refresh_listing = true;
            error.clear();
            if (!project.record_navigation(
                    { std::string{ k_cpu_address_space }, listing_address },
                    "disassembly",
                    error
                ))
            {
                status = error;
            }
        };

        while (running)
        {
            if (refresh_listing)
            {
                listing = analysis::snes::build_static_listing(
                    source,
                    {
                        .start_address = listing_address,
                        .maximum_instructions = k_listing_rows,
                        .maximum_bytes = 512u,
                        .context = context
                    }
                );
                selected = std::min(
                    selected,
                    listing.instructions.empty()
                        ? 0u
                        : listing.instructions.size() - 1u
                );
                refresh_listing = false;
            }
            if (refresh_facts)
            {
                error.clear();
                labels = project.labels(error);
                comments = project.comments(error);
                bookmarks = project.bookmarks(error);
                classifications = project.classifications(error);
                symbols = project.symbols(error);
                if (!error.empty())
                    status = error;
                refresh_facts = false;
            }

            SDL_Event event{};
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                    continue;
                }
                if (edit_kind != edit_kind_t::none)
                {
                    if (event.type == SDL_EVENT_TEXT_INPUT)
                    {
                        edit_buffer += event.text.text;
                        continue;
                    }
                    if (event.type != SDL_EVENT_KEY_DOWN
                        || event.key.repeat)
                    {
                        continue;
                    }
                    if (event.key.scancode == SDL_SCANCODE_BACKSPACE)
                    {
                        if (!edit_buffer.empty())
                            edit_buffer.pop_back();
                    }
                    else if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                    {
                        edit_kind = edit_kind_t::none;
                        SDL_StopTextInput(sdl.window);
                        status = "Edit cancelled";
                    }
                    else if (event.key.scancode == SDL_SCANCODE_RETURN
                             && !edit_buffer.empty())
                    {
                        const address_key_t location{
                            std::string{ k_cpu_address_space },
                            selected_address()
                        };
                        error.clear();
                        const bool saved{
                            edit_kind == edit_kind_t::label
                                ? project.set_label(location, edit_buffer, error)
                                : project.set_comment(location, edit_buffer, error)
                        };
                        status = saved ? "Saved project fact" : error;
                        refresh_facts = saved;
                        edit_kind = edit_kind_t::none;
                        SDL_StopTextInput(sdl.window);
                    }
                    continue;
                }
                if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat)
                    continue;

                const bool alt{
                    (event.key.mod & SDL_KMOD_ALT) != 0
                };
                if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = false;
                else if (alt && event.key.scancode == SDL_SCANCODE_LEFT)
                {
                    error.clear();
                    const auto entry{ project.navigate_back(error) };
                    if (entry.has_value())
                    {
                        listing_address = static_cast<uint32_t>(entry->location.address);
                        selected = 0u;
                        refresh_listing = true;
                    }
                    status = error.empty() ? "Navigation: back" : error;
                }
                else if (alt && event.key.scancode == SDL_SCANCODE_RIGHT)
                {
                    error.clear();
                    const auto entry{ project.navigate_forward(error) };
                    if (entry.has_value())
                    {
                        listing_address = static_cast<uint32_t>(entry->location.address);
                        selected = 0u;
                        refresh_listing = true;
                    }
                    status = error.empty() ? "Navigation: forward" : error;
                }
                else if (event.key.scancode == SDL_SCANCODE_UP && selected > 0u)
                    --selected;
                else if (event.key.scancode == SDL_SCANCODE_DOWN
                         && selected + 1u < listing.instructions.size())
                    ++selected;
                else if (event.key.scancode == SDL_SCANCODE_PAGEUP)
                    navigate((listing_address - 0x40u) & 0x00ffffffu);
                else if (event.key.scancode == SDL_SCANCODE_PAGEDOWN)
                    navigate(listing.next_address);
                else if (event.key.scancode == SDL_SCANCODE_RETURN
                         && !listing.instructions.empty())
                {
                    const auto& instruction{ listing.instructions[selected] };
                    if (instruction.direct_target.has_value())
                        navigate(*instruction.direct_target);
                    else
                        status = "Selected instruction has no statically known target";
                }
                else if (event.key.scancode == SDL_SCANCODE_L
                         || event.key.scancode == SDL_SCANCODE_SEMICOLON)
                {
                    edit_kind = event.key.scancode == SDL_SCANCODE_L
                        ? edit_kind_t::label
                        : edit_kind_t::comment;
                    edit_buffer.clear();
                    SDL_StartTextInput(sdl.window);
                }
                else if (event.key.scancode == SDL_SCANCODE_B)
                {
                    const uint32_t address{ selected_address() };
                    error.clear();
                    const bool saved{
                        project.add_bookmark(
                            { std::string{ k_cpu_address_space }, address },
                            "Bookmark " + formatted_address(address),
                            error
                        )
                    };
                    status = saved ? "Bookmark saved" : error;
                    refresh_facts = saved;
                }
                else if ((event.key.scancode == SDL_SCANCODE_C
                          || event.key.scancode == SDL_SCANCODE_D)
                         && !listing.instructions.empty())
                {
                    const auto& instruction{ listing.instructions[selected] };
                    const workbench::classification_kind_t kind{
                        event.key.scancode == SDL_SCANCODE_C
                            ? workbench::classification_kind_t::code
                            : workbench::classification_kind_t::data
                    };
                    error.clear();
                    const bool saved{
                        project.set_classification(
                            { std::string{ k_cpu_address_space }, instruction.address },
                            std::max<uint8_t>(instruction.encoded_size, 1u),
                            kind,
                            error
                        )
                    };
                    status = saved
                        ? (kind == workbench::classification_kind_t::code
                            ? "Classified as code"
                            : "Classified as data")
                        : error;
                    refresh_facts = saved;
                }
                else if (event.key.scancode == SDL_SCANCODE_H)
                {
                    error.clear();
                    const bool imported{ project.import_snes_hardware_symbols(error) };
                    status = imported ? "Hardware symbols refreshed" : error;
                    refresh_facts = imported;
                }
            }

            int width{};
            int height{};
            static_cast<void>(SDL_GetRenderOutputSize(sdl.renderer, &width, &height));
            static_cast<void>(SDL_SetRenderDrawColor(sdl.renderer, 14, 17, 23, 255));
            static_cast<void>(SDL_RenderClear(sdl.renderer));

            const float left_width{ 285.f };
            const float right_width{ 310.f };
            const SDL_FRect left{ 8.f, 34.f, left_width, height - 76.f };
            const SDL_FRect center{
                left.x + left.w + 8.f,
                34.f,
                std::max(280.f, width - left_width - right_width - 32.f),
                height - 76.f
            };
            const SDL_FRect right{
                center.x + center.w + 8.f,
                34.f,
                right_width,
                height - 76.f
            };
            draw_panel(sdl.renderer, left, 20, 25, 34);
            draw_panel(sdl.renderer, center, 18, 22, 30);
            draw_panel(sdl.renderer, right, 20, 25, 34);

            static_cast<void>(SDL_SetRenderDrawColor(sdl.renderer, 207, 220, 240, 255));
            draw_text(sdl.renderer, 12.f, 12.f, "CLOVER WORKBENCH  |  Persistent SNES analysis project");
            draw_text(sdl.renderer, left.x + 10.f, 48.f, "PROJECT FACTS");
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                68.f,
                "Labels " + std::to_string(labels.size())
                    + "  Comments " + std::to_string(comments.size())
            );
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                84.f,
                "Bookmarks " + std::to_string(bookmarks.size())
                    + "  Symbols " + std::to_string(symbols.size())
            );
            float fact_y{ 112.f };
            for (auto iterator{ bookmarks.rbegin() };
                 iterator != bookmarks.rend() && fact_y < left.y + left.h - 24.f;
                 ++iterator)
            {
                draw_text(
                    sdl.renderer,
                    left.x + 10.f,
                    fact_y,
                    formatted_address(static_cast<uint32_t>(iterator->location.address))
                        + " " + iterator->name
                );
                fact_y += 16.f;
            }
            if (bookmarks.empty())
                draw_text(sdl.renderer, left.x + 10.f, fact_y, "No bookmarks yet (B)");

            draw_text(
                sdl.renderer,
                center.x + 10.f,
                48.f,
                "DISASSEMBLY  E=1 M=1 X=1 D=0000 DB=00"
            );
            float row_y{ 72.f };
            for (size_t index{ 0 }; index < listing.instructions.size(); ++index)
            {
                const auto& instruction{ listing.instructions[index] };
                if (index == selected)
                {
                    const SDL_FRect selection{
                        center.x + 5.f,
                        row_y - 3.f,
                        center.w - 10.f,
                        15.f
                    };
                    static_cast<void>(SDL_SetRenderDrawColor(
                        sdl.renderer, 47, 76, 118, 255
                    ));
                    static_cast<void>(SDL_RenderFillRect(sdl.renderer, &selection));
                    static_cast<void>(SDL_SetRenderDrawColor(
                        sdl.renderer, 238, 244, 255, 255
                    ));
                }
                const std::string marker{
                    fact_at(classifications, instruction.address) != nullptr ? "* " : "  "
                };
                draw_text(
                    sdl.renderer,
                    center.x + 10.f,
                    row_y,
                    marker + formatted_address(instruction.address) + "  "
                        + formatted_bytes(instruction) + "  "
                        + analysis::snes::format_instruction(instruction)
                );
                row_y += 16.f;
                if (row_y >= center.y + center.h - 12.f)
                    break;
            }

            const uint32_t current{ selected_address() };
            draw_text(sdl.renderer, right.x + 10.f, 48.f, "INSPECTOR");
            draw_text(sdl.renderer, right.x + 10.f, 68.f, formatted_address(current));
            float inspector_y{ 92.f };
            if (const auto* label{ fact_at(labels, current) }; label != nullptr)
            {
                draw_text(sdl.renderer, right.x + 10.f, inspector_y, "Label: " + label->text);
                inspector_y += 18.f;
            }
            if (const auto* comment{ fact_at(comments, current) }; comment != nullptr)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Comment: " + comment->text
                );
                inspector_y += 18.f;
            }
            if (const auto* classification{
                    fact_at(classifications, current)
                };
                classification != nullptr)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    classification->kind == workbench::classification_kind_t::code
                        ? "Classification: code"
                        : "Classification: data"
                );
                inspector_y += 18.f;
            }
            inspector_y += 12.f;
            draw_text(sdl.renderer, right.x + 10.f, inspector_y, "UP/DOWN  select");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 16.f, "ENTER    follow target");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 32.f, "PGUP/DN  move listing");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 48.f, "L / ;    label / comment");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 64.f, "B        bookmark");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 80.f, "C / D    code / data");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 96.f, "ALT+LEFT/RIGHT history");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 112.f, "H        refresh symbols");

            static_cast<void>(SDL_SetRenderDrawColor(sdl.renderer, 159, 176, 202, 255));
            const std::string footer{
                edit_kind == edit_kind_t::none
                    ? status
                    : (edit_kind == edit_kind_t::label ? "Label: " : "Comment: ")
                        + edit_buffer + "_"
            };
            draw_text(sdl.renderer, 12.f, height - 26.f, footer);
            static_cast<void>(SDL_RenderPresent(sdl.renderer));
            SDL_Delay(8u);
        }
        return 0;
    }
}
