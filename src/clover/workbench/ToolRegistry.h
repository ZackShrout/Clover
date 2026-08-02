//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace clover::workbench
{
    class active_tool_t;

    using tool_id_t = std::string;
    using command_id_t = std::string;

    inline constexpr std::string_view k_disassembly_tool_id{
        "workbench.disassembly"
    };

    struct tool_descriptor_t
    {
        tool_id_t id{};
        std::string title{};
        std::string owner{};
    };

    enum class command_activation_t : uint8_t
    {
        invoke,
        open_tool,
        toggle_tool
    };

    struct command_descriptor_t
    {
        command_id_t id{};
        std::string label{};
        std::string owner{};
        command_activation_t activation{ command_activation_t::invoke };
        std::optional<tool_id_t> tool{};
    };

    class tool_registry_t
    {
    public:
        [[nodiscard]] bool register_tool(tool_descriptor_t descriptor);
        [[nodiscard]] bool register_command(command_descriptor_t descriptor);

        [[nodiscard]] const tool_descriptor_t* tool(
            std::string_view id
        ) const noexcept;
        [[nodiscard]] const command_descriptor_t* command(
            std::string_view id
        ) const noexcept;
        [[nodiscard]] const std::vector<tool_descriptor_t>& tools() const noexcept;
        [[nodiscard]] const std::vector<command_descriptor_t>& commands() const noexcept;
        [[nodiscard]] bool activate_command(
            std::string_view id,
            active_tool_t& active_tool
        ) const;

    private:
        std::vector<tool_descriptor_t> _tools{};
        std::vector<command_descriptor_t> _commands{};
    };

    class active_tool_t
    {
    public:
        explicit active_tool_t(
            tool_id_t fallback = tool_id_t{ k_disassembly_tool_id }
        );

        [[nodiscard]] std::string_view active() const noexcept;
        [[nodiscard]] std::string_view fallback() const noexcept;
        [[nodiscard]] bool is_active(std::string_view id) const noexcept;
        void activate(std::string_view id);
        void close(std::string_view id);
        void toggle(std::string_view id);

    private:
        tool_id_t _fallback{};
        tool_id_t _active{};
    };

    // Transitional adapter for the SDL shell. It lets the first refactor slice
    // replace mutually exclusive view booleans with one active tool without a
    // simultaneous rewrite of every view's rendering and input code.
    class tool_active_flag_t
    {
    public:
        tool_active_flag_t(active_tool_t& selection, std::string_view tool) noexcept;

        [[nodiscard]] operator bool() const noexcept;
        tool_active_flag_t& operator=(bool active);

    private:
        active_tool_t* _selection{};
        std::string_view _tool{};
    };
}
