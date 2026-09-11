#include "core/bus.h"

#include "core/cpu6502.h"
#include "core/ppu2C02.h"

namespace nes {

uint8_t Bus::read(uint16_t addr) {
    if (addr <= 0x1FFF) {
        return ram_[addr & 0x07FF];
    }
    if (addr <= 0x3FFF) {
        return ppu_ ? ppu_->cpuRead(addr) : 0;
    }
    if (addr == 0x4016) {
        return controllers_[0].read();
    }
    if (addr == 0x4017) {
        return controllers_[1].read();
    }
    if (addr >= 0x4020 && cartridge_) {
        uint8_t value = 0;
        if (cartridge_->cpuRead(addr, value)) {
            return value;
        }
    }
    return 0; // Open bus (APU registers not yet implemented, unmapped space).
}

void Bus::write(uint16_t addr, uint8_t value) {
    if (addr <= 0x1FFF) {
        ram_[addr & 0x07FF] = value;
        return;
    }
    if (addr <= 0x3FFF) {
        if (ppu_) ppu_->cpuWrite(addr, value);
        return;
    }
    if (addr == 0x4014) {
        dmaPage_ = value;
        dmaAddr_ = 0;
        dmaTransfer_ = true;
        return;
    }
    if (addr == 0x4016) {
        // Both controller ports share the same strobe line on real hardware.
        controllers_[0].write(value);
        controllers_[1].write(value);
        return;
    }
    if (addr >= 0x4020 && cartridge_) {
        cartridge_->cpuWrite(addr, value);
    }
    // $4000-$4013, $4015, $4017 (APU registers) are not implemented yet: ignored.
}

void Bus::reset() {
    if (cpu_) cpu_->reset();
    if (ppu_) ppu_->reset();
    systemClockCounter_ = 0;
    dmaTransfer_ = false;
    dmaDummyCycle_ = true;
    dmaPage_ = 0;
    dmaAddr_ = 0;
    dmaData_ = 0;
}

void Bus::clock() {
    if (ppu_) ppu_->clock();

    if (systemClockCounter_ % 3 == 0) {
        uint64_t cpuCycle = systemClockCounter_ / 3;
        if (dmaTransfer_) {
            if (dmaDummyCycle_) {
                if (cpuCycle % 2 == 1) dmaDummyCycle_ = false;
            } else if (cpuCycle % 2 == 0) {
                dmaData_ = read(static_cast<uint16_t>((dmaPage_ << 8) | dmaAddr_));
            } else {
                if (ppu_) ppu_->oamWrite(dmaAddr_, dmaData_);
                dmaAddr_++;
                if (dmaAddr_ == 0) { // Wrapped after 256 bytes: DMA done.
                    dmaTransfer_ = false;
                    dmaDummyCycle_ = true;
                }
            }
        } else if (cpu_) {
            cpu_->clock();
        }
    }

    if (ppu_ && ppu_->nmiRequested()) {
        ppu_->clearNmiRequest();
        if (cpu_) cpu_->nmi();
    }

    systemClockCounter_++;
}

} // namespace nes
