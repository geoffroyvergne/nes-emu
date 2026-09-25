#pragma once

#include "Mapper.hpp"

// Mapper 0 (NROM): no bank switching.
//   CPU $8000-$FFFF  16KB PRG-ROM mirrored twice (NROM-128) or 32KB (NROM-256)
//   PPU $0000-$1FFF  8KB CHR-ROM, or CHR-RAM when the header declares none
class Mapper_000 : public Mapper {
public:
    using Mapper::Mapper;

    bool cpuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool cpuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr, std::uint8_t data) override;
    bool ppuMapRead(std::uint16_t addr, std::uint32_t& mappedAddr) override;
    bool ppuMapWrite(std::uint16_t addr, std::uint32_t& mappedAddr) override;
};
