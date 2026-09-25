#include "Mapper_000.hpp"

bool Mapper_000::cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr < 0x8000) {
        return false;
    }
    // One 16KB bank appears at both $8000 and $C000; two banks fill the 32KB window.
    mappedAddr = addr & (prgBanks > 1 ? 0x7FFF : 0x3FFF);
    return true;
}

bool Mapper_000::cpuMapWrite(std::uint16_t /*addr*/, std::uint32_t& /*mappedAddr*/, std::uint8_t /*data*/) {
    return false; // PRG-ROM is read-only and NROM has no registers
}

bool Mapper_000::ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr > 0x1FFF) {
        return false;
    }
    mappedAddr = addr;
    return true;
}

bool Mapper_000::ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) {
    if (addr > 0x1FFF || !usesChrRam()) {
        return false; // CHR-ROM is read-only
    }
    mappedAddr = addr;
    return true;
}
