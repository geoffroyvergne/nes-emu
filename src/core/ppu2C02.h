#pragma once

#include <array>
#include <cstdint>

#include "core/cartridge.h"

namespace nes {

// The NES's 2C02 picture processing unit: background + sprite rendering,
// scrolling, and vblank/NMI timing. Runs at 3x the CPU clock rate; the
// owning Bus is responsible for calling clock() 3 times per CPU cycle and
// for delivering an NMI to the CPU when nmiRequested() is true.
//
// Address-space and timing details (loopy scroll registers, the background
// shift-register pipeline, sprite evaluation, sprite-0 hit) follow the
// well-documented NESDev PPU rendering model.
class Ppu2C02 {
public:
    Ppu2C02();

    void connectCartridge(Cartridge* cartridge) { cartridge_ = cartridge; }
    void reset();

    // Advances exactly one PPU cycle (1/3 of a CPU cycle).
    void clock();

    // CPU-facing register interface ($2000-$2007, mirrored through $3FFF).
    uint8_t cpuRead(uint16_t addr);
    void cpuWrite(uint16_t addr, uint8_t data);

    // Used by the Bus to perform OAM DMA ($4014).
    void oamWrite(uint8_t index, uint8_t data) { oam_[index] = data; }

    bool frameComplete() const { return frameComplete_; }
    void clearFrameComplete() { frameComplete_ = false; }

    bool nmiRequested() const { return nmiRequested_; }
    void clearNmiRequest() { nmiRequested_ = false; }

    // 256x240 framebuffer, one packed 0x00RRGGBB value per pixel, row-major.
    const std::array<uint32_t, 256 * 240>& frameBuffer() const { return frame_; }

private:
    uint8_t ppuRead(uint16_t addr);
    void ppuWrite(uint16_t addr, uint8_t data);

    void incrementScrollX();
    void incrementScrollY();
    void transferAddressX();
    void transferAddressY();
    void loadBackgroundShifters();
    void updateShifters();

    Cartridge* cartridge_ = nullptr;

    std::array<std::array<uint8_t, 1024>, 2> nameTable_{};
    std::array<uint8_t, 32> paletteTable_{};
    std::array<uint8_t, 256> oam_{};

    // PPUCTRL ($2000) bits.
    static constexpr uint8_t kCtrlNametableX = 0x01;
    static constexpr uint8_t kCtrlNametableY = 0x02;
    static constexpr uint8_t kCtrlIncrementMode = 0x04;
    static constexpr uint8_t kCtrlPatternSprite = 0x08;
    static constexpr uint8_t kCtrlPatternBackground = 0x10;
    static constexpr uint8_t kCtrlSpriteSize = 0x20;
    static constexpr uint8_t kCtrlEnableNmi = 0x80;

    // PPUMASK ($2001) bits.
    static constexpr uint8_t kMaskGrayscale = 0x01;
    static constexpr uint8_t kMaskRenderBgLeft = 0x02;
    static constexpr uint8_t kMaskRenderSpritesLeft = 0x04;
    static constexpr uint8_t kMaskRenderBg = 0x08;
    static constexpr uint8_t kMaskRenderSprites = 0x10;

    // PPUSTATUS ($2002) bits.
    static constexpr uint8_t kStatusSpriteOverflow = 0x20;
    static constexpr uint8_t kStatusSpriteZeroHit = 0x40;
    static constexpr uint8_t kStatusVerticalBlank = 0x80;

    uint8_t control_ = 0;
    uint8_t mask_ = 0;
    uint8_t status_ = 0;
    uint8_t oamAddr_ = 0;

    // "Loopy" scroll registers: 15 meaningful bits laid out as
    // fine_y(3) | nametable_y(1) | nametable_x(1) | coarse_y(5) | coarse_x(5).
    uint16_t vramAddr_ = 0;
    uint16_t tramAddr_ = 0;
    uint8_t fineX_ = 0;
    bool addressLatch_ = false;
    uint8_t dataBuffer_ = 0;

    int32_t scanline_ = -1;
    int32_t cycle_ = 0;
    bool oddFrame_ = false;

    uint8_t bgNextTileId_ = 0;
    uint8_t bgNextTileAttrib_ = 0;
    uint8_t bgNextTileLsb_ = 0;
    uint8_t bgNextTileMsb_ = 0;
    uint16_t bgShifterPatternLo_ = 0;
    uint16_t bgShifterPatternHi_ = 0;
    uint16_t bgShifterAttribLo_ = 0;
    uint16_t bgShifterAttribHi_ = 0;

    struct SpriteEntry {
        uint8_t y = 0xFF;
        uint8_t id = 0xFF;
        uint8_t attribute = 0xFF;
        uint8_t x = 0xFF;
    };
    std::array<SpriteEntry, 8> spritesOnScanline_{};
    uint8_t spriteCount_ = 0;
    std::array<uint8_t, 8> spriteShifterPatternLo_{};
    std::array<uint8_t, 8> spriteShifterPatternHi_{};
    bool spriteZeroHitPossible_ = false;
    bool spriteZeroBeingRendered_ = false;

    bool frameComplete_ = false;
    bool nmiRequested_ = false;

    std::array<uint32_t, 256 * 240> frame_{};

    static const std::array<uint32_t, 64> kPalette;
};

} // namespace nes
