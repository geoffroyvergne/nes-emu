cmake_minimum_required(VERSION 3.20)
project(nes-emu CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra)

find_package(SDL2 REQUIRED)

# Core emulator library: CPU, PPU, cartridge/mappers, controller.
# Deliberately has zero SDL/platform dependency so it can be unit tested headlessly
# and reused by any future frontend.
add_library(nes_core STATIC
    src/core/bus.cpp
    src/core/cpu6502.cpp
    src/core/ppu2C02.cpp
    src/core/cartridge.cpp
    src/core/mappers/mapper_nrom.cpp
)
target_include_directories(nes_core PUBLIC src)

add_executable(nes-emu
    src/main.cpp
    src/platform/sdl_app.cpp
)
target_link_libraries(nes-emu PRIVATE nes_core SDL2::SDL2 SDL2::SDL2main)

enable_testing()
add_subdirectory(tests)
