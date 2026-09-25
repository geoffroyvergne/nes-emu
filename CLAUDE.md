# NES Emulator Project Style Guide & Instructions

## Project Overview
An accurate, modular Nintendo Entertainment System (NES) emulator written from scratch.

## Tech Stack
- **Language:** C++ (C++20 standard)
- **Build System:** CMake (v3.15+)
- **Graphics/Audio/Input:** SDL2

## Build & Run Commands
- **Configure CMake:** `cmake -B build`
- **Build Project:** `cmake --build build`
- **Run Emulator:** `./build/NesEmulator [--pal | --ntsc] path/to/game.nes` (region: flag > ROM header > file name like "(E)"/"(Europe)" > NTSC; the chosen region and its source are printed at startup); `--input-debug` prints the NES buttons each player holds whenever they change
- **CPU trace (nestest):** `./build/NesEmulator --test-mode nestest.nes` → writes `emulator_execution.log`; compare with `nestest.log` after stripping its PPU column: `sed -E 's/ PPU:[ 0-9]{3},[ 0-9]{3}//' nestest.log`
- **Clean Build:** `rm -rf build`

## Code Style & Architecture Guidelines
- **Naming Conventions:** 
  - PascalCase for Classes and Structs (e.g., `Cpu6502`).
  - camelCase for functions and variables (e.g., `readByte()`, `programCounter`).
  - UPPER_CASE for constants and macros.
- **Project Structure:**
  - `src/`: Source files (`.cpp`)
  - `include/`: Header files (`.hpp`)
  - `external/`: Third-party libraries if needed
- **Architecture rules:**
  - Modern C++ practices (RAII, smart pointers, strict typing with `<cstdint>`).
  - Decoupled components: Bus, CPU, PPU, APU, and Cartridge must interact via a centralized Bus system.
  - Cartridge boards are `Mapper` subclasses (`Mapper_NNN`, one per iNES mapper id): they only translate addresses and hold board registers; `Cartridge` owns PRG/CHR/PRG-RAM memory and instantiates the mapper in a `switch (mapperId)`.
  - No global state.

## Current Roadmap
- [x] Step 1: Initialize CMake and SDL2 window framework
- [x] Step 2: Implement Cartridge/iNES ROM parser
- [x] Step 3: Implement CPU (Ricoh 2A03 / MOS 6502) & Bus Memory Map
  - [x] Step 3.1: Central Bus & CPU memory map (RAM, PPU/APU stubs, cartridge routing, NROM)
  - [x] Step 3.2: CPU core skeleton (registers, RESET, clock loop, opcode lookup table, all addressing modes)
  - [x] Step 3.3: Full 256-entry opcode table (official + unofficial), nestest-format disassembler, `--test-mode` trace to `emulator_execution.log`
  - [x] Step 3.4: Instruction set implementation
    - [x] Load/store/transfer, JMP/JSR/RTS, flag set/clear, branches (+1/+2 cycles), INC/DEC/INX/DEX/INY/DEY, NOP
    - [x] ADC/SBC, AND/ORA/EOR, BIT, CMP/CPX/CPY, ASL/LSR/ROL/ROR
    - [x] Stack (PHA/PHP/PLA/PLP), BRK/RTI, NMI (IRQ line not wired yet: needs APU/mappers)
    - [x] Unofficial opcodes used by nestest (LAX, SAX, DCP, ISB, SLO, RLA, SRE, RRA); full nestest.log matches except the displayed value of write-only APU registers (FF vs 00)
    - [ ] Remaining unstable unofficial opcodes (ANC, ALR, ARR, AXS, XAA, LXA, LAS, TAS, SHA, SHX, SHY) and JAM
- [ ] Step 4: Implement PPU (Picture Processing Unit) pixel pipeline
  - [x] Step 4.1: `Ppu2C02` registers $2000-$2007 (w latch, loopy v/t/x, $2007 read buffer), PPU memory map (CHR via cartridge, nametable mirroring, palette mirroring), scanline/dot timing, VBlank + NMI, 3:1 clocking in the main loop
  - [x] Step 4.2: Pattern table decoding (2-bit planar tiles) + CHR debug view of both tables in the SDL window (grayscale)
  - [x] Step 4.3: NES 64-color system palette, palette RAM mirroring ($3F10/14/18/1C -> $3F00/04/08/0C, 32-byte repeat), `getColorFromPaletteRAM`, colored pattern table + palette swatch debug view (P cycles palettes)
  - [x] Step 4.4: Background rendering (nametable + attribute quadrants + pattern table + palettes, PPUMASK bg enable/left clip/grayscale), 256x240 frame texture in SDL (Tab toggles the pattern table debug view)
  - [x] Step 4.5: 8x8 sprites (`ObjectAttributeEntry` view of OAM, 8 per scanline, OAM-order priority, behind-background bit, H/V flip, transparency, PPUMASK enable/left clip) composited over the background; OAM DMA via $4014 with 513/514-cycle CPU stall
  - [ ] Step 4.5b: Sprite flags and sizes
    - [x] Sprite 0 hit (detected while drawing each scanline against the scrolled background, raised at dot x+1, cleared at pre-render dot 1)
    - [x] 8x16 sprites (PPUCTRL bit 5: table from tile bit 0, top/bottom tiles id&$FE / +1, vertical flip over 16 rows)
    - [ ] Sprite overflow flag
  - [x] Step 4.6: Scanline renderer driven by loopy v/t/x: each line drawn at dot 1 from v + fine X; fine/coarse Y increment at dot 256, horizontal t->v copy at 257, vertical t->v copy at pre-render 280-304, odd-frame skipped dot. Mid-frame $2005/$2006 splits take effect on the next line
  - [ ] Step 4.7: Dot-accurate fetch pipeline (mid-scanline effects, $2007 access during rendering, MMC3 A12 IRQ timing)
- [ ] Step 5: Implement Input (Controllers) and APU (Audio) (Current)
  - [x] Step 5.1: `Controller` shift registers on $4016/$4017 (strobe reload, A,B,Select,Start,Up,Down,Left,Right order, 1s after 8 reads, open-bus bit 6), host input in `HostInput` (`KeyboardPad` + `GamepadManager`): player 1 = keyboard (Z=A, X=B, Right Shift/A=Select, Enter=Start, arrows) + first SDL game controller, player 2 = second controller; pad buttons by position (bottom face = A, right/left face = B, +/Start, -/Select, D-pad + left stick), hot-plug; SDL's default drivers (override with env vars such as SDL_JOYSTICK_HIDAPI=0); opposite D-pad directions cancel out
  - [ ] Step 5.2: APU (Current)
    - [x] `Apu2A03` + reusable `PulseChannel` (duty, envelope, sweep, length counter), Pulse 1 on $4000-$4003, $4015 enable/status, frame counter (4/5-step, ~240 Hz quarter frames), nonlinear pulse mixer, 44.1 kHz resampling + 90 Hz high-pass, SDL audio queue with audio-paced main loop (~50 ms queue)
    - [x] Pulse 2 ($4004-$4007: second `PulseChannel`, two's-complement sweep negate)
    - [x] `TriangleChannel` ($4008-$400B: 32-step sequence, linear + length counters, CPU-rate timer) and `NoiseChannel` ($400C-$400F: 15-bit LFSR long/short mode, NTSC period table, envelope), $4015 bits 0-3, exact nonlinear pulse + triangle/noise mixer tables
    - [x] Timing: the wall clock is the master. `FrameLimiter` wakes at each frame deadline (60.0988 / 50.007 fps), the frame is emulated and presented once at a fixed offset after the deadline (`PresentScheduler`), giving ~0.3 ms frame-delivery jitter on any refresh rate. `SDL_RenderPresent` is never used for pacing (on macOS/Metal it returns at irregular times). Audio follows via `AudioRateControl` (APU output rate nudged by at most +-0.5% to hold the SDL queue near 50 ms). Emulated fps shown in the title bar
    - [x] NTSC/PAL regions: `Region.hpp` holds every region-dependent constant (CPU clock, PPU dots per CPU cycle 3 vs 3.2, 262 vs 312 scanlines, odd-frame skip, APU frame counter steps, noise periods); detected from NES 2.0 byte 12 / iNES byte 9 / file name, overridable with --pal/--ntsc
    - [ ] DMC ($4010-$4013: delta-modulated samples read from PRG via the bus, CPU stall cycles)
    - [x] CPU IRQ line: frame counter IRQ and mapper IRQs (DMC IRQ with the DMC)
- [ ] Step 6: Mappers (Current)
  - [x] `Mapper` base class (cpuMapRead/Write, ppuMapRead/Write, runtime mirroring, PRG-RAM enable); `Mapper_000` (NROM); `Mapper_001` (MMC1: 5-bit serial shift register with bit-7 reset, consecutive-cycle write ignore for RMW instructions, PRG modes 0-3, 4KB/8KB CHR, 4 mirroring modes incl. single-screen, PRG-RAM enable, SUROM 512KB); unsupported mappers fail at load with a clear error
  - [ ] Battery-backed PRG-RAM saved to a `.sav` file next to the ROM (Zelda, Final Fantasy, ...)
  - [x] `Mapper_004` (MMC3: R0-R7 bank registers, 8KB PRG in 2 layouts, 1KB/2KB CHR with A12 inversion, mirroring, scanline IRQ counter with latch/reload/ack, clocked by the PPU at dot 260 of rendered lines); CPU IRQ line (`Bus::isIrqAsserted()` = APU frame IRQ | mapper IRQ, taken at instruction boundaries when I is clear)
  - [ ] Mapper 2 (UxROM), 3 (CNROM), 7 (AxROM)
