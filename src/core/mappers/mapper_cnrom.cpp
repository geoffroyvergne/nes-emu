#include "core/mappers/mapper_cnrom.h"

#include "core/state_io.h"

namespace nes {

MapperCnrom::MapperCnrom(uint8_t prgBanks16k, uint8_t chrBanks8k, Mirroring mirroring)
    : prgBanks16k_(prgBanks16k), chrBanks8k_(chrBanks8k), mirroring_(mirroring) {}

bool MapperCnrom::cpuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr < 0x8000) return false;
    // 16KB PRG mirrors into both halves of the window; 32KB indexes straight through.
    mappedAddr = addr & (prgBanks16k_ > 1 ? 0x7FFF : 0x3FFF);
    return true;
}

bool MapperCnrom::cpuMapWrite(uint16_t addr, uint32_t& /*mappedAddr*/, uint8_t data) {
    if (addr >= 0x8000) {
        chrBank_ = data; // CHR bank select; real boards decode only as many bits as needed.
    }
    return false;
}

bool MapperCnrom::ppuMapRead(uint16_t addr, uint32_t& mappedAddr) {
    if (addr > 0x1FFF) return false;
    if (chrBanks8k_ == 0) {
        mappedAddr = addr; // No CHR ROM declared: fall back to a single 8KB CHR RAM bank.
    } else {
        mappedAddr = static_cast<uint32_t>(chrBank_ % chrBanks8k_) * 0x2000 + addr;
    }
    return true;
}

bool MapperCnrom::ppuMapWrite(uint16_t addr, uint32_t& mappedAddr) {
    if (addr <= 0x1FFF && chrBanks8k_ == 0) { // CHR RAM only - real CHR ROM is read-only.
        mappedAddr = addr;
        return true;
    }
    return false;
}

void MapperCnrom::saveState(StateWriter& w) const { w.write(chrBank_); }
void MapperCnrom::loadState(StateReader& r) { chrBank_ = r.read<uint8_t>(); }

} // namespace nes
