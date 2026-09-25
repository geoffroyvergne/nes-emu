#include "Ppu2C02.hpp"

#include "Cartridge.hpp"

#include <algorithm>
#include <utility>

namespace {

// CPU register numbers ($2000 + n)
constexpr std::uint16_t PPUCTRL = 0;
constexpr std::uint16_t PPUMASK = 1;
constexpr std::uint16_t PPUSTATUS = 2;
constexpr std::uint16_t OAMADDR = 3;
constexpr std::uint16_t OAMDATA = 4;
constexpr std::uint16_t PPUSCROLL = 5;
constexpr std::uint16_t PPUADDR = 6;
constexpr std::uint16_t PPUDATA = 7;

constexpr std::uint16_t PPU_ADDR_MASK = 0x3FFF;
constexpr std::uint16_t VRAM_ADDR_MASK = 0x7FFF;
constexpr std::uint16_t PATTERN_TABLE_END = 0x1FFF;
constexpr std::uint16_t NAMETABLE_END = 0x3EFF;
constexpr std::uint16_t PALETTE_START = 0x3F00;

constexpr std::uint16_t NAMETABLE_SIZE = Ppu2C02::NAMETABLE_BYTES;
constexpr std::uint8_t GRAYSCALE_MASK = 0x30; // PPUMASK grayscale keeps only the luma column of the system palette

using Color = Ppu2C02::Color;

// 2C02 system palette (the widely used NTSC palette from the nesdev wiki), indexed by the 6-bit
// values stored in palette RAM. Rows are luma levels $0x-$3x; columns are hues.
// $0D is "blacker than black" and $xE/$xF are black on real hardware.
constexpr std::array<Color, Ppu2C02::SYSTEM_PALETTE_SIZE> SYSTEM_PALETTE = {{
    {84, 84, 84},    {0, 30, 116},    {8, 16, 144},    {48, 0, 136},    {68, 0, 100},    {92, 0, 48},
    {84, 4, 0},      {60, 24, 0},     {32, 42, 0},     {8, 58, 0},      {0, 64, 0},      {0, 60, 0},
    {0, 50, 60},     {0, 0, 0},       {0, 0, 0},       {0, 0, 0},

    {152, 150, 152}, {8, 76, 196},    {48, 50, 236},   {92, 30, 228},   {136, 20, 176},  {160, 20, 100},
    {152, 34, 32},   {120, 60, 0},    {84, 90, 0},     {40, 114, 0},    {8, 124, 0},     {0, 118, 40},
    {0, 102, 120},   {0, 0, 0},       {0, 0, 0},       {0, 0, 0},

    {236, 238, 236}, {76, 154, 236},  {120, 124, 236}, {176, 98, 236},  {228, 84, 236},  {236, 88, 180},
    {236, 106, 100}, {212, 136, 32},  {160, 170, 0},   {116, 196, 0},   {76, 208, 32},   {56, 204, 108},
    {56, 180, 204},  {60, 60, 60},    {0, 0, 0},       {0, 0, 0},

    {236, 238, 236}, {168, 204, 236}, {188, 188, 236}, {212, 178, 236}, {236, 174, 236}, {236, 174, 212},
    {236, 180, 176}, {228, 196, 144}, {204, 210, 120}, {180, 222, 120}, {168, 226, 144}, {152, 226, 180},
    {160, 214, 228}, {160, 162, 160}, {0, 0, 0},       {0, 0, 0},
}};

constexpr std::uint8_t PALETTE_ENTRY_MASK = 0x3F; // Palette RAM stores 6-bit system palette indexes

// Masks for fields of the v/t loopy registers.
constexpr std::uint16_t LOOPY_COARSE_X = 0x001F;
constexpr std::uint16_t LOOPY_COARSE_Y = 0x03E0;
constexpr std::uint16_t LOOPY_NAMETABLE = 0x0C00;
constexpr std::uint16_t LOOPY_FINE_Y = 0x7000;

} // namespace

void Ppu2C02::connectCartridge(std::shared_ptr<Cartridge> newCartridge) {
    cartridge = std::move(newCartridge);
}

void Ppu2C02::setRegion(const RegionTiming& timing) {
    scanlinesPerFrame = timing.scanlinesPerFrame;
    skipsOddFrameDot = timing.skipsOddFrameDot;
}

void Ppu2C02::reset() {
    ctrl = 0x00;
    mask = 0x00;
    status = 0x00;
    oamAddr = 0x00;
    vramAddr = 0x0000;
    tempVramAddr = 0x0000;
    fineX = 0x00;
    writeToggle = false;
    readBuffer = 0x00;
    ioLatch = 0x00;
    scanline = 0;
    cycle = 0;
    nmiRequested = false;
    frameComplete = false;
    oddFrame = false;
    sprite0HitCycle = -1;
}

// ---------------------------------------------------------------------------------------------
// CPU-facing registers

std::uint8_t Ppu2C02::cpuRead(std::uint16_t addr, bool readOnly) {
    switch (addr & 0x0007) {
    case PPUSTATUS: {
        // Only the top 3 bits are driven; the rest is whatever was last on the PPU bus.
        const auto result = static_cast<std::uint8_t>((status & 0xE0) | (ioLatch & 0x1F));
        if (!readOnly) {
            status &= static_cast<std::uint8_t>(~STATUS_VBLANK);
            writeToggle = false;
            ioLatch = result;
        }
        return result;
    }
    case OAMDATA: {
        const std::uint8_t result = oamMemory[oamAddr];
        if (!readOnly) {
            ioLatch = result;
        }
        return result;
    }
    case PPUDATA: {
        const std::uint16_t addr14 = vramAddr & PPU_ADDR_MASK;
        if (readOnly) {
            return addr14 >= PALETTE_START ? tblPalette[paletteIndex(addr14)] : readBuffer;
        }
        std::uint8_t result;
        if (addr14 >= PALETTE_START) {
            // Palette reads are immediate; the buffer is filled with the nametable byte "under" the palette.
            result = static_cast<std::uint8_t>((ppuRead(addr14) & 0x3F) | (ioLatch & 0xC0));
            readBuffer = ppuRead(static_cast<std::uint16_t>(addr14 - 0x1000));
        } else {
            // Everything else returns the previous read's value: the first read after
            // setting $2006 is a dummy read.
            result = readBuffer;
            readBuffer = ppuRead(addr14);
        }
        incrementVramAddr();
        ioLatch = result;
        return result;
    }
    default:
        // PPUCTRL, PPUMASK, OAMADDR, PPUSCROLL and PPUADDR are write-only.
        return ioLatch;
    }
}

void Ppu2C02::cpuWrite(std::uint16_t addr, std::uint8_t data) {
    ioLatch = data;

    switch (addr & 0x0007) {
    case PPUCTRL: {
        const bool nmiWasEnabled = (ctrl & CTRL_NMI_ENABLE) != 0;
        ctrl = data;
        // t: ...NN.. ........ <- d: ......NN
        tempVramAddr = static_cast<std::uint16_t>((tempVramAddr & ~LOOPY_NAMETABLE) | ((data & CTRL_NAMETABLE) << 10));
        // Enabling NMI while already in VBlank fires an NMI immediately.
        if (!nmiWasEnabled && (ctrl & CTRL_NMI_ENABLE) != 0 && (status & STATUS_VBLANK) != 0) {
            nmiRequested = true;
        }
        break;
    }
    case PPUMASK:
        mask = data;
        break;
    case PPUSTATUS:
        break; // Read-only
    case OAMADDR:
        oamAddr = data;
        break;
    case OAMDATA:
        oamMemory[oamAddr] = data;
        ++oamAddr;
        break;
    case PPUSCROLL:
        if (!writeToggle) {
            // First write, X scroll: t: ....... ...XXXXX <- d: XXXXX...; x <- d: .....xxx
            tempVramAddr = static_cast<std::uint16_t>((tempVramAddr & ~LOOPY_COARSE_X) | (data >> 3));
            fineX = data & 0x07;
        } else {
            // Second write, Y scroll: t: yyy.. YYYYY..... <- d: YYYYYyyy
            tempVramAddr = static_cast<std::uint16_t>((tempVramAddr & ~(LOOPY_FINE_Y | LOOPY_COARSE_Y)) |
                                                      ((data & 0x07) << 12) | ((data & 0xF8) << 2));
        }
        writeToggle = !writeToggle;
        break;
    case PPUADDR:
        if (!writeToggle) {
            // First write, high byte: t: .AAAAAA ........ <- d: ..AAAAAA (bit 14 is cleared)
            tempVramAddr = static_cast<std::uint16_t>((tempVramAddr & 0x00FF) | ((data & 0x3F) << 8));
        } else {
            // Second write, low byte: t: ....... AAAAAAAA <- d; then v <- t
            tempVramAddr = static_cast<std::uint16_t>((tempVramAddr & 0xFF00) | data);
            vramAddr = tempVramAddr;
        }
        writeToggle = !writeToggle;
        break;
    case PPUDATA:
        ppuWrite(vramAddr & PPU_ADDR_MASK, data);
        incrementVramAddr();
        break;
    default:
        break;
    }
}

void Ppu2C02::incrementVramAddr() {
    // TODO(Step 4 rendering): during rendering, $2007 access bumps coarse X and Y instead.
    const std::uint16_t step = (ctrl & CTRL_INCREMENT_32) != 0 ? 32 : 1;
    vramAddr = static_cast<std::uint16_t>((vramAddr + step) & VRAM_ADDR_MASK);
}

// ---------------------------------------------------------------------------------------------
// PPU address space

std::uint8_t Ppu2C02::ppuRead(std::uint16_t addr) {
    addr &= PPU_ADDR_MASK;
    if (addr <= PATTERN_TABLE_END) {
        std::uint8_t data = 0x00;
        if (cartridge) {
            cartridge->ppuRead(addr, data);
        }
        return data;
    }
    if (addr <= NAMETABLE_END) {
        return tblName[mirrorNametable(addr)];
    }
    return tblPalette[paletteIndex(addr)];
}

void Ppu2C02::ppuWrite(std::uint16_t addr, std::uint8_t data) {
    addr &= PPU_ADDR_MASK;
    if (addr <= PATTERN_TABLE_END) {
        if (cartridge) {
            cartridge->ppuWrite(addr, data);
        }
    } else if (addr <= NAMETABLE_END) {
        tblName[mirrorNametable(addr)] = data;
    } else {
        tblPalette[paletteIndex(addr)] = data & PALETTE_ENTRY_MASK;
    }
}

std::uint16_t Ppu2C02::mirrorNametable(std::uint16_t addr) const {
    // $2000-$2FFF (and its $3000 mirror) holds 4 logical 1KB nametables backed by 2KB of VRAM.
    const auto offset = static_cast<std::uint16_t>(addr & (NAMETABLE_SIZE - 1));
    const auto table = static_cast<std::uint16_t>((addr >> 10) & 0x03);

    std::uint16_t physicalTable = 0;
    switch (cartridge ? cartridge->getMirroring() : Mirroring::Horizontal) {
    case Mirroring::Vertical:
        physicalTable = table & 0x01; // $2000=$2800, $2400=$2C00
        break;
    case Mirroring::Horizontal:
        physicalTable = table >> 1; // $2000=$2400, $2800=$2C00
        break;
    case Mirroring::SingleScreenLower:
        physicalTable = 0;
        break;
    case Mirroring::SingleScreenUpper:
        physicalTable = 1;
        break;
    case Mirroring::FourScreen:
        // TODO: four-screen boards add 2KB of VRAM on the cartridge; fall back to vertical until then.
        physicalTable = table & 0x01;
        break;
    }
    return static_cast<std::uint16_t>(physicalTable * NAMETABLE_SIZE + offset);
}

// ---------------------------------------------------------------------------------------------
// Pattern tables

Ppu2C02::Tile Ppu2C02::decodeTile(int tableIndex, std::uint8_t tileIndex) {
    Tile tile{};
    for (int y = 0; y < TILE_SIZE; ++y) {
        const std::uint16_t addr = patternRowAddress(tableIndex, tileIndex, y);
        const TileRow row = decodeTileRow(ppuRead(addr), ppuRead(static_cast<std::uint16_t>(addr + TILE_SIZE)));
        std::copy(row.begin(), row.end(), tile.begin() + static_cast<std::ptrdiff_t>(y * TILE_SIZE));
    }
    return tile;
}

Ppu2C02::Color Ppu2C02::getColorFromPaletteRAM(std::uint8_t paletteId, std::uint8_t pixelIndex) {
    const auto addr = static_cast<std::uint16_t>(PALETTE_RAM_START + (paletteId & 0x07) * COLORS_PER_PALETTE +
                                                 (pixelIndex & 0x03));
    return SYSTEM_PALETTE[ppuRead(addr) & PALETTE_ENTRY_MASK];
}

const Ppu2C02::PatternTableImage& Ppu2C02::getPatternTable(int tableIndex, std::uint8_t paletteId) {
    PatternTableImage& image = patternTableImages[static_cast<std::size_t>(tableIndex & 1)];

    // Resolve the palette's 4 colors once instead of per pixel.
    std::array<std::uint32_t, COLORS_PER_PALETTE> colors{};
    for (std::size_t i = 0; i < colors.size(); ++i) {
        colors[i] = toArgb(getColorFromPaletteRAM(paletteId, static_cast<std::uint8_t>(i)));
    }

    for (int tileY = 0; tileY < PATTERN_TABLE_TILES_PER_ROW; ++tileY) {
        for (int tileX = 0; tileX < PATTERN_TABLE_TILES_PER_ROW; ++tileX) {
            const auto tileIndex = static_cast<std::uint8_t>(tileY * PATTERN_TABLE_TILES_PER_ROW + tileX);

            for (int y = 0; y < TILE_SIZE; ++y) {
                const std::uint16_t addr = patternRowAddress(tableIndex, tileIndex, y);
                const TileRow row = decodeTileRow(ppuRead(addr), ppuRead(static_cast<std::uint16_t>(addr + TILE_SIZE)));

                const int pixelY = tileY * TILE_SIZE + y;
                auto* out = &image[static_cast<std::size_t>(pixelY * PATTERN_TABLE_PIXELS + tileX * TILE_SIZE)];
                for (std::size_t x = 0; x < row.size(); ++x) {
                    out[x] = colors[row[x]];
                }
            }
        }
    }
    return image;
}

// ---------------------------------------------------------------------------------------------
// Scanline rendering

Ppu2C02::ResolvedPalettes Ppu2C02::resolvePalettes() const {
    const std::uint8_t colorMask = (mask & MASK_GRAYSCALE) != 0 ? GRAYSCALE_MASK : PALETTE_ENTRY_MASK;
    const std::uint32_t backdrop = toArgb(SYSTEM_PALETTE[tblPalette[0] & colorMask]);
    ResolvedPalettes palettes{};
    for (std::size_t p = 0; p < palettes.size(); ++p) {
        palettes[p][0] = backdrop; // Never drawn for sprites (transparent); backdrop for background
        for (std::size_t i = 1; i < COLORS_PER_PALETTE; ++i) {
            palettes[p][i] = toArgb(SYSTEM_PALETTE[tblPalette[p * COLORS_PER_PALETTE + i] & colorMask]);
        }
    }
    return palettes;
}

std::uint16_t Ppu2C02::incrementCoarseX(std::uint16_t addr) {
    // Coarse X wraps from 31 to 0 and flips the horizontal nametable bit.
    if ((addr & LOOPY_COARSE_X) == LOOPY_COARSE_X) {
        return static_cast<std::uint16_t>((addr & ~LOOPY_COARSE_X) ^ 0x0400);
    }
    return static_cast<std::uint16_t>(addr + 1);
}

std::uint16_t Ppu2C02::incrementFineY(std::uint16_t addr) {
    if ((addr & LOOPY_FINE_Y) != LOOPY_FINE_Y) {
        return static_cast<std::uint16_t>(addr + 0x1000);
    }
    // Fine Y overflows into coarse Y. Row 29 is the last tile row: wrap to 0 and flip the vertical
    // nametable bit. Rows 30/31 (attribute area, reachable via $2005) wrap to 0 without flipping.
    addr &= static_cast<std::uint16_t>(~LOOPY_FINE_Y);
    int coarseY = (addr & LOOPY_COARSE_Y) >> 5;
    if (coarseY == NAMETABLE_ROWS - 1) {
        coarseY = 0;
        addr ^= 0x0800;
    } else if (coarseY == 31) {
        coarseY = 0;
    } else {
        ++coarseY;
    }
    return static_cast<std::uint16_t>((addr & ~LOOPY_COARSE_Y) | (coarseY << 5));
}

void Ppu2C02::copyHorizontalBits() {
    constexpr std::uint16_t horizontal = LOOPY_COARSE_X | 0x0400;
    vramAddr = static_cast<std::uint16_t>((vramAddr & ~horizontal) | (tempVramAddr & horizontal));
}

void Ppu2C02::copyVerticalBits() {
    constexpr std::uint16_t vertical = LOOPY_FINE_Y | 0x0800 | LOOPY_COARSE_Y;
    vramAddr = static_cast<std::uint16_t>((vramAddr & ~vertical) | (tempVramAddr & vertical));
}

int Ppu2C02::renderScanline(std::span<std::uint32_t, SCREEN_WIDTH> line, int y, std::uint16_t lineAddr) {
    // Palettes are resolved per line so mid-frame palette changes show up where they happen.
    const ResolvedPalettes palettes = resolvePalettes();
    const std::uint32_t backdrop = palettes[0][0];
    std::array<bool, SCREEN_WIDTH> backgroundOpaque{};

    // --- Background: 33 tiles starting at lineAddr, shifted left by fine X.
    if ((mask & MASK_SHOW_BACKGROUND) != 0) {
        const int patternTable = (ctrl & CTRL_BACKGROUND_TABLE) != 0 ? 1 : 0;
        const int fineY = (lineAddr & LOOPY_FINE_Y) >> 12;
        std::uint16_t addr = lineAddr;
        for (int tileX = -fineX; tileX < SCREEN_WIDTH; tileX += TILE_SIZE) {
            // Nametable byte: $2000 | NN YYYYY XXXXX. Attribute byte: $23C0 | NN | (YYY >> 2) << 3 | XXX >> 2.
            const std::uint8_t tileIndex = ppuRead(static_cast<std::uint16_t>(NAMETABLE_BASE | (addr & 0x0FFF)));
            const std::uint8_t attribute = ppuRead(static_cast<std::uint16_t>(
                NAMETABLE_BASE + ATTRIBUTE_TABLE_OFFSET + (addr & LOOPY_NAMETABLE) + ((addr >> 4) & 0x38) + ((addr >> 2) & 0x07)));
            // Quadrant: coarse Y bit 1 (v bit 6) selects bottom, coarse X bit 1 (v bit 1) selects right.
            const int shift = ((addr >> 4) & 0x04) | (addr & 0x02);
            const auto& colors = palettes[(attribute >> shift) & 0x03];

            const std::uint16_t patternAddr = patternRowAddress(patternTable, tileIndex, fineY);
            const TileRow pixels =
                decodeTileRow(ppuRead(patternAddr), ppuRead(static_cast<std::uint16_t>(patternAddr + TILE_SIZE)));
            for (int px = 0; px < TILE_SIZE; ++px) {
                const int x = tileX + px;
                if (x < 0 || x >= SCREEN_WIDTH) {
                    continue;
                }
                const std::uint8_t colorIndex = pixels[static_cast<std::size_t>(px)];
                line[static_cast<std::size_t>(x)] = colors[colorIndex];
                backgroundOpaque[static_cast<std::size_t>(x)] = colorIndex != 0;
            }
            addr = incrementCoarseX(addr);
        }
        if ((mask & MASK_SHOW_BACKGROUND_LEFT) == 0) {
            std::fill_n(line.begin(), TILE_SIZE, backdrop);
            std::fill_n(backgroundOpaque.begin(), TILE_SIZE, false);
        }
    } else {
        std::fill(line.begin(), line.end(), backdrop);
    }

    // --- Sprites
    if ((mask & MASK_SHOW_SPRITES) == 0) {
        return -1;
    }
    // TODO: the sprite overflow flag.
    const int patternTable = (ctrl & CTRL_SPRITE_TABLE) != 0 ? 1 : 0; // 8x8 mode only
    const bool tallSprites = (ctrl & CTRL_SPRITE_SIZE_16) != 0;         // PPUCTRL bit 5: 8x16 sprites
    const int spriteHeight = tallSprites ? 2 * TILE_SIZE : TILE_SIZE;
    const int firstVisibleX = (mask & MASK_SHOW_SPRITES_LEFT) != 0 ? 0 : TILE_SIZE;

    // Evaluation: the first 8 sprites in OAM order that cover this scanline.
    std::array<std::uint8_t, SPRITES_PER_SCANLINE> lineSprites{};
    std::size_t lineSpriteCount = 0;
    for (int i = 0; i < SPRITE_COUNT && lineSpriteCount < lineSprites.size(); ++i) {
        const int row = y - (oamMemory[static_cast<std::size_t>(i) * 4] + 1);
        if (row >= 0 && row < spriteHeight) {
            lineSprites[lineSpriteCount++] = static_cast<std::uint8_t>(i);
        }
    }

    // Among overlapping sprites the lowest OAM index owns the pixel, even when it is behind the
    // background (so a hidden low-index sprite also hides higher-index sprites under it).
    std::array<bool, SCREEN_WIDTH> pixelTaken{};
    int sprite0HitX = -1;
    for (std::size_t n = 0; n < lineSpriteCount; ++n) {
        const ObjectAttributeEntry sprite = getOamEntry(lineSprites[n]);
        int row = y - (sprite.y + 1);
        if ((sprite.attribute & SPRITE_FLIP_VERTICAL) != 0) {
            row = spriteHeight - 1 - row; // Flips the whole sprite: in 8x16 mode the two tiles swap
        }
        const std::uint16_t addr = spriteRowAddress(tallSprites, patternTable, sprite.id, row);
        const TileRow pixels = decodeTileRow(ppuRead(addr), ppuRead(static_cast<std::uint16_t>(addr + TILE_SIZE)));

        const bool flipH = (sprite.attribute & SPRITE_FLIP_HORIZONTAL) != 0;
        const bool behindBackground = (sprite.attribute & SPRITE_BEHIND_BACKGROUND) != 0;
        const bool isSprite0 = lineSprites[n] == 0;
        const auto& colors = palettes[4 + (sprite.attribute & SPRITE_PALETTE_MASK)];

        for (int px = 0; px < TILE_SIZE; ++px) {
            const int x = sprite.x + px;
            if (x >= SCREEN_WIDTH) {
                break; // Sprites don't wrap around the right edge
            }
            const std::uint8_t colorIndex = pixels[static_cast<std::size_t>(flipH ? TILE_SIZE - 1 - px : px)];
            const auto ux = static_cast<std::size_t>(x);
            if (colorIndex == 0 || x < firstVisibleX || pixelTaken[ux]) {
                continue; // Transparent, clipped, or already owned by a lower-index sprite
            }
            // Sprite 0 hit: opaque sprite 0 over opaque background (both already exclude the
            // clipped left columns), never at x = 255. Priority doesn't matter.
            if (isSprite0 && sprite0HitX < 0 && backgroundOpaque[ux] && x != SCREEN_WIDTH - 1) {
                sprite0HitX = x;
            }
            pixelTaken[ux] = true;
            if (!behindBackground || !backgroundOpaque[ux]) {
                line[ux] = colors[colorIndex];
            }
        }
    }
    return sprite0HitX;
}

void Ppu2C02::renderFrame(FrameSpan frame) {
    std::uint16_t addr = tempVramAddr;
    for (int y = 0; y < SCREEN_HEIGHT; ++y) {
        (void)renderScanline(frame.subspan(static_cast<std::size_t>(y * SCREEN_WIDTH)).first<SCREEN_WIDTH>(), y, addr);
        addr = incrementFineY(addr);
    }
}

// ---------------------------------------------------------------------------------------------
// Timing

void Ppu2C02::clock() {
    const bool visibleLine = scanline < SCREEN_HEIGHT;
    const bool preRenderLine = scanline == scanlinesPerFrame - 1;

    if (visibleLine && cycle == 1) {
        // Draw the whole line from the scroll position in v. Mid-line register writes take effect
        // on the next line, which covers status-bar splits (write after sprite 0 hit / IRQ).
        auto line = std::span<std::uint32_t, SCREEN_WIDTH>(&frameBuffer[static_cast<std::size_t>(scanline * SCREEN_WIDTH)],
                                                           SCREEN_WIDTH);
        if (isRenderingEnabled()) {
            const int hitX = renderScanline(line, scanline, vramAddr);
            sprite0HitCycle = hitX >= 0 ? hitX + 1 : -1; // Dot 1 outputs pixel 0
        } else {
            std::fill(line.begin(), line.end(), resolvePalettes()[0][0]);
            sprite0HitCycle = -1;
        }
    }
    if (visibleLine && cycle == sprite0HitCycle) {
        status |= STATUS_SPRITE_ZERO_HIT;
    }

    if ((visibleLine || preRenderLine) && isRenderingEnabled()) {
        if (cycle == 256) {
            vramAddr = incrementFineY(vramAddr);
        } else if (cycle == 257) {
            copyHorizontalBits();
        } else if (preRenderLine && cycle >= 280 && cycle <= 304) {
            copyVerticalBits();
        }
    }

    if (cycle == 1) {
        if (scanline == VBLANK_SCANLINE) {
            status |= STATUS_VBLANK;
            if ((ctrl & CTRL_NMI_ENABLE) != 0) {
                nmiRequested = true;
            }
        } else if (preRenderLine) {
            status &= static_cast<std::uint8_t>(~(STATUS_VBLANK | STATUS_SPRITE_ZERO_HIT | STATUS_SPRITE_OVERFLOW));
        }
    }

    ++cycle;
    // NTSC only: with rendering on, odd frames skip the last dot of the pre-render line.
    if (preRenderLine && cycle == DOTS_PER_SCANLINE - 1 && oddFrame && skipsOddFrameDot && isRenderingEnabled()) {
        ++cycle;
    }
    if (cycle >= DOTS_PER_SCANLINE) {
        cycle = 0;
        sprite0HitCycle = -1;
        if (++scanline >= scanlinesPerFrame) {
            scanline = 0;
            oddFrame = !oddFrame;
            frameComplete = true;
        }
    }
}

bool Ppu2C02::pollNmi() {
    return std::exchange(nmiRequested, false);
}

bool Ppu2C02::pollFrameComplete() {
    return std::exchange(frameComplete, false);
}
