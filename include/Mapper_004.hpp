#pragma once

#include "Mapper.hpp"

#include <array>
#include <cstdint>

// Mapper 4 (Nintendo MMC3: TxROM boards - Super Mario Bros. 2/3, Kirby's Adventure, Mega Man 3-6, ...).
//
// Registers (even/odd address within each 8KB range):
//   $8000 even  Bank select   CP...RRR  R = which bank register the next $8001 write sets,
//                                       P = PRG layout, C = CHR A12 inversion
//   $8001 odd   Bank data     value for R0-R7
//   $A000 even  Mirroring     bit 0: 0 = vertical, 1 = horizontal (ignored on four-screen boards)
//   $A001 odd   PRG-RAM protect (not enforced: MMC6 and many dumps disagree with it)
//   $C000 even  IRQ latch     reload value of the scanline counter
//   $C001 odd   IRQ reload    counter reloads from the latch on the next scanline clock
//   $E000 even  IRQ disable   also acknowledges a pending IRQ
//   $E001 odd   IRQ enable
//
// PRG (8KB banks): P = 0: $8000 = R6, $A000 = R7, $C000 = second-last, $E000 = last
//                  P = 1: $8000 = second-last, $A000 = R7, $C000 = R6, $E000 = last
// CHR (1KB units): C = 0: $0000 = R0 (2KB), $0800 = R1 (2KB), $1000-$1C00 = R2-R5 (1KB each)
//                  C = 1: the same with the $0000 and $1000 halves swapped
//
// The IRQ counter is clocked once per scanline (the PPU calls clockScanline()): when it is 0 or a
// reload was requested it is reloaded from the latch, otherwise decremented; reaching 0 with IRQs
// enabled raises the IRQ line until $E000 is written.
class Mapper_004 : public Mapper {
public:
    Mapper_004(int prgBanks16k, int chrBanks8k, Mirroring headerMirroring);

    bool cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool cpuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr, std::uint8_t data) override;
    bool ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) override;

    [[nodiscard]] Mirroring getMirroring() const override;
    void clockScanline() override;
    [[nodiscard]] bool isIrqPending() const override { return irqPending; }

private:
    void updateBanks();

    std::uint8_t bankSelect = 0;
    std::array<std::uint8_t, 8> bankRegisters{}; // R0-R7
    bool horizontalMirroring = false;

    std::uint8_t irqLatch = 0;
    std::uint8_t irqCounter = 0;
    bool irqReload = false;
    bool irqEnabled = false;
    bool irqPending = false;

    std::array<std::uint32_t, 4> prgOffsets{}; // 8KB windows at $8000, $A000, $C000, $E000
    std::array<std::uint32_t, 8> chrOffsets{}; // 1KB windows at PPU $0000, $0400, ..., $1C00
};
