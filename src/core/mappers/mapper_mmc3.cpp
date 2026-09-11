#include "core/mappers/mapper_mmc3.h"

#include "core/state_io.h"

namespace nes {

MapperMmc3::MapperMmc3(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring headerMirroring)
    : prgBanks8k_(static_cast<uint8_t>(prgBanks16k * 2)),
      chrBanks8k_(chrBanks8k),
      fourScreen_(headerMirroring == Mirroring::FourScreen) {}

bool MapperMmc3::cpuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr < 0x8000) return false;

    uint8_t r6 = static_cast<uint8_t>(bankRegisters_[6] % prgBanks8k_);
    uint8_t r7 = static_cast<uint8_t>(bankRegisters_[7] % prgBanks8k_);
    uint8_t secondLast = static_cast<uint8_t>(prgBanks8k_ - 2);
    uint8_t last = static_cast<uint8_t>(prgBanks8k_ - 1);
    bool prgMode1 = bankSelect_ & 0x40;

    uint8_t bank;
    if (addr < 0xA000) { // $8000-$9FFF
        bank = prgMode1 ? secondLast : r6;
    } else if (addr < 0xC000) { // $A000-$BFFF
        bank = r7;
    } else if (addr < 0xE000) { // $C000-$DFFF
        bank = prgMode1 ? r6 : secondLast;
    } else { // $E000-$FFFF
        bank = last;
    }
    mappedAddr = static_cast<uint32_t>(bank) * 0x2000 + (addr & 0x1FFF);
    return true;
}

bool MapperMmc3::cpuMapWrite(uint16_t addr, uint32_t& /*mappedAddr*/, uint8_t data) {
    if (addr < 0x8000) return false;
    bool even = (addr & 1) == 0;

    if (addr < 0xA000) {
        if (even) {
            bankSelect_ = data;
        } else {
            bankRegisters_[bankSelect_ & 0x07] = data;
        }
    } else if (addr < 0xC000) {
        if (even) {
            mirroringBit_ = data & 0x01;
        } else {
            prgRamProtect_ = data;
        }
    } else if (addr < 0xE000) {
        if (even) {
            irqLatch_ = data;
        } else {
            irqReloadPending_ = true;
        }
    } else {
        if (even) {
            irqEnabled_ = false;
            irqPending_ = false;
        } else {
            irqEnabled_ = true;
        }
    }
    return false;
}

uint32_t MapperMmc3::chrAddress(uint16_t addr) const {
    uint16_t a = addr & 0x1FFF;
    bool invert = bankSelect_ & 0x80;
    uint16_t effective = invert ? static_cast<uint16_t>(a ^ 0x1000) : a;

    if (effective < 0x0800) {
        return static_cast<uint32_t>(bankRegisters_[0] & 0xFE) * 0x400 + (effective & 0x7FF);
    }
    if (effective < 0x1000) {
        return static_cast<uint32_t>(bankRegisters_[1] & 0xFE) * 0x400 + (effective & 0x7FF);
    }
    if (effective < 0x1400) {
        return static_cast<uint32_t>(bankRegisters_[2]) * 0x400 + (effective & 0x3FF);
    }
    if (effective < 0x1800) {
        return static_cast<uint32_t>(bankRegisters_[3]) * 0x400 + (effective & 0x3FF);
    }
    if (effective < 0x1C00) {
        return static_cast<uint32_t>(bankRegisters_[4]) * 0x400 + (effective & 0x3FF);
    }
    return static_cast<uint32_t>(bankRegisters_[5]) * 0x400 + (effective & 0x3FF);
}

bool MapperMmc3::ppuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr > 0x1FFF) return false;
    mappedAddr = chrBanks8k_ == 0 ? addr : chrAddress(addr);
    return true;
}

bool MapperMmc3::ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) {
    if (addr > 0x1FFF || chrBanks8k_ != 0) return false; // CHR ROM is read-only.
    mappedAddr = addr;
    return true;
}

Mirroring MapperMmc3::mirroring() const {
    if (fourScreen_) return Mirroring::FourScreen;
    return mirroringBit_ ? Mirroring::Horizontal : Mirroring::Vertical;
}

void MapperMmc3::scanlineTick() {
    if (irqCounter_ == 0 || irqReloadPending_) {
        irqCounter_ = irqLatch_;
        irqReloadPending_ = false;
    } else {
        irqCounter_--;
    }
    if (irqCounter_ == 0 && irqEnabled_) {
        irqPending_ = true;
    }
}

void MapperMmc3::saveState(StateWriter& w) const {
    w.write(bankSelect_);
    w.writeArray(bankRegisters_);
    w.write(mirroringBit_);
    w.write(prgRamProtect_);
    w.write(irqLatch_);
    w.write(irqCounter_);
    w.write(irqReloadPending_);
    w.write(irqEnabled_);
    w.write(irqPending_);
}

void MapperMmc3::loadState(StateReader& r) {
    bankSelect_ = r.read<uint8_t>();
    r.readArray(bankRegisters_);
    mirroringBit_ = r.read<uint8_t>();
    prgRamProtect_ = r.read<uint8_t>();
    irqLatch_ = r.read<uint8_t>();
    irqCounter_ = r.read<uint8_t>();
    irqReloadPending_ = r.read<bool>();
    irqEnabled_ = r.read<bool>();
    irqPending_ = r.read<bool>();
}

} // namespace nes
