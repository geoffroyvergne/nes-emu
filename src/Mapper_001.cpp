#include "Mapper_001.hpp"

namespace {

constexpr std::uint32_t PRG_BANK_SIZE = 16 * 1024;
constexpr std::uint32_t CHR_BANK_SIZE = 4 * 1024;
constexpr int SUROM_PRG_BANKS = 32; // 512KB: needs the outer 256KB select

} // namespace

Mapper_001::Mapper_001(int prgBanks16k, int chrBanks8k, Mirroring headerMirroring)
    : Mapper(prgBanks16k, chrBanks8k, headerMirroring) {
    updateBanks();
}

// ---------------------------------------------------------------------------------------------
// Register interface

bool Mapper_001::cpuMapWrite(std::uint16_t addr, std::uint32_t& /*mappedAddr*/, std::uint8_t data) {
    if (addr < 0x8000) {
        return false;
    }

    // MMC1 ignores a write on the cycle right after another one. That only happens with
    // read-modify-write instructions (INC/DEC/ASL/... $8000+), which write the old value and then
    // the new one on consecutive cycles: only the first write counts. Games rely on this, e.g.
    // "INC $FFFF" on a $FF byte to reset the mapper (the second write, $00, must not shift in a bit).
    if (hasLastWrite && cpuWriteStamp == lastWriteStamp) {
        return false;
    }
    hasLastWrite = true;
    lastWriteStamp = cpuWriteStamp;

    if ((data & 0x80) != 0) {
        // Reset: discard any partial value and lock the last PRG bank at $C000 (mode 3), so the
        // reset/interrupt vectors are always reachable.
        shiftRegister = SHIFT_EMPTY;
        control |= CONTROL_PRG_MODE_3;
        updateBanks();
        return false;
    }

    // The marker bit reaching bit 0 means this is the 5th write.
    const bool complete = (shiftRegister & 0x01) != 0;
    shiftRegister = static_cast<std::uint8_t>((shiftRegister >> 1) | ((data & 0x01) << 4));
    if (complete) {
        writeRegister(addr, shiftRegister);
        shiftRegister = SHIFT_EMPTY;
    }
    return false; // Register write: nothing lands in PRG-ROM
}

void Mapper_001::writeRegister(std::uint16_t addr, std::uint8_t value) {
    switch ((addr >> 13) & 0x03) { // Only the address of the 5th write matters
    case 0: control = value; break;
    case 1: chrBank0 = value; break;
    case 2: chrBank1 = value; break;
    case 3: prgBank = value; break;
    default: break;
    }
    updateBanks();
}

Mirroring Mapper_001::getMirroring() const {
    switch (control & 0x03) {
    case 0: return Mirroring::SingleScreenLower;
    case 1: return Mirroring::SingleScreenUpper;
    case 2: return Mirroring::Vertical;
    default: return Mirroring::Horizontal;
    }
}

// ---------------------------------------------------------------------------------------------
// Banking

void Mapper_001::updateBanks() {
    // CHR: two 4KB windows. In 8KB mode, bank 0 selects an aligned pair (low bit ignored).
    // CHR-RAM boards have 8KB = two 4KB banks; CHR-ROM up to 128KB = 32 banks.
    const std::uint32_t chrBankCount = usesChrRam() ? 2 : static_cast<std::uint32_t>(chrBanks) * 2;
    if ((control & 0x10) != 0) {
        chrOffset0000 = (chrBank0 % chrBankCount) * CHR_BANK_SIZE;
        chrOffset1000 = (chrBank1 % chrBankCount) * CHR_BANK_SIZE;
    } else {
        const std::uint32_t pair = (chrBank0 & 0x1E) % chrBankCount;
        chrOffset0000 = pair * CHR_BANK_SIZE;
        chrOffset1000 = ((pair + 1) % chrBankCount) * CHR_BANK_SIZE;
    }

    // PRG: 16KB banks within a 256KB half. SUROM (512KB) picks the half with CHR bank 0 bit 4.
    const std::uint32_t totalBanks = static_cast<std::uint32_t>(prgBanks);
    const std::uint32_t outer = (totalBanks >= SUROM_PRG_BANKS && (chrBank0 & 0x10) != 0) ? 16 : 0;
    const std::uint32_t banksInHalf = totalBanks >= SUROM_PRG_BANKS ? 16 : totalBanks;
    const std::uint32_t bank = prgBank & 0x0F;
    std::uint32_t low = 0;
    std::uint32_t high = 0;
    switch ((control >> 2) & 0x03) {
    case 0:
    case 1: // 32KB mode: an aligned pair of 16KB banks
        low = bank & 0x0E;
        high = low + 1;
        break;
    case 2: // First bank fixed at $8000, switchable at $C000
        low = 0;
        high = bank;
        break;
    default: // Switchable at $8000, last bank fixed at $C000
        low = bank;
        high = banksInHalf - 1;
        break;
    }
    prgOffset8000 = (outer + low % banksInHalf) % totalBanks * PRG_BANK_SIZE;
    prgOffsetC000 = (outer + high % banksInHalf) % totalBanks * PRG_BANK_SIZE;
}

bool Mapper_001::cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr < 0x8000) {
        return false;
    }
    mappedAddr = (addr < 0xC000 ? prgOffset8000 : prgOffsetC000) + (addr & 0x3FFF);
    return true;
}

bool Mapper_001::ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr > 0x1FFF) {
        return false;
    }
    mappedAddr = (addr < 0x1000 ? chrOffset0000 : chrOffset1000) + (addr & 0x0FFF);
    return true;
}

bool Mapper_001::ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (!usesChrRam()) {
        return false; // CHR-ROM is read-only
    }
    return ppuMapRead(addr, mappedAddr);
}
