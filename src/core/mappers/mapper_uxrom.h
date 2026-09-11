#pragma once

#include "core/mapper.h"

namespace nes {

// Mapper 002 (UxROM): PRG ROM is switched 16KB at a time at $8000-$BFFF via
// any write to $8000-$FFFF; $C000-$FFFF is permanently fixed to the last
// 16KB bank. CHR is always RAM (8KB, no banking).
class MapperUxrom final : public Mapper {
public:
    MapperUxrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring);

    bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) override;
    Mirroring mirroring() const override { return mirroring_; }

    void saveState(StateWriter& w) const override;
    void loadState(StateReader& r) override;

private:
    uint8_t prgBanks16k_;
    uint8_t chrBanks8k_;
    Mirroring mirroring_;
    uint8_t prgBank_ = 0;
};

} // namespace nes
