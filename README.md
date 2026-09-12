# nes-emu

A Nintendo Entertainment System emulator in C++20 / CMake.

## Status

Games using mapper 0 (NROM), 1 (MMC1), 2 (UxROM), 3 (CNROM), or 4 (MMC3) -
the large majority of the licensed library - render and play with sound.

## Features

- Full MOS 6502 CPU core: all official opcodes, correct addressing-mode
  cycle timing (including page-cross and branch-taken extra cycles), stack,
  interrupts, and the indirect-JMP page-boundary hardware bug.
- Full 2C02 PPU: background + sprite rendering, scrolling, sprite-0 hit and
  overflow, OAM DMA, NMI/vblank timing.
- Full 2A03 APU: 2 pulse channels (with sweep), triangle, noise, and DMC,
  mixed through the standard NES non-linear mixer formula and streamed to
  the audio device at 44.1kHz.
- Audio-glitch mitigations: a short startup mute window (covers a game's
  init-routine transient blips) and proactive buffer-underrun avoidance
  (re-prebuffers before the audio queue actually runs dry, including on
  gamepad hot-connect, which can otherwise stall the audio thread) eliminate
  the clicking/scratching that used to happen at boot and when a controller
  connects.
- Cycle-accurate system clock (`Bus::clock()`) driving CPU, PPU, and APU at
  their real relative rates.
- NTSC and PAL timing: different CPU/PPU/APU clock rates, PPU:CPU ratio
  (NTSC 3:1 vs PAL's non-integer 16:5), scanline count (262 vs 312), and
  frame sequencer timing. Auto-detected from the ROM header by default (see
  Run below for overriding it, since that header is often wrong).
- iNES ROM loading behind an extensible `Mapper` interface: NROM, MMC1,
  UxROM, CNROM, and MMC3 (including MMC3's scanline IRQ counter, used by
  games for split-scroll status bars).
- Keyboard and gamepad input (`SDL_GameController`, with hotplug support):
  player 1 is keyboard and/or the first connected gamepad, player 2 is a
  second gamepad if one's connected (see Controls below).
- SDL2 frontend: windowed rendering of the PPU framebuffer, paced to the
  NES's real ~60.0988 Hz frame rate with an adjustable speed multiplier
  (0.25x-4x) instead of relying on vsync; audio is resampled to match speed
  changes rather than muting.
- Save states (one slot, S/L), pause, soft reset, fullscreen toggle, and a
  live FPS counter (see Controls below).

## Known issues

- Some mapper 4 (MMC3) games that use the scanline IRQ counter for
  split-scroll effects (e.g. Super Mario Bros. 3's status bar) can show a
  vertical rendering artifact near the screen edges while scrolling. The
  MMC3 IRQ counter currently clocks off a fixed per-scanline tick rather
  than the real chip's PPU-address-bus (A12) edge detection, which is
  usually close enough but not exact for every game's timing. Under
  investigation - root-causing this precisely (rather than papering over the
  symptom) needs a real copy of the affected ROM, which isn't bundled here.

## Milestones

- [x] 1. Project scaffold (CMake + SDL2 window)
- [x] 2. iNES ROM loading + `Mapper` interface + NROM (mapper 0)
- [x] 3. 6502 CPU core
- [x] 4. CPU validation (self-contained unit tests + optional `nestest` golden-log check)
- [x] 5. PPU core (background + sprite rendering, NMI timing)
- [x] 6. Playable silent loop (SDL framebuffer rendering + keyboard input)
- [x] 7. More mappers (MMC1, UxROM, CNROM, MMC3 with scanline IRQ)
- [x] 8. APU (audio): 2 pulse channels, triangle, noise, DMC
- [x] 9. Joystick/gamepad support (`SDL_GameController`)
- [x] 10. Polish: save states, pause/reset/fullscreen, FPS counter (no config
      file or vsync toggle - the latter doesn't fit this project's custom
      frame pacer, which replaces vsync entirely; see Run/Controls)

## Controls

### NES controller (player 1: keyboard and/or gamepad; player 2: gamepad only)

| NES button | Key         | Gamepad             |
|------------|-------------|----------------------|
| D-pad      | Arrow keys  | D-pad or left stick  |
| A          | A           | A                    |
| B          | B           | B                    |
| Start      | Enter       | Start                |
| Select     | Right Shift | Back                 |

A/B are matched by the printed letter (keycode), not physical key position,
so they work correctly on non-QWERTY layouts too (e.g. on AZERTY, pressing
the key labeled "A" triggers NES A, even though that's a different physical
key than on QWERTY).

Any SDL-recognized gamepad works out of the box, including hotplugging one
in mid-session. Player 1's gamepad works alongside the keyboard (both
control the same player); a second connected gamepad controls player 2
(there's no keyboard mapping for player 2).

### Emulator controls

| Action                | Key               |
|------------------------|-------------------|
| Speed up (+25%)        | `=` / Numpad `+`  |
| Slow down (-25%)       | `-` / Numpad `-`  |
| Reset speed to 100%    | `0` / Numpad `0`  |
| Pause / resume          | P                 |
| Reset (soft reset)      | R                 |
| Toggle fullscreen       | F                 |
| Save state              | S                 |
| Load state              | L                 |
| Quit                    | Esc               |

Speed ranges from 25% to 400%; audio is resampled to match (pitched up/down,
like a tape), so it keeps playing continuously through speed changes instead
of muting. Save state writes to a single slot per ROM (`<rom path>.state`,
next to the ROM file) - saving again overwrites it. The window title always
shows the current speed, live FPS, and `PAUSED` when paused.

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
./build/nes-emu [path/to/rom.nes] [--pal|--ntsc]
```

With no ROM argument, it just opens an empty window (useful for confirming
SDL2 is wired up correctly).

By default, NTSC vs PAL timing is auto-detected: first from the ROM's iNES
header (frequently wrong - most dumping tools left it at NTSC regardless of
the ROM's actual region), and if that claims NTSC, as a fallback from a
standard No-Intro/GoodNES-style region tag in the filename (e.g. `Game
(Europe).nes` or `Game (E).nes`, checked against Europe/Germany/France/
Italy/Spain/UK/Australia). Still not foolproof - if a game's speed or music
pitch seems off, pass `--pal` or `--ntsc` to force the correct region, or
adjust the live speed multiplier with `-`/`+`/`0` (see Controls). The
startup log line shows which region is active and where that came from
(`header`, `filename`, `header (default)`, `--pal`, or `--ntsc`).

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

`mapper_unit_test` drives MMC1, UxROM, CNROM, and MMC3 directly through the
`Mapper` interface, covering bank-switching arithmetic, MMC1's 5-write
serial protocol, and MMC3's scanline IRQ counter - no ROM files needed.

`apu_unit_test` drives the APU through its register interface (the same
surface real software uses), covering length-counter timing, the frame
sequencer's 4-step IRQ and IRQ-free 5-step mode, and an end-to-end
silence-vs-tone mixer check.

`save_state_unit_test` checks the save-state system with a round-trip
fidelity test: save, run many more cycles (mutating CPU/PPU/APU/mapper
state), load the earlier save back, save again - the two byte buffers must
be exactly identical. Uses a tiny synthetic NROM ROM generated to a temp
file at test time, not a bundled ROM.

## Project layout

- `src/core/` - the emulator core (Bus, 6502 CPU, 2C02 PPU, Controller,
  Cartridge/Mapper). No SDL dependency; built as a static library
  (`nes_core`) so it can be unit tested headlessly and reused by any future
  frontend.
- `src/platform/` - the SDL2 frontend (window, texture presentation, input).
- `tests/` - CTest-registered test executables.
