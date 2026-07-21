cmake_minimum_required(VERSION 3.20)
project(Clover LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOVER_BUILD_SDL_FRONTEND "Build the SDL3 desktop frontend." OFF)

set(CLOVER_CORE_SOURCES
    src/clover/core/snes/Apu.cpp
    src/clover/core/snes/ApuOpsAlu.cpp
    src/clover/core/snes/ApuOpsBranchBit.cpp
    src/clover/core/snes/ApuOpsControl.cpp
    src/clover/core/snes/ApuOpsLoadStore.cpp
    src/clover/core/snes/dsp/SPC_DSP.cpp
    src/clover/core/snes/Bus.cpp
    src/clover/core/snes/Cartridge.cpp
    src/clover/core/snes/Console.cpp
    src/clover/core/snes/Cpu.cpp
    src/clover/core/snes/CpuOpsAlu.cpp
    src/clover/core/snes/CpuOpsBranch.cpp
    src/clover/core/snes/CpuOpsJump.cpp
    src/clover/core/snes/CpuOpsLoad.cpp
    src/clover/core/snes/CpuOpsMemory.cpp
    src/clover/core/snes/CpuOpsModify.cpp
    src/clover/core/snes/CpuOpsStack.cpp
    src/clover/core/snes/CpuOpsSystem.cpp
    src/clover/core/snes/CpuOpsTransfer.cpp
    src/clover/core/snes/Dma.cpp
    src/clover/core/FrameBuffer.cpp
    src/clover/core/snes/Interrupts.cpp
    src/clover/core/snes/Ppu.cpp
    src/clover/core/snes/Scheduler.cpp
)

set(CLOVER_APP_SOURCES
    src/clover/App.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
    src/main.cpp
)

add_library(clover_core STATIC ${CLOVER_CORE_SOURCES})
target_include_directories(clover_core PUBLIC src)

add_executable(clover ${CLOVER_APP_SOURCES})
target_include_directories(clover PRIVATE src)
target_link_libraries(clover PRIVATE clover_core)

if(CLOVER_BUILD_SDL_FRONTEND)
    find_package(SDL3 CONFIG REQUIRED)
    set(CLOVER_SDL_DEFAULT_ROM_PATH
        "${CMAKE_SOURCE_DIR}/roms/local/Final Fantasy 3 (USA).smc"
        CACHE FILEPATH
        "ROM loaded by clover_sdl when no ROM path is supplied on the command line."
    )
    add_executable(clover_sdl
        src/clover/frontend/EmulatorCore.cpp
        src/clover/frontend/SnesEmulatorCore.cpp
        src/clover/platform/sdl/SdlAppShell.cpp
        src/sdl_main.cpp
    )
    target_include_directories(clover_sdl PRIVATE src)
    target_link_libraries(clover_sdl PRIVATE clover_core SDL3::SDL3)
    target_compile_definitions(clover_sdl PRIVATE
        CLOVER_SDL_DEFAULT_ROM_PATH="${CLOVER_SDL_DEFAULT_ROM_PATH}"
    )
endif()

add_executable(clover_smoke_test
    tests/SmokeTest.cpp
)
target_include_directories(clover_smoke_test PRIVATE src)
target_link_libraries(clover_smoke_test PRIVATE clover_core)

add_executable(clover_hardware_loop_test
    tests/HardwareLoopTest.cpp
)
target_include_directories(clover_hardware_loop_test PRIVATE src)
target_link_libraries(clover_hardware_loop_test PRIVATE clover_core)

add_executable(clover_rom_bringup
    tests/RomBringup.cpp
)
target_include_directories(clover_rom_bringup PRIVATE src)
target_link_libraries(clover_rom_bringup PRIVATE clover_core)

add_executable(clover_local_rom_regression_test
    tests/LocalRomRegressionTest.cpp
)
target_include_directories(clover_local_rom_regression_test PRIVATE src)
target_link_libraries(clover_local_rom_regression_test PRIVATE clover_core)

add_executable(clover_bsnes_bringup
    tests/BsnesBringup.cpp
)
target_include_directories(clover_bsnes_bringup PRIVATE src)

add_executable(clover_seeded_micro_bringup
    tests/SeededMicroBringup.cpp
)
target_include_directories(clover_seeded_micro_bringup PRIVATE src)
target_link_libraries(clover_seeded_micro_bringup PRIVATE clover_core)

add_executable(clover_dma_visibility_bringup
    tests/DmaVisibilityBringup.cpp
)
target_include_directories(clover_dma_visibility_bringup PRIVATE src)
target_link_libraries(clover_dma_visibility_bringup PRIVATE clover_core)

add_executable(clover_frame_compare
    tests/FrameCompare.cpp
)
target_include_directories(clover_frame_compare PRIVATE src)

add_executable(clover_frame_range_compare
    tests/FrameRangeCompare.cpp
)
target_include_directories(clover_frame_range_compare PRIVATE src)

enable_testing()
add_test(NAME clover_smoke_test COMMAND clover_smoke_test)
add_test(NAME clover_hardware_loop_test COMMAND clover_hardware_loop_test)
add_test(NAME clover_local_rom_regression_test COMMAND clover_local_rom_regression_test)
set_tests_properties(clover_local_rom_regression_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
