//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include "clover/frontend/EmulatorCore.h"
#include "clover/workbench/Debugger.h"
#include "clover/workbench/ToolRegistry.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>

namespace clover::workbench
{
    class project_t;

    class workbench_target_support_t
    {
    public:
        virtual ~workbench_target_support_t() = default;

        [[nodiscard]] virtual frontend::system_id_t system() const noexcept = 0;
        [[nodiscard]] virtual std::string_view stable_id() const noexcept = 0;
        [[nodiscard]] virtual std::string_view display_name() const noexcept = 0;
        [[nodiscard]] virtual std::unique_ptr<frontend::emulator_core_t>
            create_core() const noexcept = 0;
        [[nodiscard]] virtual std::unique_ptr<debugger_t>
            create_debugger() const = 0;
        [[nodiscard]] virtual bool prepare_project(
            project_t& project,
            const std::filesystem::path& projects_root,
            std::span<const std::byte> media,
            std::string& error
        ) const = 0;
        [[nodiscard]] virtual bool refresh_hardware_symbols(
            project_t& project,
            std::string& error
        ) const = 0;
        virtual void register_tools(tool_registry_t& registry) const = 0;
    };

    [[nodiscard]] std::unique_ptr<workbench_target_support_t>
        create_workbench_target_support(frontend::system_id_t system);
}
