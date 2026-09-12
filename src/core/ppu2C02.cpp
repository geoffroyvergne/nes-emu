#include "core/ppu2C02.h"

#include "core/state_io.h"

namespace nes {

// Standard NES/2C02 NTSC palette, 64 entries, packed as 0x00RRGGBB.
const std::array<uint32_t, 64> Ppu2C02::kPalette = {{
    0x545454, 0x001E74, 0x081090, 0x300088, 0x440064, 0x5C0030, 0x540400, 0x3C1800,
    0x202A00, 0x083A00, 0x004000, 0x003C00, 0x00323C, 0x000000, 0x000000, 0x000000,
    0x989698, 0x084CC4, 0x3032EC, 0x5C1EE4, 0x8814B0, 0xA01464, 0x982220, 0x783C00,
    0x545A00, 0x287200, 0x087C00, 0x007628, 0x006678, 0x000000, 0x000000, 0x000000,
    0xECEEEC, 0x4C9AEC, 0x787CEC, 0xB062EC, 0xE454EC, 0xEC58B4, 0xEC6A64, 0xD48820,
    0xA0AA00, 0x74C400, 0x4CD020, 0x38CC6C, 0x38B4CC, 0x3C3C3C, 0x000000, 0x000000,
    0xECEEEC, 0xA8CCEC, 0xBCBCEC, 0xD4B2EC, 0xECAEEC, 0xECAED4, 0xECB4B0, 0xE4C490,
    0xCCD278, 0xB4DE78, 0xA8E290, 0x98E2B4, 0xA0D6E4, 0xA0A2A0, 0x000000, 0x000000,
}};

Ppu2C02::Ppu2C02() { frame_.fill(0); }

void Ppu2C02::setRegion(bool isPal) {
    scanlinesPerFrame_ = isPal ? 312 : 262;
    oddFrameSkipEnabled_ = !isPal;
}

void Ppu2C02::reset() {
    control_ = 0;
    mask_ = 0;
    status_ = 0;
    oamAddr_ = 0;
    vramAddr_ = 0;
    tramAddr_ = 0;
    fineX_ = 0;
    addressLatch_ = false;
    dataBuffer_ = 0;
    scanline_ = -1;
    cycle_ = 0;
    oddFrame_ = false;
    bgNextTileId_ = bgNextTileAttrib_ = bgNextTileLsb_ = bgNextTileMsb_ = 0;
    bgShifterPatternLo_ = bgShifterPatternHi_ = 0;
    bgShifterAttribLo_ = bgShifterAttribHi_ = 0;
    frameComplete_ = false;
    nmiRequested_ = false;
}

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

uint8_t Ppu2C02::ppuRead(uint16_t addr) {
    addr &= 0x3FFF;

    if (addr <= 0x1FFF) {
        uint8_t value = 0;
        if (cartridge_) cartridge_->ppuRead(addr, value);
        return value;
    }
    if (addr <= 0x3EFF) {
        uint16_t nt = addr & 0x0FFF;
        uint8_t quadrant = static_cast<uint8_t>(nt / 0x400);
        uint16_t offset = nt & 0x03FF;
        Mirroring mirroring = cartridge_ ? cartridge_->mirroring() : Mirroring::Horizontal;
        uint8_t table;
        switch (mirroring) {
            case Mirroring::Vertical: table = quadrant & 1; break;
            case Mirroring::SingleScreenLow: table = 0; break;
            case Mirroring::SingleScreenHigh: table = 1; break;
            case Mirroring::Horizontal:
            case Mirroring::FourScreen:
            default: table = quadrant >> 1; break;
        }
        return nameTable_[table][offset];
    }

    uint16_t p = addr & 0x001F;
    if (p == 0x10 || p == 0x14 || p == 0x18 || p == 0x1C) p &= 0x0F;
    uint8_t value = paletteTable_[p];
    if (mask_ & kMaskGrayscale) value &= 0x30;
    return value;
}

void Ppu2C02::ppuWrite(uint16_t addr, uint8_t data) {
    addr &= 0x3FFF;

    if (addr <= 0x1FFF) {
        if (cartridge_) cartridge_->ppuWrite(addr, data);
        return;
    }
    if (addr <= 0x3EFF) {
        uint16_t nt = addr & 0x0FFF;
        uint8_t quadrant = static_cast<uint8_t>(nt / 0x400);
        uint16_t offset = nt & 0x03FF;
        Mirroring mirroring = cartridge_ ? cartridge_->mirroring() : Mirroring::Horizontal;
        uint8_t table;
        switch (mirroring) {
            case Mirroring::Vertical: table = quadrant & 1; break;
            case Mirroring::SingleScreenLow: table = 0; break;
            case Mirroring::SingleScreenHigh: table = 1; break;
            case Mirroring::Horizontal:
            case Mirroring::FourScreen:
            default: table = quadrant >> 1; break;
        }
        nameTable_[table][offset] = data;
        return;
    }

    uint16_t p = addr & 0x001F;
    if (p == 0x10 || p == 0x14 || p == 0x18 || p == 0x1C) p &= 0x0F;
    paletteTable_[p] = data;
}

// ---------------------------------------------------------------------------
// CPU-facing registers
// ---------------------------------------------------------------------------

uint8_t Ppu2C02::cpuRead(uint16_t addr) {
    switch (addr & 0x0007) {
        case 0x0002: { // PPUSTATUS
            uint8_t data = (status_ & 0xE0) | (dataBuffer_ & 0x1F);
            status_ &= static_cast<uint8_t>(~kStatusVerticalBlank);
            addressLatch_ = false;
            return data;
        }
        case 0x0004: // OAMDATA
            return oam_[oamAddr_];
        case 0x0007: { // PPUDATA
            uint8_t data = dataBuffer_;
            dataBuffer_ = ppuRead(vramAddr_);
            if (vramAddr_ >= 0x3F00) data = dataBuffer_;
            vramAddr_ = static_cast<uint16_t>(vramAddr_ + ((control_ & kCtrlIncrementMode) ? 32 : 1));
            return data;
        }
        default:
            return 0;
    }
}

void Ppu2C02::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr & 0x0007) {
        case 0x0000: // PPUCTRL
            control_ = data;
            tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0xF3FF) | ((data & 0x03) << 10));
            break;
        case 0x0001: // PPUMASK
            mask_ = data;
            break;
        case 0x0003: // OAMADDR
            oamAddr_ = data;
            break;
        case 0x0004: // OAMDATA
            oam_[oamAddr_++] = data;
            break;
        case 0x0005: // PPUSCROLL
            if (!addressLatch_) {
                fineX_ = data & 0x07;
                tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0xFFE0) | (data >> 3));
                addressLatch_ = true;
            } else {
                tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0x8FFF) | ((data & 0x07) << 12));
                tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0xFC1F) | ((data & 0xF8) << 2));
                addressLatch_ = false;
            }
            break;
        case 0x0006: // PPUADDR
            if (!addressLatch_) {
                tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0x00FF) | ((data & 0x3F) << 8));
                addressLatch_ = true;
            } else {
                tramAddr_ = static_cast<uint16_t>((tramAddr_ & 0xFF00) | data);
                vramAddr_ = tramAddr_;
                addressLatch_ = false;
            }
            break;
        case 0x0007: // PPUDATA
            ppuWrite(vramAddr_, data);
            vramAddr_ = static_cast<uint16_t>(vramAddr_ + ((control_ & kCtrlIncrementMode) ? 32 : 1));
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// Scroll register helpers (see NESDev "PPU scrolling" for the bit layout)
// ---------------------------------------------------------------------------

void Ppu2C02::incrementScrollX() {
    if (!(mask_ & (kMaskRenderBg | kMaskRenderSprites))) return;
    if ((vramAddr_ & 0x001F) == 31) {
        vramAddr_ &= ~0x001F;
        vramAddr_ ^= 0x0400; // flip nametable_x
    } else {
        vramAddr_++;
    }
}

void Ppu2C02::incrementScrollY() {
    if (!(mask_ & (kMaskRenderBg | kMaskRenderSprites))) return;
    if ((vramAddr_ & 0x7000) != 0x7000) {
        vramAddr_ = static_cast<uint16_t>(vramAddr_ + 0x1000);
    } else {
        vramAddr_ &= ~0x7000;
        uint16_t y = (vramAddr_ & 0x03E0) >> 5;
        if (y == 29) {
            y = 0;
            vramAddr_ ^= 0x0800; // flip nametable_y
        } else if (y == 31) {
            y = 0;
        } else {
            y++;
        }
        vramAddr_ = static_cast<uint16_t>((vramAddr_ & ~0x03E0) | (y << 5));
    }
}

void Ppu2C02::transferAddressX() {
    if (!(mask_ & (kMaskRenderBg | kMaskRenderSprites))) return;
    vramAddr_ = static_cast<uint16_t>((vramAddr_ & ~0x041F) | (tramAddr_ & 0x041F));
}

void Ppu2C02::transferAddressY() {
    if (!(mask_ & (kMaskRenderBg | kMaskRenderSprites))) return;
    vramAddr_ = static_cast<uint16_t>((vramAddr_ & ~0x7BE0) | (tramAddr_ & 0x7BE0));
}

void Ppu2C02::loadBackgroundShifters() {
    bgShifterPatternLo_ = static_cast<uint16_t>((bgShifterPatternLo_ & 0xFF00) | bgNextTileLsb_);
    bgShifterPatternHi_ = static_cast<uint16_t>((bgShifterPatternHi_ & 0xFF00) | bgNextTileMsb_);
    bgShifterAttribLo_ = static_cast<uint16_t>((bgShifterAttribLo_ & 0xFF00) | ((bgNextTileAttrib_ & 0b01) ? 0xFF : 0x00));
    bgShifterAttribHi_ = static_cast<uint16_t>((bgShifterAttribHi_ & 0xFF00) | ((bgNextTileAttrib_ & 0b10) ? 0xFF : 0x00));
}

void Ppu2C02::updateShifters() {
    if (mask_ & kMaskRenderBg) {
        bgShifterPatternLo_ <<= 1;
        bgShifterPatternHi_ <<= 1;
        bgShifterAttribLo_ <<= 1;
        bgShifterAttribHi_ <<= 1;
    }
    if ((mask_ & kMaskRenderSprites) && cycle_ >= 1 && cycle_ < 258) {
        for (uint8_t i = 0; i < spriteCount_; i++) {
            if (spritesOnScanline_[i].x > 0) {
                spritesOnScanline_[i].x--;
            } else {
                spriteShifterPatternLo_[i] = static_cast<uint8_t>(spriteShifterPatternLo_[i] << 1);
                spriteShifterPatternHi_[i] = static_cast<uint8_t>(spriteShifterPatternHi_[i] << 1);
            }
        }
    }
}

// Pattern-table address for sprite `spriteIndex`'s low bitplane on the
// current scanline (high bitplane is this address + 8).
uint16_t Ppu2C02::spritePatternAddressLo(uint8_t spriteIndex) const {
    const SpriteEntry& sprite = spritesOnScanline_[spriteIndex];
    uint8_t spriteHeight = (control_ & kCtrlSpriteSize) ? 16 : 8;
    bool flipV = sprite.attribute & 0x80;
    int16_t rowInSprite = static_cast<int16_t>(scanline_) - static_cast<int16_t>(sprite.y);

    if (spriteHeight == 8) {
        uint16_t base = (control_ & kCtrlPatternSprite) ? 0x1000 : 0x0000;
        uint8_t row = flipV ? static_cast<uint8_t>(7 - rowInSprite) : static_cast<uint8_t>(rowInSprite);
        return static_cast<uint16_t>(base | (sprite.id << 4) | row);
    }
    uint16_t base = (sprite.id & 0x01) ? 0x1000 : 0x0000;
    uint8_t tile = sprite.id & 0xFE;
    uint8_t row = static_cast<uint8_t>(rowInSprite);
    if (flipV) row = static_cast<uint8_t>(15 - rowInSprite);
    if (row >= 8) {
        tile++;
        row -= 8;
    }
    return static_cast<uint16_t>(base | (tile << 4) | row);
}

// ---------------------------------------------------------------------------
// Main clock
// ---------------------------------------------------------------------------

void Ppu2C02::clock() {
    if (scanline_ >= -1 && scanline_ < 240) {
        if (scanline_ == 0 && cycle_ == 0 && oddFrame_ && oddFrameSkipEnabled_ &&
            (mask_ & (kMaskRenderBg | kMaskRenderSprites))) {
            cycle_ = 1; // Odd-frame cycle skip (NTSC only - PAL's PPU:CPU ratio doesn't need it).
        }

        if (scanline_ == -1 && cycle_ == 1) {
            status_ &= static_cast<uint8_t>(~(kStatusVerticalBlank | kStatusSpriteZeroHit | kStatusSpriteOverflow));
            for (auto& lo : spriteShifterPatternLo_) lo = 0;
            for (auto& hi : spriteShifterPatternHi_) hi = 0;
        }

        if ((cycle_ >= 2 && cycle_ < 258) || (cycle_ >= 321 && cycle_ < 338)) {
            updateShifters();
            switch ((cycle_ - 1) % 8) {
                case 0:
                    loadBackgroundShifters();
                    bgNextTileId_ = ppuRead(static_cast<uint16_t>(0x2000 | (vramAddr_ & 0x0FFF)));
                    break;
                case 2: {
                    uint16_t coarseX = vramAddr_ & 0x001F;
                    uint16_t coarseY = (vramAddr_ >> 5) & 0x001F;
                    uint16_t nametableBits = vramAddr_ & 0x0C00;
                    uint16_t attrAddr = static_cast<uint16_t>(0x23C0 | nametableBits | ((coarseY >> 2) << 3) | (coarseX >> 2));
                    bgNextTileAttrib_ = ppuRead(attrAddr);
                    if (coarseY & 0x02) bgNextTileAttrib_ >>= 4;
                    if (coarseX & 0x02) bgNextTileAttrib_ >>= 2;
                    bgNextTileAttrib_ &= 0x03;
                    break;
                }
                case 4: {
                    uint16_t fineY = (vramAddr_ >> 12) & 0x07;
                    uint16_t base = (control_ & kCtrlPatternBackground) ? 0x1000 : 0x0000;
                    bgNextTileLsb_ = ppuRead(static_cast<uint16_t>(base + (bgNextTileId_ << 4) + fineY));
                    break;
                }
                case 6: {
                    uint16_t fineY = (vramAddr_ >> 12) & 0x07;
                    uint16_t base = (control_ & kCtrlPatternBackground) ? 0x1000 : 0x0000;
                    bgNextTileMsb_ = ppuRead(static_cast<uint16_t>(base + (bgNextTileId_ << 4) + fineY + 8));
                    break;
                }
                case 7:
                    incrementScrollX();
                    break;
                default:
                    break;
            }
        }

        if (cycle_ == 256) incrementScrollY();
        if (cycle_ == 257) {
            loadBackgroundShifters();
            transferAddressX();
        }
        if (cycle_ == 338 || cycle_ == 340) {
            bgNextTileId_ = ppuRead(static_cast<uint16_t>(0x2000 | (vramAddr_ & 0x0FFF)));
        }
        if (scanline_ == -1 && cycle_ >= 280 && cycle_ < 305) {
            transferAddressY();
        }

        // MMC3-style mappers clock their scanline IRQ counter from PPU
        // address bus bit 12 (A12) rising edges, which happen naturally
        // around this point in the scanline as the PPU shifts from
        // background to sprite pattern-table fetches. Cycle 260 is a
        // well-established fixed-point approximation of that transition.
        if (cycle_ == 260 && (mask_ & (kMaskRenderBg | kMaskRenderSprites)) && cartridge_) {
            cartridge_->scanlineTick();
        }

        // Sprite evaluation for the NEXT scanline.
        if (cycle_ == 257 && scanline_ >= 0) {
            spritesOnScanline_.fill(SpriteEntry{});
            spriteCount_ = 0;
            spriteZeroHitPossible_ = false;

            uint8_t spriteHeight = (control_ & kCtrlSpriteSize) ? 16 : 8;
            uint8_t n = 0;
            while (n < 64 && spriteCount_ < 9) {
                int16_t diff = static_cast<int16_t>(scanline_) - static_cast<int16_t>(oam_[n * 4 + 0]);
                if (diff >= 0 && diff < spriteHeight) {
                    if (spriteCount_ < 8) {
                        if (n == 0) spriteZeroHitPossible_ = true;
                        spritesOnScanline_[spriteCount_].y = oam_[n * 4 + 0];
                        spritesOnScanline_[spriteCount_].id = oam_[n * 4 + 1];
                        spritesOnScanline_[spriteCount_].attribute = oam_[n * 4 + 2];
                        spritesOnScanline_[spriteCount_].x = oam_[n * 4 + 3];
                        spriteCount_++;
                    }
                }
                n++;
            }
            status_ = static_cast<uint8_t>((status_ & ~kStatusSpriteOverflow) | (spriteCount_ > 8 ? kStatusSpriteOverflow : 0));
        }

        if (cycle_ == 340) {
            for (uint8_t i = 0; i < spriteCount_; i++) {
                uint16_t patternAddrLo = spritePatternAddressLo(i);
                bool flipH = spritesOnScanline_[i].attribute & 0x40;

                uint8_t lo = ppuRead(patternAddrLo);
                uint8_t hi = ppuRead(static_cast<uint16_t>(patternAddrLo + 8));

                if (flipH) {
                    auto flipByte = [](uint8_t b) {
                        b = static_cast<uint8_t>((b & 0xF0) >> 4 | (b & 0x0F) << 4);
                        b = static_cast<uint8_t>((b & 0xCC) >> 2 | (b & 0x33) << 2);
                        b = static_cast<uint8_t>((b & 0xAA) >> 1 | (b & 0x55) << 1);
                        return b;
                    };
                    lo = flipByte(lo);
                    hi = flipByte(hi);
                }
                spriteShifterPatternLo_[i] = lo;
                spriteShifterPatternHi_[i] = hi;
            }
        }
    }

    if (scanline_ == 241 && cycle_ == 1) {
        status_ |= kStatusVerticalBlank;
        if (control_ & kCtrlEnableNmi) nmiRequested_ = true;
    }

    // Compositing.
    uint8_t bgPixel = 0, bgPalette = 0;
    if (mask_ & kMaskRenderBg) {
        uint16_t bitMux = static_cast<uint16_t>(0x8000 >> fineX_);
        uint8_t p0 = (bgShifterPatternLo_ & bitMux) ? 1 : 0;
        uint8_t p1 = (bgShifterPatternHi_ & bitMux) ? 1 : 0;
        bgPixel = static_cast<uint8_t>((p1 << 1) | p0);
        uint8_t pal0 = (bgShifterAttribLo_ & bitMux) ? 1 : 0;
        uint8_t pal1 = (bgShifterAttribHi_ & bitMux) ? 1 : 0;
        bgPalette = static_cast<uint8_t>((pal1 << 1) | pal0);
    }

    uint8_t fgPixel = 0, fgPalette = 0, fgPriority = 0;
    if (mask_ & kMaskRenderSprites) {
        spriteZeroBeingRendered_ = false;
        for (uint8_t i = 0; i < spriteCount_; i++) {
            if (spritesOnScanline_[i].x == 0) {
                uint8_t lo = (spriteShifterPatternLo_[i] & 0x80) ? 1 : 0;
                uint8_t hi = (spriteShifterPatternHi_[i] & 0x80) ? 1 : 0;
                fgPixel = static_cast<uint8_t>((hi << 1) | lo);
                fgPalette = static_cast<uint8_t>((spritesOnScanline_[i].attribute & 0x03) + 4);
                fgPriority = (spritesOnScanline_[i].attribute & 0x20) == 0;
                if (fgPixel != 0) {
                    if (i == 0) spriteZeroBeingRendered_ = true;
                    break;
                }
            }
        }
    }

    uint8_t pixel = 0, palette = 0;
    if (bgPixel != 0 || fgPixel != 0) {
        if (bgPixel == 0) {
            pixel = fgPixel;
            palette = fgPalette;
        } else if (fgPixel == 0) {
            pixel = bgPixel;
            palette = bgPalette;
        } else if (fgPriority) {
            pixel = fgPixel;
            palette = fgPalette;
        } else {
            pixel = bgPixel;
            palette = bgPalette;
        }

        if (spriteZeroHitPossible_ && spriteZeroBeingRendered_ && bgPixel != 0 && fgPixel != 0 &&
            (mask_ & kMaskRenderBg) && (mask_ & kMaskRenderSprites)) {
            bool leftClip = !(mask_ & kMaskRenderBgLeft) || !(mask_ & kMaskRenderSpritesLeft);
            if (leftClip ? (cycle_ >= 9 && cycle_ < 258) : (cycle_ >= 1 && cycle_ < 258)) {
                status_ |= kStatusSpriteZeroHit;
            }
        }
    }

    if (cycle_ >= 1 && cycle_ <= 256 && scanline_ >= 0 && scanline_ < 240) {
        uint8_t colorIndex = ppuRead(static_cast<uint16_t>(0x3F00 + (palette << 2) + pixel)) & 0x3F;
        frame_[static_cast<size_t>(scanline_) * 256 + (cycle_ - 1)] = kPalette[colorIndex];
    }

    cycle_++;
    if (cycle_ >= 341) {
        cycle_ = 0;
        scanline_++;
        if (scanline_ >= scanlinesPerFrame_ - 1) {
            scanline_ = -1;
            frameComplete_ = true;
            oddFrame_ = !oddFrame_;
        }
    }
}

void Ppu2C02::saveState(StateWriter& w) const {
    w.writeArray(nameTable_);
    w.writeArray(paletteTable_);
    w.writeArray(oam_);
    w.write(control_);
    w.write(mask_);
    w.write(status_);
    w.write(oamAddr_);
    w.write(vramAddr_);
    w.write(tramAddr_);
    w.write(fineX_);
    w.write(addressLatch_);
    w.write(dataBuffer_);
    w.write(scanline_);
    w.write(cycle_);
    w.write(oddFrame_);
    w.write(bgNextTileId_);
    w.write(bgNextTileAttrib_);
    w.write(bgNextTileLsb_);
    w.write(bgNextTileMsb_);
    w.write(bgShifterPatternLo_);
    w.write(bgShifterPatternHi_);
    w.write(bgShifterAttribLo_);
    w.write(bgShifterAttribHi_);
    w.writeArray(spritesOnScanline_);
    w.write(spriteCount_);
    w.writeArray(spriteShifterPatternLo_);
    w.writeArray(spriteShifterPatternHi_);
    w.write(spriteZeroHitPossible_);
    w.write(spriteZeroBeingRendered_);
    w.write(frameComplete_);
    w.write(nmiRequested_);
    w.writeArray(frame_);
}

void Ppu2C02::loadState(StateReader& r) {
    r.readArray(nameTable_);
    r.readArray(paletteTable_);
    r.readArray(oam_);
    control_ = r.read<uint8_t>();
    mask_ = r.read<uint8_t>();
    status_ = r.read<uint8_t>();
    oamAddr_ = r.read<uint8_t>();
    vramAddr_ = r.read<uint16_t>();
    tramAddr_ = r.read<uint16_t>();
    fineX_ = r.read<uint8_t>();
    addressLatch_ = r.read<bool>();
    dataBuffer_ = r.read<uint8_t>();
    scanline_ = r.read<int32_t>();
    cycle_ = r.read<int32_t>();
    oddFrame_ = r.read<bool>();
    bgNextTileId_ = r.read<uint8_t>();
    bgNextTileAttrib_ = r.read<uint8_t>();
    bgNextTileLsb_ = r.read<uint8_t>();
    bgNextTileMsb_ = r.read<uint8_t>();
    bgShifterPatternLo_ = r.read<uint16_t>();
    bgShifterPatternHi_ = r.read<uint16_t>();
    bgShifterAttribLo_ = r.read<uint16_t>();
    bgShifterAttribHi_ = r.read<uint16_t>();
    r.readArray(spritesOnScanline_);
    spriteCount_ = r.read<uint8_t>();
    r.readArray(spriteShifterPatternLo_);
    r.readArray(spriteShifterPatternHi_);
    spriteZeroHitPossible_ = r.read<bool>();
    spriteZeroBeingRendered_ = r.read<bool>();
    frameComplete_ = r.read<bool>();
    nmiRequested_ = r.read<bool>();
    r.readArray(frame_);
}

} // namespace nes
