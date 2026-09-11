# nes-emu

A Nintendo Entertainment System emulator in C++20 / CMake.

## Status

Milestones 1-6 of the project plan: project scaffold, iNES ROM loading with
an extensible `Mapper` interface (NROM/mapper 0 implemented), a full 6502 CPU
core, and a full PPU (background + sprite rendering, scrolling, OAM DMA,
NMI/vblank timing) driven by a cycle-accurate system clock in `Bus::clock()`.
Keyboard input is wired to controller 1. The result: NROM games render and
are playable, silently - no APU/audio yet, and only mapper 0 ROMs load so
far (more mappers are the next milestone).

Controls: arrow keys = D-pad, X = A, Z = B, Enter = Start, Right Shift =
Select.

## Prerequisites (macOS)

```sh
brew install cmake sdl2
```

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j
```

## Run

```sh
./build/nes-emu [path/to/rom.nes]
```

With no ROM argument, it just opens an empty window (useful for confirming
SDL2 is wired up correctly). Only mapper 0 (NROM) ROMs load successfully so
far; more mappers land in a later milestone.

## Test

```sh
ctest --test-dir build --output-on-failure
```

`cpu_unit_test` is a self-contained suite of hand-assembled 6502 programs
covering addressing modes, flags, cycle counts, the stack, and the
indirect-JMP page-boundary hardware bug - no external files needed.

`cpu_nestest_test` additionally cross-checks the CPU against the well-known
`nestest.nes` golden execution log, if you've downloaded it into
`tests/roms/` (see `tests/roms/README.md`) - it's not bundled since it's a
third-party tool. It's skipped (reported as passing) when absent.

## Project layout

- `src/core/` - the emulator core (Bus, 6502 CPU, 2C02 PPU, Controller,
  Cartridge/Mapper). No SDL dependency; built as a static library
  (`nes_core`) so it can be unit tested headlessly and reused by any future
  frontend.
- `src/platform/` - the SDL2 frontend (window, texture presentation, input).
- `tests/` - CTest-registered test executables.

## Roadmap

See the project plan for the full milestone list: more mappers (MMC1, UxROM,
CNROM, MMC3), APU audio, then joystick/gamepad support and polish
(save states, config, etc).
