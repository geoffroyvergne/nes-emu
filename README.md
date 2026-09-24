# NES Emulator

A modular Nintendo Entertainment System emulator written in C++20 with SDL2.

## Dependencies

You need a C++20 compiler, CMake 3.15+ and the SDL2 development package.

### Linux

```sh
# Debian / Ubuntu
sudo apt install build-essential cmake libsdl2-dev
# Fedora
sudo dnf install gcc-c++ cmake SDL2-devel
# Arch
sudo pacman -S base-devel cmake sdl2
```

### macOS

Install the Xcode Command Line Tools (`xcode-select --install`), then:

```sh
brew install cmake sdl2
```

### Windows

Install Visual Studio 2022 (with "Desktop development with C++") and CMake, then get SDL2 through [vcpkg](https://vcpkg.io):

```powershell
vcpkg install sdl2:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>/scripts/buildsystems/vcpkg.cmake
```

Or download the `SDL2-devel-*-VC.zip` from the [SDL releases page](https://github.com/libsdl-org/SDL/releases) and pass `-DSDL2_DIR=<path>/cmake` when configuring. Copy `SDL2.dll` next to the executable before running.

## Build & Run

```sh
cmake -B build
cmake --build build
./build/NesEmulator path/to/game.nes
```

Press **Esc** or close the window to quit.
