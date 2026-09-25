#pragma once

#include "Mirroring.hpp"

#include <cstdint>

// Cartridge board logic: translates CPU ($8000-$FFFF) and PPU ($0000-$1FFF) addresses into offsets
// within the cartridge's PRG and CHR memory, and handles writes to the board's own registers.
// The Cartridge owns the memory; a Mapper only decides where an access lands.
//
// Map functions return true when the access hits cartridge memory, with mappedAddr set to the offset
// into PRG-ROM or CHR memory. cpuMapWrite returns false for register writes (the data went to the
// mapper, not to memory). $6000-$7FFF PRG-RAM is handled by the Cartridge, gated by isPrgRamEnabled().
class Mapper {
public:
    Mapper(int prgBanks16k, int chrBanks8k, Mirroring headerMirroring)
        : prgBanks(prgBanks16k), chrBanks(chrBanks8k), mirroring(headerMirroring) {}
    virtual ~Mapper() = default;
    Mapper(const Mapper&) = delete;
    Mapper& operator=(const Mapper&) = delete;

    virtual bool cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) = 0;
    virtual bool cpuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr, std::uint8_t data) = 0;
    virtual bool ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) = 0;
    virtual bool ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) = 0;

    // Mirroring currently in effect (the header's, unless the mapper controls it).
    [[nodiscard]] virtual Mirroring getMirroring() const { return mirroring; }
    [[nodiscard]] virtual bool isPrgRamEnabled() const { return true; }

    // Identifies the CPU instruction performing the next cpuMapWrite. Two writes with the same
    // stamp came from one read-modify-write instruction on consecutive cycles (see Mapper_001).
    void setCpuWriteStamp(std::uint64_t stamp) { cpuWriteStamp = stamp; }

protected:
    [[nodiscard]] bool usesChrRam() const { return chrBanks == 0; }

    int prgBanks; // Number of 16KB PRG-ROM banks
    int chrBanks; // Number of 8KB CHR-ROM banks (0 = 8KB CHR-RAM)
    Mirroring mirroring;
    std::uint64_t cpuWriteStamp = 0;
};
