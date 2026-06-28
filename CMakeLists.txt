cmake_minimum_required(VERSION 3.20)
project(Clover LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(CLOVER_BUILD_SDL_FRONTEND "Build the SDL3 desktop frontend." OFF)

set(CLOVER_CORE_SOURCES
    src/clover/core/Console.cpp
    src/clover/core/FrameBuffer.cpp
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

enable_testing()
add_test(NAME clover_smoke_test COMMAND clover_smoke_test)
