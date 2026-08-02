//
// Created by Zack Shrout on 8/1/26.
// Copyright (c) 2026 BunnySoft. All rights reserved.
//

#include "clover/workbench/WorkbenchTargetSupport.h"

#include "clover/workbench/snes/SnesWorkbenchSupport.h"

namespace clover::workbench
{
    std::unique_ptr<workbench_target_support_t>
        create_workbench_target_support(frontend::system_id_t system)
    {
        switch (system)
        {
        case frontend::system_id_t::snes:
            return std::make_unique<snes::snes_workbench_support_t>();
        }
        return nullptr;
    }

    std::unique_ptr<workbench_target_support_t>
        identify_workbench_target_support(std::span<const std::byte> media)
    {
        // The registry is intentionally centralized here. Adding a system
        // extends this list without adding system branches to the host.
        constexpr frontend::system_id_t systems[]{
            frontend::system_id_t::snes
        };
        for (const frontend::system_id_t system : systems)
        {
            std::unique_ptr<workbench_target_support_t> support{
                create_workbench_target_support(system)
            };
            if (support == nullptr)
            {
                continue;
            }
            std::unique_ptr<frontend::emulator_core_t> core{
                support->create_core()
            };
            if (core != nullptr && core->load_media(media))
            {
                return support;
            }
        }
        return nullptr;
    }
}
