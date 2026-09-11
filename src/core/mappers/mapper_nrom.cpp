#include "core/mappers/mapper_nrom.h"

namespace nes {

MapperNrom::MapperNrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring)
    : prgBanks16k_(prgBanks16k), chrBanks8k_(chrBanks8k), mirroring_(mirroring) {}

bool MapperNrom::cpuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr >= 0x8000) {
        // 32KB PRG: addr - 0x8000 indexes straight through.
        // 16KB PRG: mirror the single bank into both halves of the window.
        mappedAddr = addr & (prgBanks16k_ > 1 ? 0x7FFF : 0x3FFF);
        return true;
    }
    return false;
}

bool MapperNrom::cpuMapWrite(uint16_t /*addr*/, uint32_t& /*mappedAddr*/, uint8_t /*data*/) {
    // NROM has no PRG RAM and no bank-switch registers to write to.
    return false;
}

bool MapperNrom::ppuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr <= 0x1FFF) {
        mappedAddr = addr;
        return true;
    }
    return false;
}

bool MapperNrom::ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) {
    if (addr <= 0x1FFF && chrBanks8k_ == 0) {
        // CHR RAM (no CHR ROM banks declared): writable.
        mappedAddr = addr;
        return true;
    }
    return false;
}

} // namespace nes
