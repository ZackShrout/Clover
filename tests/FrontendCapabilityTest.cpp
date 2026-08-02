//
// Created by Zack Shrout on 7/20/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/frontend/EmulatorCore.h"
#include "clover/frontend/snes/SnesDiagnosticCapabilities.h"

#include <string_view>

int main()
{
    using namespace clover::frontend;

    std::unique_ptr<emulator_core_t> core{ create_emulator_core(system_id_t::snes) };
    if (!core)
        return 1;
    video_plane_control_t* const control{ core->video_plane_control() };
    if (control == nullptr)
        return 2;
    const std::span<const video_plane_descriptor_t> planes{ control->video_planes() };
    if (planes.size() != 5u
        || planes[0].label != std::string_view{ "BG1" }
        || planes[4].label != std::string_view{ "Objects" })
    {
        return 3;
    }
    for (const video_plane_descriptor_t& plane : planes)
    {
        if (!plane.enabled)
            return 4;
    }
    if (!control->set_video_plane_enabled(planes[0].id, false)
        || control->video_planes()[0].enabled
        || !control->set_video_plane_enabled(planes[0].id, true)
        || !control->video_planes()[0].enabled)
    {
        return 5;
    }
    video_plane_frame_view_t diagnostic_frame{};
    if (control->inspect_video_plane_frame(planes[0].id, diagnostic_frame)
        || diagnostic_frame.pixels != nullptr)
    {
        return 6;
    }
    if (dynamic_cast<snes::tile_layer_diagnostics_t*>(core.get()) != nullptr
        || dynamic_cast<snes::object_layer_diagnostics_t*>(core.get()) != nullptr
        || dynamic_cast<snes::dma_transfer_diagnostics_t*>(core.get()) != nullptr)
    {
        return 7;
    }
    return control->set_video_plane_enabled(99u, false) ? 8 : 0;
}
