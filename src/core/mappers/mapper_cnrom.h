#pragma once

#include "core/mapper.h"

namespace nes {

// Mapper 003 (CNROM): PRG ROM is fixed (16KB mirrored or 32KB, like NROM).
// CHR ROM is switched 8KB at a time via any write to $8000-$FFFF.
class MapperCnrom final : public Mapper {
public:
    MapperCnrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring);

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
    uint8_t chrBank_ = 0;
};

} // namespace nes
