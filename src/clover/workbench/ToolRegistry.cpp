//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/ToolRegistry.h"

#include <algorithm>
#include <utility>

namespace clover::workbench
{
    bool tool_registry_t::register_tool(tool_descriptor_t descriptor)
    {
        if (descriptor.id.empty() || descriptor.title.empty()
            || tool(descriptor.id) != nullptr)
        {
            return false;
        }
        _tools.push_back(std::move(descriptor));
        return true;
    }

    bool tool_registry_t::register_command(command_descriptor_t descriptor)
    {
        if (descriptor.id.empty() || descriptor.label.empty()
            || command(descriptor.id) != nullptr)
        {
            return false;
        }
        if (descriptor.activation != command_activation_t::invoke
            && (!descriptor.tool.has_value()
                || tool(*descriptor.tool) == nullptr))
        {
            return false;
        }
        _commands.push_back(std::move(descriptor));
        return true;
    }

    const tool_descriptor_t* tool_registry_t::tool(
        std::string_view id
    ) const noexcept
    {
        const auto found{
            std::find_if(
                _tools.begin(),
                _tools.end(),
                [id](const tool_descriptor_t& descriptor)
                {
                    return descriptor.id == id;
                }
            )
        };
        return found == _tools.end() ? nullptr : &*found;
    }

    const command_descriptor_t* tool_registry_t::command(
        std::string_view id
    ) const noexcept
    {
        const auto found{
            std::find_if(
                _commands.begin(),
                _commands.end(),
                [id](const command_descriptor_t& descriptor)
                {
                    return descriptor.id == id;
                }
            )
        };
        return found == _commands.end() ? nullptr : &*found;
    }

    const std::vector<tool_descriptor_t>& tool_registry_t::tools() const noexcept
    {
        return _tools;
    }

    const std::vector<command_descriptor_t>&
        tool_registry_t::commands() const noexcept
    {
        return _commands;
    }

    bool tool_registry_t::activate_command(
        std::string_view id,
        active_tool_t& active_tool
    ) const
    {
        const command_descriptor_t* const descriptor{ command(id) };
        if (descriptor == nullptr || !descriptor->tool.has_value())
            return false;
        switch (descriptor->activation)
        {
        case command_activation_t::open_tool:
            active_tool.activate(*descriptor->tool);
            return true;
        case command_activation_t::toggle_tool:
            active_tool.toggle(*descriptor->tool);
            return true;
        case command_activation_t::invoke:
            return false;
        }
        return false;
    }

    active_tool_t::active_tool_t(tool_id_t fallback)
        : _fallback{ std::move(fallback) },
          _active{ _fallback }
    {
    }

    std::string_view active_tool_t::active() const noexcept
    {
        return _active;
    }

    std::string_view active_tool_t::fallback() const noexcept
    {
        return _fallback;
    }

    bool active_tool_t::is_active(std::string_view id) const noexcept
    {
        return _active == id;
    }

    void active_tool_t::activate(std::string_view id)
    {
        _active = id.empty() ? _fallback : tool_id_t{ id };
    }

    void active_tool_t::close(std::string_view id)
    {
        if (is_active(id))
            _active = _fallback;
    }

    void active_tool_t::toggle(std::string_view id)
    {
        if (is_active(id))
            _active = _fallback;
        else
            activate(id);
    }

    tool_active_flag_t::tool_active_flag_t(
        active_tool_t& selection,
        std::string_view tool
    ) noexcept
        : _selection{ &selection },
          _tool{ tool }
    {
    }

    tool_active_flag_t::operator bool() const noexcept
    {
        return _selection != nullptr && _selection->is_active(_tool);
    }

    tool_active_flag_t& tool_active_flag_t::operator=(bool active)
    {
        if (_selection == nullptr)
            return *this;
        if (active)
            _selection->activate(_tool);
        else
            _selection->close(_tool);
        return *this;
    }
}
