cmake_minimum_required(VERSION 3.15)

project(NesEmulator
    VERSION 0.1.0
    DESCRIPTION "A modular NES emulator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# SDL2 ships a CMake config package (SDL2Config.cmake) on all major platforms
# (Homebrew, apt, vcpkg, MSYS2, official dev zips). It provides imported targets.
find_package(SDL2 REQUIRED CONFIG)

add_executable(NesEmulator
    src/main.cpp
    src/Bus.cpp
    src/Cartridge.cpp
    src/Controller.cpp
    src/Cpu6502.cpp
    src/Ppu2C02.cpp
)

target_include_directories(NesEmulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# SDL2::SDL2main supplies WinMain on Windows; it is absent on other platforms.
if(TARGET SDL2::SDL2main)
    target_link_libraries(NesEmulator PRIVATE SDL2::SDL2main)
endif()
target_link_libraries(NesEmulator PRIVATE SDL2::SDL2)

if(MSVC)
    target_compile_options(NesEmulator PRIVATE /W4 /permissive-)
else()
    target_compile_options(NesEmulator PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
endif()
