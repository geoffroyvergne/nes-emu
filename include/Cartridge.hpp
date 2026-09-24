#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>
#include <vector>

enum class Mirroring {
    Horizontal,
    Vertical,
    FourScreen,
};

[[nodiscard]] std::string_view toString(Mirroring mirroring);

// A game cartridge loaded from an iNES (.nes) file.
// The constructor throws std::runtime_error if the file cannot be read or is not a valid iNES image.
class Cartridge {
public:
    static constexpr std::size_t PRG_BANK_SIZE = 16 * 1024;
    static constexpr std::size_t CHR_BANK_SIZE = 8 * 1024;
    static constexpr std::size_t PRG_RAM_SIZE = 8 * 1024;

    explicit Cartridge(const std::filesystem::path& romPath);

    // CPU-side access to cartridge space ($4020-$FFFF). Returns true if the cartridge claimed
    // the address; unclaimed reads leave data untouched. Hardcoded to mapper 0 (NROM) for now:
    //   $6000-$7FFF  8KB PRG-RAM
    //   $8000-$FFFF  PRG-ROM (a 16KB image is mirrored into both halves; writes are ignored)
    bool cpuRead(std::uint16_t addr, std::uint8_t& data);
    bool cpuWrite(std::uint16_t addr, std::uint8_t data);

    // PPU-side access to the pattern tables ($0000-$1FFF). Same contract as cpuRead/cpuWrite.
    // Writes only land when the board has CHR-RAM; CHR-ROM ignores them.
    bool ppuRead(std::uint16_t addr, std::uint8_t& data);
    bool ppuWrite(std::uint16_t addr, std::uint8_t data);

    [[nodiscard]] std::uint8_t getPrgBankCount() const { return prgBankCount; }
    [[nodiscard]] std::uint8_t getChrBankCount() const { return chrBankCount; }
    [[nodiscard]] std::uint8_t getMapperId() const { return mapperId; }
    [[nodiscard]] Mirroring getMirroring() const { return mirroring; }
    [[nodiscard]] bool hasTrainer() const { return trainerPresent; }
    [[nodiscard]] bool hasBatteryRam() const { return batteryRam; }
    // True when the cartridge has no CHR-ROM and uses 8KB of CHR-RAM instead.
    [[nodiscard]] bool usesChrRam() const { return chrBankCount == 0; }

private:
    std::vector<std::uint8_t> prgRom;
    std::vector<std::uint8_t> chrRom;
    std::vector<std::uint8_t> prgRam = std::vector<std::uint8_t>(PRG_RAM_SIZE, 0);
    // Mask applied to (addr - $8000): $3FFF for 16KB PRG-ROM, $7FFF for 32KB.
    std::uint16_t prgAddrMask = 0x3FFF;

    std::uint8_t prgBankCount = 0;
    std::uint8_t chrBankCount = 0;
    std::uint8_t mapperId = 0;
    Mirroring mirroring = Mirroring::Horizontal;
    bool trainerPresent = false;
    bool batteryRam = false;
};
