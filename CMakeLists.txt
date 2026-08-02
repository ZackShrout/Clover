cmake_minimum_required(VERSION 3.20)
project(Clover VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOVER_BUILD_SDL_FRONTEND "Build the SDL3 desktop frontend." OFF)
option(CLOVER_PACKAGE_DISTRIBUTION "Build an OS-native distributable application." OFF)
option(CLOVER_ENABLE_PROFILING_SYMBOLS
    "Keep optimized Windows code and emit native symbols for sampling profiles."
    OFF)
set(CLOVER_EXPECTED_CXX_COMPILER_ID "" CACHE STRING
    "Required C++ compiler ID for reproducible distribution builds.")
set(CLOVER_EXPECTED_CXX_COMPILER_VERSION "" CACHE STRING
    "Required C++ compiler version for reproducible distribution builds.")
set(CLOVER_EXPECTED_MSVC_TOOLSET_PREFIX "" CACHE STRING
    "Required VCToolsVersion prefix for reproducible Windows distribution builds.")

if(CLOVER_PACKAGE_DISTRIBUTION)
    if(CLOVER_EXPECTED_CXX_COMPILER_ID
        AND NOT CMAKE_CXX_COMPILER_ID STREQUAL CLOVER_EXPECTED_CXX_COMPILER_ID)
        message(FATAL_ERROR
            "Distribution build requires ${CLOVER_EXPECTED_CXX_COMPILER_ID}; "
            "found ${CMAKE_CXX_COMPILER_ID}.")
    endif()
    if(CLOVER_EXPECTED_CXX_COMPILER_VERSION
        AND NOT CMAKE_CXX_COMPILER_VERSION VERSION_EQUAL
            CLOVER_EXPECTED_CXX_COMPILER_VERSION)
        message(FATAL_ERROR
            "Distribution build requires compiler version "
            "${CLOVER_EXPECTED_CXX_COMPILER_VERSION}; "
            "found ${CMAKE_CXX_COMPILER_VERSION}.")
    endif()
    if(WIN32 AND CLOVER_EXPECTED_MSVC_TOOLSET_PREFIX)
        string(FIND "$ENV{VCToolsVersion}"
            "${CLOVER_EXPECTED_MSVC_TOOLSET_PREFIX}" CLOVER_TOOLSET_PREFIX_POSITION)
        if(NOT CLOVER_TOOLSET_PREFIX_POSITION EQUAL 0)
            message(FATAL_ERROR
                "Distribution build requires MSVC tools "
                "${CLOVER_EXPECTED_MSVC_TOOLSET_PREFIX}x; "
                "found $ENV{VCToolsVersion}.")
        endif()
    endif()
endif()

set(CLOVER_BUILD_COMPILER
    "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(DEFINED ENV{VCToolsVersion} AND NOT "$ENV{VCToolsVersion}" STREQUAL "")
    set(CLOVER_BUILD_PLATFORM_TOOLSET "$ENV{VCToolsVersion}")
else()
    set(CLOVER_BUILD_PLATFORM_TOOLSET "native")
endif()
set(CLOVER_RUNTIME_VERSION "${PROJECT_VERSION}")
if(DEFINED ENV{CLOVER_ARTIFACT_VERSION} AND NOT "$ENV{CLOVER_ARTIFACT_VERSION}" STREQUAL "")
    set(CLOVER_RUNTIME_VERSION "$ENV{CLOVER_ARTIFACT_VERSION}")
endif()
set(CLOVER_BUILD_REVISION "local")
if(DEFINED ENV{GITHUB_SHA} AND NOT "$ENV{GITHUB_SHA}" STREQUAL "")
    set(CLOVER_BUILD_REVISION "$ENV{GITHUB_SHA}")
endif()

if(CLOVER_ENABLE_PROFILING_SYMBOLS)
    if(NOT MSVC)
        message(FATAL_ERROR
            "CLOVER_ENABLE_PROFILING_SYMBOLS currently supports MSVC-compatible "
            "Windows toolchains only.")
    endif()
    # /Zi and /Zo preserve optimized-code line/function information. Link a full
    # PDB without changing the Release optimization level used by the beta build.
    add_compile_options("/Zi" "/Zo")
    add_link_options("/DEBUG:FULL" "/INCREMENTAL:NO")
endif()

find_package(unofficial-sqlite3 CONFIG QUIET)
if(TARGET unofficial::sqlite3::sqlite3)
    add_library(SQLite::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
else()
    find_package(SQLite3 REQUIRED)
endif()

if(MSVC AND CLOVER_PACKAGE_DISTRIBUTION)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

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

add_library(clover_utils STATIC
    src/clover/utils/FileSystem.cpp
    src/clover/utils/SystemInfo.cpp
)
target_include_directories(clover_utils PUBLIC src)

add_library(clover_core STATIC ${CLOVER_CORE_SOURCES})
target_include_directories(clover_core PUBLIC src)

# Workbench uses a separately compiled release-optimized core whose PPU can
# retain diagnostic layer frames.  The ordinary Player core is deliberately
# compiled without this definition, so its hot paths and object layout contain
# no Workbench capture code or storage.
add_library(clover_core_workbench STATIC ${CLOVER_CORE_SOURCES})
target_include_directories(clover_core_workbench PUBLIC src)
target_compile_definitions(clover_core_workbench PUBLIC
    CLOVER_WORKBENCH_PPU_LAYER_CAPTURE=1
    CLOVER_WORKBENCH_DMA_PROVENANCE=1
)

add_library(clover_media_identity STATIC
    src/clover/frontend/MediaIdentity.cpp
)
target_include_directories(clover_media_identity PUBLIC src)

add_library(clover_analysis STATIC
    src/clover/analysis/Oam.cpp
    src/clover/analysis/Palette.cpp
    src/clover/analysis/TileGraphics.cpp
    src/clover/analysis/TileMap.cpp
    src/clover/analysis/TypedData.cpp
    src/clover/analysis/snes/Decoder.cpp
    src/clover/analysis/snes/Formatter.cpp
    src/clover/analysis/snes/HardwareSymbols.cpp
    src/clover/analysis/snes/HybridAnalyzer.cpp
    src/clover/analysis/snes/Instruction.cpp
    src/clover/analysis/snes/StaticListing.cpp
)
target_include_directories(clover_analysis PUBLIC src)

add_library(clover_workbench_project STATIC
    src/clover/workbench/Project.cpp
)
target_include_directories(clover_workbench_project PUBLIC src)
target_link_libraries(clover_workbench_project PUBLIC
    clover_analysis
    clover_media_identity
    clover_utils
    SQLite::SQLite3
)

add_library(clover_workbench_debugger STATIC
    src/clover/workbench/LivePpuSnapshot.cpp
    src/clover/workbench/SnesDebugger.cpp
)
target_include_directories(clover_workbench_debugger PUBLIC src)
target_link_libraries(clover_workbench_debugger PUBLIC
    clover_analysis
)

add_library(clover_snes_checkpoint STATIC
    src/clover/frontend/SnesCheckpoint.cpp
)
target_include_directories(clover_snes_checkpoint PUBLIC src)
target_link_libraries(clover_snes_checkpoint PUBLIC clover_core clover_media_identity)

add_library(clover_snes_checkpoint_workbench STATIC
    src/clover/frontend/SnesCheckpoint.cpp
)
target_include_directories(clover_snes_checkpoint_workbench PUBLIC src)
target_link_libraries(clover_snes_checkpoint_workbench PUBLIC
    clover_core_workbench
    clover_media_identity
)

add_executable(clover ${CLOVER_APP_SOURCES})
target_include_directories(clover PRIVATE src)
target_link_libraries(clover PRIVATE clover_core clover_snes_checkpoint)
if(WIN32 AND CLOVER_PACKAGE_DISTRIBUTION)
    set_target_properties(clover PROPERTIES OUTPUT_NAME clover-cli)
endif()

add_executable(clover_workbench_disasm
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
    src/workbench_disasm_main.cpp
)
target_include_directories(clover_workbench_disasm PRIVATE src)
target_link_libraries(clover_workbench_disasm PRIVATE
    clover_analysis
    clover_snes_checkpoint
)

add_executable(clover_workbench_analyze
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
    src/workbench_analyze_main.cpp
)
target_include_directories(clover_workbench_analyze PRIVATE src)
target_link_libraries(clover_workbench_analyze PRIVATE
    clover_snes_checkpoint
    clover_workbench_debugger
    clover_workbench_project
)

if(CLOVER_BUILD_SDL_FRONTEND)
    find_package(SDL3 CONFIG REQUIRED)
    add_library(clover_rom_library STATIC
        src/clover/platform/RomLibrary.cpp
    )
    target_include_directories(clover_rom_library PUBLIC src)
    target_link_libraries(clover_rom_library PUBLIC
        clover_media_identity
        clover_utils
        SQLite::SQLite3
    )
    if(APPLE AND CLOVER_PACKAGE_DISTRIBUTION)
        add_executable(clover_sdl MACOSX_BUNDLE
            src/clover/frontend/EmulatorCore.cpp
            src/clover/frontend/SnesEmulatorCore.cpp
            src/clover/platform/sdl/SdlAppShell.cpp
            src/sdl_main.cpp
        )
    elseif(WIN32 AND CLOVER_PACKAGE_DISTRIBUTION)
        add_executable(clover_sdl WIN32
            src/clover/frontend/EmulatorCore.cpp
            src/clover/frontend/SnesEmulatorCore.cpp
            src/clover/platform/sdl/SdlAppShell.cpp
            src/sdl_main.cpp
        )
    else()
        add_executable(clover_sdl
            src/clover/frontend/EmulatorCore.cpp
            src/clover/frontend/SnesEmulatorCore.cpp
            src/clover/platform/sdl/SdlAppShell.cpp
            src/sdl_main.cpp
        )
    endif()
    target_include_directories(clover_sdl PRIVATE src)
    target_link_libraries(clover_sdl PRIVATE
        clover_core
        clover_rom_library
        clover_snes_checkpoint
        clover_utils
        SDL3::SDL3
    )
    target_compile_definitions(clover_sdl PRIVATE
        CLOVER_VERSION="${CLOVER_RUNTIME_VERSION}"
        CLOVER_BUILD_REVISION="${CLOVER_BUILD_REVISION}"
        CLOVER_BUILD_COMPILER="${CLOVER_BUILD_COMPILER}"
        CLOVER_BUILD_PLATFORM_TOOLSET="${CLOVER_BUILD_PLATFORM_TOOLSET}"
    )
    if(MSVC)
        target_link_options(clover_sdl PRIVATE "/STACK:16777216")
    endif()

    add_executable(clover_workbench
        src/clover/frontend/EmulatorCore.cpp
        src/clover/frontend/SnesEmulatorCore.cpp
        src/clover/platform/sdl/WorkbenchAppShell.cpp
        src/workbench_main.cpp
    )
    target_include_directories(clover_workbench PRIVATE src)
    target_link_libraries(clover_workbench PRIVATE
        clover_core_workbench
        clover_snes_checkpoint_workbench
        clover_workbench_debugger
        clover_workbench_project
        clover_utils
        SDL3::SDL3
    )
    target_compile_definitions(
        clover_workbench PRIVATE
        CLOVER_VERSION="${CLOVER_RUNTIME_VERSION}"
        CLOVER_BUILD_REVISION="${CLOVER_BUILD_REVISION}"
        CLOVER_BUILD_COMPILER="${CLOVER_BUILD_COMPILER}"
        CLOVER_BUILD_PLATFORM_TOOLSET="${CLOVER_BUILD_PLATFORM_TOOLSET}"
    )
    if(MSVC)
        target_link_options(clover_workbench PRIVATE "/STACK:16777216")
    endif()
    if(CLOVER_PACKAGE_DISTRIBUTION)
        set_target_properties(clover_sdl PROPERTIES OUTPUT_NAME Clover)
        if(APPLE)
            set_target_properties(clover_sdl PROPERTIES
                MACOSX_BUNDLE_BUNDLE_NAME Clover
                MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
                MACOSX_BUNDLE_COPYRIGHT "Copyright (c) 2026 BunnySoft"
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.bunnysoft.clover"
                MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
                MACOSX_BUNDLE_LONG_VERSION_STRING "${PROJECT_VERSION}"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
            )
        endif()
    endif()

    include(GNUInstallDirs)
    if(APPLE AND CLOVER_PACKAGE_DISTRIBUTION)
        install(TARGETS clover_sdl BUNDLE DESTINATION . COMPONENT Runtime)
        install(FILES
            "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-2.1.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/SDL3.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/Snes9x.txt"
            DESTINATION "Clover.app/Contents/Resources/licenses"
            COMPONENT Runtime
        )
    elseif(WIN32)
        install(TARGETS clover_sdl RUNTIME DESTINATION . COMPONENT Runtime)
        install(FILES
            "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-2.1.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/SDL3.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/Snes9x.txt"
            DESTINATION licenses
            COMPONENT Runtime
        )
    elseif(UNIX AND NOT APPLE)
        install(TARGETS clover_sdl RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/linux/com.bunnysoft.clover.desktop"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/applications"
            COMPONENT Runtime
        )
        install(FILES "${CMAKE_SOURCE_DIR}/packaging/Clover.svg"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME com.bunnysoft.clover.svg
            COMPONENT Runtime
        )
        install(FILES
            "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md"
            "${CMAKE_SOURCE_DIR}/LICENSES/LGPL-2.1.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/SDL3.txt"
            "${CMAKE_SOURCE_DIR}/LICENSES/Snes9x.txt"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/clover/licenses"
            COMPONENT Runtime
        )
    else()
        install(TARGETS clover_sdl RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Runtime)
    endif()

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
target_link_libraries(clover_frontend_capability_test PRIVATE
    clover_core
    clover_snes_checkpoint
)

add_executable(clover_analysis_boundary_test
    tests/AnalysisBoundaryTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(clover_analysis_boundary_test PRIVATE src)
target_link_libraries(clover_analysis_boundary_test PRIVATE
    clover_core
    clover_media_identity
    clover_snes_checkpoint
)

add_executable(clover_decoder_test
    tests/DecoderTest.cpp
)
target_include_directories(clover_decoder_test PRIVATE src)
target_link_libraries(clover_decoder_test PRIVATE clover_analysis clover_core)

add_executable(clover_hybrid_analyzer_test
    tests/HybridAnalyzerTest.cpp
)
target_include_directories(clover_hybrid_analyzer_test PRIVATE src)
target_link_libraries(clover_hybrid_analyzer_test PRIVATE clover_analysis)

add_executable(clover_typed_data_test
    tests/TypedDataTest.cpp
)
target_include_directories(clover_typed_data_test PRIVATE src)
target_link_libraries(clover_typed_data_test PRIVATE clover_analysis)

add_executable(clover_palette_test
    tests/PaletteTest.cpp
)
target_include_directories(clover_palette_test PRIVATE src)
target_link_libraries(clover_palette_test PRIVATE clover_analysis)

add_executable(clover_tile_graphics_test
    tests/TileGraphicsTest.cpp
)
target_include_directories(clover_tile_graphics_test PRIVATE src)
target_link_libraries(clover_tile_graphics_test PRIVATE clover_analysis)

add_executable(clover_tile_map_test
    tests/TileMapTest.cpp
)
target_include_directories(clover_tile_map_test PRIVATE src)
target_link_libraries(clover_tile_map_test PRIVATE clover_analysis)

add_executable(clover_oam_test
    tests/OamTest.cpp
)
target_include_directories(clover_oam_test PRIVATE src)
target_link_libraries(clover_oam_test PRIVATE clover_analysis)

add_executable(clover_machine_state_test
    tests/MachineStateTest.cpp
)
target_include_directories(clover_machine_state_test PRIVATE src)
target_link_libraries(clover_machine_state_test PRIVATE clover_core)

add_executable(clover_checkpoint_test
    tests/CheckpointTest.cpp
)
target_include_directories(clover_checkpoint_test PRIVATE src)
target_link_libraries(clover_checkpoint_test PRIVATE clover_snes_checkpoint)

add_executable(clover_checkpoint_replay_test
    tests/CheckpointReplayTest.cpp
)
target_include_directories(clover_checkpoint_replay_test PRIVATE src)
target_link_libraries(clover_checkpoint_replay_test PRIVATE clover_snes_checkpoint)

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

if(UNIX)
    add_executable(clover_bsnes_bringup
        tests/BsnesBringup.cpp
    )
    target_include_directories(clover_bsnes_bringup PRIVATE src)
endif()

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

add_executable(clover_file_system_test
    tests/FileSystemTest.cpp
)
target_include_directories(clover_file_system_test PRIVATE src)
target_link_libraries(clover_file_system_test PRIVATE clover_utils)

add_executable(clover_workbench_project_test
    tests/WorkbenchProjectTest.cpp
)
target_include_directories(clover_workbench_project_test PRIVATE src)
target_link_libraries(clover_workbench_project_test PRIVATE
    clover_workbench_project
)

add_executable(clover_workbench_debugger_test
    tests/WorkbenchDebuggerTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(clover_workbench_debugger_test PRIVATE src)
target_link_libraries(clover_workbench_debugger_test PRIVATE
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
)

add_executable(clover_workbench_analysis_integration_test
    tests/WorkbenchAnalysisIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(
    clover_workbench_analysis_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_analysis_integration_test PRIVATE
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
    clover_workbench_project
)

add_executable(clover_workbench_palette_integration_test
    tests/WorkbenchPaletteIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(
    clover_workbench_palette_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_palette_integration_test PRIVATE
    clover_analysis
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
)

add_executable(clover_workbench_tile_graphics_integration_test
    tests/WorkbenchTileGraphicsIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(
    clover_workbench_tile_graphics_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_tile_graphics_integration_test PRIVATE
    clover_analysis
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
)

add_executable(clover_workbench_tile_map_integration_test
    tests/WorkbenchTileMapIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)

add_executable(clover_workbench_oam_integration_test
    tests/WorkbenchOamIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(
    clover_workbench_oam_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_oam_integration_test PRIVATE
    clover_analysis
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
)

add_executable(clover_workbench_layer_frame_integration_test
    tests/WorkbenchLayerFrameIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)

add_executable(clover_workbench_dma_provenance_integration_test
    tests/WorkbenchDmaProvenanceIntegrationTest.cpp
    src/clover/frontend/EmulatorCore.cpp
    src/clover/frontend/SnesEmulatorCore.cpp
)
target_include_directories(
    clover_workbench_dma_provenance_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_dma_provenance_integration_test PRIVATE
    clover_core_workbench
    clover_snes_checkpoint_workbench
)
target_include_directories(
    clover_workbench_layer_frame_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_layer_frame_integration_test PRIVATE
    clover_core_workbench
    clover_snes_checkpoint_workbench
    clover_workbench_debugger
)
target_include_directories(
    clover_workbench_tile_map_integration_test PRIVATE src
)
target_link_libraries(clover_workbench_tile_map_integration_test PRIVATE
    clover_analysis
    clover_core
    clover_snes_checkpoint
    clover_workbench_debugger
)

if(MSVC)
    set(CLOVER_TEST_TARGETS
        clover_smoke_test
        clover_hardware_loop_test
        clover_hardware_profile_test
        clover_save_ram_test
        clover_frontend_capability_test
        clover_analysis_boundary_test
        clover_decoder_test
        clover_hybrid_analyzer_test
        clover_typed_data_test
        clover_palette_test
        clover_tile_graphics_test
        clover_tile_map_test
        clover_oam_test
        clover_machine_state_test
        clover_checkpoint_test
        clover_local_rom_regression_test
        clover_sdl_audio_pacing_test
        clover_file_system_test
        clover_workbench_project_test
        clover_workbench_debugger_test
        clover_workbench_analysis_integration_test
        clover_workbench_palette_integration_test
        clover_workbench_tile_graphics_integration_test
        clover_workbench_tile_map_integration_test
        clover_workbench_oam_integration_test
        clover_workbench_layer_frame_integration_test
        clover_workbench_dma_provenance_integration_test
    )
    if(TARGET clover_rom_library_test)
        list(APPEND CLOVER_TEST_TARGETS clover_rom_library_test)
    endif()
    foreach(CLOVER_TEST_TARGET IN LISTS CLOVER_TEST_TARGETS)
        target_link_options(${CLOVER_TEST_TARGET} PRIVATE "/STACK:67108864")
    endforeach()
endif()

enable_testing()
add_test(NAME clover_smoke_test COMMAND clover_smoke_test)
add_test(NAME clover_hardware_loop_test COMMAND clover_hardware_loop_test)
add_test(NAME clover_hardware_profile_test COMMAND clover_hardware_profile_test)
add_test(NAME clover_save_ram_test COMMAND clover_save_ram_test)
add_test(NAME clover_frontend_capability_test COMMAND clover_frontend_capability_test)
add_test(NAME clover_analysis_boundary_test COMMAND clover_analysis_boundary_test)
add_test(NAME clover_decoder_test COMMAND clover_decoder_test)
add_test(NAME clover_hybrid_analyzer_test COMMAND clover_hybrid_analyzer_test)
add_test(NAME clover_typed_data_test COMMAND clover_typed_data_test)
add_test(NAME clover_palette_test COMMAND clover_palette_test)
add_test(NAME clover_tile_graphics_test COMMAND clover_tile_graphics_test)
add_test(NAME clover_tile_map_test COMMAND clover_tile_map_test)
add_test(NAME clover_oam_test COMMAND clover_oam_test)
add_test(NAME clover_machine_state_test COMMAND clover_machine_state_test)
add_test(NAME clover_checkpoint_test COMMAND clover_checkpoint_test)
add_test(NAME clover_checkpoint_replay_test COMMAND clover_checkpoint_replay_test)
add_test(NAME clover_local_rom_regression_test COMMAND clover_local_rom_regression_test)
add_test(NAME clover_sdl_audio_pacing_test COMMAND clover_sdl_audio_pacing_test)
add_test(NAME clover_file_system_test COMMAND clover_file_system_test)
add_test(NAME clover_workbench_project_test COMMAND clover_workbench_project_test)
add_test(NAME clover_workbench_debugger_test COMMAND clover_workbench_debugger_test)
add_test(
    NAME clover_workbench_analysis_integration_test
    COMMAND clover_workbench_analysis_integration_test
)
add_test(
    NAME clover_workbench_palette_integration_test
    COMMAND clover_workbench_palette_integration_test
)
add_test(
    NAME clover_workbench_tile_graphics_integration_test
    COMMAND clover_workbench_tile_graphics_integration_test
)
add_test(
    NAME clover_workbench_tile_map_integration_test
    COMMAND clover_workbench_tile_map_integration_test
)
add_test(
    NAME clover_workbench_oam_integration_test
    COMMAND clover_workbench_oam_integration_test
)
add_test(
    NAME clover_workbench_layer_frame_integration_test
    COMMAND clover_workbench_layer_frame_integration_test
)
add_test(
    NAME clover_workbench_dma_provenance_integration_test
    COMMAND clover_workbench_dma_provenance_integration_test
)
if(CLOVER_BUILD_SDL_FRONTEND)
    add_test(NAME clover_rom_library_test COMMAND clover_rom_library_test)
endif()
set_tests_properties(clover_local_rom_regression_test PROPERTIES
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

if(WIN32 AND CLOVER_BUILD_SDL_FRONTEND AND CLOVER_PACKAGE_DISTRIBUTION)
    set(CPACK_PACKAGE_NAME "Clover")
    set(CPACK_PACKAGE_VENDOR "BunnySoft")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Clover Super Nintendo emulator")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "Clover")
    set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/THIRD_PARTY_NOTICES.md")
    set(CPACK_NSIS_DISPLAY_NAME "Clover")
    set(CPACK_NSIS_PACKAGE_NAME "Clover")
    set(CPACK_NSIS_EXECUTABLES_DIRECTORY ".")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "Clover.exe")
    set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)
    set(CPACK_NSIS_MODIFY_PATH OFF)
    set(CPACK_PACKAGE_EXECUTABLES "Clover" "Clover")
    include(CPack)
endif()
