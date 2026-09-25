#include "Mapper_004.hpp"

namespace {

constexpr std::uint32_t PRG_BANK_SIZE = 8 * 1024;
constexpr std::uint32_t CHR_BANK_SIZE = 1024;

} // namespace

Mapper_004::Mapper_004(int prgBanks16k, int chrBanks8k, Mirroring headerMirroring)
    : Mapper(prgBanks16k, chrBanks8k, headerMirroring) {
    horizontalMirroring = headerMirroring == Mirroring::Horizontal;
    updateBanks();
}

bool Mapper_004::cpuMapWrite(std::uint16_t addr, std::uint32_t& /*mappedAddr*/, std::uint8_t data) {
    if (addr < 0x8000) {
        return false;
    }
    const bool odd = (addr & 0x0001) != 0;
    switch ((addr >> 13) & 0x03) {
    case 0: // $8000-$9FFF
        if (odd) {
            bankRegisters[bankSelect & 0x07] = data;
        } else {
            bankSelect = data;
        }
        updateBanks();
        break;
    case 1: // $A000-$BFFF
        if (!odd) {
            horizontalMirroring = (data & 0x01) != 0;
        }
        break; // Odd: PRG-RAM protect, deliberately not enforced
    case 2: // $C000-$DFFF
        if (odd) {
            irqCounter = 0; // Reloaded from the latch on the next scanline clock
            irqReload = true;
        } else {
            irqLatch = data;
        }
        break;
    default: // $E000-$FFFF
        irqEnabled = odd;
        if (!odd) {
            irqPending = false; // Disabling also acknowledges
        }
        break;
    }
    return false; // Register write, nothing lands in PRG-ROM
}

Mirroring Mapper_004::getMirroring() const {
    if (mirroring == Mirroring::FourScreen) {
        return Mirroring::FourScreen; // Board wiring overrides the register
    }
    return horizontalMirroring ? Mirroring::Horizontal : Mirroring::Vertical;
}

void Mapper_004::clockScanline() {
    if (irqCounter == 0 || irqReload) {
        irqCounter = irqLatch;
        irqReload = false;
    } else {
        --irqCounter;
    }
    if (irqCounter == 0 && irqEnabled) {
        irqPending = true;
    }
}

void Mapper_004::updateBanks() {
    const auto prgBankCount = static_cast<std::uint32_t>(prgBanks) * 2; // In 8KB units
    const auto bank8k = [&](std::uint32_t bank) { return (bank % prgBankCount) * PRG_BANK_SIZE; };
    const std::uint32_t secondLast = prgBankCount - 2;
    const std::uint32_t last = prgBankCount - 1;
    if ((bankSelect & 0x40) == 0) {
        prgOffsets = {bank8k(bankRegisters[6] & 0x3F), bank8k(bankRegisters[7] & 0x3F), bank8k(secondLast), bank8k(last)};
    } else {
        prgOffsets = {bank8k(secondLast), bank8k(bankRegisters[7] & 0x3F), bank8k(bankRegisters[6] & 0x3F), bank8k(last)};
    }

    // CHR in 1KB units; CHR-RAM boards have 8KB = 8 units.
    const std::uint32_t chrBankCount = usesChrRam() ? 8 : static_cast<std::uint32_t>(chrBanks) * 8;
    const auto bank1k = [&](std::uint32_t bank) { return (bank % chrBankCount) * CHR_BANK_SIZE; };
    const std::array<std::uint32_t, 8> layout = {
        bank1k(bankRegisters[0] & 0xFEu), bank1k(bankRegisters[0] | 0x01u), // R0: 2KB at $0000
        bank1k(bankRegisters[1] & 0xFEu), bank1k(bankRegisters[1] | 0x01u), // R1: 2KB at $0800
        bank1k(bankRegisters[2]),         bank1k(bankRegisters[3]),         // R2-R5: 1KB at $1000-$1C00
        bank1k(bankRegisters[4]),         bank1k(bankRegisters[5]),
    };
    const bool inverted = (bankSelect & 0x80) != 0; // Swap the $0000 and $1000 halves
    for (std::size_t i = 0; i < chrOffsets.size(); ++i) {
        chrOffsets[i] = layout[inverted ? i ^ 4 : i];
    }
}

bool Mapper_004::cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr < 0x8000) {
        return false;
    }
    mappedAddr = prgOffsets[(addr >> 13) & 0x03] + (addr & 0x1FFF);
    return true;
}

bool Mapper_004::ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr > 0x1FFF) {
        return false;
    }
    mappedAddr = chrOffsets[addr >> 10] + (addr & 0x03FF);
    return true;
}

bool Mapper_004::ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (!usesChrRam()) {
        return false; // CHR-ROM is read-only
    }
    return ppuMapRead(addr, mappedAddr);
}
