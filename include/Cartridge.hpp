#pragma once

#include "Mapper.hpp"
#include "Mirroring.hpp"
#include "Region.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

// A game cartridge loaded from an iNES (.nes) file: PRG-ROM, CHR-ROM/RAM, PRG-RAM, and the board's
// Mapper, which decides where every access lands. The constructor throws std::runtime_error if the
// file cannot be read, is not a valid iNES image, or uses a mapper that isn't implemented.
class Cartridge {
public:
    static constexpr std::size_t PRG_BANK_SIZE = 16 * 1024;
    static constexpr std::size_t CHR_BANK_SIZE = 8 * 1024;
    static constexpr std::size_t PRG_RAM_SIZE = 8 * 1024;

    explicit Cartridge(const std::filesystem::path& romPath);

    // Mapper ids with an implementation.
    [[nodiscard]] static bool isMapperSupported(int mapperId);
    [[nodiscard]] static std::string_view mapperName(int mapperId);

    // CPU-side access to cartridge space ($4020-$FFFF). Returns true if the cartridge claimed
    // the address; unclaimed reads leave data untouched.
    //   $6000-$7FFF  8KB PRG-RAM (when the mapper enables it)
    //   $8000-$FFFF  PRG-ROM through the mapper; writes go to mapper registers
    // writeStamp identifies the CPU write (equal stamps = consecutive cycles, see Mapper).
    bool cpuRead(std::uint16_t addr, std::uint8_t& data);
    bool cpuWrite(std::uint16_t addr, std::uint8_t data, std::uint64_t writeStamp = 0);

    // PPU-side access to the pattern tables ($0000-$1FFF) through the mapper. Same contract as
    // cpuRead/cpuWrite. Writes only land when the board has CHR-RAM; CHR-ROM ignores them.
    bool ppuRead(std::uint16_t addr, std::uint8_t& data);
    bool ppuWrite(std::uint16_t addr, std::uint8_t data);

    [[nodiscard]] std::uint8_t getPrgBankCount() const { return prgBankCount; }
    [[nodiscard]] std::uint8_t getChrBankCount() const { return chrBankCount; }
    [[nodiscard]] std::uint8_t getMapperId() const { return mapperId; }
    // Current nametable mirroring: from the header, or set at runtime by the mapper (MMC1).
    [[nodiscard]] Mirroring getMirroring() const { return mapper->getMirroring(); }
    [[nodiscard]] bool hasTrainer() const { return trainerPresent; }
    [[nodiscard]] bool hasBatteryRam() const { return batteryRam; }
    // TV system declared in the header, if any: NES 2.0 byte 12, or iNES byte 9 bit 0 when the header
    // is trustworthy. Most iNES 1.0 dumps leave it unset (reads as "none"), so callers need a fallback.
    [[nodiscard]] std::optional<Region> getHeaderRegion() const { return headerRegion; }
    // True when the cartridge has no CHR-ROM and uses 8KB of CHR-RAM instead.
    [[nodiscard]] bool usesChrRam() const { return chrBankCount == 0; }

private:
    std::vector<std::uint8_t> prgRom;
    std::vector<std::uint8_t> chrMemory; // CHR-ROM, or 8KB of CHR-RAM
    std::vector<std::uint8_t> prgRam = std::vector<std::uint8_t>(PRG_RAM_SIZE, 0);
    std::unique_ptr<Mapper> mapper;

    std::uint8_t prgBankCount = 0;
    std::uint8_t chrBankCount = 0;
    std::uint8_t mapperId = 0;
    Mirroring headerMirroring = Mirroring::Horizontal;
    bool trainerPresent = false;
    bool batteryRam = false;
    std::optional<Region> headerRegion;
};
