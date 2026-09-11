#pragma once

#include <cstdint>

namespace nes {

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
};

} // namespace nes
