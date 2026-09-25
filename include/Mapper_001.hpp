#pragma once

#include "Mapper.hpp"

#include <cstdint>

// Mapper 1 (Nintendo MMC1: SxROM boards - Zelda, Metroid, Mega Man 2, Final Fantasy, ...).
//
// The CPU talks to MMC1 serially: every write to $8000-$FFFF feeds one bit into a 5-bit shift
// register (bit 0 of the data, LSB first). The 5th write copies the value into the internal register
// selected by address bits 13-14 of that 5th write, then the shift register clears:
//   $8000-$9FFF  Control   CPPMM  C = CHR mode (0: 8KB, 1: two 4KB), PP = PRG mode, MM = mirroring
//   $A000-$BFFF  CHR bank 0 (4KB at PPU $0000, or 8KB at $0000 when C = 0; low bit ignored then)
//   $C000-$DFFF  CHR bank 1 (4KB at PPU $1000; ignored in 8KB mode)
//   $E000-$FFFF  PRG bank  RPPPP  PPPP = 16KB bank, R = PRG-RAM disable
// A write with bit 7 set resets the shift register at once and forces PRG mode 3.
//
// PRG modes: 0/1 = switch 32KB at $8000 (bank number's low bit ignored); 2 = first bank fixed at
// $8000, switch $C000; 3 = switch $8000, last bank fixed at $C000 (power-on state).
// 512KB boards (SUROM) use bit 4 of the CHR bank registers to select the 256KB PRG half.
class Mapper_001 : public Mapper {
public:
    Mapper_001(int prgBanks16k, int chrBanks8k, Mirroring headerMirroring);

    bool cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool cpuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr, std::uint8_t data) override;
    bool ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) override;

    [[nodiscard]] Mirroring getMirroring() const override;
    [[nodiscard]] bool isPrgRamEnabled() const override { return (prgBank & 0x10) == 0; }

private:
    static constexpr std::uint8_t SHIFT_EMPTY = 0x10; // Marker bit: reaches bit 0 after 4 shifts
    static constexpr std::uint8_t CONTROL_PRG_MODE_3 = 0x0C;

    void writeRegister(std::uint16_t addr, std::uint8_t value);
    void updateBanks();

    std::uint8_t shiftRegister = SHIFT_EMPTY;
    std::uint8_t control = CONTROL_PRG_MODE_3;
    std::uint8_t chrBank0 = 0;
    std::uint8_t chrBank1 = 0;
    std::uint8_t prgBank = 0;

    bool hasLastWrite = false;
    std::uint64_t lastWriteStamp = 0;

    // Resolved offsets, recomputed when a register changes.
    std::uint32_t prgOffset8000 = 0; // Offset of the 16KB window at $8000
    std::uint32_t prgOffsetC000 = 0; // Offset of the 16KB window at $C000
    std::uint32_t chrOffset0000 = 0; // Offset of the 4KB window at PPU $0000
    std::uint32_t chrOffset1000 = 0; // Offset of the 4KB window at PPU $1000
};
