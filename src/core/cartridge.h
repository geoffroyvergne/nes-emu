#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/mapper.h"

namespace nes {

// The TV system the ROM's header claims it targets. Informational only for
// now - the emulator always runs NTSC timing regardless (see Cartridge's
// header comment for why this reads as unreliable in practice).
enum class TvSystem { NTSC, PAL };

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

    // Read from the iNES 1.0 header's TV-system byte. Unreliable in
    // practice - most iNES 1.0 dumping tools left this at 0 (NTSC)
    // regardless of the ROM's actual region, since it was rarely
    // consulted - so treat this as a hint, not a guarantee. NES 2.0 headers
    // (not distinguished from iNES 1.0 here) encode region more reliably,
    // but reading that format isn't implemented yet.
    TvSystem tvSystem() const { return tvSystem_; }

    bool irqPending() const { return mapper_->irqPending(); }
    void scanlineTick() { mapper_->scanlineTick(); }

    // Saves/restores PRG RAM, CHR RAM (if present - CHR ROM is immutable and
    // skipped), and the mapper's bank-switching state. Assumes `this` is the
    // same Cartridge instance the state was saved from (same ROM already
    // loaded) - PRG/CHR ROM contents themselves aren't included.
    void saveState(StateWriter& w) const;
    void loadState(StateReader& r);

private:
    std::vector<uint8_t> prgRom_;
    std::vector<uint8_t> chrMem_; // ROM if chrBanks8k_ > 0, else RAM
    std::vector<uint8_t> prgRam_ = std::vector<uint8_t>(8192, 0);

    uint8_t mapperId_ = 0;
    uint8_t prgBanks16k_ = 0;
    uint8_t chrBanks8k_ = 0;
    TvSystem tvSystem_ = TvSystem::NTSC;

    std::unique_ptr<Mapper> mapper_;
};

} // namespace nes
