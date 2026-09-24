#include "Cartridge.hpp"

#include <array>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>

namespace {

constexpr std::size_t HEADER_SIZE = 16;
constexpr std::size_t TRAINER_SIZE = 512;

constexpr std::uint16_t PRG_RAM_START = 0x6000;
constexpr std::uint16_t PRG_ROM_START = 0x8000;
constexpr std::uint16_t CHR_END = 0x1FFF;

// Flags 6
constexpr std::uint8_t FLAG6_VERTICAL_MIRRORING = 0x01;
constexpr std::uint8_t FLAG6_BATTERY_RAM = 0x02;
constexpr std::uint8_t FLAG6_TRAINER = 0x04;
constexpr std::uint8_t FLAG6_FOUR_SCREEN = 0x08;

// Flags 7 bits 2-3 == 0b10 identify an NES 2.0 header.
constexpr std::uint8_t FLAG7_FORMAT_MASK = 0x0C;
constexpr std::uint8_t FLAG7_FORMAT_NES2 = 0x08;

struct INesHeader {
    std::array<char, 4> magic;
    std::uint8_t prgBanks;
    std::uint8_t chrBanks;
    std::uint8_t flags6;
    std::uint8_t flags7;
    std::array<std::uint8_t, 8> padding;
};
static_assert(sizeof(INesHeader) == HEADER_SIZE);

void readExact(std::ifstream& file, void* dest, std::size_t size, const char* what) {
    file.read(static_cast<char*>(dest), static_cast<std::streamsize>(size));
    if (static_cast<std::size_t>(file.gcount()) != size) {
        throw std::runtime_error(std::string("ROM file is truncated while reading ") + what);
    }
}

} // namespace

std::string_view toString(Mirroring mirroring) {
    switch (mirroring) {
    case Mirroring::Horizontal: return "Horizontal";
    case Mirroring::Vertical: return "Vertical";
    case Mirroring::FourScreen: return "Four-screen";
    }
    return "Unknown";
}

Cartridge::Cartridge(const std::filesystem::path& romPath) {
    std::ifstream file(romPath, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cannot open ROM file: " + romPath.string());
    }

    INesHeader header{};
    readExact(file, &header, HEADER_SIZE, "header");

    if (header.magic != std::array<char, 4>{'N', 'E', 'S', '\x1A'}) {
        throw std::runtime_error("Not an iNES file (bad magic): " + romPath.string());
    }
    if (header.prgBanks == 0) {
        throw std::runtime_error("iNES header declares zero PRG-ROM banks");
    }

    prgBankCount = header.prgBanks;
    chrBankCount = header.chrBanks;
    trainerPresent = (header.flags6 & FLAG6_TRAINER) != 0;
    batteryRam = (header.flags6 & FLAG6_BATTERY_RAM) != 0;

    if ((header.flags6 & FLAG6_FOUR_SCREEN) != 0) {
        mirroring = Mirroring::FourScreen;
    } else if ((header.flags6 & FLAG6_VERTICAL_MIRRORING) != 0) {
        mirroring = Mirroring::Vertical;
    } else {
        mirroring = Mirroring::Horizontal;
    }

    // Some old dumps have junk (e.g. "DiskDude!") in bytes 7-15. For plain iNES headers, a
    // non-zero tail means flags 7 is unreliable, so only the lower mapper nibble is trusted.
    const bool isNes2 = (header.flags7 & FLAG7_FORMAT_MASK) == FLAG7_FORMAT_NES2;
    const bool tailIsClean = header.padding[4] == 0 && header.padding[5] == 0 &&
                             header.padding[6] == 0 && header.padding[7] == 0;
    const auto lowerNibble = static_cast<std::uint8_t>(header.flags6 >> 4);
    const auto upperNibble = static_cast<std::uint8_t>(header.flags7 & 0xF0);
    mapperId = (isNes2 || tailIsClean) ? static_cast<std::uint8_t>(upperNibble | lowerNibble) : lowerNibble;

    if (trainerPresent) {
        file.seekg(static_cast<std::streamoff>(TRAINER_SIZE), std::ios::cur);
    }

    prgRom.resize(prgBankCount * PRG_BANK_SIZE);
    // NROM sees at most 32KB; larger images expose their first 32KB until real mappers exist.
    prgAddrMask = prgBankCount > 1 ? 0x7FFF : 0x3FFF;
    readExact(file, prgRom.data(), prgRom.size(), "PRG-ROM");

    if (chrBankCount > 0) {
        chrRom.resize(chrBankCount * CHR_BANK_SIZE);
        readExact(file, chrRom.data(), chrRom.size(), "CHR-ROM");
    } else {
        // No CHR-ROM: the board provides 8KB of CHR-RAM instead.
        chrRom.resize(CHR_BANK_SIZE, 0);
    }
}

bool Cartridge::cpuRead(std::uint16_t addr, std::uint8_t& data) {
    if (addr >= PRG_ROM_START) {
        data = prgRom[static_cast<std::uint16_t>(addr - PRG_ROM_START) & prgAddrMask];
        return true;
    }
    if (addr >= PRG_RAM_START) {
        data = prgRam[addr - PRG_RAM_START];
        return true;
    }
    return false;
}

bool Cartridge::cpuWrite(std::uint16_t addr, std::uint8_t data) {
    if (addr >= PRG_ROM_START) {
        return true; // ROM: NROM has no mapper registers, so the write is swallowed.
    }
    if (addr >= PRG_RAM_START) {
        prgRam[addr - PRG_RAM_START] = data;
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(std::uint16_t addr, std::uint8_t& data) {
    if (addr > CHR_END) {
        return false;
    }
    data = chrRom[addr];
    return true;
}

bool Cartridge::ppuWrite(std::uint16_t addr, std::uint8_t data) {
    if (addr > CHR_END) {
        return false;
    }
    if (usesChrRam()) {
        chrRom[addr] = data;
    }
    return true;
}
