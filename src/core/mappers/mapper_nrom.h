#pragma once

#include "core/mapper.h"

namespace nes {

// Mapper 000 (NROM): the simplest cartridge type, no bank switching.
// 16KB or 32KB fixed PRG ROM (16KB is mirrored across $8000-$FFFF),
// 8KB fixed CHR ROM (or CHR RAM if prgChrRamFallback requests it).
class MapperNrom final : public Mapper {
public:
    MapperNrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring);

    bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) override;
    Mirroring mirroring() const override { return mirroring_; }

private:
    uint8_t prgBanks16k_;
    uint8_t chrBanks8k_;
    Mirroring mirroring_;
};

} // namespace nes
