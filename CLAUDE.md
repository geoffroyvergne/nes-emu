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
- **Run Emulator:** `./build/NesEmulator path/to/game.nes`
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
    - [ ] 8x16 sprites, sprite overflow flag
  - [x] Step 4.6: Scanline renderer driven by loopy v/t/x: each line drawn at dot 1 from v + fine X; fine/coarse Y increment at dot 256, horizontal t->v copy at 257, vertical t->v copy at pre-render 280-304, odd-frame skipped dot. Mid-frame $2005/$2006 splits take effect on the next line
  - [ ] Step 4.7: Dot-accurate fetch pipeline (mid-scanline effects, $2007 access during rendering, MMC3 A12 IRQ timing)
- [ ] Step 5: Implement Input (Controllers) and APU (Audio)
  - [x] Step 5.1: `Controller` shift registers on $4016/$4017 (strobe reload, A,B,Select,Start,Up,Down,Left,Right order, 1s after 8 reads, open-bus bit 6), keyboard for player 1: Z=A, X=B, Space=Select, Enter=Start, arrows
  - [ ] Step 5.2: APU (pulse, triangle, noise, DMC, frame counter IRQ) + SDL audio
