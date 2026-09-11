#pragma once

#include "core/mapper.h"

namespace nes {

// Mapper 001 (MMC1/SxROM). CPU writes to $8000-$FFFF feed a 5-bit serial
// shift register (one data bit per write, LSB of the write first); on the
// 5th write the accumulated 5-bit value loads one of four internal
// registers selected by which $8xxx/$Axxx/$Cxxx/$Exxx region was written:
//   $8000-$9FFF: control (mirroring, PRG bank mode, CHR bank mode)
//   $A000-$BFFF: CHR bank 0
//   $C000-$DFFF: CHR bank 1
//   $E000-$FFFF: PRG bank
// Writing with bit 7 set resets the shift register immediately (not part of
// the 5-write sequence) and forces PRG bank mode to "fix last bank at
// $C000", matching real hardware's power-on/reset behavior.
class MapperMmc1 final : public Mapper {
public:
    MapperMmc1(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring headerMirroring);

    bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) override;
    Mirroring mirroring() const override;

    void saveState(StateWriter& w) const override;
    void loadState(StateReader& r) override;

private:
    uint32_t chrAddress(uint16_t addr) const;

    uint8_t prgBanks16k_;
    uint8_t chrBanks8k_;

    uint8_t shiftRegister_ = 0;
    uint8_t shiftCount_ = 0;

    uint8_t control_ = 0x0C; // PRG mode 3 (fixed last bank at $C000), CHR 8KB mode, mirroring 0.
    uint8_t chrBank0_ = 0;
    uint8_t chrBank1_ = 0;
    uint8_t prgBank_ = 0;
};

} // namespace nes
