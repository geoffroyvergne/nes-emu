#pragma once

#include "Controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

class Cartridge;
class Ppu2C02;
class Apu2A03;

// Central CPU bus: routes every CPU memory access to the component that owns the address.
//
//   $0000-$1FFF  2KB system RAM, mirrored every $0800
//   $2000-$3FFF  PPU registers ($2000-$2007), mirrored every 8 bytes
//   $4000-$401F  APU and I/O registers ($4014 OAM DMA, $4016/$4017 controllers)
//   $4020-$FFFF  Cartridge space (PRG-RAM, PRG-ROM, mapper registers)
class Bus {
public:
    static constexpr std::size_t RAM_SIZE = 2 * 1024;

    void insertCartridge(std::shared_ptr<Cartridge> newCartridge);
    // The PPU is owned elsewhere (it is clocked by the system loop); the bus only routes to it.
    void connectPpu(Ppu2C02& newPpu) { ppu = &newPpu; }
    // Same for the APU ($4000-$4013, $4015, $4017 writes; $4015 reads).
    void connectApu(Apu2A03& newApu) { apu = &newApu; }

    static constexpr std::uint16_t OAM_DMA = 0x4014;
    static constexpr std::uint16_t APU_STATUS = 0x4015;
    static constexpr std::uint16_t CONTROLLER_1 = 0x4016; // Write: strobe both pads; read: pad 1
    static constexpr std::uint16_t CONTROLLER_2 = 0x4017; // Read: pad 2 (writes go to the APU frame counter)
    static constexpr int CONTROLLER_COUNT = 2;

    // consecutiveCycle: this write happens on the CPU cycle right after the previous write (the
    // second write of a read-modify-write instruction). Some mappers (MMC1) ignore such writes.
    void cpuWrite(std::uint16_t addr, std::uint8_t data, bool consecutiveCycle = false);
    // readOnly = true is for debuggers/disassemblers: the read must not trigger side effects
    // (e.g. clearing the PPU status flag when reading $2002).
    [[nodiscard]] std::uint8_t cpuRead(std::uint16_t addr, bool readOnly = false);

    // True once after a write to $4014 copied a page into OAM; the system loop must then stall
    // the CPU (Cpu6502::stallForOamDma). Reading it clears the request.
    [[nodiscard]] bool pollOamDma();

    // Host input: port 0 = player 1, port 1 = player 2.
    [[nodiscard]] Controller& getController(int port) { return controllers[static_cast<std::size_t>(port & 1)]; }

private:
    std::array<std::uint8_t, RAM_SIZE> cpuRam{};
    std::shared_ptr<Cartridge> cartridge;
    Ppu2C02* ppu = nullptr;
    Apu2A03* apu = nullptr;
    bool oamDmaTriggered = false;
    std::uint64_t writeStamp = 0; // Same value for writes on consecutive CPU cycles
    std::array<Controller, CONTROLLER_COUNT> controllers{};

    void runOamDma(std::uint8_t page);
};
