#include "core/mappers/mapper_uxrom.h"

#include "core/state_io.h"

namespace nes {

MapperUxrom::MapperUxrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring)
    : prgBanks16k_(prgBanks16k), chrBanks8k_(chrBanks8k), mirroring_(mirroring) {}

bool MapperUxrom::cpuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr < 0x8000) return false;
    if (addr < 0xC000) {
        mappedAddr = static_cast<uint32_t>(prgBank_ % prgBanks16k_) * 0x4000 + (addr & 0x3FFF);
    } else {
        mappedAddr = static_cast<uint32_t>(prgBanks16k_ - 1) * 0x4000 + (addr & 0x3FFF);
    }
    return true;
}

bool MapperUxrom::cpuMapWrite(uint16_t addr, uint32_t& /*mappedAddr*/, uint8_t data) {
    if (addr >= 0x8000) {
        prgBank_ = data; // Bank select register; real boards decode only as many bits as needed.
    }
    return false; // Fully consumed by the mapper - never writes through to ROM.
}

bool MapperUxrom::ppuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr <= 0x1FFF) {
        mappedAddr = addr;
        return true;
    }
    return false;
}

bool MapperUxrom::ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) {
    if (addr <= 0x1FFF && chrBanks8k_ == 0) { // CHR RAM
        mappedAddr = addr;
        return true;
    }
    return false;
}

void MapperUxrom::saveState(StateWriter& w) const { w.write(prgBank_); }
void MapperUxrom::loadState(StateReader& r) { prgBank_ = r.read<uint8_t>(); }

} // namespace nes
