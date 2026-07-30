//
// Created by Zack Shrout on 7/27/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/analysis/Palette.h"
#include "clover/analysis/ProgramModel.h"
#include "clover/analysis/TileGraphics.h"
#include "clover/analysis/TypedData.h"
#include "clover/frontend/EmulatorCore.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

struct sqlite3;

namespace clover::workbench
{
    inline constexpr uint32_t k_project_schema_version{ 7u };
    inline constexpr std::string_view k_analyzer_version{ "clover-analyzer-2" };
    inline constexpr std::string_view k_decoder_version{ "wdc65c816-decoder-1" };

    enum class fact_layer_t : uint8_t
    {
        user,
        imported,
        derived
    };

    enum class classification_kind_t : uint8_t
    {
        code,
        data
    };

    struct project_identity_t
    {
        frontend::system_id_t system{ frontend::system_id_t::snes };
        std::string canonical_media_sha256{};
        uint64_t canonical_media_size{ 0 };
    };

    using address_key_t = analysis::address_t;

    struct analysis_generation_t
    {
        uint64_t generation{ 0 };
        std::string analyzer_version{};
        std::string decoder_version{};
        std::string input_fingerprint{};
        bool current{ false };
    };

    struct named_fact_t
    {
        address_key_t location{};
        std::string text{};
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
        uint64_t analysis_generation{ 0 };
    };

    struct bookmark_t
    {
        int64_t id{ 0 };
        address_key_t location{};
        std::string name{};
    };

    struct classification_t
    {
        address_key_t location{};
        uint64_t length{ 0 };
        classification_kind_t kind{ classification_kind_t::code };
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
        uint64_t analysis_generation{ 0 };
    };

    struct symbol_t
    {
        address_key_t location{};
        std::string name{};
        std::string description{};
        fact_layer_t layer{ fact_layer_t::imported };
        std::string source{};
        uint64_t analysis_generation{ 0 };
    };

    struct project_data_type_t
    {
        analysis::data_type_t definition{};
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
    };

    struct project_typed_object_t
    {
        analysis::typed_object_t object{};
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
    };

    struct project_palette_t
    {
        analysis::palette_asset_t asset{};
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
    };

    struct project_tile_asset_t
    {
        analysis::tile_asset_t asset{};
        fact_layer_t layer{ fact_layer_t::user };
        std::string source{};
    };

    struct navigation_entry_t
    {
        int64_t sequence{ 0 };
        address_key_t location{};
        std::string view{};
    };

    struct project_breakpoint_t
    {
        int64_t id{ 0 };
        address_key_t location{};
        bool enabled{ true };
    };

    enum class project_watch_access_t : uint8_t
    {
        read = 1u << 0u,
        write = 1u << 1u,
        read_write = (1u << 0u) | (1u << 1u)
    };

    struct project_watchpoint_t
    {
        int64_t id{ 0 };
        address_key_t location{};
        uint64_t length{ 1 };
        project_watch_access_t access{ project_watch_access_t::read_write };
        bool enabled{ true };
    };

    class project_t
    {
    public:
        project_t() = default;
        ~project_t();

        project_t(const project_t&) = delete;
        project_t& operator=(const project_t&) = delete;

        [[nodiscard]] bool open(const std::filesystem::path& projects_root,
                                frontend::system_id_t system,
                                std::span<const std::byte> media,
                                std::string& error);
        void close() noexcept;
        [[nodiscard]] bool is_open() const noexcept;
        [[nodiscard]] const project_identity_t& identity() const noexcept;
        [[nodiscard]] const std::filesystem::path& path() const noexcept;
        [[nodiscard]] static std::filesystem::path path_for(
            const std::filesystem::path& projects_root,
            frontend::system_id_t system,
            std::string_view canonical_media_sha256
        );

        [[nodiscard]] bool set_label(const address_key_t& location,
                                     std::string_view label,
                                     std::string& error);
        [[nodiscard]] bool set_comment(const address_key_t& location,
                                       std::string_view comment,
                                       std::string& error);
        [[nodiscard]] bool add_bookmark(const address_key_t& location,
                                        std::string_view name,
                                        std::string& error);
        [[nodiscard]] bool remove_bookmark(int64_t id, std::string& error);
        [[nodiscard]] bool set_classification(const address_key_t& location,
                                              uint64_t length,
                                              classification_kind_t kind,
                                              std::string& error);
        [[nodiscard]] bool set_derived_label(const address_key_t& location,
                                             std::string_view label,
                                             std::string_view source,
                                             std::string& error);
        [[nodiscard]] bool import_snes_hardware_symbols(std::string& error);

        [[nodiscard]] std::vector<named_fact_t> labels(std::string& error) const;
        [[nodiscard]] std::vector<named_fact_t> comments(std::string& error) const;
        [[nodiscard]] std::vector<bookmark_t> bookmarks(std::string& error) const;
        [[nodiscard]] std::vector<classification_t> classifications(
            std::string& error
        ) const;
        [[nodiscard]] std::vector<symbol_t> symbols(std::string& error) const;
        [[nodiscard]] bool set_data_type(
            const analysis::data_type_t& definition,
            std::string& error
        );
        [[nodiscard]] bool set_typed_object(
            const analysis::typed_object_t& object,
            std::string& error
        );
        [[nodiscard]] bool remove_typed_object(
            std::string_view stable_id,
            std::string& error
        );
        [[nodiscard]] std::vector<project_data_type_t> data_types(
            std::string& error
        ) const;
        [[nodiscard]] std::vector<project_typed_object_t> typed_objects(
            std::string& error
        ) const;
        [[nodiscard]] bool set_palette(
            const analysis::palette_asset_t& asset,
            std::string& error
        );
        [[nodiscard]] bool remove_palette(
            std::string_view stable_id,
            std::string& error
        );
        [[nodiscard]] std::vector<project_palette_t> palettes(
            std::string& error
        ) const;
        [[nodiscard]] bool set_tile_asset(
            const analysis::tile_asset_t& asset,
            std::string& error
        );
        [[nodiscard]] bool remove_tile_asset(
            std::string_view stable_id,
            std::string& error
        );
        [[nodiscard]] std::vector<project_tile_asset_t> tile_assets(
            std::string& error
        ) const;

        [[nodiscard]] bool begin_analysis_generation(
            std::string_view analyzer_version,
            std::string_view decoder_version,
            std::string& error
        );
        [[nodiscard]] uint64_t analysis_generation(std::string& error) const;
        [[nodiscard]] bool publish_analysis(
            const analysis::program_model_t& model,
            std::string_view analyzer_version,
            std::string_view decoder_version,
            std::string_view input_fingerprint,
            uint64_t& generation,
            std::string& error
        );
        [[nodiscard]] std::vector<analysis_generation_t> analysis_generations(
            std::string& error
        ) const;
        [[nodiscard]] std::optional<analysis::program_model_t> current_analysis(
            std::string& error
        ) const;
        [[nodiscard]] std::optional<analysis::program_model_t> analysis(
            uint64_t generation,
            std::string& error
        ) const;

        [[nodiscard]] bool record_navigation(const address_key_t& location,
                                             std::string_view view,
                                             std::string& error);
        [[nodiscard]] std::optional<navigation_entry_t> current_navigation(
            std::string& error
        ) const;
        [[nodiscard]] std::optional<navigation_entry_t> navigate_back(
            std::string& error
        );
        [[nodiscard]] std::optional<navigation_entry_t> navigate_forward(
            std::string& error
        );
        [[nodiscard]] std::vector<navigation_entry_t> navigation_history(
            std::string& error
        ) const;
        [[nodiscard]] bool set_debug_breakpoint(const address_key_t& location,
                                                bool enabled,
                                                std::string& error);
        [[nodiscard]] bool remove_debug_breakpoint(
            const address_key_t& location,
            std::string& error
        );
        [[nodiscard]] std::vector<project_breakpoint_t> debug_breakpoints(
            std::string& error
        ) const;
        [[nodiscard]] bool set_debug_watchpoint(
            const address_key_t& location,
            uint64_t length,
            project_watch_access_t access,
            bool enabled,
            std::string& error
        );
        [[nodiscard]] bool remove_debug_watchpoint(
            const address_key_t& location,
            uint64_t length,
            project_watch_access_t access,
            std::string& error
        );
        [[nodiscard]] std::vector<project_watchpoint_t> debug_watchpoints(
            std::string& error
        ) const;

    private:
        [[nodiscard]] bool migrate(std::string& error);
        [[nodiscard]] bool establish_identity(std::string& error);
        [[nodiscard]] bool upsert_named_fact(const char* table,
                                             const address_key_t& location,
                                             std::string_view text,
                                             fact_layer_t layer,
                                             std::string_view source,
                                             uint64_t generation,
                                             std::string& error);
        [[nodiscard]] std::vector<named_fact_t> named_facts(
            const char* table,
            std::string& error
        ) const;
        [[nodiscard]] std::optional<navigation_entry_t> move_navigation(
            bool forward,
            std::string& error
        );

        sqlite3* _database{ nullptr };
        project_identity_t _identity{};
        std::filesystem::path _path{};
    };
}
