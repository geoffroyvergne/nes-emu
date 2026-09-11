#include "core/cartridge.h"

#include <fstream>
#include <stdexcept>

#include "core/mappers/mapper_nrom.h"

namespace nes {

namespace {

constexpr size_t kHeaderSize = 16;
constexpr size_t kTrainerSize = 512;
constexpr size_t kPrgBankSize = 16384;
constexpr size_t kChrBankSize = 8192;

} // namespace

Cartridge::Cartridge(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Cartridge: could not open ROM file: " + path);
    }

    std::vector<uint8_t> header(kHeaderSize);
    file.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
    if (!file || header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        throw std::runtime_error("Cartridge: not a valid iNES ROM (bad header): " + path);
    }

    prgBanks16k_ = header[4];
    chrBanks8k_ = header[5];
    const uint8_t flags6 = header[6];
    const uint8_t flags7 = header[7];

    if (prgBanks16k_ == 0) {
        throw std::runtime_error("Cartridge: ROM declares zero PRG ROM banks: " + path);
    }

    mapperId_ = static_cast<uint8_t>((flags7 & 0xF0) | (flags6 >> 4));

    Mirroring mirroring;
    if (flags6 & 0x08) {
        mirroring = Mirroring::FourScreen;
    } else {
        mirroring = (flags6 & 0x01) ? Mirroring::Vertical : Mirroring::Horizontal;
    }

    if (flags6 & 0x04) {
        // Trainer present: 512 bytes we don't use, but must skip.
        file.seekg(static_cast<std::streamoff>(kTrainerSize), std::ios::cur);
    }

    prgRom_.resize(static_cast<size_t>(prgBanks16k_) * kPrgBankSize);
    file.read(reinterpret_cast<char*>(prgRom_.data()), static_cast<std::streamsize>(prgRom_.size()));
    if (!file) {
        throw std::runtime_error("Cartridge: ROM file truncated (PRG ROM): " + path);
    }

    if (chrBanks8k_ > 0) {
        chrMem_.resize(static_cast<size_t>(chrBanks8k_) * kChrBankSize);
        file.read(reinterpret_cast<char*>(chrMem_.data()), static_cast<std::streamsize>(chrMem_.size()));
        if (!file) {
            throw std::runtime_error("Cartridge: ROM file truncated (CHR ROM): " + path);
        }
    } else {
        // No CHR ROM banks: cartridge uses 8KB of CHR RAM instead.
        chrMem_.resize(kChrBankSize, 0);
    }

    switch (mapperId_) {
        case 0:
            mapper_ = std::make_unique<MapperNrom>(prgBanks16k_, chrBanks8k_, mirroring);
            break;
        default:
            throw std::runtime_error("Cartridge: unsupported mapper " + std::to_string(mapperId_) +
                                      " (only NROM/0 is implemented so far): " + path);
    }
}

bool Cartridge::cpuRead(uint16_t addr, uint8_t& value) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        value = prgRam_[addr - 0x6000];
        return true;
    }
    if (addr >= 0x8000) {
        uint32_t mapped = 0;
        if (mapper_->cpuMapRead(addr, mapped) && mapped < prgRom_.size()) {
            value = prgRom_[mapped];
            return true;
        }
    }
    return false;
}

bool Cartridge::cpuWrite(uint16_t addr, uint8_t value) {
    if (addr >= 0x6000 && addr <= 0x7FFF) {
        prgRam_[addr - 0x6000] = value;
        return true;
    }
    if (addr >= 0x8000) {
        uint32_t mapped = 0;
        // Whether or not the mapper claims a PRG-RAM-backed address, the
        // cartridge owns this whole range; bank-switch registers are applied
        // as a side effect inside the mapper itself.
        mapper_->cpuMapWrite(addr, mapped, value);
        return true;
    }
    return false;
}

bool Cartridge::ppuRead(uint16_t addr, uint8_t& value) {
    uint32_t mapped = 0;
    if (mapper_->ppuMapRead(addr, mapped) && mapped < chrMem_.size()) {
        value = chrMem_[mapped];
        return true;
    }
    return false;
}

bool Cartridge::ppuWrite(uint16_t addr, uint8_t value) {
    uint32_t mapped = 0;
    if (mapper_->ppuMapWrite(addr, mapped) && mapped < chrMem_.size()) {
        chrMem_[mapped] = value;
        return true;
    }
    return false;
}

} // namespace nes
