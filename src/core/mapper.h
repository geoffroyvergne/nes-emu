#pragma once

#include <cstdint>

namespace nes {

class StateWriter;
class StateReader;

// Mirroring mode the cartridge/mapper reports for nametable mirroring.
// Used by the PPU (added in a later milestone) to decide how the two
// physical 1KB nametables map to the four logical nametable slots.
enum class Mirroring {
    Horizontal,
    Vertical,
    SingleScreenLow,
    SingleScreenHigh,
    FourScreen,
};

// A Mapper translates CPU/PPU addresses into offsets within a cartridge's
// PRG/CHR banks, implementing whatever bank-switching scheme that cartridge's
// mapper chip uses. Every mapper (NROM, MMC1, ...) implements this interface;
// the Bus and PPU only ever talk to a Cartridge, never to a concrete mapper.
class Mapper {
public:
    virtual ~Mapper() = default;

    // CPU reads in $6000-$7FFF (PRG RAM) / $8000-$FFFF (PRG ROM).
    // Returns true and sets `mappedAddr` to an index into the cartridge's
    // PRG RAM/ROM array if this mapper handles `addr`.
    virtual bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr) = 0;

    // CPU writes in the same range. Returns true and sets `mappedAddr` if the
    // write should land in PRG RAM. Returns false if the mapper fully
    // consumed the write itself (e.g. a bank-switch register) - the
    // cartridge then does not touch its PRG RAM/ROM arrays.
    virtual bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data) = 0;

    // PPU address space ($0000-$1FFF CHR ROM/RAM).
    virtual bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr) = 0;
    virtual bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) = 0;

    virtual Mirroring mirroring() const = 0;

    virtual void reset() {}

    // Called once per visible/pre-render scanline (while rendering is
    // enabled) so mappers with a scanline-based IRQ counter (MMC3) can clock
    // it. A no-op for mappers without one.
    virtual void scanlineTick() {}

    // True if this mapper currently wants to assert the CPU's IRQ line
    // (e.g. MMC3's scanline counter hit zero). The mapper is responsible for
    // clearing this itself once the game acknowledges the interrupt in
    // whatever mapper-specific way real hardware defines.
    virtual bool irqPending() const { return false; }

    // Save-state support: writes/reads this mapper's bank-switching and
    // (where applicable) IRQ-counter state, in the same fixed order every
    // time. Does not include PRG/CHR ROM contents (immutable) or PRG RAM
    // (owned and saved by Cartridge itself).
    virtual void saveState(StateWriter& w) const = 0;
    virtual void loadState(StateReader& r) = 0;
};

} // namespace nes
