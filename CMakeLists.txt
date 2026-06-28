cmake_minimum_required(VERSION 3.20)
project(Clover LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOVER_BUILD_SDL_FRONTEND "Build the SDL3 desktop frontend." OFF)

set(CLOVER_CORE_SOURCES
    src/clover/core/Apu.cpp
    src/clover/core/Bus.cpp
    src/clover/core/Cartridge.cpp
    src/clover/core/Console.cpp
    src/clover/core/Cpu.cpp
    src/clover/core/CpuOpsAlu.cpp
    src/clover/core/CpuOpsBranch.cpp
    src/clover/core/CpuOpsJump.cpp
    src/clover/core/CpuOpsLoad.cpp
    src/clover/core/CpuOpsMemory.cpp
    src/clover/core/CpuOpsModify.cpp
    src/clover/core/CpuOpsStack.cpp
    src/clover/core/CpuOpsSystem.cpp
    src/clover/core/CpuOpsTransfer.cpp
    src/clover/core/Dma.cpp
    src/clover/core/FrameBuffer.cpp
    src/clover/core/Interrupts.cpp
    src/clover/core/Ppu.cpp
    src/clover/core/Scheduler.cpp
)

set(CLOVER_APP_SOURCES
    src/clover/App.cpp
    src/main.cpp
)

add_library(clover_core STATIC ${CLOVER_CORE_SOURCES})
target_include_directories(clover_core PUBLIC src)

add_executable(clover ${CLOVER_APP_SOURCES})
target_include_directories(clover PRIVATE src)
target_link_libraries(clover PRIVATE clover_core)

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

enable_testing()
add_test(NAME clover_smoke_test COMMAND clover_smoke_test)
add_test(NAME clover_hardware_loop_test COMMAND clover_hardware_loop_test)
