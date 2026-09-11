#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/mapper.h"

namespace nes {

// Loads an iNES (.nes) ROM file and owns its PRG/CHR data, delegating
// address translation to the appropriate Mapper implementation.
class Cartridge {
public:
    // Throws std::runtime_error with a human-readable message on any
    // malformed file, unsupported mapper, or I/O failure.
    explicit Cartridge(const std::string& path);

    bool cpuRead(uint16_t addr, uint8_t& value);
    bool cpuWrite(uint16_t addr, uint8_t value);
    bool ppuRead(uint16_t addr, uint8_t& value);
    bool ppuWrite(uint16_t addr, uint8_t value);

    Mirroring mirroring() const { return mapper_->mirroring(); }
    uint8_t mapperId() const { return mapperId_; }

private:
    std::vector<uint8_t> prgRom_;
    std::vector<uint8_t> chrMem_; // ROM if chrBanks8k_ > 0, else RAM
    std::vector<uint8_t> prgRam_ = std::vector<uint8_t>(8192, 0);

    uint8_t mapperId_ = 0;
    uint8_t prgBanks16k_ = 0;
    uint8_t chrBanks8k_ = 0;

    std::unique_ptr<Mapper> mapper_;
};

} // namespace nes
