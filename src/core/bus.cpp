#include "core/bus.h"

#include "core/apu2A03.h"
#include "core/cpu6502.h"
#include "core/ppu2C02.h"
#include "core/state_io.h"

namespace nes {

namespace {
bool isApuRegister(uint16_t addr) {
    return (addr >= 0x4000 && addr <= 0x4013) || addr == 0x4015 || addr == 0x4017;
}
} // namespace

uint8_t Bus::read(uint16_t addr) {
    if (addr <= 0x1FFF) {
        return ram_[addr & 0x07FF];
    }
    if (addr <= 0x3FFF) {
        return ppu_ ? ppu_->cpuRead(addr) : 0;
    }
    if (addr == 0x4015) {
        return apu_ ? apu_->cpuRead(addr) : 0;
    }
    if (addr == 0x4016) {
        return controllers_[0].read();
    }
    if (addr == 0x4017) {
        // $4017 is write-only for the APU frame counter; reads go to controller 2.
        return controllers_[1].read();
    }
    if (addr >= 0x4020 && cartridge_) {
        uint8_t value = 0;
        if (cartridge_->cpuRead(addr, value)) {
            return value;
        }
    }
    return 0; // Open bus.
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
    if (isApuRegister(addr)) {
        if (apu_) apu_->cpuWrite(addr, value);
        return;
    }
    if (addr >= 0x4020 && cartridge_) {
        cartridge_->cpuWrite(addr, value);
    }
}

void Bus::setRegion(bool isPal) {
    if (isPal) {
        ppuTicksPerCpuTick_ = 5;
        ppuTicksDenominator_ = 16;
    } else {
        ppuTicksPerCpuTick_ = 1;
        ppuTicksDenominator_ = 3;
    }
    cpuTickAccumulator_ = 0;
}

void Bus::reset() {
    if (cpu_) cpu_->reset();
    if (ppu_) ppu_->reset();
    if (apu_) apu_->reset();
    cpuTickAccumulator_ = 0;
    cpuCycleCount_ = 0;
    dmaTransfer_ = false;
    dmaDummyCycle_ = true;
    dmaPage_ = 0;
    dmaAddr_ = 0;
    dmaData_ = 0;
}

void Bus::clock() {
    if (ppu_) ppu_->clock();

    cpuTickAccumulator_ += ppuTicksPerCpuTick_;
    if (cpuTickAccumulator_ >= ppuTicksDenominator_) {
        cpuTickAccumulator_ -= ppuTicksDenominator_;

        // The APU shares the CPU's clock and, unlike the CPU, keeps running
        // during OAM DMA stalls on real hardware.
        if (apu_) apu_->clock();

        if (dmaTransfer_) {
            if (dmaDummyCycle_) {
                if (cpuCycleCount_ % 2 == 1) dmaDummyCycle_ = false;
            } else if (cpuCycleCount_ % 2 == 0) {
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
        cpuCycleCount_++;
    }

    // Real 6502 hardware only polls for interrupts at instruction
    // boundaries (the last cycle of each instruction), never mid-
    // instruction. Our CPU model executes a whole instruction atomically on
    // the cycle it's fetched (see Cpu6502::clock()), so cyclesLeft_ > 0
    // covers exactly the "mid-instruction" window - deliver only when
    // instructionComplete() is true. Without this gate, nmi()/irq() would
    // push whatever pc_/status_ happen to be at that moment (already
    // pointing past the in-flight instruction) and corrupt the instruction
    // stream; with frequent interrupt sources (e.g. MMC3's per-scanline IRQ)
    // this was hitting on nearly every scanline instead of being a rare
    // corner case.
    bool atInstructionBoundary = cpu_ && cpu_->instructionComplete();

    if (atInstructionBoundary && ppu_ && ppu_->nmiRequested()) {
        ppu_->clearNmiRequest();
        cpu_->nmi();
    }

    // Mapper-driven IRQ (e.g. MMC3's scanline counter) and/or APU IRQ (frame
    // counter, DMC). Level-triggered: each source clears its own pending
    // flag once acknowledged in whatever way real hardware defines for it
    // (MMC3: write $E000; APU: read $4015 for the frame IRQ, $4015/$4010
    // writes for the DMC IRQ), so no separate clear step is needed here.
    bool irq = (cartridge_ && cartridge_->irqPending()) || (apu_ && apu_->irqPending());
    if (atInstructionBoundary && irq) {
        cpu_->irq();
    }
}

void Bus::saveState(StateWriter& w) const {
    w.writeArray(ram_);
    w.write(cpuTickAccumulator_);
    w.write(cpuCycleCount_);
    w.write(dmaTransfer_);
    w.write(dmaDummyCycle_);
    w.write(dmaPage_);
    w.write(dmaAddr_);
    w.write(dmaData_);
}

void Bus::loadState(StateReader& r) {
    r.readArray(ram_);
    cpuTickAccumulator_ = r.read<uint32_t>();
    cpuCycleCount_ = r.read<uint64_t>();
    dmaTransfer_ = r.read<bool>();
    dmaDummyCycle_ = r.read<bool>();
    dmaPage_ = r.read<uint8_t>();
    dmaAddr_ = r.read<uint8_t>();
    dmaData_ = r.read<uint8_t>();
}

} // namespace nes
