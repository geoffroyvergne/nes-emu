#pragma once

#include <array>
#include <cstdint>

#include "core/cartridge.h"
#include "core/controller.h"

namespace nes {

class Ppu2C02;
class Cpu6502;
class Apu2A03;
class StateWriter;
class StateReader;

// The NES system bus and clock driver. Owns the 2KB of internal work RAM and
// the two controller ports, forwards cartridge-space reads/writes
// ($4020-$FFFF) to whatever Cartridge is inserted, forwards $2000-$3FFF to
// the PPU's registers, $4000-$4013/$4015/$4017 to the APU's registers, and
// services OAM DMA ($4014).
//
// Bus does not own the CPU, PPU, APU, or Cartridge - main() constructs them
// and wires them together via connectCpu()/connectPpu()/connectApu()/
// insertCartridge() so that Cpu6502 (which holds a Bus&) and Bus (which
// needs to reach the CPU to deliver NMIs/IRQs and the PPU/APU to deliver
// register access) don't need a circular ownership relationship.
//
// clock() is the system clock: it steps the PPU every call and the CPU (and
// APU) at the region-appropriate rate (NTSC: a clean 3 PPU cycles per CPU
// cycle; PAL: 16 PPU cycles per 5 CPU cycles, a non-integer 3.2:1 ratio
// handled via a fractional accumulator - see setRegion()), matching real NES
// timing, and is how a full frame gets driven in the main loop.
class Bus {
public:
    Bus() { ram_.fill(0); }

    void connectPpu(Ppu2C02* ppu) { ppu_ = ppu; }
    void connectCpu(Cpu6502* cpu) { cpu_ = cpu; }
    void connectApu(Apu2A03* apu) { apu_ = apu; }

    // Bus does not own the cartridge; the caller controls its lifetime and
    // must keep it alive for as long as it stays inserted.
    void insertCartridge(Cartridge* cartridge) { cartridge_ = cartridge; }

    Controller& controller(int port) { return controllers_[port]; }

    uint8_t read(uint16_t addr);
    void write(uint16_t addr, uint8_t value);

    // Configures the PPU:CPU clock ratio for NTSC (3:1, the default) or PAL
    // (16:5). Call before reset()/clock(); does not itself touch the PPU or
    // APU's own region settings (see Ppu2C02::setRegion()/Apu2A03::setRegion()).
    void setRegion(bool isPal);

    // Resets CPU + PPU and the system clock/DMA state. Requires connectCpu()
    // and connectPpu() to have been called first.
    void reset();

    // Advances the whole system by one PPU cycle (and the CPU/APU at the
    // rate set by setRegion(), honoring in-flight OAM DMA). Requires
    // connectCpu() and connectPpu().
    void clock();

    // Save-state support: internal RAM, the PPU:CPU clock accumulator, and
    // in-flight OAM DMA state. Does not include controller state (driven
    // live by input each frame) or the region config from setRegion().
    void saveState(StateWriter& w) const;
    void loadState(StateReader& r);

private:
    std::array<uint8_t, 2048> ram_{};
    Cartridge* cartridge_ = nullptr;
    Ppu2C02* ppu_ = nullptr;
    Cpu6502* cpu_ = nullptr;
    Apu2A03* apu_ = nullptr;
    std::array<Controller, 2> controllers_{};

    // CPU clocks once every time a `ppuTicksPerCpuTick_`-accumulation
    // reaches `ppuTicksDenominator_` (NTSC: 1/3 per PPU tick; PAL: 5/16).
    uint32_t ppuTicksPerCpuTick_ = 1;
    uint32_t ppuTicksDenominator_ = 3;
    uint32_t cpuTickAccumulator_ = 0;
    uint64_t cpuCycleCount_ = 0; // Total CPU clocks so far; drives DMA cycle parity.

    bool dmaTransfer_ = false;
    bool dmaDummyCycle_ = true;
    uint8_t dmaPage_ = 0;
    uint8_t dmaAddr_ = 0;
    uint8_t dmaData_ = 0;
};

} // namespace nes
