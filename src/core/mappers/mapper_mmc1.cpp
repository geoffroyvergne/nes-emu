#include "core/mappers/mapper_mmc1.h"

#include "core/state_io.h"

namespace nes {

MapperMmc1::MapperMmc1(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring /*headerMirroring*/)
    : prgBanks16k_(prgBanks16k), chrBanks8k_(chrBanks8k) {}

bool MapperMmc1::cpuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr < 0x8000) return false;

    uint8_t prgMode = (control_ >> 2) & 0x03;
    uint8_t bank = prgBank_ & 0x0F;

    if (prgMode <= 1) { // 32KB mode: switch the whole $8000-$FFFF window, ignoring the low bank bit.
        uint8_t bank32 = bank >> 1;
        mappedAddr = static_cast<uint32_t>(bank32) * 0x8000 + (addr & 0x7FFF);
    } else if (prgMode == 2) { // Fixed first bank at $8000, switchable at $C000.
        mappedAddr = addr < 0xC000 ? (addr & 0x3FFF) : static_cast<uint32_t>(bank) * 0x4000 + (addr & 0x3FFF);
    } else { // prgMode == 3: switchable at $8000, fixed last bank at $C000.
        mappedAddr = addr < 0xC000 ? static_cast<uint32_t>(bank) * 0x4000 + (addr & 0x3FFF)
                                    : static_cast<uint32_t>(prgBanks16k_ - 1) * 0x4000 + (addr & 0x3FFF);
    }
    return true;
}

bool MapperMmc1::cpuMapWrite(uint16_t addr, uint32_t& /*mappedAddr*/, uint8_t data) {
    if (addr < 0x8000) return false;

    if (data & 0x80) {
        shiftRegister_ = 0;
        shiftCount_ = 0;
        control_ |= 0x0C; // Reset forces PRG mode 3.
        return false;
    }

    shiftRegister_ = static_cast<uint8_t>(shiftRegister_ | ((data & 0x01) << shiftCount_));
    shiftCount_++;

    if (shiftCount_ == 5) {
        uint8_t value = shiftRegister_ & 0x1F;
        switch ((addr >> 13) & 0x03) {
            case 0: control_ = value; break;    // $8000-$9FFF
            case 1: chrBank0_ = value; break;   // $A000-$BFFF
            case 2: chrBank1_ = value; break;   // $C000-$DFFF
            case 3: prgBank_ = value; break;    // $E000-$FFFF
            default: break;
        }
        shiftRegister_ = 0;
        shiftCount_ = 0;
    }
    return false;
}

uint32_t MapperMmc1::chrAddress(uint16_t addr) const {
    bool chr4kMode = control_ & 0x10;
    if (!chr4kMode) {
        uint8_t bank8 = chrBank0_ >> 1;
        return static_cast<uint32_t>(bank8) * 0x2000 + addr;
    }
    if (addr < 0x1000) {
        return static_cast<uint32_t>(chrBank0_) * 0x1000 + addr;
    }
    return static_cast<uint32_t>(chrBank1_) * 0x1000 + (addr - 0x1000);
}

bool MapperMmc1::ppuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr > 0x1FFF) return false;
    mappedAddr = chrBanks8k_ == 0 ? addr : chrAddress(addr);
    return true;
}

bool MapperMmc1::ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) {
    if (addr > 0x1FFF || chrBanks8k_ != 0) return false; // CHR ROM is read-only.
    mappedAddr = addr;
    return true;
}

Mirroring MapperMmc1::mirroring() const {
    switch (control_ & 0x03) {
        case 0: return Mirroring::SingleScreenLow;
        case 1: return Mirroring::SingleScreenHigh;
        case 2: return Mirroring::Vertical;
        default: return Mirroring::Horizontal;
    }
}

void MapperMmc1::saveState(StateWriter& w) const {
    w.write(shiftRegister_);
    w.write(shiftCount_);
    w.write(control_);
    w.write(chrBank0_);
    w.write(chrBank1_);
    w.write(prgBank_);
}

void MapperMmc1::loadState(StateReader& r) {
    shiftRegister_ = r.read<uint8_t>();
    shiftCount_ = r.read<uint8_t>();
    control_ = r.read<uint8_t>();
    chrBank0_ = r.read<uint8_t>();
    chrBank1_ = r.read<uint8_t>();
    prgBank_ = r.read<uint8_t>();
}

} // namespace nes
