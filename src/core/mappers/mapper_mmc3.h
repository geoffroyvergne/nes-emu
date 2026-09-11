#pragma once

#include <array>

#include "core/mapper.h"

namespace nes {

// Mapper 004 (MMC3/TxROM). PRG ROM: four 8KB windows at $8000/$A000/$C000/
// $E000; $E000 is always fixed to the last bank, $A000 is always switchable
// via R7, and $8000/$C000 swap between "switchable via R6" and "fixed to
// the second-last bank" depending on the bank-select register's mode bit.
// CHR ROM: two 2KB + four 1KB windows (or the mirror image of that layout,
// selected by the bank-select register's invert bit).
//
// Also implements MMC3's scanline IRQ counter: real hardware clocks it on
// PPU A12 rising edges, which we approximate (like most simplified NES
// emulators) by having the PPU call scanlineTick() once per visible/
// pre-render scanline while rendering is enabled - accurate enough for the
// split-scroll effects (e.g. status bars) that rely on it.
//
// Known limitation: PRG-RAM enable/write-protect ($A001) is parsed but not
// enforced - Cartridge always allows $6000-$7FFF reads/writes regardless of
// mapper, which is harmless for normal gameplay.
class MapperMmc3 final : public Mapper {
public:
    MapperMmc3(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring headerMirroring);

    bool cpuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool cpuMapWrite(uint16_t addr, uint32_t& mappedAddr, uint8_t data) override;
    bool ppuMapRead(uint16_t addr, uint32_t& mappedAddr) override;
    bool ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) override;
    Mirroring mirroring() const override;

    void scanlineTick() override;
    bool irqPending() const override { return irqPending_; }

    void saveState(StateWriter& w) const override;
    void loadState(StateReader& r) override;

private:
    uint32_t chrAddress(uint16_t addr) const;

    uint8_t prgBanks8k_; // Total 8KB PRG banks (prgBanks16k * 2).
    uint8_t chrBanks8k_;
    bool fourScreen_;

    uint8_t bankSelect_ = 0;
    std::array<uint8_t, 8> bankRegisters_{};

    uint8_t mirroringBit_ = 0;
    uint8_t prgRamProtect_ = 0;

    uint8_t irqLatch_ = 0;
    uint8_t irqCounter_ = 0;
    bool irqReloadPending_ = false;
    bool irqEnabled_ = false;
    bool irqPending_ = false;
};

} // namespace nes
