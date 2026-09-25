#include "Bus.hpp"

#include "Apu2A03.hpp"
#include "Cartridge.hpp"
#include "Ppu2C02.hpp"

#include <utility>

namespace {

constexpr std::uint16_t RAM_END = 0x1FFF;
constexpr std::uint16_t RAM_MIRROR_MASK = 0x07FF;
constexpr std::uint16_t PPU_REGISTERS_END = 0x3FFF;
constexpr std::uint16_t APU_IO_END = 0x401F;
constexpr std::uint16_t PPU_OAMDATA = 0x2004;

} // namespace

void Bus::insertCartridge(std::shared_ptr<Cartridge> newCartridge) {
    cartridge = std::move(newCartridge);
}

void Bus::cpuWrite(std::uint16_t addr, std::uint8_t data, bool consecutiveCycle) {
    if (!consecutiveCycle) {
        ++writeStamp;
    }
    if (addr <= RAM_END) {
        cpuRam[addr & RAM_MIRROR_MASK] = data;
    } else if (addr <= PPU_REGISTERS_END) {
        if (ppu != nullptr) {
            ppu->cpuWrite(addr, data); // The PPU decodes the 8-byte mirroring itself.
        }
    } else if (addr == OAM_DMA) {
        runOamDma(data);
    } else if (addr == CONTROLLER_1) {
        // One strobe line is wired to both controller ports.
        for (Controller& controller : controllers) {
            controller.writeStrobe(data);
        }
    } else if (addr <= APU_IO_END) {
        // $4000-$4013, $4015, $4017 (frame counter); $4018-$401F are unused test registers.
        if (apu != nullptr) {
            apu->cpuWrite(addr, data);
        }
    } else if (cartridge) {
        cartridge->cpuWrite(addr, data, writeStamp);
    }
}

void Bus::runOamDma(std::uint8_t page) {
    // Copies $XX00-$XXFF into OAM through OAMDATA ($2004), exactly like the hardware: the copy
    // starts at the current OAMADDR and wraps. The whole page is copied at once; the 513/514
    // cycles it takes are charged to the CPU by the system loop.
    const auto base = static_cast<std::uint16_t>(page << 8);
    for (std::uint16_t i = 0; i < 256; ++i) {
        const std::uint8_t data = cpuRead(static_cast<std::uint16_t>(base | i));
        if (ppu != nullptr) {
            ppu->cpuWrite(PPU_OAMDATA, data);
        }
    }
    oamDmaTriggered = true;
}

bool Bus::pollOamDma() {
    return std::exchange(oamDmaTriggered, false);
}

std::uint8_t Bus::cpuRead(std::uint16_t addr, bool readOnly) {
    // Unmapped addresses read as 0 for now; real hardware returns the last value on the data bus.
    std::uint8_t data = 0x00;

    if (addr <= RAM_END) {
        data = cpuRam[addr & RAM_MIRROR_MASK];
    } else if (addr <= PPU_REGISTERS_END) {
        if (ppu != nullptr) {
            data = ppu->cpuRead(addr, readOnly);
        }
    } else if (addr == CONTROLLER_1 || addr == CONTROLLER_2) {
        data = controllers[addr - CONTROLLER_1].read(readOnly);
    } else if (addr == APU_STATUS) {
        if (apu != nullptr) {
            data = apu->readStatus(readOnly);
        }
    } else if (addr <= APU_IO_END) {
        // Other APU registers are write-only.
    } else if (cartridge) {
        cartridge->cpuRead(addr, data);
    }

    return data;
}
