#include "Cartridge.hpp"

#include "Mapper_000.hpp"
#include "Mapper_001.hpp"
#include "Mapper_004.hpp"

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

bool Cartridge::isMapperSupported(int mapperId) {
    return mapperId == 0 || mapperId == 1 || mapperId == 4;
}

std::string_view Cartridge::mapperName(int mapperId) {
    switch (mapperId) {
    case 0: return "NROM";
    case 1: return "MMC1";
    case 2: return "UxROM";
    case 3: return "CNROM";
    case 4: return "MMC3";
    case 7: return "AxROM";
    default: return "unknown";
    }
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
        headerMirroring = Mirroring::FourScreen;
    } else if ((header.flags6 & FLAG6_VERTICAL_MIRRORING) != 0) {
        headerMirroring = Mirroring::Vertical;
    } else {
        headerMirroring = Mirroring::Horizontal;
    }

    // Some old dumps have junk (e.g. "DiskDude!") in bytes 7-15. For plain iNES headers, a
    // non-zero tail means flags 7 is unreliable, so only the lower mapper nibble is trusted.
    const bool isNes2 = (header.flags7 & FLAG7_FORMAT_MASK) == FLAG7_FORMAT_NES2;
    const bool tailIsClean = header.padding[4] == 0 && header.padding[5] == 0 &&
                             header.padding[6] == 0 && header.padding[7] == 0;
    const auto lowerNibble = static_cast<std::uint8_t>(header.flags6 >> 4);
    const auto upperNibble = static_cast<std::uint8_t>(header.flags7 & 0xF0);
    mapperId = (isNes2 || tailIsClean) ? static_cast<std::uint8_t>(upperNibble | lowerNibble) : lowerNibble;

    // TV system. padding[] holds header bytes 8-15, so byte 9 = padding[1] and byte 12 = padding[4].
    if (isNes2) {
        // Byte 12 bits 0-1: 0 = NTSC, 1 = PAL, 2 = multi-region (runs on NTSC), 3 = Dendy (50 Hz,
        // closest to PAL timing).
        const int timing = header.padding[4] & 0x03;
        headerRegion = (timing == 1 || timing == 3) ? Region::Pal : Region::Ntsc;
    } else if (tailIsClean && (header.padding[1] & 0xFE) == 0 && (header.padding[1] & 0x01) != 0) {
        // iNES 1.0 byte 9 bit 0 = PAL; the other bits are reserved, so any junk means "don't trust it".
        // A clear bit is the default for most dumps and says nothing, so only PAL is reported.
        headerRegion = Region::Pal;
    }

    if (trainerPresent) {
        file.seekg(static_cast<std::streamoff>(TRAINER_SIZE), std::ios::cur);
    }

    prgRom.resize(prgBankCount * PRG_BANK_SIZE);
    readExact(file, prgRom.data(), prgRom.size(), "PRG-ROM");

    if (chrBankCount > 0) {
        chrMemory.resize(chrBankCount * CHR_BANK_SIZE);
        readExact(file, chrMemory.data(), chrMemory.size(), "CHR-ROM");
    } else {
        // No CHR-ROM: the board provides 8KB of CHR-RAM instead.
        chrMemory.resize(CHR_BANK_SIZE, 0);
    }

    switch (mapperId) {
    case 0:
        mapper = std::make_unique<Mapper_000>(prgBankCount, chrBankCount, headerMirroring);
        break;
    case 1:
        mapper = std::make_unique<Mapper_001>(prgBankCount, chrBankCount, headerMirroring);
        break;
    case 4:
        mapper = std::make_unique<Mapper_004>(prgBankCount, chrBankCount, headerMirroring);
        break;
    default:
        throw std::runtime_error("Mapper " + std::to_string(mapperId) + " (" + std::string(mapperName(mapperId)) +
                                 ") is not supported yet (supported: 0 NROM, 1 MMC1, 4 MMC3)");
    }
}

bool Cartridge::cpuRead(std::uint16_t addr, std::uint8_t& data) {
    if (addr >= PRG_ROM_START) {
        std::uint32_t mapped = 0;
        if (mapper->cpuMapRead(addr, mapped)) {
            data = prgRom[mapped];
            return true;
        }
        return false;
    }
    if (addr >= PRG_RAM_START && mapper->isPrgRamEnabled()) {
        data = prgRam[addr - PRG_RAM_START];
        return true;
    }
    return false; // Disabled PRG-RAM and $4020-$5FFF are open bus
}

bool Cartridge::cpuWrite(std::uint16_t addr, std::uint8_t data, std::uint64_t writeStamp) {
    if (addr >= PRG_ROM_START) {
        mapper->setCpuWriteStamp(writeStamp);
        std::uint32_t mapped = 0;
        if (mapper->cpuMapWrite(addr, mapped, data)) {
            prgRom[mapped] = data; // Only for boards with writable memory in this range
        }
        return true;
    }
    if (addr >= PRG_RAM_START) {
        if (mapper->isPrgRamEnabled()) {
            prgRam[addr - PRG_RAM_START] = data;
        }
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(std::uint16_t addr, std::uint8_t& data) {
    std::uint32_t mapped = 0;
    if (!mapper->ppuMapRead(addr, mapped)) {
        return false;
    }
    data = chrMemory[mapped];
    return true;
}

bool Cartridge::ppuWrite(std::uint16_t addr, std::uint8_t data) {
    if (addr > CHR_END) {
        return false;
    }
    std::uint32_t mapped = 0;
    if (mapper->ppuMapWrite(addr, mapped)) {
        chrMemory[mapped] = data;
    }
    return true; // CHR-ROM writes are swallowed
}
