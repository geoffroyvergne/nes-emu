# nes-emu

A Nintendo Entertainment System emulator in C++20 / CMake.

## Status

NROM games render and are playable, silently - no APU/audio yet, and only
mapper 0 ROMs load so far (more mappers are the next milestone).

## Features

- Full MOS 6502 CPU core: all official opcodes, correct addressing-mode
  cycle timing (including page-cross and branch-taken extra cycles), stack,
  interrupts, and the indirect-JMP page-boundary hardware bug.
- Full 2C02 PPU: background + sprite rendering, scrolling, sprite-0 hit and
  overflow, OAM DMA, NMI/vblank timing.
- Cycle-accurate system clock (`Bus::clock()`) driving CPU and PPU at their
  real 1:3 ratio.
- iNES ROM loading behind an extensible `Mapper` interface (NROM/mapper 0
  implemented so far).
- Keyboard input wired to controller 1 (see Controls below).
- SDL2 frontend: windowed rendering of the PPU framebuffer, paced to the
  NES's real ~60.0988 Hz frame rate with an adjustable speed multiplier
  (0.25x-4x) instead of relying on vsync.

## Milestones

- [x] 1. Project scaffold (CMake + SDL2 window)
- [x] 2. iNES ROM loading + `Mapper` interface + NROM (mapper 0)
- [x] 3. 6502 CPU core
- [x] 4. CPU validation (self-contained unit tests + optional `nestest` golden-log check)
- [x] 5. PPU core (background + sprite rendering, NMI timing)
- [x] 6. Playable silent loop (SDL framebuffer rendering + keyboard input)
- [ ] 7. More mappers (MMC1, UxROM, CNROM, MMC3)
- [ ] 8. APU (audio): 2 pulse channels, triangle, noise, DMC
- [ ] 9. Joystick/gamepad support (`SDL_GameController`)
- [ ] 10. Polish: save states, pause/reset/fullscreen, config file, FPS counter/vsync toggle

## Controls

| NES button | Key         |
|------------|-------------|
| D-pad      | Arrow keys  |
| A          | X           |
| B          | Z           |
| Start      | Enter       |
| Select     | Right Shift |

A/B are matched by the printed letter (keycode), not physical key position,
so they work correctly on non-QWERTY layouts too (e.g. on AZERTY, pressing
the key labeled "Z" triggers B, even though that's a different physical key
than on QWERTY).

| Action              | Key           |
|---------------------|---------------|
| Speed up (+25%)      | `=` / Numpad `+` |
| Slow down (-25%)     | `-` / Numpad `-` |
| Reset speed to 100%  | `0` / Numpad `0` |

Speed ranges from 25% to 400%; the current value is shown in the window
title. Gamepad/joystick support is milestone 9 (not implemented yet).

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
SDL2 is wired up correctly).

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
