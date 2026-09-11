#pragma once

#include <array>
#include <cstdint>

#include "core/cartridge.h"
#include "core/controller.h"

namespace nes {

class Ppu2C02;
class Cpu6502;

// The NES system bus and clock driver. Owns the 2KB of internal work RAM and
// the two controller ports, forwards cartridge-space reads/writes
// ($4020-$FFFF) to whatever Cartridge is inserted, forwards $2000-$3FFF to
// the PPU's registers, and services OAM DMA ($4014).
//
// Bus does not own the CPU, PPU, or Cartridge - main() constructs them and
// wires them together via connectCpu()/connectPpu()/insertCartridge() so
// that Cpu6502 (which holds a Bus&) and Bus (which needs to reach the CPU to
// deliver NMIs and the PPU to deliver register access) don't need a
// circular ownership relationship.
//
// clock() is the system clock: it steps the PPU every call and the CPU every
// third call (the PPU runs at 3x the CPU rate), matching real NES timing,
// and is how a full frame gets driven in the main loop.
class Bus {
public:
    Bus() { ram_.fill(0); }

    void connectPpu(Ppu2C02* ppu) { ppu_ = ppu; }
    void connectCpu(Cpu6502* cpu) { cpu_ = cpu; }

    // Bus does not own the cartridge; the caller controls its lifetime and
    // must keep it alive for as long as it stays inserted.
    void insertCartridge(Cartridge* cartridge) { cartridge_ = cartridge; }

    Controller& controller(int port) { return controllers_[port]; }

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);

    // Resets CPU + PPU and the system clock/DMA state. Requires connectCpu()
    // and connectPpu() to have been called first.
    void reset();

    // Advances the whole system by one PPU cycle (and the CPU by one cycle
    // every third call, honoring in-flight OAM DMA). Requires connectCpu()
    // and connectPpu().
    void clock();

private:
    std::array<uint8_t, 2048> ram_{};
    Cartridge* cartridge_ = nullptr;
    Ppu2C02* ppu_ = nullptr;
    Cpu6502* cpu_ = nullptr;
    std::array<Controller, 2> controllers_{};

    uint64_t systemClockCounter_ = 0;

    bool dmaTransfer_ = false;
    bool dmaDummyCycle_ = true;
    uint8_t dmaPage_ = 0;
    uint8_t dmaAddr_ = 0;
    uint8_t dmaData_ = 0;
};

} // namespace nes
