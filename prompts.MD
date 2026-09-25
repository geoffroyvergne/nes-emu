# Prompt Step 1: Initiate Claude.MD README.MD

# Prompt Step 2: 

Act as a senior systems engineer and expert in computer emulation. We are developing a Nintendo Entertainment System (NES) emulator from scratch in C++. 

Please read the instructions, tech stack, and code style guidelines from the `CLAUDE.md` file located in the root directory.

For this first step, please initialize the base project structure by generating the following files:
1. A modern `CMakeLists.txt` file configured to find and link the SDL2 library cleanly.
2. A `src/main.cpp` file that initializes SDL2, creates a window with the native NES resolution (256x240 pixels, scaled up by 3x or 4x for modern displays), clears the screen to black, and handles a basic event loop to close the window via the window close button or the Escape key.
3. A `README.md` file briefly detailing the system dependencies needed to build the project on Linux, macOS, and Windows.

Do not write any CPU or PPU logic yet. Focus strictly on getting this compilation and window framework up and running.

# Prompt Step 3: The iNES ROM Parser

This step is crucial because it allows you to load a .nes file into memory and extract the game code (PRG-ROM) and graphics (CHR-ROM).

We successfully validated the SDL2 window setup. Let's move to Step 2 of our roadmap: Implementing the Cartridge and the iNES (.nes) ROM parser.

Implement this according to the guidelines in `CLAUDE.md` (Modern C++, PascalCase for classes, camelCase for methods, decoupled architecture). 

Create the following files:
1. `include/Cartridge.hpp` and `src/Cartridge.cpp`:
   - A `Cartridge` class that handles loading a `.nes` file from disk.
   - It must parse the 16-byte iNES header to extract:
     * PRG-ROM size (number of 16KB banks)
     * CHR-ROM size (number of 8KB banks)
     * Mapper ID (combine lower and upper nibbles from flags 6 and 7)
     * Mirroring mode (Horizontal, Vertical, or Four-screen from flag 6)
     * Trainer presence (if present, skip the 512 bytes following the header)
   - Store the raw PRG-ROM and CHR-ROM data using `std::vector<uint8_t>`.
   - Provide public methods like `readPRG(uint16_t addr)` and `readCHR(uint16_t addr)` (stubs for now, as mappers will handle this later).

2. Update `src/main.cpp`:
   - Modify it to accept a ROM file path as a command-line argument (e.g., `./NesEmulator path/to/game.nes`).
   - Instantiate the `Cartridge`, load the file, and print the header metadata to the console (PRG size, CHR size, Mapper ID, Mirroring type) to verify successful parsing.

3. Update `CMakeLists.txt` to include the new source files.
4. Update the roadmap in `CLAUDE.md` to mark Step 1 as completed and Step 2 as in-progress.

Generate the code now. Make sure to handle basic file-not-found error checking.

# Prompt Step 3.1: Implementation of the Memory Bus

The bus is the backbone of the emulator. It centralizes all data read and write operations and routes requests to the appropriate component (RAM, CPU, PPU, or cartridge) based on the requested address (memory mapping).

Step 2 (Cartridge/iNES Parser) is complete. Let's move to Step 3.1: Implementing the Central Bus (Memory Map).

The Bus will act as the communications backbone of our emulator. It needs to orchestrate memory reads and writes, routing them to the correct component based on the NES memory map layout.

Implement the following files according to the guidelines in `CLAUDE.md`:

1. `include/Bus.hpp` and `src/Bus.cpp`:
   - Create a `Bus` class.
   - It must contain the main NES System RAM: an array or `std::array<uint8_t, 2048>` (2KB of CPU RAM).
   - It must hold a shared pointer or reference to the current `Cartridge`.
   - Implement two core public methods:
     * `void cpuWrite(uint16_t addr, uint8_t data);`
     * `uint8_t cpuRead(uint16_t addr, bool readOnly = false);`
   - Implement the basic CPU Memory Mapping routing inside these methods:
     * `0x0000` to `0x1FFF`: System RAM (2KB, mirrored every 2KB up to 0x1FFF). Use `addr & 0x07FF` to handle mirroring.
     * `0x2000` to `0x3FFF`: PPU Registers (8 registers, mirrored every 8 bytes up to 0x3FFF). For now, just create a stub or log this access.
     * `0x4000` to `0x401F`: APU and I/O Registers. Create a stub.
     * `0x4020` to `0xFFFF`: Cartridge space (PRG-ROM, PRG-RAM, and Mappers). Route these reads/writes directly to the `Cartridge` object.

2. Update `include/Cartridge.hpp` and `src/Cartridge.cpp`:
   - Add public methods `bool cpuRead(uint16_t addr, uint8_t& data);` and `bool cpuWrite(uint16_t addr, uint8_t data);`.
   - For now, implement a simple Mapper 0 (NROM) hardcoded mapping inside these functions:
     * If reading from `0x8000` to `0xFFFF`, map the address to the PRG-ROM space. (If PRG-ROM is 16KB, mirror the address using `(addr - 0x8000) & 0x3FFF`. If it's 32KB, use `(addr - 0x8000) & 0x7FFF`).

3. Connect components in `src/main.cpp`:
   - Modify the initialization logic so that after loading the `Cartridge`, it is injected/connected into the `Bus` instance.

4. Update `CMakeLists.txt` with the new files.
5. Update `CLAUDE.md` to mark this sub-step as active or completed.

Generate the code keeping it clean, modular, and performance-oriented.

# Prompt Step 3.2: The CPU implementation (Ricoh 2A03 based on the MOS 6502)
This step lays the foundations for the NES processor. It sets up the registers (A, X, Y, Status, Stack Pointer, Program Counter) and the fundamental Fetch-Decode-Execute cycle, implements interrupt handling (such as the RESET signal), and connects the CPU to the bus created in the previous step.

Step 3.1 (Central Bus) is complete. Let's move to Step 3.2: Implementing the CPU Core (Ricoh 2A03 / MOS 6502) Structure and Execution Loop.

We need to implement the CPU registers, the system clock/cycle tracking, communication with the Bus, the RESET sequence, and the skeleton of the instruction decoding pipeline.

Implement the following according to the guidelines in `CLAUDE.md`:

1. `include/Cpu6502.hpp` and `src/Cpu6502.cpp`:
   - Create a `Cpu6502` class.
   - It must hold a reference or pointer to the central `Bus` object to read/write memory.
   - Define the CPU Registers:
     * `uint8_t a;` (Accumulator)
     * `uint8_t x;`, `uint8_t y;` (Index Registers)
     * `uint8_t stp;` (Stack Pointer)
     * `uint16_t pc;` (Program Counter)
     * `uint8_t status;` (Status Register / Flags)
   - Define an enum for the Status Flags bits:
     * C (Carry, bit 0), Z (Zero, bit 1), I (Interrupt Disable, bit 2), D (Decimal Mode, bit 3), B (Break, bit 4), U (Unused, bit 5), V (Overflow, bit 6), N (Negative, bit 7). Note: NES 2A03 doesn't support Decimal mode, but the flag exists.
   - Implement the core public methods:
     * `void reset();` -> Reads the initial PC vector from memory addresses `0xFFFC` and `0xFFFD`, resets registers (SP to 0xFD, status to 0x24), and sets internal cycle count to 7 (RESET takes 7 cycles).
     * `void clock();` -> The main execution tick. If internal cycles remaining is 0, fetch the next opcode from `pc`, increment `pc`, look up the instruction, and execute it. Decrement cycles remaining by 1.
   - Implement internal helper methods:
     * `uint8_t read(uint16_t addr);` and `void write(uint16_t addr, uint8_t data);` that route directly to the `Bus`.
   - For now, create a lightweight structural system to map opcodes to functions. You can use an array of structs containing a function pointer for the operation, a function pointer for the addressing mode, and the base cycle count (similar to the famous OLC 6502 architecture). Implement just a few dummy opcodes or a `NOP` to prove the loop works.

2. Update `include/Bus.hpp` and `src/Bus.cpp`:
   - Connect the `Cpu6502` to the `Bus` so they share bidirectional access if necessary (or inject the Bus into the CPU).

3. Update `src/main.cpp`:
   - Instantiate the `Cpu6502`, connect it to the `Bus`, and call `cpu.reset()`.

4. Update `CMakeLists.txt` and `CLAUDE.md` roadmap.

Generate clean, highly-optimized C++20 code. Do not implement all 150+ opcodes yet; focus entirely on the registers, the clock logic, the RESET vector behavior, and the instruction lookup table infrastructure.

# Prompt Step 3.3: Les modes d'adressage et le framework de logging (compatible avec nestest.log)
This step will equip your CPU with the ability to locate its operands in NES memory (addressing modes) while creating the essential diagnostic tool to verify that your CPU behaves exactly like a real MOS 6502 processor.

Step 3.2 is complete. Let's move to Step 3.3: Implementing CPU Addressing Modes and the Nestest-compatible Logging Framework.

To make our CPU functional, it must know where to fetch data. We also need a strict logging mechanism to compare our execution line-by-line with the golden reference file `nestest.log`.

Implement the following updates according to the guidelines in `CLAUDE.md`:

1. Update `include/Cpu6502.hpp` and `src/Cpu6502.cpp`:
   - Implement the 12 6502 Addressing Modes as internal member functions. Each function must calculate the target absolute memory address, handle potential page boundaries (adding an extra cycle if a page cross occurs, which is critical for cycle-accurate emulation), and return `0` or `1` (indicating if an extra cycle is needed).
   - Addressing modes to implement:
     * Implied (IMP), Immediate (IMM), Zero Page (ZP), Zero Page X (ZPX), Zero Page Y (ZPY), Relative (REL), Absolute (ABS), Absolute X (ABSX), Absolute Y (ABSY), Indirect (IND), Indirect X (INDX), Indirect Y (INDY).
   - Store the calculated address in an internal variable `uint16_t fetchedAddress;` and any read data in `uint8_t fetchedData;`.

2. Implement a Logging/Disassembler Utility:
   - Create a dedicated method or helper class `std::string disassembleLine(uint16_t addr);` or a logging function `void logExecutionState();`.
   - The log output format **MUST strictly match the standard `nestest.log` format**. Example format:
     `C000  4C F5 C5  JMP $C5F5                       A:00 X:00 Y:00 P:24 SP:FD CYC:7`
     *(Address, Hex Opcode/Bytes, Assembly Mnemonic, Target, Registers A, X, Y, Status P, Stack Pointer SP, and cumulative CPU Cycles CYC)*.
   - Note: Ensure your status register byte output prints the Break and Unused flags exactly as `nestest` expects during execution.

3. Update `src/main.cpp`:
   - Add a command-line flag or automated mode (e.g., `--test-mode`) that skips the normal SDL rendering loop, forces the CPU PC to start at `0xC000` (the automation entry point for `nestest.nes`), and dumps the state log to a file named `emulator_execution.log` or directly to `std::cout` at each `clock()` instruction tick.

4. Update `CLAUDE.md` and `CMakeLists.txt` to track these additions.

Generate clean, highly-organized C++20 code. Ensure that the lookup table structure from Step 3.2 is now populated with these addressing mode function pointers. Do not implement the full body of the 150+ instruction execution methods yet; just map their function pointers to stubs so the disassembler infrastructure can resolve them.

# Prompt Step 3.4: Implementation of Opcodes / Instructions
To avoid overloading Claude Code and generating a 2,000-line file all at once (which often introduces copy-paste bugs), this prompt is structured to implement instructions in logical functional groups. It prioritizes the instructions needed to execute the beginning of the famous "nestest" test.

Step 3.3 (Addressing Modes & Logging Framework) is fully operational. Let's move to Step 3.4: Implementing the 6502 Opcode Operations.

To ensure stability and precision, we will implement the instructions by functional groups. Each instruction function must read data using `fetchedData` (or from `fetchedAddress`), perform the operation, update the appropriate Status Flags (Negative, Zero, Carry, Overflow, etc.), and add any conditional branch cycles if required.

Implement the following instructions inside `src/Cpu6502.cpp` and update `include/Cpu6502.hpp`:

1. Group 1: Load, Store, and Transfer Instructions
   - `LDA`, `LDX`, `LDY` (Load Accumulator, X, Y - updates N, Z flags)
   - `STA`, `STX`, `STY` (Store Accumulator, X, Y to memory)
   - `TAX`, `TAY`, `TXA`, `TYA`, `TSX`, `TXS` (Register transfers - updates N, Z flags except for TXS)

2. Group 2: Jumps, Calls, and Status Flag Changes
   - `JMP` (Absolute and Indirect Jump)
   - `JSR` (Jump to Subroutine - pushes PC-1 to stack)
   - `RTS` (Return from Subroutine - pulls PC and increments)
   - `CLC`, `SEC`, `CLD`, `SED`, `CLI`, `SEI`, `CLV` (Clear and Set Flag instructions)

3. Group 3: Branches (Conditional Jumps)
   - `BCC`, `BCS`, `BNE`, `BEQ`, `BPL`, `BMI`, `BVC`, `BVS`
   - Implement the branch cycle logic: +1 cycle if branch succeeds, and an additional +1 cycle if it branches to a different memory page.

4. Group 4: Increment, Decrement, and NOP
   - `INC`, `DEC`, `INX`, `DEX`, `INY`, `DEY` (Updates N, Z flags)
   - `NOP` (No Operation)

5. Populate the Instruction Lookup Table:
   - Wire all legal opcodes corresponding to these instructions to their respective operation functions and addressing modes in your CPU lookup matrix. For un-implemented opcodes (like math instructions ADC/SBC or illegal opcodes), point them to a safe temporary handler that prints an error or acts as a NOP.

6. Update `CLAUDE.md` to track progress.

Generate clean, robust C++20 code. Focus on readability and micro-optimizations since these functions will execute millions of times per second.

# Prompt Step 4.1: PPU Foundations and Interconnection

The CPU successfully passes the automated nestest execution! We are ready to move to Step 4 of our roadmap: Implementing the PPU (Picture Processing Unit) Foundations and Registers.

The PPU runs in parallel with the CPU. For every 1 CPU cycle, the PPU executes 3 cycles. It has its own isolated memory map and communicates with the CPU via 8 memory-mapped registers located at CPU addresses 0x2000 - 0x2007.

Implement the following infrastructure according to our `CLAUDE.md` guidelines:

1. Create `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp`:
   - Create a `Ppu2C02` class.
   - Implement the internal PPU Memory Map structures:
     * `std::array<uint8_t, 2048> tblName;` (Nametables for background layout)
     * `std::array<uint8_t, 32> tblPalette;` (Palette RAM indexes)
     * `std::array<uint8_t, 256> oamMemory;` (Object Attribute Memory for 64 sprites)
   - Implement the bidirectional communication methods for the CPU:
     * `uint8_t cpuRead(uint16_t addr, bool readOnly = false);`
     * `void cpuWrite(uint16_t addr, uint8_t data);`
   - Map these functions to the 8 standard NES registers (handle mirroring for CPU 0x2000-0x3FFF):
     * `0x2000` (PPUCTRL), `0x2001` (PPUMASK), `0x2002` (PPUSTATUS), `0x2003` (OAMADDR), `0x2004` (OAMDATA), `0x2005` (PPUSCROLL), `0x2006` (PPUADDR), `0x2007` (PPUDATA).
   - Implement the internal register latch logic (especially for `PPUADDR` 0x2006 and `PPUSCROLL` 0x2005 which require 2 consecutive writes to full 16-bit address/scroll registers).

2. Implement PPU Internal Read/Write (VRAM access):
   - Methods `uint8_t ppuRead(uint16_t addr);` and `void ppuWrite(uint16_t addr, uint8_t data);` to navigate its own 14-bit addressing space (Pattern tables from Cartridge CHR-ROM, Nametables with Nametable Mirroring, and Palette RAM).

3. Clock and VBlank Timing Skeleton:
   - Implement a `void clock();` method for the PPU.
   - Keep track of `scanline` (0 to 261) and `cycle` (0 to 340).
   - When reaching scanline 241 at cycle 1, trigger the **VBlank** flag in `PPUSTATUS` (0x2002). If the NMI bit is enabled in `PPUCTRL` (0x2000), issue a Non-Maskable Interrupt (`NMI`) to the CPU.

4. Connect the PPU to the System:
   - Update `include/Bus.hpp` and `src/Bus.cpp` to route CPU memory accesses from `0x2000` to `0x3FFF` directly to the `Ppu2C02` instance.
   - Update `src/main.cpp` to instantiate the PPU, link it to the Bus, and synchronize their clock cycles inside the main loop (execute 3 PPU clocks for every 1 CPU clock).

5. Update `CMakeLists.txt` and `CLAUDE.md`.

Generate clean and robust C++20 code, focusing heavily on the 0x2006/0x2007 address latch and register mechanics, as they are the gates games use to upload graphics.

# Prompt Step 4.2: 

Step 4.1 (PPU Infrastructure & Registers) is wired up. Let's move to Step 4.2: Implementing Pattern Table Decoding and CHR Visual Debug Rendering.

Before building the full game frame pipeline, we need to ensure our PPU can correctly read, decode, and render the raw 8x8 pixel tiles stored in the Cartridge's CHR-ROM (Pattern Tables). The NES uses a 2-bit planar format (2 bits per pixel) to fetch color indexes.

Implement the following updates according to our `CLAUDE.md` guidelines:

1. Update `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp`:
   - Implement a method to extract and decode a specific 8x8 tile from the Pattern Tables (Pattern Table 0 at 0x0000-0x0FFF, Pattern Table 1 at 0x1000-0x1FFF).
   - Each tile takes 16 bytes of memory: the first 8 bytes represent bit 0 of the color index for each pixel, and the next 8 bytes represent bit 1. Decode these to form a 2-bit color index (value 0 to 3) for each of the 64 pixels.
   - Implement a public helper method for debugging, such as `SDL_Texture* getPatternTableTexture(int index, uint8_t paletteId)`, or returning a raw buffer, which formats an entire Pattern Table (16x16 tiles = 128x128 pixels) into an image array. For now, since palettes aren't fully linked, map color indexes 0, 1, 2, 3 to hardcoded grayscale values (e.g., Black, Dark Gray, Light Gray, White).

2. Update `src/main.cpp`:
   - Modify the main SDL loop or create a side-by-side viewport. Instead of a blank black screen, call the PPU's pattern table decoding helper.
   - Use `SDL_UpdateTexture` and `SDL_RenderCopy` to draw the two 128x128 Pattern Tables onto the SDL Window at 3x or 4x scale.

3. Update `CLAUDE.md` to log this step as active/completed.

Generate clean C++20 code. Keep the bitwise decoding logic modular and well-commented, as this exact 2-bit tile fetching routine will form the core loop of our future background and sprite renderers.

# Prompt Step 4.3 : Implementing Color Palettes

Step 4.2 (Pattern Table viewer) is up and running. Let's move to Step 4.3: Implementing the NES Color Palette System.

The NES does not use standard RGB values natively; it outputs video signals mapped to an internal 64-color system palette. Games select specific subsets of these colors by writing into the PPU Palette RAM (0x3F00 to 0x3F1F), creating 4 background palettes and 4 sprite palettes.

Implement the following updates according to our `CLAUDE.md` guidelines:

1. Create a System Palette Reference:
   - Inside `src/Ppu2C02.cpp` (or a dedicated namespace/utility), define the standard NES 2C02 RGB system palette array containing the 64 static color structures (e.g., using an array of `struct Color { uint8_t r, g, b; }`). Use a well-known, accurate color palette table (like the composite/NTSC palette).

2. Update `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp`:
   - Refine the PPU Palette RAM internal reading and writing logic (`0x3F00` to `0x3F1F` inside `ppuRead`/`ppuWrite`).
   - Implement the official **Palette Mirroring rules**:
     * Memory locations `0x3F10`, `0x3F14`, `0x3F18`, and `0x3F1C` are mirrors of `0x3F00`, `0x3F04`, `0x3F08`, and `0x3F0C` respectively (universal background color mirrors). Ensure any write or read handles this mirroring using an address mask or redirection.
   - Implement a helper method `Color getColorFromPaletteRAM(uint8_t paletteId, uint8_t pixelIndex);`:
     * This function should look into the PPU Palette RAM to find the system palette index, and then return the corresponding RGB `Color` from our 64-color system array.

3. Update the Debug Renderer in `src/main.cpp`:
   - Modify the Pattern Table debug viewer from Step 4.2 so that it no longer uses generic grayscales.
   - It should now fetch colors using the actual active Palettes in Palette RAM, allowing us to see the game textures in their true colors once a ROM writes to the palette memory.

4. Update `CLAUDE.md` to track progress.

Generate clean and accurate C++20 code. Make sure that the palette mirroring logic is perfectly bulletproof, as any bug here will cause completely corrupted or invisible graphics in games.

# Prompt Step 4.4: NameTables Render (Background)

Step 4.3 (Palette System) is complete. Let's move to Step 4.4: Implementing Nametable Decoding and Background Frame Rendering.

I want to render the full static background layer. The NES layout uses Nametables (32x30 grids of tile indexes) located at VRAM 0x2000-0x2FFF, and Attribute Tables (located at the end of each nametable) which determine which of the 4 background palettes is applied to each 2x2 tile block.

Implement the following updates according to our `CLAUDE.md` guidelines:

1. Update `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp`:
   - Implement the Nametable Mirroring selection logic (Vertical vs. Horizontal mirroring) configured by the Cartridge flags. Map the PPU VRAM addresses `0x2000` to `0x2FFF` into our physical `tblName[2048]` memory array based on the active mirroring mode.
   - Implement a full frame generation routine, such as `void renderBackground(uint32_t* frameBuffer);`:
     * Loop through all 30 scanlines and 32 columns (960 total tiles per screen).
     * For each tile, fetch its index from the current Nametable memory.
     * Fetch its color palette selection from the corresponding Attribute Table byte (decode the 2-bit palette ID for the specific quadrant of the 4x4 tile block).
     * Fetch the 8x8 pixel layout from the active Pattern Table using the tile index.
     * Map each pixel to its true RGB color using the decoded palette ID and the color system from Step 4.3.
     * Write the resulting 256x240 pixel data into a raw 32-bit pixel array/buffer.

2. Update `src/main.cpp`:
   - Set up an SDL texture dedicated to the active screen frame buffer (`256x240` resolution).
   - In the main loop, once the PPU reaches the VBlank state (scanline 241), call the PPU's rendering routine to update your framebuffer.
   - Use `SDL_UpdateTexture` and `SDL_RenderCopy` to draw the completed game screen onto the SDL Window instead of the old debug pattern tables.

3. Update `CLAUDE.md` to track progress.

Generate clean, highly efficient C++20 code. Since this routine processes thousands of pixels every frame, prioritize bitwise optimizations when reading the attribute tables and decoding palettes.

# Prompt Step 4.5: Sprites (OAM) to finally display moving characters

Step 4.4 (Nametable Background Rendering) is complete. Let's move to Step 4.5: Implementing Sprites (OAM) and Object DMA Transfer.

I need to render the foreground layer containing movable objects (sprites). The NES uses Object Attribute Memory (OAM), a 256-byte internal PPU memory that stores properties for up to 64 sprites (4 bytes per sprite: Y-position, Tile Index, Attributes, and X-position). Games typically update this memory using a fast Direct Memory Access (DMA) transfer triggered by writing to CPU register 0x4014.

Implement the following updates according to our `CLAUDE.md` guidelines:

1. Update `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp`:
   - Create an internal structure `struct ObjectAttributeEntry` representing the 4 bytes of a sprite:
     * `uint8_t y;` (Y position - 1)
     * `uint8_t id;` (Tile index in Pattern Tables)
     * `uint8_t attribute;` (Flags: Palette ID, Priority against background, Horizontal flip, Vertical flip)
     * `uint8_t x;` (X position)
   - Ensure the existing `oamMemory` array can be accessed sequentially via this structure layout.
   - Implement basic 8x8 sprite decoding inside the PPU frame generation pipeline:
     * Modify the rendering loop to blend sprites over the background layer.
     * Respect the **Priority bit** (determine if the sprite pixel appears in front of or behind a non-transparent background pixel).
     * Handle **Horizontal and Vertical flipping** by reversing the pixel fetching order when reading the 8x8 tile.
     * For now, focus on standard 8x8 sprites (ignore 8x16 mode to keep things simple).

2. Implement CPU OAM DMA Transfer (`0x4014`):
   - Update `include/Bus.hpp` and `src/Bus.cpp` to intercept writes to address `0x4014`.
   - When a page number `XX` is written to `0x4014`, trigger a DMA block transfer that reads 256 bytes from CPU memory address `0xXX00` - `0xXXFF` and writes them directly into the PPU's `oamMemory`.
   - **Cycle Stalling (Crucial for timing):** This DMA transfer halts the CPU. Suspend the CPU execution loop for 513 or 514 cycles (depending on whether the CPU clock is on an even or odd cycle when DMA starts).

3. Update the Frame Buffer Renderer:
   - Combine the background pixel array from Step 4.4 and the new sprite layer into the final `256x240` framebuffer sent to the SDL texture.

4. Update `CLAUDE.md` to track progress.

Generate clean and highly optimized C++20 code. Ensure sprite color index 0 is correctly treated as completely transparent so the background can show through.

# Prompte Step 4.6: Scrolling

Step 4.5 (Sprites & DMA) is complete. Let's move to Step 4.6: Implementing Fine Scrolling and Loopy Registers (v, t, x, w).

To support games that scroll horizontally or vertically (like Super Mario Bros), we need to transition our PPU from a simple static full-frame buffer to a cycle-accurate or scanline-accurate rendering pipeline driven by the official NES scrolling architecture (the "Loopy Registers").

Implement the following updates inside `include/Ppu2C02.hpp` and `src/Ppu2C02.cpp` according to our `CLAUDE.md` guidelines:

1. Implement the Internal Loopy Registers:
   - Add the following member variables to the `Ppu2C02` class (wrap them cleanly, preferably using a bit field or standard structures):
     * `uint16_t v;` (Current VRAM address, 15 bits: coarse Y, coarse X, nametable select, fine Y)
     * `uint16_t t;` (Temporary VRAM address, 15 bits, stores background scroll offsets)
     * `uint8_t fineX;` (Fine X scroll offset, 3 bits)
     * `bool w;` (First/second write toggle latch, 1 bit)

2. Update CPU Register Interconnections:
   - Rewrite the CPU-to-PPU register write logic for the scrolling gates:
     * `0x2000` (PPUCTRL): Update the nametable select bits inside `t`.
     * `0x2005` (PPUSCROLL): First write sets coarse X and fine X in `t`, sets `w` to true. Second write sets coarse Y and fine Y in `t`, resets `w` to false.
     * `0x2006` (PPUADDR): First write sets high byte of `t` (clearing bit 15), sets `w` to true. Second write sets low byte of `t`, copies `t` into `v`, resets `w` to false.
     * `0x2007` (PPUDATA): Reading/writing must automatically increment the current VRAM address `v` by either 1 or 32 (depending on the PPUCTRL increment flag).

3. Implement Scanline-driven Address Increments:
   - Inside the PPU `clock()` loop, if rendering is enabled (background or sprites visible in PPUMASK):
     * **Increment Coarse X:** At the end of every 8th pixel cycle (cycles 8, 16, ..., 248, 256) on active scanlines, increment the coarse X bits inside `v`. Handle wrapping across nametable boundaries.
     * **Increment Fine Y:** At cycle 256 of active scanlines, increment the fine Y bits inside `v`. If fine Y overflows, increment coarse Y (and handle nametable vertical wrapping).
     * **Transfer X:** At cycle 257 of active scanlines, copy the horizontal scrolling bits from `t` into `v`.
     * **Transfer Y:** During the pre-render scanline (scanline 261), between cycles 280 and 304, repeatedly copy the vertical scrolling bits from `t` into `v`.

4. Update Background Renderer:
   - Refactor `renderBackground` to fetch tiles dynamically based on the state of register `v` at that exact coordinate rather than a hardcoded 0-32 grid loop.

5. Update `CLAUDE.md` to track progress.

Generate clean, highly-performant C++20 code. Pay meticulous attention to bitmasks, as shifting errors in the Loopy registers are the primary cause of screen shaking or split-screen graphical glitches.

# Debug visual problem

I am getting a solid green screen when launching a ROM. This means the CPU is likely running and writing a default background color index into the Palette RAM, but the game loop is stuck or the rendering matrix is misplaced. 

Let's debug this together. Please analyze `src/main.cpp`, `src/Ppu2C02.cpp`, and `src/Cpu6502.cpp` to look for these specific common emulator traps:

1. **PPU/CPU Sync:** Ensure that our main execution loop runs exactly 3 PPU cycles for every 1 CPU cycle, and that the CPU actually processes the NMI interrupt when the PPU triggers it at scanline 241.
2. **PPUSTATUS (0x2002) Read Behavior:** Verify that reading from 0x2002 returns the current VBlank flag (bit 7), but immediately clears that flag in memory and resets the Loopy latch `w` to false. 
3. **Loopy Register updates via 0x2006/0x2007:** Check if writing to 0x2006 correctly builds the 15-bit `t` and copies it to `v` on the second write. Ensure that reads or writes to 0x2007 safely increment `v` by 1 or 32, without corrupting the fine scroll bits.
4. **Sprite 0 Hit or Scanline Timing:** Check if the game is waiting for a Sprite 0 hit flag or a VBlank flag that never updates because our clock counters are drifting.

Inspect these files, add logs if necessary to see where the CPU is spinning, or fix the bitwise logic in the PPU registers.


# Prompt Step 5.1: Controls IN OUT joypad, keyboard

The graphics are now rendering beautifully! However, the character controls are completely erratic and unplayable. This is a classic symptom of an incorrect shift register implementation for the NES Controllers (address 0x4016).

The NES expects a specific latching mechanism:
1. When the CPU writes to 0x4016, bit 0 acts as a "Strobe" flag. If it's set to 1, the controller continuously captures the real-time state of the 8 buttons (A, B, Select, Start, Up, Down, Left, Right). 
2. When the Strobe flag transitions back to 0, the current state of the 8 buttons is locked into an internal 8-bit shift register.
3. When the CPU reads from 0x4016, it only reads bit 0 of that register (returning 1 if the button is pressed, 0 if not). Crucially, every read MUST shift the internal register by 1 bit to prepare the next button for the subsequent read. If the CPU reads beyond the 8 buttons, it should return 1 or 0 safely without re-triggering random buttons.

Please implement or fix this behavior according to our `CLAUDE.md` guidelines:

1. Update `include/Bus.hpp` and `src/Bus.cpp` (or create a dedicated `Controller` structure):
   - Add variables to track `uint8_t controllerState[2];` (the real-time keyboard/SDL state for Player 1 and Player 2).
   - Add variables to track `uint8_t controllerShiftRegister[2];` (the locked state being read).
   - Add a `uint8_t controllerStrobe;` flag.
   - Refactor the CPU read/write logic for `0x4016` (and `0x4017` for Player 2):
     * **Write to 0x4016:** Store the strobe value. If `controllerStrobe & 1` is true, continuously copy `controllerState` into `controllerShiftRegister`.
     * **Read from 0x4016:** If strobe is on, return the live status of the 'A' button. If strobe is off, return the lowest bit of `controllerShiftRegister[0]`, then shift `controllerShiftRegister[0]` right by 1 bit (`>> 1`). 

2. Update `src/main.cpp` (SDL Event Loop):
   - In your SDL event loop (`SDL_POLLEVENT`), capture `SDL_KEYDOWN` and `SDL_KEYUP` events.
   - Map your keyboard keys (e.g., Z/X for A/B, Space/Enter for Select/Start, and Arrow Keys for D-pad) directly to the corresponding bits of the `controllerState[0]` byte.

Review the input handling code and fix these bitwise shift register mechanics so our inputs become stable and responsive.

# Prompt Step 6: Scroling

The game starts and I can now enter the level with working controls! However, the screen does not scroll when the character moves forward. This is because we need to implement the "Sprite 0 Hit" evaluation, which games use to separate fixed status bars (like scores) from the scrolling game world.

Implement the Sprite 0 Hit detection according to our `CLAUDE.md` guidelines:

1. Update the PPU Clock Loop (`src/Ppu2C02.cpp`):
   - During the visible cycles (cycles 1 to 256) of the active rendering scanlines (0 to 239):
     * Check if background rendering and sprite rendering are both enabled in `PPUMASK` (0x2001).
     * Check if the PPU is currently rendering an opaque pixel from **Sprite 0** (the very first entry in OAM, index 0).
     * Check if that sprite pixel overlaps with an opaque background pixel at the exact same screen coordinates.
     * Ensure we ignore the very left edge of the screen if bits 1 or 2 of `PPUMASK` are disabled (which hide the first 8 pixels of background/sprites).
   - If all conditions are met, set bit 6 (Sprite 0 Hit) of the `PPUSTATUS` register (0x2002).

2. Handle Flag Clearing:
   - Ensure the Sprite 0 Hit flag is cleared at **cycle 1 of scanline 261** (the pre-render scanline), right when the PPU prepares the next frame.

3. Verify Loopy Register Synchronization:
   - Review how the fine scroll offsets from `t` and `fineX` are applied during pixel generation to ensure that once the Sprite 0 Hit is triggered and the game writes new scroll values, the layout updates correctly.

Implement this scanline pixel overlap logic so the game can break out of its status bar loop and start scrolling dynamically.

# Prompt Step 7: APU initialization and the Pulse 1 channel

The emulator is now visually perfect and scrolls flawlessly! Let's move to Step 5 of our roadmap: Implementing the Audio Processing Unit (APU) foundations and the first Pulse (Square Wave) channel.

We will use SDL2 Audio Queue functions (`SDL_QueueAudio`) to stream raw audio samples smoothly from our emulation loop to the host system's speakers. The APU registers are mapped to CPU addresses 0x4000 to 0x4015.

Implement the following architecture according to our `CLAUDE.md` guidelines:

1. Create `include/Apu2A03.hpp` and `src/Apu2A03.cpp`:
   - Create an `Apu2A03` class.
   - Implement the register mapping for **Pulse 1** (CPU addresses 0x4000 to 0x4003):
     * `0x4000`: Duty cycle (12.5%, 25%, 50%, 75%), volume, and envelope flags.
     * `0x4001`: Sweep unit controls (for sliding pitches).
     * `0x4002`: Timer low byte (controls frequency/pitch).
     * `0x4003`: Timer high byte and Length Counter (controls duration).
   - Implement a Frame Counter structure that ticks at ~240Hz to clock the APU envelopes and length counters.
   - Implement a digital synthesizer function inside the `clock()` method that generates a square wave sample based on the active duty cycle and timer frequency.
   - Maintain an internal audio sample buffer.

2. Integrate SDL2 Audio in `src/main.cpp`:
   - Initialize the SDL Audio device using `SDL_OpenAudioDevice` with a standard sample rate (e.g., 44100 Hz, 1 byte or 1 float per sample, mono or stereo).
   - In the main emulator execution loop, for every CPU cycle, tick the APU.
   - Downsample or collect the APU audio output, and when the buffer accumulates enough samples, push them to the device using `SDL_QueueAudio`. Ensure the audio queue size is monitored to prevent latency or buffer starvation.

3. Connect APU to the Bus:
   - Update `include/Bus.hpp` and `src/Bus.cpp` to route memory writes from `0x4000` to `0x4013` and `0x4015` (APU Status) into the `Apu2A03` instance.

4. Update `CMakeLists.txt` and `CLAUDE.md`.

Generate clean C++20 code. Focus on clean register isolation for the Pulse 1 channel so we can easily copy this structure for the Pulse 2 channel in the next sub-step.

# Prompt Step 7.2: Pulse 2, Triangle, Noise and Mixer

The audio framework is running smoothly without slowing down the emulator, but the music sounds incomplete or wrong because we are only playing the Pulse 1 channel. To get the authentic NES sound, we need to implement the remaining core audio channels and mix them together.

Implement the remaining audio channels inside `include/Apu2A03.hpp` and `src/Apu2A03.cpp` according to our `CLAUDE.md` guidelines:

1. **Pulse 2 Channel (CPU 0x4004 - 0x4007):**
   - Duplicate the Pulse 1 square-wave logic for Pulse 2, mapping it to its own set of registers. Note that Pulse 2 uses a slightly different sweep behavior (negative sweep bit shifts differently than Pulse 1), so ensure the sweep subtraction logic accounts for this NES hardware quirk.

2. **Triangle Channel (CPU 0x4008, 0x400A, 0x400B):**
   - Implement the Triangle wave channel used for basslines.
   - Unlike the Pulse channels, the Triangle channel does not have a volume control wrapper; its volume is controlled by stepping through a static 32-step triangle waveform sequence.
   - Implement its Linear Counter and Length Counter to control its duration and silencing.

3. **Noise Channel (CPU 0x400C, 0x400E, 0x400F):**
   - Implement the Noise channel used for percussion, drums, and explosions.
   - It requires a Pseudo-Random Number Generator using a 15-bit Linear Feedback Shift Register (LFSR). On every clock tick, shift the LFSR and use bit 0 to output either a 0 or a volume sample (white noise).
   - Implement its timer table lookup to map the 4-bit frequency index to the official NES period cycle values.

4. **Audio Mixing (The Mixer):**
   - Implement a simple linear or official non-linear APU mixer to combine the output of all active channels into a single output audio sample:
     `output = pulse_output + triangle_noise_output`
     * `pulse_output = 95.88 / ((8128 / (pulse1 + pulse2)) + 100)`
     * `triangle_noise_output = 159.79 / ((1 / ((triangle / 8227) + (noise / 12241))) + 100)`
   - (An approximated weighted linear mix is also acceptable if it keeps performance optimal).

5. Update `CLAUDE.md` roadmap to reflect APU completion.

generate clean C++20 code. Once done, all standard game melodies, bass tracks, and coin/jump sound effects should blend perfectly.

# Prompt Sound too fast

The audio quality and channels are mixed correctly, but the sound plays way too fast compared to the gameplay. This is a synchronization and timing issue between the CPU/APU clock and the SDL2 audio device queue.

Analyze `src/main.cpp` and `src/Apu2A03.cpp` to implement a strict frame rate limiter and correct the audio sampling rate:

1. **Implement Video/Frame Rate Limiting in `src/main.cpp`:**
   - The NES NTSC standard runs at exactly **60.098 frames per second** (roughly 16.63 milliseconds per frame).
   - Ensure that our main execution loop is throttled. If we are using `SDL_RENDERER_PRESENTVSYNC` in our SDL Renderer, ensure the CPU loop naturally waits for the monitor's refresh rate. 
   - If V-Sync is not enough or disabled, implement a precise time-delta sleep mechanism using `<chrono>` to ensure a full frame (29780.5 CPU cycles) takes exactly ~16.6 milliseconds to execute.

2. **Fix the Audio Downsampling Ratio in `src/Apu2A03.cpp`:**
   - The NES CPU clock runs at **1,789,773 Hz**. If our SDL Audio device is opened at **44,100 Hz**, we need to collect and average samples accurately.
   - The exact cycle ratio is `1789773 / 44100 = ~40.58`. This means the APU should push exactly **1 sample to SDL2 every 40.58 CPU cycles**.
   - Review the sample accumulation buffer logic. Ensure we are not overloading `SDL_QueueAudio` and that the audio queue doesn't grow indefinitely (if the queue is too large, drop samples or dynamically adjust).

Generate the updated synchronization and downsampling code so that the audio pitch and speed match the original speed of the game perfectly.

# Prompt PAL / NRSC

The audio and gameplay are still running too fast. Your intuition is correct: we need to properly handle PAL vs NTSC regions, as the clock rates, frame targets, and audio downsampling ratios differ significantly between them.

Update the architecture to support dynamic region switching (NTSC by default, PAL if detected or toggled) according to our `CLAUDE.md` guidelines:

1. **Update `Cartridge` or `Bus` to detect Region:**
   - Look at the iNES header (Byte 9 contains TV system flags in iNES format, and flags 12 in iNES 2.0). If it indicates a PAL ROM, set a `bool isPal = true;` flag in the system. Alternatively, allow a command-line flag like `--pal` to force PAL mode.

2. **Make System Constants Dynamic:**
   - Instead of hardcoded values, update `src/main.cpp`, `src/Cpu6502.cpp`, and `src/Apu2A03.cpp` to use variables based on the active region:
     * **If NTSC:** CPU clock = 1789773 Hz, Frame rate = 60.098 Hz, Cycles per frame = 29780.5, Audio Ratio = 1789773 / 44100 (~40.58).
     * **If PAL:** CPU clock = 1662607 Hz, Frame rate = 50.007 Hz, Cycles per frame = 33247.6, Audio Ratio = 1662607 / 44100 (~37.70).

3. **Implement an Audio-Driven Sync Loop (Audio Throttling) in `src/main.cpp`:**
   - Video-based sleeping (like V-Sync) can fail if your modern monitor runs at 144Hz or 120Hz instead of 60Hz/50Hz.
   - The absolute best way to sync an emulator is **Audio Throttling**: Inside the main loop, check the size of the SDL audio queue using `SDL_GetQueuedAudioSize`. 
   - If the audio queue contains too many samples (e.g., more than ~2048 samples or about 46ms of audio), make the main loop sleep for 1 or 2 milliseconds (`std::this_thread::sleep_for`) before executing the next CPU/PPU cycles. This forces the emulator to naturally adapt to the exact playback speed of your sound card.

Refactor the timing and audio downsampling configuration to support this flexible architecture.

# Prompt Mapper MMC1 for bigger games

Some complex games are still getting stuck on a green screen. After checking the logs, these games use Mapper 1 (MMC1) instead of Mapper 0 (NROM). We need to refactor our Cartridge architecture to support dynamic Mappers and implement the MMC1 protocol.

Update the architecture according to our `CLAUDE.md` guidelines:

1. **Create a Base Mapper Class:**
   - Create `include/Mapper.hpp` and `src/Mapper.cpp` defining an abstract base class `Mapper`.
   - It must provide pure virtual methods:
     * `bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr);`
     * `bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data);`
     * `bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr);`
     * `bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr);`

2. **Implement Mapper 0 (NROM) into the new structure:**
   - Create `Mapper_000` inheriting from `Mapper` and move our current hardcoded NROM mapping logic into it.

3. **Implement Mapper 1 (MMC1) Structure (`Mapper_001`):**
   - Create a class `Mapper_001` that handles MMC1 bank-switching logic.
   - Implement the internal 5-bit shift register mechanics unique to MMC1:
     * Writes to `0x8000-0xFFFF` do not write data directly; they interact with a shift register. If bit 7 of the written data is 1, reset the shift register. Otherwise, shift the data in. On the 5th write, update the internal control registers based on the target address bits 13 and 14:
       - Control Register (`0x8000-0x9FFF`): Sets mirroring (Horizontal, Vertical, Single-Screen) and PRG/CHR bank switching modes.
       - CHR Bank 0 & 1 Registers (`0xA000-0xDFFF`): Switches 4KB or 8KB graphic banks.
       - PRG Bank Register (`0xE000-0xFFFF`): Switches 16KB code banks dynamically.

4. **Update `Cartridge`:**
   - When loading the ROM, use a `switch(mapperID)` statement to instantiate the correct Mapper subclass (`std::unique_ptr<Mapper>`).
   - Route all `cpuRead/Write` and `ppuRead/Write` address translations through the active mapper object before accessing the raw PRG/CHR vectors.

5. Update `CMakeLists.txt` and `CLAUDE.md`.

Generate clean C++20 code. Ensure the shift register reset rules are precise, as games like The Legend of Zelda heavily rely on exact MMC1 behavior to load text and maps.

# Prompt Joypad

The emulator is fully synchronized, the graphics scroll flawlessly, and the sound is perfect! The very last piece of the puzzle is to bind our actual host keyboard keys to the `controllerState[0]` byte inside `src/main.cpp`.

Update the SDL input polling logic in `src/main.cpp` using these exact specifications:

1. **Define the Key Mapping:**
   Let's use a standard, intuitive keyboard setup for Player 1:
   - **Z** key maps to **Button A** (Bit 0)
   - **X** key maps to **Button B** (Bit 1)
   - **Right Shift** or **A** key maps to **Select** (Bit 2)
   - **Enter** key maps to **Start** (Bit 3)
   - **Up Arrow** maps to **Up** (Bit 4)
   - **Down Arrow** maps to **Down** (Bit 5)
   - **Left Arrow** maps to **Left** (Bit 6)
   - **Right Arrow** maps to **Right** (Bit 7)

2. **Handle SDL Events (`SDL_KEYDOWN` and `SDL_KEYUP`):**
   - When a key is **pressed** (`SDL_KEYDOWN`), perform a bitwise OR (`|=`) to set that specific button's bit to 1 inside `controllerState[0]`.
   - When a key is **released** (`SDL_KEYUP`), perform a bitwise AND with a bitwise NOT (`&= ~`) to clear that button's bit back to 0 inside `controllerState[0]`.

3. **Prevent Impossible D-Pad Combos (Anti-glitch hardware rule):**
   - If both Up and Down are pressed simultaneously, clear both or prioritize one.
   - If both Left and Right are pressed simultaneously, clear both. (Simultaneous left+right presses can cause memory corruption glitches in games like Super Mario Bros).

Generate the input mapping code in `src/main.cpp` so we can finally play the games!

# Prompt game controller issur : 

The emulator console prints "Controller connected: Nintendo Switch Pro Controller (player 1)", which means macOS permissions are fine and SDL2 detects the hardware. However, the game does not react to any button presses. This is because our `src/main.cpp` is likely only listening to keyboard events and hasn't initialized or bound the `SDL_GameController` events.

Update `src/main.cpp` to add native Game Controller support alongside our keyboard mapping according to our `CLAUDE.md` guidelines:

1. **Initialize and Open the Controller:**
   - In the initialization section (or when handling `SDL_CONTROLLERDEVICEADDED`), explicitly open the controller using `SDL_GameControllerOpen(device_index)` and store the `SDL_GameController*` pointer globally or inside your window/input context.
   - Properly close it with `SDL_GameControllerClose` on `SDL_CONTROLLERDEVICEREMOVED` or when exiting the application to prevent memory leaks.

2. **Handle Controller Button Events:**
   - In the `SDL_PollEvent` loop, add cases for `SDL_CONTROLLERBUTTONDOWN` and `SDL_CONTROLLERBUTTONUP`.
   - Map the Pro Controller buttons directly to our 8-bit `controllerState[0]` registry using bitwise operations (`|=` to press, `&= ~` to release):
     * `SDL_CONTROLLER_BUTTON_B` (Physical bottom button) -> NES **Button A** (Bit 0)
     * `SDL_CONTROLLER_BUTTON_A` (Physical right button) or `SDL_CONTROLLER_BUTTON_Y` (Physical left button) -> NES **Button B** (Bit 1)
     * `SDL_CONTROLLER_BUTTON_BACK` (Minus button) -> NES **Select** (Bit 2)
     * `SDL_CONTROLLER_BUTTON_START` (Plus button) -> NES **Start** (Bit 3)
     * `SDL_CONTROLLER_BUTTON_DPAD_UP` -> NES **Up** (Bit 4)
     * `SDL_CONTROLLER_BUTTON_DPAD_DOWN` -> NES **Down** (Bit 5)
     * `SDL_CONTROLLER_BUTTON_DPAD_LEFT` -> NES **Left** (Bit 6)
     * `SDL_CONTROLLER_BUTTON_DPAD_RIGHT` -> NES **Right** (Bit 7)

3. **Keep Coexistence:** Ensure that BOTH the keyboard and the gamepad can modify `controllerState[0]` simultaneously so the user can switch between them seamlessly.

Refactor the input loop in `src/main.cpp` to implement this controller binding architecture.
