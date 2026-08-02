//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/platform/sdl/snes/SnesWorkbenchApplication.h"
#include "clover/platform/sdl/snes/SnesToolRenderer.h"

#include "clover/analysis/snes/Oam.h"
#include "clover/analysis/snes/Palette.h"
#include "clover/analysis/snes/TileGraphics.h"
#include "clover/analysis/snes/TileMap.h"
#include "clover/analysis/TypedData.h"
#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"
#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/SnesEmulatorCore.h"
#include "clover/utils/FileSystem.h"
#include "clover/workbench/snes/LivePpuSnapshot.h"
#include "clover/workbench/Project.h"
#include "clover/workbench/ToolRegistry.h"
#include "clover/workbench/WorkbenchTargetSupport.h"
#include "clover/workbench/snes/SnesWorkbenchSupport.h"
#include "clover/workbench/snes/SnesInstructionServices.h"
#include "clover/workbench/snes/SnesPresentationModel.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iomanip>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace
{
    using clover::workbench::address_key_t;

    constexpr size_t k_listing_rows{ 38u };
    // A prime-sized slice avoids repeatedly sampling short polling loops at
    // the same phase while keeping Pause latency short enough for interaction.
    constexpr size_t k_run_instruction_slice{ 8191u };

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
        comment,
        watchpoint
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

    [[nodiscard]] std::string formatted_b_bus_targets(
        uint8_t base,
        uint8_t offset_mask
    )
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0');
        bool first{ true };
        for (uint8_t offset{ 0u }; offset < 4u; ++offset)
        {
            if ((offset_mask & static_cast<uint8_t>(1u << offset)) == 0u)
                continue;
            if (!first)
                output << '/';
            output << "$21" << std::setw(2)
                   << static_cast<unsigned>(base + offset);
            first = false;
        }
        if (first)
        {
            output << "$21" << std::setw(2)
                   << static_cast<unsigned>(base);
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

    void draw_text(SDL_Renderer* renderer,
                   float x,
                   float y,
                   std::string_view text,
                   Uint8 red = 207u,
                   Uint8 green = 220u,
                   Uint8 blue = 240u)
    {
        const std::string owned{ text };
        static_cast<void>(SDL_SetRenderDrawColor(
            renderer,
            red,
            green,
            blue,
            255u
        ));
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
                                      uint64_t address,
                                      std::string_view address_space)
    {
        const auto found{
            std::find_if(
                facts.begin(),
                facts.end(),
                [address, address_space](const Fact& fact)
                {
                    return fact.location.address_space == address_space
                        && fact.location.address == address;
                }
            )
        };
        return found == facts.end() ? nullptr : &*found;
    }

    [[nodiscard]] const clover::analysis::data_type_t* data_type(
        const std::vector<clover::workbench::project_data_type_t>& types,
        std::string_view stable_id
    )
    {
        const auto found{
            std::find_if(
                types.begin(),
                types.end(),
                [stable_id](
                    const clover::workbench::project_data_type_t& type
                )
                {
                    return type.definition.stable_id == stable_id;
                }
            )
        };
        return found == types.end() ? nullptr : &found->definition;
    }

    [[nodiscard]] const clover::workbench::project_typed_object_t*
    typed_object_at(
        const std::vector<clover::workbench::project_typed_object_t>& objects,
        const std::vector<clover::workbench::project_data_type_t>& types,
        uint64_t address,
        std::string_view address_space
    )
    {
        const auto found{
            std::find_if(
                objects.begin(),
                objects.end(),
                [&types, address, address_space](
                    const clover::workbench::project_typed_object_t& object
                )
                {
                    const clover::analysis::data_type_t* const type{
                        data_type(types, object.object.type_id)
                    };
                    return type != nullptr
                        && object.object.location.address_space
                            == address_space
                        && object.object.location.address <= address
                        && address - object.object.location.address
                            < type->byte_size;
                }
            )
        };
        return found == objects.end() ? nullptr : &*found;
    }

    [[nodiscard]] std::string formatted_memory(
        std::span<const std::byte> bytes
    )
    {
        std::ostringstream output{};
        output << std::uppercase << std::hex << std::setfill('0');
        for (size_t index{ 0 }; index < bytes.size(); ++index)
        {
            if (index != 0u)
                output << ' ';
            output << std::setw(2)
                   << static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[index]));
        }
        return output.str();
    }

    [[nodiscard]] std::string stop_reason_name(
        clover::workbench::debugger_stop_reason_t reason
    )
    {
        using clover::workbench::debugger_stop_reason_t;
        switch (reason)
        {
        case debugger_stop_reason_t::none: return "ready";
        case debugger_stop_reason_t::pause: return "paused";
        case debugger_stop_reason_t::instruction_step: return "instruction step";
        case debugger_stop_reason_t::breakpoint: return "breakpoint";
        case debugger_stop_reason_t::watchpoint: return "watchpoint";
        case debugger_stop_reason_t::run_to_cursor: return "run to cursor";
        case debugger_stop_reason_t::step_over: return "step over";
        case debugger_stop_reason_t::step_out: return "step out";
        case debugger_stop_reason_t::waiting: return "CPU waiting";
        case debugger_stop_reason_t::processor_stopped: return "CPU stopped";
        case debugger_stop_reason_t::observation_overflow:
            return "observation overflow";
        case debugger_stop_reason_t::error: return "debugger error";
        }
        return "debugger";
    }

    [[nodiscard]] std::string_view confidence_name(
        clover::analysis::confidence_t confidence
    )
    {
        using clover::analysis::confidence_t;
        switch (confidence)
        {
        case confidence_t::confirmed: return "confirmed";
        case confidence_t::strongly_inferred: return "strong";
        case confidence_t::weakly_inferred: return "weak";
        case confidence_t::unresolved: return "unresolved";
        case confidence_t::conflicting: return "conflicting";
        }
        return "unknown";
    }

    [[nodiscard]] std::string_view edge_kind_name(
        clover::analysis::edge_kind_t kind
    )
    {
        using clover::analysis::edge_kind_t;
        switch (kind)
        {
        case edge_kind_t::fallthrough: return "fallthrough";
        case edge_kind_t::conditional_branch: return "branch";
        case edge_kind_t::jump: return "jump";
        case edge_kind_t::call: return "call";
        case edge_kind_t::return_: return "return";
        case edge_kind_t::interrupt: return "interrupt";
        case edge_kind_t::unresolved: return "unresolved";
        }
        return "edge";
    }

    [[nodiscard]] std::string_view code_identity_name(
        clover::analysis::code_identity_t identity
    )
    {
        using clover::analysis::code_identity_t;
        switch (identity)
        {
        case code_identity_t::canonical_media: return "ROM";
        case code_identity_t::writable_memory: return "writable";
        case code_identity_t::unavailable: return "unknown";
        }
        return "unknown";
    }
}

namespace clover::platform::sdl::snes
{
    int run_snes_workbench_application(int argc, char** argv)
    {
        std::printf("Clover Workbench build: compiler=%s platform_toolset=%s\n",
                    CLOVER_BUILD_COMPILER,
                    CLOVER_BUILD_PLATFORM_TOOLSET);
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

        std::unique_ptr<workbench::workbench_target_support_t> target_support{
            workbench::identify_workbench_target_support(media)
        };
        if (target_support == nullptr)
        {
            std::fprintf(stderr, "No Workbench support recognizes this media.\n");
            return 1;
        }
        std::unique_ptr<frontend::emulator_core_t> core{
            target_support->create_core()
        };
        if (core == nullptr || !core->load_media(media))
        {
            std::fprintf(stderr, "Media was rejected by the selected Workbench support.\n");
            return 1;
        }
        frontend::debug_target_t* const target{ core->debug_target() };
        if (target == nullptr)
        {
            std::fprintf(stderr, "The selected debug target is unavailable.\n");
            return 1;
        }
        auto* const tile_diagnostics{
            dynamic_cast<frontend::snes::tile_layer_diagnostics_t*>(core.get())
        };
        auto* const object_diagnostics{
            dynamic_cast<frontend::snes::object_layer_diagnostics_t*>(core.get())
        };
        auto* const dma_diagnostics{
            dynamic_cast<frontend::snes::dma_transfer_diagnostics_t*>(core.get())
        };
        if (tile_diagnostics == nullptr
            || object_diagnostics == nullptr
            || dma_diagnostics == nullptr)
        {
            std::fprintf(
                stderr,
                "The selected SNES target lacks required Workbench diagnostics.\n"
            );
            return 1;
        }
        std::unique_ptr<workbench::analysis_services_t> analysis_services_owner{
            target_support->create_analysis_services(*target)
        };
        if (analysis_services_owner == nullptr)
        {
            std::fprintf(stderr, "Workbench analysis composition failed.\n");
            return 1;
        }
        workbench::analysis_services_t& analysis_services{
            *analysis_services_owner
        };
        const frontend::address_space_id_t instruction_address_space{
            analysis_services.instruction_address_space()
        };
        const std::string instruction_address_space_name{
            analysis_services.instruction_address_space_name()
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
        if (!target_support->prepare_project(
                project,
                project_root,
                media,
                error
            ))
        {
            std::fprintf(stderr, "Unable to open Workbench project: %s\n", error.c_str());
            return 1;
        }

        uint32_t listing_address{
            command.address_set
                ? command.address
                : static_cast<uint32_t>(
                    analysis_services.default_entry().value_or(0u)
                )
        };
        if (!project.record_navigation(
                { instruction_address_space_name, listing_address },
                "disassembly",
                error
            ))
        {
            std::fprintf(stderr, "Unable to record navigation: %s\n", error.c_str());
            return 1;
        }

        core->power_on();
        std::unique_ptr<workbench::debugger_t> debugger_owner{
            target_support->create_debugger()
        };
        if (debugger_owner == nullptr)
        {
            std::fprintf(stderr, "Workbench debugger composition failed.\n");
            return 1;
        }
        workbench::debugger_t& debugger{ *debugger_owner };
        if (!debugger.initialize(*target, error))
        {
            std::fprintf(stderr, "Unable to attach debugger: %s\n", error.c_str());
            return 1;
        }
        error.clear();
        for (const workbench::project_breakpoint_t& saved
             : project.debug_breakpoints(error))
        {
            const uint64_t id{
                debugger.add_breakpoint({
                    instruction_address_space,
                    saved.location.address
                })
            };
            static_cast<void>(
                debugger.set_breakpoint_enabled(id, saved.enabled)
            );
        }
        for (const workbench::project_watchpoint_t& saved
             : project.debug_watchpoints(error))
        {
            const uint64_t id{
                debugger.add_watchpoint(
                    {
                        instruction_address_space,
                        saved.location.address
                    },
                    saved.length,
                    static_cast<workbench::watch_access_t>(saved.access)
                )
            };
            static_cast<void>(
                debugger.set_watchpoint_enabled(id, saved.enabled)
            );
        }
        if (!error.empty())
        {
            std::fprintf(
                stderr,
                "Unable to restore debugger points: %s\n",
                error.c_str()
            );
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
            SDL_Texture* video_texture{};
            ~sdl_cleanup_t()
            {
                if (video_texture != nullptr)
                    SDL_DestroyTexture(video_texture);
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
        const frontend::display_info_t display{ core->display_info() };
        sdl.video_texture = SDL_CreateTexture(
            sdl.renderer,
            SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,
            static_cast<int>(display.framebuffer_width),
            static_cast<int>(display.framebuffer_height)
        );
        if (sdl.video_texture == nullptr)
        {
            std::fprintf(stderr, "Unable to create Workbench video texture: %s\n",
                         SDL_GetError());
            return 1;
        }
        static_cast<void>(
            SDL_SetTextureScaleMode(sdl.video_texture, SDL_SCALEMODE_NEAREST)
        );
        static_cast<void>(
            SDL_SetTextureBlendMode(sdl.video_texture, SDL_BLENDMODE_BLEND)
        );

        workbench::live_processor_state_t live_state{};
        workbench::disassembly_listing_t listing{};
        std::vector<workbench::named_fact_t> labels{};
        std::vector<workbench::named_fact_t> comments{};
        std::vector<workbench::bookmark_t> bookmarks{};
        std::vector<workbench::classification_t> classifications{};
        std::vector<workbench::symbol_t> symbols{};
        std::vector<workbench::project_data_type_t> data_types{};
        std::vector<workbench::project_typed_object_t> typed_objects{};
        workbench::snes::snes_presentation_model_t snes_presentation{};
        auto& palettes{ snes_presentation.palettes };
        auto& tile_assets{ snes_presentation.tile_assets };
        auto& tile_maps{ snes_presentation.tile_maps };
        analysis::program_model_t analysis_model{};
        if (const auto stored{ project.current_analysis(error) };
            stored.has_value())
        {
            analysis_model = *stored;
        }
        error.clear();
        size_t selected{ 0u };
        bool refresh_listing{ true };
        bool refresh_facts{ true };
        bool running{ true };
        bool traced_continue{ false };
        workbench::tool_registry_t tools{};
        target_support->register_tools(tools);
        workbench::active_tool_t active_tool{};
        workbench::tool_active_flag_t output_view{
            active_tool,
            workbench::snes::k_output_tool_id
        };
        workbench::tool_active_flag_t palette_view{
            active_tool,
            workbench::snes::k_palette_tool_id
        };
        auto& palette_index{ snes_presentation.palette_index };
        auto& selected_color{ snes_presentation.selected_color };
        workbench::tool_active_flag_t graphics_view{
            active_tool,
            workbench::snes::k_tile_graphics_tool_id
        };
        auto& tile_asset_index{ snes_presentation.tile_asset_index };
        auto& selected_tile{ snes_presentation.selected_tile };
        auto& selected_pixel_x{ snes_presentation.selected_pixel_x };
        auto& selected_pixel_y{ snes_presentation.selected_pixel_y };
        workbench::tool_active_flag_t tile_map_view{
            active_tool,
            workbench::snes::k_tile_map_tool_id
        };
        auto& rendered_bg_view{ snes_presentation.rendered_bg_view };
        auto& tile_map_index{ snes_presentation.tile_map_index };
        auto& selected_map_x{ snes_presentation.selected_map_x };
        auto& selected_map_y{ snes_presentation.selected_map_y };
        auto& pending_tile_map_id{ snes_presentation.pending_tile_map_id };
        auto& live_map_layer_index{ snes_presentation.live_map_layer_index };
        auto& live_ppu_snapshot{ snes_presentation.live_ppu_snapshot };
        auto& tile_map_full_view{ snes_presentation.tile_map_full_view };
        workbench::tool_active_flag_t object_view{
            active_tool,
            workbench::snes::k_object_tool_id
        };
        auto& selected_object{ snes_presentation.selected_object };
        workbench::tool_active_flag_t dma_view{
            active_tool,
            workbench::snes::k_dma_tool_id
        };
        auto& dma_transfers{ snes_presentation.dma_transfers };
        auto& dma_inspection{ snes_presentation.dma_inspection };
        auto& selected_dma_transfer{ snes_presentation.selected_dma_transfer };
        uint64_t debugger_instructions_executed{};
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
                    { instruction_address_space_name, listing_address },
                    "disassembly",
                    error
                ))
            {
                status = error;
            }
        };
        const auto inspect_byte = [target](
            const analysis::address_t& address
        ) -> std::optional<uint8_t>
        {
            const std::span<const frontend::address_space_descriptor_t> spaces{
                target->address_spaces()
            };
            const auto space{
                std::find_if(
                    spaces.begin(),
                    spaces.end(),
                    [&address](
                        const frontend::address_space_descriptor_t& descriptor
                    )
                    {
                        return descriptor.stable_id == address.address_space;
                    }
                )
            };
            if (space == spaces.end())
                return std::nullopt;
            std::byte byte{};
            const frontend::memory_inspection_result_t result{
                target->inspect_memory(
                    { space->id, address.address },
                    std::span<std::byte>{ &byte, 1u }
                )
            };
            if (result.status
                    != frontend::memory_inspection_status_t::complete
                || result.bytes_read != 1u)
            {
                return std::nullopt;
            }
            return std::to_integer<uint8_t>(byte);
        };
        const auto decode_typed = [&](const analysis::typed_object_t& object)
        {
            std::vector<analysis::data_type_t> definitions{};
            definitions.reserve(data_types.size());
            for (const workbench::project_data_type_t& type : data_types)
                definitions.push_back(type.definition);
            return analysis::decode_typed_object(
                definitions,
                object,
                inspect_byte
            );
        };
        const auto decode_palette = [&](const analysis::palette_asset_t& asset)
        {
            return analysis::decode_palette(asset, inspect_byte);
        };
        const auto decode_tiles = [&](const analysis::tile_asset_t& asset)
        {
            return analysis::decode_tiles(asset, inspect_byte);
        };
        const auto decode_tile_map = [&](
            const analysis::tile_map_asset_t& asset
        )
        {
            return analysis::decode_tile_map(asset, inspect_byte);
        };
        const auto run_analysis = [&]()
        {
            workbench::analysis_publication_t publication{};
            error.clear();
            if (!analysis_services.analyze_and_publish(
                    project,
                    classifications,
                    debugger,
                    publication,
                    error
                ))
            {
                status = error;
                return;
            }
            analysis_model = std::move(publication.model);
            refresh_facts = true;
            status = std::move(publication.status);
        };

        while (running)
        {
            const bool debugger_was_running{
                debugger.run_state() == workbench::debugger_run_state_t::running
            };
            if (debugger.run_state() == workbench::debugger_run_state_t::running)
            {
                error.clear();
                debugger_instructions_executed += traced_continue
                    ? debugger.pump(k_run_instruction_slice, error)
                    : debugger.pump_fast(k_run_instruction_slice, error);
                if (!error.empty())
                    status = error;
            }
            const workbench::debugger_run_state_t debugger_state{
                debugger.run_state()
            };
            if (debugger_was_running
                && debugger_state == workbench::debugger_run_state_t::stopped)
            {
                error.clear();
                if (debugger.live_state(live_state, error))
                {
                    listing_address = static_cast<uint32_t>(
                        live_state.instruction_address.value
                    );
                    selected = 0u;
                    refresh_listing = true;
                    status = "Stopped: "
                        + stop_reason_name(debugger.last_stop().reason);
                    if (!debugger.last_stop().detail.empty())
                        status += " - " + debugger.last_stop().detail;
                }
                else
                {
                    status = error;
                }
            }
            if (tile_map_view && live_map_layer_index.has_value())
            {
                std::string snapshot_error{};
                if (!snes_presentation.refresh_live_snapshot(
                        *tile_diagnostics, *target, snapshot_error
                    ))
                {
                    tile_map_view = false;
                    status = snapshot_error;
                }
            }
            if (refresh_listing)
            {
                error.clear();
                static_cast<void>(debugger.live_state(live_state, error));
                listing = analysis_services.build_listing(
                    listing_address,
                    k_listing_rows,
                    live_state
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
                data_types = project.data_types(error);
                typed_objects = project.typed_objects(error);
                snes_presentation.refresh_assets(
                    project,
                    active_tool,
                    error
                );
                if (!error.empty())
                    status = error;
                refresh_facts = false;
            }
            if (dma_view)
                snes_presentation.refresh_dma(*dma_diagnostics);

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
                        if (edit_kind == edit_kind_t::watchpoint)
                        {
                            uint32_t watch_address{};
                            const bool parsed{
                                parse_address(edit_buffer, watch_address)
                            };
                            if (parsed)
                            {
                                error.clear();
                                const address_key_t location{
                                    instruction_address_space_name,
                                    watch_address
                                };
                                if (project.set_debug_watchpoint(
                                        location,
                                        1u,
                                        workbench::project_watch_access_t::read_write,
                                        true,
                                        error
                                    ))
                                {
                                    static_cast<void>(debugger.add_watchpoint(
                                        {
                                            instruction_address_space,
                                            watch_address
                                        },
                                        1u,
                                        workbench::watch_access_t::read_write
                                    ));
                                    status = "Read/write watchpoint added at "
                                        + formatted_address(watch_address);
                                }
                                else
                                {
                                    status = error;
                                }
                            }
                            else
                            {
                                status = "Invalid watchpoint address";
                            }
                            edit_kind = edit_kind_t::none;
                            SDL_StopTextInput(sdl.window);
                            continue;
                        }
                        const address_key_t location{
                            instruction_address_space_name,
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
                const bool control{
                    (event.key.mod & SDL_KMOD_CTRL) != 0
                };
                if (event.key.scancode == SDL_SCANCODE_ESCAPE)
                    running = false;
                else if (event.key.scancode == SDL_SCANCODE_TAB)
                {
                    static_cast<void>(tools.activate_command(
                        "workbench.output.toggle",
                        active_tool
                    ));
                    if (output_view)
                    {
                        palette_view = false;
                        graphics_view = false;
                        tile_map_view = false;
                        object_view = false;
                        dma_view = false;
                    }
                    status = output_view
                        ? "Live game output"
                        : "Disassembly view";
                }
                else if (event.key.scancode == SDL_SCANCODE_F5)
                {
                    error.clear();
                    const bool was_running{
                        debugger.run_state()
                            == workbench::debugger_run_state_t::running
                    };
                    if (!was_running)
                    {
                        traced_continue =
                            (event.key.mod & SDL_KMOD_SHIFT) != 0;
                    }
                    const bool changed{
                        was_running
                            ? debugger.pause(error)
                            : debugger.resume(error)
                    };
                    status = changed
                        ? (debugger.run_state()
                                == workbench::debugger_run_state_t::running
                            ? (traced_continue
                                ? "Debugger running (traced)"
                                : "Debugger running (fast)")
                            : "Debugger paused")
                        : error;
                    if (changed
                        && debugger.run_state()
                            == workbench::debugger_run_state_t::stopped
                        && debugger.live_state(live_state, error))
                    {
                        listing_address = static_cast<uint32_t>(
                            live_state.instruction_address.value
                        );
                        selected = 0u;
                        refresh_listing = true;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_F10)
                {
                    error.clear();
                    status = debugger.step_over(error)
                        ? "Step over"
                        : error;
                    if (debugger.run_state()
                        == workbench::debugger_run_state_t::stopped)
                    {
                        refresh_listing = true;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_F11)
                {
                    error.clear();
                    const bool step_out{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    const bool stepped{
                        step_out
                            ? debugger.step_out(error)
                            : debugger.step_instruction(error)
                    };
                    status = stepped
                        ? (step_out ? "Step out" : "Instruction step")
                        : error;
                    if (debugger.run_state()
                        == workbench::debugger_run_state_t::stopped)
                    {
                        refresh_listing = true;
                    }
                }
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
                else if (event.key.scancode == SDL_SCANCODE_V)
                {
                    if ((event.key.mod & SDL_KMOD_SHIFT) != 0)
                    {
                        palette_view = false;
                        status = "Disassembly view";
                    }
                    else if (palettes.empty())
                    {
                        status = "No palette assets yet (Q / Shift+Q)";
                    }
                    else
                    {
                        if (palette_view)
                            palette_index = (palette_index + 1u) % palettes.size();
                        else
                        {
                            static_cast<void>(tools.activate_command(
                                "snes.palette.toggle",
                                active_tool
                            ));
                            output_view = false;
                            graphics_view = false;
                            tile_map_view = false;
                            object_view = false;
                            dma_view = false;
                        }
                        selected_color = 0u;
                        status = "Palette "
                            + palettes[palette_index].asset.name;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_Q)
                {
                    const bool live_cgram{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    const uint32_t address{ selected_address() };
                    const analysis::palette_asset_t asset{
                        .stable_id = live_cgram
                            ? "palette@snes.cgram:000"
                            : "palette@" + formatted_address(address),
                        .name = live_cgram
                            ? "Live CGRAM"
                            : "Palette " + formatted_address(address),
                        .location = live_cgram
                            ? analysis::address_t{ "snes.cgram", 0u }
                            : analysis::address_t{
                                instruction_address_space_name,
                                address
                            },
                        .color_count = static_cast<uint16_t>(
                            live_cgram ? 256u : 16u
                        )
                    };
                    error.clear();
                    const bool saved{ project.set_palette(asset, error) };
                    status = saved
                        ? (live_cgram
                            ? "Bound live 256-color CGRAM palette"
                            : "Bound 16-color CPU-bus palette")
                        : error;
                    refresh_facts = saved;
                }
                else if (event.key.scancode == SDL_SCANCODE_G)
                {
                    if ((event.key.mod & SDL_KMOD_SHIFT) != 0)
                    {
                        graphics_view = false;
                        status = "Disassembly view";
                    }
                    else if (tile_assets.empty())
                    {
                        status = "No tile assets yet (2 / 4 / 8)";
                    }
                    else
                    {
                        if (graphics_view)
                        {
                            tile_asset_index =
                                (tile_asset_index + 1u) % tile_assets.size();
                        }
                        else
                        {
                            static_cast<void>(tools.activate_command(
                                "snes.tiles.toggle",
                                active_tool
                            ));
                            output_view = false;
                            palette_view = false;
                            tile_map_view = false;
                            object_view = false;
                            dma_view = false;
                        }
                        selected_tile = 0u;
                        selected_pixel_x = 0u;
                        selected_pixel_y = 0u;
                        status = "Tiles "
                            + tile_assets[tile_asset_index].asset.name;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_H)
                {
                    if ((event.key.mod & SDL_KMOD_SHIFT) != 0)
                    {
                        tile_map_view = false;
                        rendered_bg_view = false;
                        status = "Disassembly view";
                    }
                    else if (tile_maps.empty())
                    {
                        status = "No tile-map assets yet (Ctrl+1..4)";
                    }
                    else
                    {
                        if (tile_map_view)
                        {
                            tile_map_index =
                                (tile_map_index + 1u) % tile_maps.size();
                        }
                        else
                        {
                            static_cast<void>(tools.activate_command(
                                "snes.tile-map.toggle",
                                active_tool
                            ));
                            output_view = false;
                            palette_view = false;
                            graphics_view = false;
                            object_view = false;
                            dma_view = false;
                        }
                        selected_map_x = 0u;
                        selected_map_y = 0u;
                        tile_map_full_view = !live_ppu_snapshot.has_value()
                            || tile_maps[tile_map_index].asset.stable_id
                                != "tilemap@live-"
                                    + std::string{
                                        live_ppu_snapshot->layer.label
                                    };
                        status = "Tile map "
                            + tile_maps[tile_map_index].asset.name;
                        rendered_bg_view = false;
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_O)
                {
                    static_cast<void>(tools.activate_command(
                        "snes.objects.toggle",
                        active_tool
                    ));
                    if (object_view)
                    {
                        output_view = false;
                        palette_view = false;
                        graphics_view = false;
                        tile_map_view = false;
                        dma_view = false;
                        selected_object = 0u;
                        status = "Live OAM objects";
                    }
                    else
                    {
                        status = "Disassembly view";
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_I)
                {
                    if ((event.key.mod & SDL_KMOD_SHIFT) != 0)
                    {
                        dma_diagnostics->clear_dma_transfers();
                        dma_inspection = {};
                        selected_dma_transfer = 0u;
                        status = "Cleared DMA transfer history";
                    }
                    else
                    {
                        static_cast<void>(tools.activate_command(
                            "snes.dma.toggle",
                            active_tool
                        ));
                        if (dma_view)
                        {
                            output_view = false;
                            palette_view = false;
                            graphics_view = false;
                            tile_map_view = false;
                            object_view = false;
                            dma_inspection = dma_diagnostics->inspect_dma_transfers(
                                dma_transfers
                            );
                            selected_dma_transfer = dma_inspection.record_count
                                == 0u ? 0u : dma_inspection.record_count - 1u;
                            status = "DMA transfer history";
                        }
                        else
                        {
                            status = "Disassembly view";
                        }
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_F
                         && tile_map_view
                         && tile_map_index < tile_maps.size())
                {
                    const bool has_live_viewport{
                        live_ppu_snapshot.has_value()
                        && tile_maps[tile_map_index].asset.stable_id
                            == "tilemap@live-"
                                + std::string{
                                    live_ppu_snapshot->layer.label
                                }
                    };
                    if (has_live_viewport)
                    {
                        tile_map_full_view = !tile_map_full_view;
                        status = tile_map_full_view
                            ? "Full backing tile map"
                            : "Current scrolled BG viewport";
                    }
                    else
                    {
                        status = "This saved map has no live viewport snapshot";
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_R
                         && tile_map_view
                         && live_map_layer_index.has_value())
                {
                    rendered_bg_view = !rendered_bg_view;
                    status = rendered_bg_view
                        ? "Completed raster-accurate raw BG frame"
                        : "Coherent backing tilemap snapshot";
                }
                else if (control
                         && (event.key.scancode == SDL_SCANCODE_1
                             || event.key.scancode == SDL_SCANCODE_2
                             || event.key.scancode == SDL_SCANCODE_3
                             || event.key.scancode == SDL_SCANCODE_4))
                {
                    const size_t layer_index{
                        event.key.scancode == SDL_SCANCODE_1 ? 0u
                            : (event.key.scancode == SDL_SCANCODE_2 ? 1u
                                : (event.key.scancode == SDL_SCANCODE_3
                                    ? 2u : 3u))
                    };
                    error.clear();
                    if (!snes_presentation.capture_live_map(
                            *tile_diagnostics,
                            *target,
                            layer_index,
                            project,
                            error
                        ))
                    {
                        tile_map_view = false;
                        pending_tile_map_id.clear();
                        selected_map_x = 0u;
                        selected_map_y = 0u;
                        status = error;
                        continue;
                    }
                    const std::string layer_name{
                        live_ppu_snapshot->layer.label
                    };
                    tile_map_full_view = false;
                    rendered_bg_view = true;
                    status = "Bound live " + layer_name + " tile map";
                    refresh_facts = true;
                }
                else if (event.key.scancode == SDL_SCANCODE_2
                         || event.key.scancode == SDL_SCANCODE_4
                         || event.key.scancode == SDL_SCANCODE_8)
                {
                    const analysis::tile_format_t format{
                        event.key.scancode == SDL_SCANCODE_2
                            ? analysis::tile_format_t::snes_2bpp
                            : (event.key.scancode == SDL_SCANCODE_4
                                ? analysis::tile_format_t::snes_4bpp
                                : analysis::tile_format_t::snes_8bpp)
                    };
                    const bool live_vram{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    const uint64_t bytes_per_tile{
                        analysis::tile_bytes(format)
                    };
                    const uint32_t source_address{
                        live_vram
                            ? 0u
                            : static_cast<uint32_t>(
                                selected_address()
                                - (selected_address() % bytes_per_tile)
                            )
                    };
                    const uint8_t bpp{
                        analysis::tile_bits_per_pixel(format)
                    };
                    const auto preferred_palette{
                        std::find_if(
                            palettes.begin(),
                            palettes.end(),
                            [live_vram, &instruction_address_space_name](
                                const workbench::project_palette_t& palette
                            )
                            {
                                return palette.asset.location.address_space
                                    == (live_vram
                                        ? "snes.cgram"
                                        : instruction_address_space_name);
                            }
                        )
                    };
                    const std::string palette_id{
                        preferred_palette != palettes.end()
                            ? preferred_palette->asset.stable_id
                            : (palettes.empty()
                                ? std::string{}
                                : palettes.front().asset.stable_id)
                    };
                    const analysis::tile_asset_t asset{
                        .stable_id = "tiles@"
                            + std::string{
                                live_vram ? "snes.vram:0000:" : "snes.cpu-bus:"
                            }
                            + (live_vram
                                ? ""
                                : formatted_address(source_address) + ":")
                            + std::to_string(bpp) + "bpp",
                        .name = live_vram
                            ? "Live VRAM " + std::to_string(bpp) + "bpp"
                            : std::to_string(bpp) + "bpp tiles "
                                + formatted_address(source_address),
                        .location = live_vram
                            ? analysis::address_t{ "snes.vram", 0u }
                            : analysis::address_t{
                                instruction_address_space_name,
                                source_address
                            },
                        .tile_count = live_vram ? 256u : 64u,
                        .format = format,
                        .palette_id = palette_id,
                        .palette_base = 0u
                    };
                    error.clear();
                    const bool saved{ project.set_tile_asset(asset, error) };
                    status = saved
                        ? "Bound " + std::to_string(bpp) + "bpp "
                            + (live_vram ? "live VRAM tiles" : "CPU-bus tiles")
                        : error;
                    refresh_facts = saved;
                }
                else if ((event.key.scancode == SDL_SCANCODE_LEFT
                          || event.key.scancode == SDL_SCANCODE_RIGHT
                          || event.key.scancode == SDL_SCANCODE_UP
                          || event.key.scancode == SDL_SCANCODE_DOWN)
                         && snes_presentation.navigate(
                             active_tool.active(),
                             event.key.scancode == SDL_SCANCODE_LEFT
                                 ? workbench::snes::presentation_direction_t::left
                                 : (event.key.scancode == SDL_SCANCODE_RIGHT
                                     ? workbench::snes::presentation_direction_t::right
                                     : (event.key.scancode == SDL_SCANCODE_UP
                                         ? workbench::snes::presentation_direction_t::up
                                         : workbench::snes::presentation_direction_t::down)),
                             (event.key.mod & SDL_KMOD_SHIFT) != 0
                         ))
                {
                }
                else if (!palette_view && !graphics_view && !tile_map_view
                         && !object_view && !dma_view
                         && event.key.scancode == SDL_SCANCODE_UP
                         && selected > 0u)
                    --selected;
                else if (!palette_view && !graphics_view && !tile_map_view
                         && !object_view && !dma_view
                         && event.key.scancode == SDL_SCANCODE_DOWN
                         && selected + 1u < listing.instructions.size())
                    ++selected;
                else if (event.key.scancode == SDL_SCANCODE_PAGEUP)
                    navigate((listing_address - 0x40u) & 0x00ffffffu);
                else if (event.key.scancode == SDL_SCANCODE_PAGEDOWN)
                    navigate(listing.next_address);
                else if (event.key.scancode == SDL_SCANCODE_RETURN
                         && dma_view
                         && selected_dma_transfer < dma_inspection.record_count)
                {
                    const frontend::snes::dma_transfer_record_t& transfer{
                        dma_transfers[selected_dma_transfer]
                    };
                    const bool follow_source{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    navigate(
                        follow_source
                            ? transfer.first_a_bus_address
                            : transfer.initiator_address
                    );
                    dma_view = false;
                    status = follow_source
                        ? "Followed DMA source"
                        : "Followed DMA initiator";
                }
                else if (event.key.scancode == SDL_SCANCODE_RETURN
                         && !listing.instructions.empty())
                {
                    const auto& instruction{ listing.instructions[selected] };
                    if (instruction.direct_target.has_value())
                        navigate(*instruction.direct_target);
                    else
                        status = "Selected instruction has no statically known target";
                }
                else if (event.key.scancode == SDL_SCANCODE_F9)
                {
                    const uint32_t address{ selected_address() };
                    auto found{
                        std::find_if(
                            debugger.breakpoints().begin(),
                            debugger.breakpoints().end(),
                            [address, instruction_address_space](
                                const workbench::breakpoint_t& breakpoint
                            )
                            {
                                return breakpoint.address.space
                                        == instruction_address_space
                                    && breakpoint.address.value == address;
                            }
                        )
                    };
                    if (found == debugger.breakpoints().end())
                    {
                        error.clear();
                        if (project.set_debug_breakpoint(
                                { instruction_address_space_name, address },
                                true,
                                error
                            ))
                        {
                            static_cast<void>(debugger.add_breakpoint({
                                instruction_address_space,
                                address
                            }));
                            status = "Breakpoint added at "
                                + formatted_address(address);
                        }
                        else
                        {
                            status = error;
                        }
                    }
                    else
                    {
                        const uint64_t id{ found->id };
                        error.clear();
                        if (project.remove_debug_breakpoint(
                                { instruction_address_space_name, address },
                                error
                            ))
                        {
                            static_cast<void>(debugger.remove_breakpoint(id));
                            status = "Breakpoint removed";
                        }
                        else
                        {
                            status = error;
                        }
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_T)
                {
                    error.clear();
                    status = debugger.run_to(
                        {
                            instruction_address_space,
                            selected_address()
                        },
                        error
                    ) ? "Running to cursor" : error;
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
                else if (event.key.scancode == SDL_SCANCODE_M)
                {
                    edit_kind = edit_kind_t::watchpoint;
                    edit_buffer.clear();
                    SDL_StartTextInput(sdl.window);
                }
                else if (event.key.scancode == SDL_SCANCODE_B)
                {
                    const uint32_t address{ selected_address() };
                    error.clear();
                    const bool saved{
                        project.add_bookmark(
                            { instruction_address_space_name, address },
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
                            { instruction_address_space_name, instruction.address },
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
                else if (event.key.scancode == SDL_SCANCODE_Y)
                {
                    const uint32_t address{ selected_address() };
                    const bool string_binding{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    const std::string type_id{
                        string_binding ? "user.ascii16" : "clover.u8"
                    };
                    error.clear();
                    bool saved{ true };
                    if (string_binding
                        && data_type(data_types, type_id) == nullptr)
                    {
                        saved = project.set_data_type(
                            {
                                .stable_id = type_id,
                                .name = "ASCII string[16]",
                                .kind = analysis::data_type_kind_t::string,
                                .byte_size = 16u,
                                .encoding = "ascii"
                            },
                            error
                        );
                    }
                    if (saved)
                    {
                        saved = project.set_typed_object(
                            {
                                .stable_id = "typed@"
                                    + formatted_address(address),
                                .location = {
                                    instruction_address_space_name,
                                    address
                                },
                                .type_id = type_id,
                                .name = string_binding
                                    ? "String " + formatted_address(address)
                                    : "Byte " + formatted_address(address)
                            },
                            error
                        );
                    }
                    status = saved
                        ? (string_binding
                            ? "Bound ASCII string[16]"
                            : "Bound unsigned byte")
                        : error;
                    refresh_facts = saved;
                }
                else if (event.key.scancode == SDL_SCANCODE_J
                         && !typed_objects.empty())
                {
                    const uint32_t address{ selected_address() };
                    const auto next{
                        std::find_if(
                            typed_objects.begin(),
                            typed_objects.end(),
                            [address](
                                const workbench::project_typed_object_t& object
                            )
                            {
                                return object.object.location.address > address;
                            }
                        )
                    };
                    const auto& object{
                        next != typed_objects.end()
                            ? *next
                            : typed_objects.front()
                    };
                    navigate(static_cast<uint32_t>(
                        object.object.location.address
                    ));
                    status = "Typed object " + object.object.name;
                }
                else if (event.key.scancode == SDL_SCANCODE_P)
                {
                    const auto* object{
                        typed_object_at(
                            typed_objects,
                            data_types,
                            selected_address(),
                            instruction_address_space_name
                        )
                    };
                    if (object == nullptr)
                    {
                        status = "No typed object at the selected address";
                    }
                    else
                    {
                        const analysis::decoded_typed_value_t decoded{
                            decode_typed(object->object)
                        };
                        if (decoded.pointer_targets.empty())
                        {
                            status = "Typed object has no inspectable pointer";
                        }
                        else if (decoded.pointer_targets.front().address
                                 > 0x00ffffffu)
                        {
                            status = "Pointer target is outside the CPU bus";
                        }
                        else
                        {
                            navigate(static_cast<uint32_t>(
                                decoded.pointer_targets.front().address
                            ));
                            status = "Followed typed pointer";
                        }
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_H)
                {
                    error.clear();
                    const bool imported{
                        target_support->refresh_hardware_symbols(project, error)
                    };
                    status = imported ? "Hardware symbols refreshed" : error;
                    refresh_facts = imported;
                }
                else if (event.key.scancode == SDL_SCANCODE_A)
                {
                    run_analysis();
                }
                else if (event.key.scancode == SDL_SCANCODE_N
                         && !analysis_model.functions.empty())
                {
                    const uint32_t current_address{ selected_address() };
                    const auto next{
                        std::find_if(
                            analysis_model.functions.begin(),
                            analysis_model.functions.end(),
                            [current_address](
                                const analysis::function_fact_t& function
                            )
                            {
                                return function.entry.address > current_address;
                            }
                        )
                    };
                    const auto& function{
                        next != analysis_model.functions.end()
                            ? *next
                            : analysis_model.functions.front()
                    };
                    navigate(static_cast<uint32_t>(function.entry.address));
                    status = "Function " + function.stable_id;
                }
                else if (event.key.scancode == SDL_SCANCODE_X)
                {
                    const uint32_t address{ selected_address() };
                    const bool incoming{
                        (event.key.mod & SDL_KMOD_SHIFT) != 0
                    };
                    const auto reference{
                        std::find_if(
                            analysis_model.cross_references.begin(),
                            analysis_model.cross_references.end(),
                            [address, incoming](
                                const analysis::cross_reference_fact_t& fact
                            )
                            {
                                return incoming
                                    ? fact.target.address == address
                                    : fact.source.address == address;
                            }
                        )
                    };
                    if (reference != analysis_model.cross_references.end())
                    {
                        navigate(static_cast<uint32_t>(
                            incoming
                                ? reference->source.address
                                : reference->target.address
                        ));
                        status = incoming
                            ? "Followed incoming cross-reference"
                            : "Followed outgoing cross-reference";
                    }
                    else
                    {
                        status = incoming
                            ? "No incoming cross-reference"
                            : "No outgoing cross-reference";
                    }
                }
                else if (event.key.scancode == SDL_SCANCODE_K
                         && !analysis_model.conflicts.empty())
                {
                    const uint32_t current_address{ selected_address() };
                    const auto next{
                        std::find_if(
                            analysis_model.conflicts.begin(),
                            analysis_model.conflicts.end(),
                            [current_address](
                                const analysis::conflict_fact_t& conflict
                            )
                            {
                                return conflict.location.address > current_address;
                            }
                        )
                    };
                    const auto& conflict{
                        next != analysis_model.conflicts.end()
                            ? *next
                            : analysis_model.conflicts.front()
                    };
                    navigate(static_cast<uint32_t>(conflict.location.address));
                    status = "Conflict: " + conflict.detail;
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
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                100.f,
                "Breakpoints " + std::to_string(debugger.breakpoints().size())
                    + "  Watches " + std::to_string(debugger.watchpoints().size())
            );
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                116.f,
                "Functions " + std::to_string(analysis_model.functions.size())
                    + "  Blocks "
                    + std::to_string(analysis_model.basic_blocks.size())
            );
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                132.f,
                "Xrefs "
                    + std::to_string(analysis_model.cross_references.size())
                    + "  Conflicts "
                    + std::to_string(analysis_model.conflicts.size())
            );
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                148.f,
                "Types " + std::to_string(data_types.size())
                    + "  Typed objects " + std::to_string(typed_objects.size())
            );
            draw_text(
                sdl.renderer,
                left.x + 10.f,
                164.f,
                "Palettes " + std::to_string(palettes.size())
                    + "  Tiles " + std::to_string(tile_assets.size())
                    + "  Maps " + std::to_string(tile_maps.size())
            );
            float fact_y{ 192.f };
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

            std::optional<analysis::decoded_palette_t> displayed_palette{};
            std::optional<analysis::decoded_tile_set_t> displayed_tiles{};
            std::optional<analysis::decoded_palette_t> tile_palette{};
            std::optional<analysis::decoded_tile_map_t> displayed_map{};
            std::optional<analysis::decoded_tile_set_t> displayed_map_tiles{};
            std::optional<analysis::decoded_palette_t> displayed_map_palette{};
            std::optional<frontend::video_plane_frame_view_t>
                displayed_bg_frame{};
            std::optional<frontend::snes::object_layer_state_t> displayed_object_layer{};
            std::optional<analysis::decoded_snes_oam_t> displayed_oam{};
            const frontend::snes::dma_transfer_record_t* displayed_dma_transfer{
                nullptr
            };
            if (output_view)
            {
                draw_text(
                    sdl.renderer,
                    center.x + 10.f,
                    48.f,
                    "LIVE GAME OUTPUT"
                );
                // Debugger execution advances hardware outside run_frame().
                // Publish the latest completed PPU frame only while this view
                // needs it, avoiding a framebuffer copy in the other tools.
                core->refresh_video_frame();
                const frontend::video_frame_view_t frame{ core->video_frame() };
                if (frame.pixels != nullptr
                    && frame.format == frontend::pixel_format_t::argb8888
                    && frame.width <= display.framebuffer_width
                    && frame.height <= display.framebuffer_height)
                {
                    const SDL_Rect update{
                        0,
                        0,
                        static_cast<int>(frame.width),
                        static_cast<int>(frame.height)
                    };
                    static_cast<void>(SDL_UpdateTexture(
                        sdl.video_texture,
                        &update,
                        frame.pixels,
                        static_cast<int>(frame.pitch_bytes)
                    ));
                    const float available_width{ center.w - 24.f };
                    const float available_height{ center.h - 88.f };
                    const float aspect_width{
                        static_cast<float>(frame.width)
                            * display.pixel_aspect_ratio
                    };
                    const float scale{
                        std::min(
                            available_width / aspect_width,
                            available_height
                                / static_cast<float>(frame.height)
                        )
                    };
                    const float rendered_width{ aspect_width * scale };
                    const float rendered_height{
                        static_cast<float>(frame.height) * scale
                    };
                    const SDL_FRect source{
                        0.f,
                        0.f,
                        static_cast<float>(frame.width),
                        static_cast<float>(frame.height)
                    };
                    const SDL_FRect destination{
                        center.x + (center.w - rendered_width) / 2.f,
                        76.f + (available_height - rendered_height) / 2.f,
                        rendered_width,
                        rendered_height
                    };
                    static_cast<void>(SDL_RenderTexture(
                        sdl.renderer,
                        sdl.video_texture,
                        &source,
                        &destination
                    ));
                }
                else
                {
                    draw_text(
                        sdl.renderer,
                        center.x + 10.f,
                        76.f,
                        "Game output is unavailable"
                    );
                }
            }
            else if (dma_view)
            {
                selected_dma_transfer = dma_inspection.record_count == 0u
                    ? 0u
                    : std::min(
                        selected_dma_transfer,
                        dma_inspection.record_count - 1u
                    );
                if (dma_inspection.record_count != 0u)
                    displayed_dma_transfer = &dma_transfers[selected_dma_transfer];
                platform::sdl::snes::render_dma_history(
                    sdl.renderer,
                    center,
                    std::span<const frontend::snes::dma_transfer_record_t>{
                        dma_transfers.data(), dma_inspection.record_count
                    },
                    dma_inspection,
                    selected_dma_transfer,
                    [&sdl](float x, float y, std::string_view text)
                    {
                        draw_text(sdl.renderer, x, y, text);
                    }
                );
            }
            else if (tile_map_view
                     && rendered_bg_view
                     && live_map_layer_index.has_value())
            {
                frontend::video_plane_frame_view_t frame{};
                frontend::video_plane_control_t* const planes{
                    core->video_plane_control()
                };
                if (planes != nullptr
                    && planes->inspect_video_plane_frame(
                        static_cast<frontend::video_plane_id_t>(
                            *live_map_layer_index
                        ),
                        frame
                    )
                    && frame.pixels != nullptr
                    && frame.format == frontend::pixel_format_t::argb8888
                    && frame.width <= display.framebuffer_width
                    && frame.height <= display.framebuffer_height)
                {
                    displayed_bg_frame = frame;
                    const std::string layer_name{
                        live_ppu_snapshot.has_value()
                            ? std::string{ live_ppu_snapshot->layer.label }
                            : "BG" + std::to_string(
                                *live_map_layer_index + 1u
                            )
                    };
                    draw_text(
                        sdl.renderer,
                        center.x + 10.f,
                        48.f,
                        "RAW RENDERED " + layer_name + "  FRAME "
                            + std::to_string(frame.frame_index)
                    );
                    const SDL_Rect update{
                        0,
                        0,
                        static_cast<int>(frame.width),
                        static_cast<int>(frame.height)
                    };
                    static_cast<void>(SDL_UpdateTexture(
                        sdl.video_texture,
                        &update,
                        frame.pixels,
                        static_cast<int>(frame.pitch_bytes)
                    ));
                    const float available_width{ center.w - 24.f };
                    const float available_height{ center.h - 88.f };
                    const float aspect_width{
                        static_cast<float>(frame.width)
                            * display.pixel_aspect_ratio
                    };
                    const float scale{
                        std::min(
                            available_width / aspect_width,
                            available_height
                                / static_cast<float>(frame.height)
                        )
                    };
                    const float rendered_width{ aspect_width * scale };
                    const float rendered_height{
                        static_cast<float>(frame.height) * scale
                    };
                    const SDL_FRect source{
                        0.f,
                        0.f,
                        static_cast<float>(frame.width),
                        static_cast<float>(frame.height)
                    };
                    const SDL_FRect destination{
                        center.x + (center.w - rendered_width) / 2.f,
                        76.f + (available_height - rendered_height) / 2.f,
                        rendered_width,
                        rendered_height
                    };
                    constexpr float checker_size{ 16.f };
                    for (float y{ destination.y };
                         y < destination.y + destination.h;
                         y += checker_size)
                    {
                        for (float x{ destination.x };
                             x < destination.x + destination.w;
                             x += checker_size)
                        {
                            const bool light{
                                ((static_cast<int>(
                                    (x - destination.x) / checker_size
                                )
                                ^ static_cast<int>(
                                    (y - destination.y) / checker_size
                                )) & 1) != 0
                            };
                            static_cast<void>(SDL_SetRenderDrawColor(
                                sdl.renderer,
                                light ? 31u : 22u,
                                light ? 36u : 27u,
                                light ? 46u : 35u,
                                255u
                            ));
                            const SDL_FRect square{
                                x,
                                y,
                                std::min(
                                    checker_size,
                                    destination.x + destination.w - x
                                ),
                                std::min(
                                    checker_size,
                                    destination.y + destination.h - y
                                )
                            };
                            static_cast<void>(SDL_RenderFillRect(
                                sdl.renderer, &square
                            ));
                        }
                    }
                    static_cast<void>(SDL_RenderTexture(
                        sdl.renderer,
                        sdl.video_texture,
                        &source,
                        &destination
                    ));
                }
                else
                {
                    draw_text(
                        sdl.renderer,
                        center.x + 10.f,
                        48.f,
                        "RAW RENDERED BG"
                    );
                    draw_text(
                        sdl.renderer,
                        center.x + 10.f,
                        76.f,
                        "No completed diagnostic BG frame yet; run the ROM "
                        "through one frame"
                    );
                }
            }
            else if (object_view)
            {
                frontend::snes::object_layer_state_t layer{};
                std::array<uint8_t, 544> oam_bytes{};
                std::array<std::byte, 544> inspected_oam{};
                const frontend::memory_inspection_result_t oam_result{
                    target->inspect_memory(
                        { frontend::snes_debug::k_oam_space, 0u },
                        inspected_oam
                    )
                };
                if (object_diagnostics->inspect_object_layer(layer)
                    && oam_result.status
                        == frontend::memory_inspection_status_t::complete
                    && oam_result.bytes_read == inspected_oam.size())
                {
                    for (size_t index{}; index < oam_bytes.size(); ++index)
                    {
                        oam_bytes[index] = std::to_integer<uint8_t>(
                            inspected_oam[index]
                        );
                    }
                    displayed_object_layer = layer;
                    displayed_oam = analysis::decode_snes_oam(
                        {
                            .tile_base_word_address =
                                layer.tile_base_word_address,
                            .name_select = layer.name_select,
                            .base_size = layer.base_size,
                            .interlace = layer.interlace
                        },
                        [&oam_bytes](uint16_t address)
                            -> std::optional<uint8_t>
                        {
                            return oam_bytes[address];
                        }
                    );
                }

                if (displayed_oam.has_value()
                    && displayed_oam->complete())
                {
                    const analysis::decoded_palette_t object_palette{
                        decode_palette({
                            .stable_id = "palette@live-objects",
                            .name = "Live OBJ palette",
                            .location = { "snes.cgram", 0u },
                            .color_count = 256u
                        })
                    };
                    std::unordered_map<uint16_t, analysis::decoded_tile_t>
                        decoded_object_tiles{};
                    platform::sdl::snes::render_oam(
                        sdl.renderer,
                        center,
                        *displayed_oam,
                        object_palette,
                        selected_object,
                        [&decoded_object_tiles, &decode_tiles](
                            uint16_t word_address
                        ) -> std::optional<analysis::decoded_tile_t>
                        {
                            auto tile{ decoded_object_tiles.find(word_address) };
                            if (tile == decoded_object_tiles.end())
                            {
                                const analysis::decoded_tile_set_t decoded{
                                    decode_tiles({
                                        .stable_id = "obj-tile",
                                        .name = "OBJ tile",
                                        .location = {
                                            "snes.vram",
                                            static_cast<uint64_t>(word_address)
                                                * 2u
                                        },
                                        .tile_count = 1u,
                                        .format =
                                            analysis::tile_format_t::snes_4bpp
                                    })
                                };
                                if (decoded.tiles.empty())
                                    return std::nullopt;
                                tile = decoded_object_tiles.emplace(
                                    word_address,
                                    decoded.tiles.front()
                                ).first;
                            }
                            return tile->second;
                        },
                        [&sdl](float x, float y, std::string_view text)
                        {
                            draw_text(sdl.renderer, x, y, text);
                        }
                    );
                }
                else
                {
                    const analysis::decoded_snes_oam_t unavailable{
                        displayed_oam.value_or(analysis::decoded_snes_oam_t{})
                    };
                    platform::sdl::snes::render_oam(
                        sdl.renderer,
                        center,
                        unavailable,
                        {},
                        selected_object,
                        [](uint16_t)
                        {
                            return std::optional<analysis::decoded_tile_t>{};
                        },
                        [&sdl](float x, float y, std::string_view text)
                        {
                            draw_text(sdl.renderer, x, y, text);
                        }
                    );
                }
            }
            else if (tile_map_view && tile_map_index < tile_maps.size())
            {
                const auto live_assets{
                    live_ppu_snapshot.has_value()
                        ? workbench::snes::make_live_bg_assets(
                            live_ppu_snapshot->layer
                        )
                        : std::nullopt
                };
                const bool displaying_live_snapshot{
                    live_assets.has_value()
                    && tile_maps[tile_map_index].asset.stable_id
                        == live_assets->map.stable_id
                };
                if (displaying_live_snapshot)
                {
                    const auto snapshot_reader{
                        [&snapshot = *live_ppu_snapshot](
                            const analysis::address_t& address
                        )
                        {
                            return snapshot.inspect_byte(address);
                        }
                    };
                    displayed_map = analysis::decode_tile_map(
                        live_assets->map, snapshot_reader
                    );
                    displayed_map_tiles = analysis::decode_tiles(
                        live_assets->tiles, snapshot_reader
                    );
                    displayed_map_palette = analysis::decode_palette(
                        live_assets->palette, snapshot_reader
                    );
                }
                else
                {
                    displayed_map = decode_tile_map(
                        tile_maps[tile_map_index].asset
                    );
                    const auto linked_tiles{
                        std::find_if(
                            tile_assets.begin(),
                            tile_assets.end(),
                            [&displayed_map](const auto& tiles)
                            {
                                return tiles.asset.stable_id
                                    == displayed_map->asset.tile_asset_id;
                            }
                        )
                    };
                    if (linked_tiles != tile_assets.end())
                    {
                        displayed_map_tiles = decode_tiles(
                            linked_tiles->asset
                        );
                    }
                    const auto linked_palette{
                        std::find_if(
                            palettes.begin(),
                            palettes.end(),
                            [&displayed_map](const auto& palette)
                            {
                                return palette.asset.stable_id
                                    == displayed_map->asset.palette_id;
                            }
                        )
                    };
                    if (linked_palette != palettes.end())
                    {
                        displayed_map_palette = decode_palette(
                            linked_palette->asset
                        );
                    }
                }
                platform::sdl::snes::render_tile_map(
                    sdl.renderer,
                    center,
                    *displayed_map,
                    displayed_map_tiles.has_value()
                        ? &*displayed_map_tiles : nullptr,
                    displayed_map_palette.has_value()
                        ? &*displayed_map_palette : nullptr,
                    {
                        .live_snapshot = displaying_live_snapshot,
                        .full_map = tile_map_full_view,
                        .horizontal_scroll = static_cast<uint16_t>(
                            displaying_live_snapshot
                                ? live_ppu_snapshot->layer.horizontal_scroll : 0u
                        ),
                        .vertical_scroll = static_cast<uint16_t>(
                            displaying_live_snapshot
                                ? live_ppu_snapshot->layer.vertical_scroll : 0u
                        ),
                        .selected_x = selected_map_x,
                        .selected_y = selected_map_y
                    },
                    [&sdl](float x, float y, std::string_view text)
                    {
                        draw_text(sdl.renderer, x, y, text);
                    }
                );
            }
            else if (graphics_view && tile_asset_index < tile_assets.size())
            {
                displayed_tiles = decode_tiles(
                    tile_assets[tile_asset_index].asset
                );
                const std::string& palette_id{
                    displayed_tiles->asset.palette_id
                };
                const auto linked_palette{
                    std::find_if(
                        palettes.begin(),
                        palettes.end(),
                        [&palette_id](
                            const workbench::project_palette_t& palette
                        )
                        {
                            return palette.asset.stable_id == palette_id;
                        }
                    )
                };
                if (linked_palette != palettes.end())
                    tile_palette = decode_palette(linked_palette->asset);
                platform::sdl::snes::render_tiles(
                    sdl.renderer,
                    center,
                    *displayed_tiles,
                    tile_palette.has_value() ? &*tile_palette : nullptr,
                    selected_tile,
                    [&sdl](float x, float y, std::string_view text)
                    {
                        draw_text(sdl.renderer, x, y, text);
                    }
                );
            }
            else if (palette_view && palette_index < palettes.size())
            {
                displayed_palette = decode_palette(
                    palettes[palette_index].asset
                );
                platform::sdl::snes::render_palette(
                    sdl.renderer,
                    center,
                    *displayed_palette,
                    selected_color,
                    [&sdl](float x, float y, std::string_view text)
                    {
                        draw_text(sdl.renderer, x, y, text);
                    }
                );
            }
            else
            {
                draw_text(
                    sdl.renderer,
                    center.x + 10.f,
                    48.f,
                    "LIVE DISASSEMBLY  "
                        + std::string{
                            debugger.run_state()
                                    == workbench::debugger_run_state_t::running
                                ? "RUNNING"
                                : "PAUSED"
                        }
                        + (debugger.run_state()
                                == workbench::debugger_run_state_t::running
                            ? (traced_continue ? "  TRACED  " : "  FAST  ")
                                + std::to_string(
                                    debugger_instructions_executed
                                )
                                + " INSTRUCTIONS"
                            : "")
                );
                float row_y{ 72.f };
                for (size_t index{ 0 };
                     index < listing.instructions.size();
                     ++index)
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
                        static_cast<void>(
                            SDL_RenderFillRect(sdl.renderer, &selection)
                        );
                        static_cast<void>(SDL_SetRenderDrawColor(
                            sdl.renderer, 238, 244, 255, 255
                        ));
                    }
                    const bool is_current{
                        live_state.instruction_address.value
                            == instruction.address
                    };
                    const bool has_breakpoint{
                        std::any_of(
                            debugger.breakpoints().begin(),
                            debugger.breakpoints().end(),
                            [&instruction](
                                const workbench::breakpoint_t& breakpoint
                            )
                            {
                                return breakpoint.enabled
                                    && breakpoint.address.value
                                        == instruction.address;
                            }
                        )
                    };
                    const bool has_coverage{
                        fact_at(analysis_model.coverage, instruction.address, instruction_address_space_name)
                            != nullptr
                    };
                    const bool has_typed_object{
                        typed_object_at(typed_objects, data_types, instruction.address, instruction_address_space_name) != nullptr
                    };
                    const std::string marker{
                        is_current
                            ? "> "
                            : (has_breakpoint
                                ? "B "
                                : (has_typed_object
                                    ? "T "
                                    : (has_coverage
                                        ? "+ "
                                        : (fact_at(
                                            classifications,
                                            instruction.address,
                                            instruction_address_space_name
                                        ) != nullptr
                                        ? "* "
                                        : "  "))))
                    };
                    draw_text(
                        sdl.renderer,
                        center.x + 10.f,
                        row_y,
                        marker + formatted_address(instruction.address) + "  "
                            + instruction.formatted_bytes + "  "
                            + instruction.formatted_instruction
                    );
                    row_y += 16.f;
                    if (row_y >= center.y + center.h - 12.f)
                        break;
                }
            }

            const uint32_t current{ selected_address() };
            draw_text(sdl.renderer, right.x + 10.f, 48.f, "INSPECTOR");
            draw_text(sdl.renderer, right.x + 10.f, 68.f, formatted_address(current));
            float inspector_y{ 92.f };
            error.clear();
            static_cast<void>(debugger.live_state(live_state, error));
            for (size_t index{ 0 };
                 index < live_state.descriptors.size() && index < 10u;
                 ++index)
            {
                std::ostringstream register_text{};
                register_text << live_state.descriptors[index].label << '='
                              << std::uppercase << std::hex << std::setfill('0')
                              << std::setw(
                                  std::max<int>(
                                      1,
                                      live_state.descriptors[index].width_bits / 4
                                  )
                              )
                              << live_state.values[index].value;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f + static_cast<float>(index % 3u) * 92.f,
                    inspector_y + static_cast<float>(index / 3u) * 16.f,
                    register_text.str()
                );
            }
            inspector_y += 76.f;
            std::array<std::byte, 8> live_memory{};
            const auto memory_result{
                debugger.inspect_memory(
                    { instruction_address_space, current },
                    live_memory
                )
            };
            if (memory_result.status
                == frontend::memory_inspection_status_t::complete)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Memory: " + formatted_memory(live_memory)
                );
                inspector_y += 18.f;
            }
            if (displayed_palette.has_value())
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Palette: " + displayed_palette->asset.name
                );
                inspector_y += 18.f;
                std::ostringstream source_text{};
                source_text << "Source: "
                            << displayed_palette->asset.location.address_space
                            << " $" << std::uppercase << std::hex
                            << displayed_palette->asset.location.address;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    source_text.str()
                );
                inspector_y += 18.f;
                if (selected_color < displayed_palette->colors.size())
                {
                    const analysis::palette_color_t& color{
                        displayed_palette->colors[selected_color]
                    };
                    std::ostringstream raw_text{};
                    raw_text << "Color " << std::dec << color.index
                             << ": $" << std::uppercase << std::hex
                             << std::setfill('0') << std::setw(4)
                             << color.raw_value;
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        raw_text.str()
                    );
                    inspector_y += 18.f;
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "RGB: "
                            + std::to_string(color.red8) + ","
                            + std::to_string(color.green8) + ","
                            + std::to_string(color.blue8)
                            + "  5-bit "
                            + std::to_string(color.red5) + ","
                            + std::to_string(color.green5) + ","
                            + std::to_string(color.blue5)
                    );
                    inspector_y += 18.f;
                }
                if (!displayed_palette->conflicts.empty())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Palette conflict: "
                            + displayed_palette->conflicts.front().detail
                    );
                    inspector_y += 18.f;
                }
            }
            if (displayed_tiles.has_value())
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Tiles: " + displayed_tiles->asset.name
                );
                inspector_y += 18.f;
                std::ostringstream source_text{};
                source_text << "Source: "
                            << displayed_tiles->asset.location.address_space
                            << " $" << std::uppercase << std::hex
                            << displayed_tiles->asset.location.address;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    source_text.str()
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Format: "
                        + std::to_string(analysis::tile_bits_per_pixel(
                            displayed_tiles->asset.format
                        ))
                        + "bpp  Count "
                        + std::to_string(displayed_tiles->asset.tile_count)
                );
                inspector_y += 18.f;
                if (selected_tile < displayed_tiles->tiles.size())
                {
                    const size_t pixel_offset{
                        static_cast<size_t>(selected_pixel_y) * 8u
                            + selected_pixel_x
                    };
                    const uint8_t pixel{
                        displayed_tiles->tiles[selected_tile]
                            .pixels[pixel_offset]
                    };
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Tile " + std::to_string(selected_tile)
                            + "  Pixel "
                            + std::to_string(selected_pixel_x) + ","
                            + std::to_string(selected_pixel_y)
                            + " = " + std::to_string(pixel)
                    );
                    inspector_y += 18.f;
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Palette: "
                            + (displayed_tiles->asset.palette_id.empty()
                                ? "grayscale fallback"
                                : displayed_tiles->asset.palette_id)
                            + " +" + std::to_string(
                                displayed_tiles->asset.palette_base
                            )
                    );
                    inspector_y += 18.f;
                }
                if (!displayed_tiles->conflicts.empty())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Tile conflict: "
                            + displayed_tiles->conflicts.front().detail
                    );
                    inspector_y += 18.f;
                }
            }
            if (displayed_bg_frame.has_value())
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Raw rendered BG frame"
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Frame: "
                        + std::to_string(displayed_bg_frame->frame_index)
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Geometry: "
                        + std::to_string(displayed_bg_frame->width) + "x"
                        + std::to_string(displayed_bg_frame->height)
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Raster state captured per scanline"
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "R: coherent backing tilemap"
                );
                inspector_y += 18.f;
            }
            if (displayed_map.has_value())
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Map: " + displayed_map->asset.name
                );
                inspector_y += 18.f;
                std::ostringstream source_text{};
                source_text << "Source: "
                            << displayed_map->asset.location.address_space
                            << " $" << std::uppercase << std::hex
                            << displayed_map->asset.location.address;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    source_text.str()
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Geometry: " + std::to_string(displayed_map->width)
                        + "x" + std::to_string(displayed_map->height)
                        + "  " + std::to_string(
                            displayed_map->asset.tile_size
                        ) + "px"
                );
                inspector_y += 18.f;
                if (live_ppu_snapshot.has_value()
                    && tile_maps[tile_map_index].asset.stable_id
                        == "tilemap@live-"
                            + std::string{
                                live_ppu_snapshot->layer.label
                            })
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Scroll: "
                            + std::to_string(
                                live_ppu_snapshot->layer.horizontal_scroll
                            )
                            + ","
                            + std::to_string(
                                live_ppu_snapshot->layer.vertical_scroll
                            )
                            + (tile_map_full_view
                                ? "  Full map"
                                : "  Viewport")
                    );
                    inspector_y += 18.f;
                }
                const size_t entry_index{
                    static_cast<size_t>(selected_map_y)
                        * displayed_map->width + selected_map_x
                };
                if (entry_index < displayed_map->entries.size())
                {
                    const auto& entry{
                        displayed_map->entries[entry_index]
                    };
                    std::ostringstream entry_text{};
                    entry_text << "Entry " << std::dec << entry.x << ","
                               << entry.y << ": $" << std::uppercase
                               << std::hex << std::setfill('0')
                               << std::setw(4) << entry.raw_value;
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        entry_text.str()
                    );
                    inspector_y += 18.f;
                    std::ostringstream character_text{};
                    character_text << "Character: $" << std::uppercase
                                   << std::hex << std::setfill('0')
                                   << std::setw(3) << entry.character
                                   << "  Palette " << std::dec
                                   << static_cast<unsigned>(
                                       entry.palette_group
                                   );
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        character_text.str()
                    );
                    inspector_y += 18.f;
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Priority: "
                            + std::string{ entry.priority ? "high" : "low" }
                            + "  Flip: "
                            + (entry.horizontal_flip ? "H" : "-")
                            + (entry.vertical_flip ? "V" : "-")
                    );
                    inspector_y += 18.f;
                }
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Tiles: " + displayed_map->asset.tile_asset_id
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Palette: " + displayed_map->asset.palette_id
                        + " +" + std::to_string(
                            displayed_map->asset.palette_base
                        )
                );
                inspector_y += 18.f;
                if (!displayed_map->conflicts.empty())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Map conflict: "
                            + displayed_map->conflicts.front().detail
                    );
                    inspector_y += 18.f;
                }
            }
            if (displayed_oam.has_value()
                && displayed_object_layer.has_value())
            {
                const analysis::snes_oam_object_t& object{
                    displayed_oam->objects[selected_object]
                };
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Object " + std::to_string(object.index)
                        + (object.intersects_viewport
                            ? "  on-screen"
                            : "  off-screen")
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Position: " + std::to_string(object.screen_x)
                        + "," + std::to_string(object.y)
                        + "  Size " + std::to_string(object.width)
                        + "x" + std::to_string(object.height)
                );
                inspector_y += 18.f;
                std::ostringstream character_text{};
                character_text << "Character: $" << std::uppercase
                               << std::hex << std::setfill('0')
                               << std::setw(2)
                               << static_cast<unsigned>(object.character)
                               << "  Name " << std::dec
                               << (object.name_table ? 1 : 0);
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    character_text.str()
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Palette: " + std::to_string(object.palette)
                        + "  Priority " + std::to_string(object.priority)
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Flip: "
                        + std::string{ object.horizontal_flip ? "H" : "-" }
                        + (object.vertical_flip ? "V" : "-")
                        + "  " + (object.large ? "large" : "small")
                );
                inspector_y += 18.f;
                std::ostringstream base_text{};
                base_text << "OBJ base: $" << std::uppercase << std::hex
                          << std::setfill('0') << std::setw(4)
                          << displayed_object_layer->tile_base_word_address
                          << "  Select " << std::dec
                          << static_cast<unsigned>(
                              displayed_object_layer->base_size
                          );
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    base_text.str()
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "First: " + std::to_string(
                        displayed_object_layer->first_sprite
                    ) + "  Overflow: "
                        + (displayed_object_layer->range_over ? "R" : "-")
                        + (displayed_object_layer->time_over ? "T" : "-")
                        + (displayed_object_layer->active
                            ? "  active"
                            : "  inactive")
                );
                inspector_y += 18.f;
            }
            if (displayed_dma_transfer != nullptr)
            {
                const frontend::snes::dma_transfer_record_t& transfer{
                    *displayed_dma_transfer
                };
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    std::string{
                        transfer.kind == frontend::snes::dma_transfer_kind_t::general
                            ? "General DMA"
                            : "Horizontal-blank DMA"
                    } + "  Channel " + std::to_string(transfer.channel)
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Initiator: " + formatted_address(
                        transfer.initiator_address
                    )
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "A-bus: " + formatted_address(
                        transfer.first_a_bus_address
                    ) + (transfer.last_a_bus_address
                            == transfer.first_a_bus_address
                        ? std::string{}
                        : " - " + formatted_address(
                            transfer.last_a_bus_address
                        ))
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "B-bus: " + formatted_b_bus_targets(
                        transfer.b_bus_base,
                        transfer.b_bus_offset_mask
                    ) + (transfer.direction_to_b_bus
                        ? " destination"
                        : " source")
                );
                inspector_y += 18.f;
                std::ostringstream transfer_size{};
                transfer_size << "Bytes: " << std::dec << transfer.byte_count
                              << "  Mask: $" << std::uppercase << std::hex
                              << std::setfill('0') << std::setw(2)
                              << static_cast<unsigned>(transfer.channel_mask);
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    transfer_size.str()
                );
                inspector_y += 18.f;
                std::ostringstream transfer_values{};
                transfer_values << "Mode " << std::dec
                                << static_cast<unsigned>(
                                    transfer.control & 0x07u
                                )
                                << (transfer.direction_to_b_bus
                                    ? "  A->B  $"
                                    : "  B->A  $")
                                << std::uppercase << std::hex
                                << std::setfill('0') << std::setw(2)
                                << static_cast<unsigned>(transfer.first_value);
                if (transfer.byte_count > 1u)
                {
                    transfer_values << "..$" << std::setw(2)
                                    << static_cast<unsigned>(
                                        transfer.last_value
                                    );
                }
                transfer_values << (transfer.b_bus_access_valid
                    ? "  valid"
                    : "  INVALID");
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    transfer_values.str()
                );
                inspector_y += 18.f;
                std::ostringstream transfer_timing{};
                transfer_timing << "F" << transfer.frame_index << "  "
                                << transfer.first_scanline << ':'
                                << transfer.first_dot;
                if (transfer.last_scanline != transfer.first_scanline
                    || transfer.last_dot != transfer.first_dot)
                {
                    transfer_timing << " - " << transfer.last_scanline
                                    << ':' << transfer.last_dot;
                }
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    transfer_timing.str()
                );
                inspector_y += 18.f;
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "ENTER initiator; SHIFT+ENTER source"
                );
                inspector_y += 18.f;
            }
            if (!palette_view && !graphics_view && !tile_map_view
                && !object_view && !dma_view)
            {
                if (const auto* label{ fact_at(labels, current, instruction_address_space_name) };
                    label != nullptr)
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Label: " + label->text
                    );
                    inspector_y += 18.f;
                }
                if (const auto* comment{ fact_at(comments, current, instruction_address_space_name) };
                    comment != nullptr)
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
                        fact_at(classifications, current, instruction_address_space_name)
                    };
                    classification != nullptr)
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        classification->kind
                                == workbench::classification_kind_t::code
                            ? "Classification: code"
                            : "Classification: data"
                    );
                    inspector_y += 18.f;
                }
            if (const auto* typed{
                    typed_object_at(typed_objects, data_types, current, instruction_address_space_name)
                };
                typed != nullptr)
            {
                const analysis::data_type_t* const type{
                    data_type(data_types, typed->object.type_id)
                };
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Type: " + (type != nullptr
                        ? type->name
                        : typed->object.type_id)
                        + (typed->object.location.address == current
                            ? ""
                            : " +"
                                + std::to_string(
                                    current
                                    - typed->object.location.address
                                ))
                );
                inspector_y += 18.f;
                const analysis::decoded_typed_value_t decoded{
                    decode_typed(typed->object)
                };
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Value: " + decoded.display
                );
                inspector_y += 18.f;
                if (!decoded.pointer_targets.empty())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Pointer: "
                            + decoded.pointer_targets.front().address_space
                            + " "
                            + formatted_address(static_cast<uint32_t>(
                                decoded.pointer_targets.front().address
                                    & 0x00ffffffu
                            ))
                    );
                    inspector_y += 18.f;
                }
                if (!decoded.conflicts.empty())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Typed conflict: "
                            + decoded.conflicts.front().detail
                    );
                    inspector_y += 18.f;
                }
            }
            const analysis::instruction_fact_t* analyzed_instruction{
                fact_at(analysis_model.instructions, current, instruction_address_space_name)
            };
            if (analyzed_instruction != nullptr)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Analysis: "
                        + std::string{
                            confidence_name(analyzed_instruction->confidence)
                        }
                        + " / "
                        + std::string{
                            code_identity_name(
                                analyzed_instruction->code_identity
                            )
                        }
                );
                inspector_y += 18.f;
                const auto provenance{
                    std::find_if(
                        analysis_model.evidence.begin(),
                        analysis_model.evidence.end(),
                        [analyzed_instruction](
                            const analysis::evidence_fact_t& fact
                        )
                        {
                            return fact.subject_id
                                == analyzed_instruction->stable_id;
                        }
                    )
                };
                if (provenance != analysis_model.evidence.end())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Evidence: " + provenance->source
                            + (provenance->session.empty()
                                ? ""
                                : " / " + provenance->session)
                    );
                    inspector_y += 18.f;
                }
            }
            if (const auto* coverage{
                    fact_at(analysis_model.coverage, current, instruction_address_space_name)
                };
                coverage != nullptr)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Coverage: " + std::to_string(coverage->hit_count)
                        + " (" + coverage->session + ")"
                );
                inspector_y += 18.f;
            }
            const auto function{
                std::find_if(
                    analysis_model.functions.begin(),
                    analysis_model.functions.end(),
                    [current](const analysis::function_fact_t& fact)
                    {
                        return fact.entry.address == current;
                    }
                )
            };
            if (function != analysis_model.functions.end())
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Function: " + function->stable_id
                );
                inspector_y += 18.f;
            }
            const auto block{
                std::find_if(
                    analysis_model.basic_blocks.begin(),
                    analysis_model.basic_blocks.end(),
                    [current](const analysis::basic_block_fact_t& fact)
                    {
                        return fact.start.address <= current
                            && fact.end.address > current;
                    }
                )
            };
            if (block != analysis_model.basic_blocks.end())
            {
                const auto owner{
                    std::find_if(
                        analysis_model.function_blocks.begin(),
                        analysis_model.function_blocks.end(),
                        [&block](
                            const analysis::function_block_fact_t& membership
                        )
                        {
                            return membership.block_id == block->stable_id;
                        }
                    )
                };
                if (owner != analysis_model.function_blocks.end())
                {
                    draw_text(
                        sdl.renderer,
                        right.x + 10.f,
                        inspector_y,
                        "Owner: " + owner->function_id
                    );
                    inspector_y += 18.f;
                }
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Block: "
                        + formatted_address(
                            static_cast<uint32_t>(block->start.address)
                        )
                        + "-"
                        + formatted_address(
                            static_cast<uint32_t>(block->end.address)
                        )
                );
                inspector_y += 18.f;
                size_t displayed_edges{};
                for (const analysis::edge_fact_t& edge : analysis_model.edges)
                {
                    if (edge.source_block_id != block->stable_id
                        || displayed_edges >= 3u)
                    {
                        continue;
                    }
                    draw_text(
                        sdl.renderer,
                        right.x + 20.f,
                        inspector_y,
                        std::string{ edge_kind_name(edge.kind) } + " -> "
                            + (edge.target.has_value()
                                ? formatted_address(
                                    static_cast<uint32_t>(
                                        edge.target->address
                                    )
                                )
                                : "?")
                    );
                    inspector_y += 16.f;
                    ++displayed_edges;
                }
            }
            const size_t incoming_xrefs{
                static_cast<size_t>(std::count_if(
                    analysis_model.cross_references.begin(),
                    analysis_model.cross_references.end(),
                    [current](const analysis::cross_reference_fact_t& fact)
                    {
                        return fact.target.address == current;
                    }
                ))
            };
            const size_t outgoing_xrefs{
                static_cast<size_t>(std::count_if(
                    analysis_model.cross_references.begin(),
                    analysis_model.cross_references.end(),
                    [current](const analysis::cross_reference_fact_t& fact)
                    {
                        return fact.source.address == current;
                    }
                ))
            };
            if (incoming_xrefs != 0u || outgoing_xrefs != 0u)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Xrefs: in " + std::to_string(incoming_xrefs)
                        + " / out " + std::to_string(outgoing_xrefs)
                );
                inspector_y += 18.f;
            }
            if (const auto* conflict{
                    fact_at(analysis_model.conflicts, current, instruction_address_space_name)
                };
                conflict != nullptr)
            {
                draw_text(
                    sdl.renderer,
                    right.x + 10.f,
                    inspector_y,
                    "Conflict: " + conflict->detail
                );
                inspector_y += 18.f;
            }
            }
            inspector_y += 12.f;
            draw_text(sdl.renderer, right.x + 10.f, inspector_y, "Q / SHIFT+Q palette / CGRAM");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 16.f, "V / SHIFT+V view / close");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 32.f, "2 / 4 / 8 bind tile format");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 48.f, "SHIFT+2/4/8 live VRAM");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 64.f, "G / SHIFT+G tiles / close");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 80.f, "CTRL+1..4 raw rendered BG");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 96.f, "R        rendered / backing map");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 112.f, "H / SHIFT+H map / close; F full");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 128.f, "O        live OAM / close");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 144.f, "I / SHIFT+I DMA / clear");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 160.f, "Y / SHIFT+Y byte / string");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 176.f, "J / P    typed object / pointer");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 192.f, "A        analyze / publish");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 208.f, "N / K    function / conflict");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 224.f, "X / SHIFT+X xref out / in");
            inspector_y += 240.f;
            draw_text(sdl.renderer, right.x + 10.f, inspector_y, "TAB / F5 output / fast run");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 16.f, "F9       breakpoint");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 32.f, "F10      step over");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 48.f, "F11      step into");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 64.f, "SHIFT+F11 step out");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 80.f, "SHIFT+F5 traced run");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 96.f, "T / M    run-to / watch");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 128.f, "UP/DOWN  select");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 144.f, "ENTER    follow target");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 160.f, "L / ;    label / comment");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 176.f, "B        bookmark");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 192.f, "C / D    code / data");
            draw_text(sdl.renderer, right.x + 10.f, inspector_y + 208.f, "ALT+LEFT/RIGHT history");

            const std::string footer{
                edit_kind == edit_kind_t::none
                    ? status
                    : (edit_kind == edit_kind_t::label
                        ? "Label: "
                        : (edit_kind == edit_kind_t::comment
                            ? "Comment: "
                            : "Watch address: "))
                        + edit_buffer + "_"
            };
            draw_text(
                sdl.renderer,
                12.f,
                height - 26.f,
                footer,
                159u,
                176u,
                202u
            );
            static_cast<void>(SDL_RenderPresent(sdl.renderer));
            SDL_Delay(
                debugger.run_state()
                        == workbench::debugger_run_state_t::running
                    ? 1u
                    : 8u
            );
        }
        return 0;
    }
}
