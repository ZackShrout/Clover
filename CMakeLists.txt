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
    src/clover/core/snes/Cx4.cpp
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
    src/clover/core/snes/Dsp1.cpp
    src/clover/core/snes/Dsp2.cpp
    src/clover/core/snes/Dsp3.cpp
    src/clover/core/snes/Dsp4.cpp
    src/clover/core/FrameBuffer.cpp
    src/clover/core/snes/Interrupts.cpp
    src/clover/core/snes/Ppu.cpp
    src/clover/core/snes/Scheduler.cpp
    src/clover/core/snes/SuperFx.cpp
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
    find_package(SQLite3 REQUIRED)
    add_library(clover_rom_library STATIC
        src/clover/platform/RomLibrary.cpp
    )
    target_include_directories(clover_rom_library PUBLIC src)
    target_link_libraries(clover_rom_library PUBLIC SQLite::SQLite3)
    add_executable(clover_sdl
        src/clover/frontend/EmulatorCore.cpp
        src/clover/frontend/SnesEmulatorCore.cpp
        src/clover/platform/sdl/SdlAppShell.cpp
        src/sdl_main.cpp
    )
    target_include_directories(clover_sdl PRIVATE src)
    target_link_libraries(clover_sdl PRIVATE clover_core clover_rom_library SDL3::SDL3)

    add_executable(clover_rom_library_test
        tests/RomLibraryTest.cpp
    )
    target_include_directories(clover_rom_library_test PRIVATE src)
    target_link_libraries(clover_rom_library_test PRIVATE clover_rom_library)
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

add_executable(clover_hardware_profile_test
    tests/HardwareProfileTest.cpp
)
target_include_directories(clover_hardware_profile_test PRIVATE src)
target_link_libraries(clover_hardware_profile_test PRIVATE clover_core)

add_executable(clover_save_ram_test
    tests/SaveRamTest.cpp
)
target_include_directories(clover_save_ram_test PRIVATE src)
target_link_libraries(clover_save_ram_test PRIVATE clover_core)

add_executable(clover_frontend_capability_test
    tests/FrontendCapabilityTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(clover_frontend_capability_test PRIVATE src)
target_link_libraries(clover_frontend_capability_test PRIVATE clover_core)

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

add_executable(clover_sdl_audio_pacing_test
    tests/SdlAudioPacingTest.cpp
)
target_include_directories(clover_sdl_audio_pacing_test PRIVATE src)

enable_testing()
add_test(NAME clover_smoke_test COMMAND clover_smoke_test)
add_test(NAME clover_hardware_loop_test COMMAND clover_hardware_loop_test)
add_test(NAME clover_hardware_profile_test COMMAND clover_hardware_profile_test)
add_test(NAME clover_save_ram_test COMMAND clover_save_ram_test)
add_test(NAME clover_frontend_capability_test COMMAND clover_frontend_capability_test)
add_test(NAME clover_local_rom_regression_test COMMAND clover_local_rom_regression_test)
add_test(NAME clover_sdl_audio_pacing_test COMMAND clover_sdl_audio_pacing_test)
if(CLOVER_BUILD_SDL_FRONTEND)
    add_test(NAME clover_rom_library_test COMMAND clover_rom_library_test)
endif()
set_tests_properties(clover_local_rom_regression_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
