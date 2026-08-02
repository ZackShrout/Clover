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
}
