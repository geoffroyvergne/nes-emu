#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

class Cartridge;

// Ricoh 2C02 PPU: CPU-facing registers ($2000-$2007), its own 14-bit address space, and the
// scanline/dot timing that drives VBlank and NMI.
//
// PPU address space:
//   $0000-$1FFF  Pattern tables (cartridge CHR-ROM/CHR-RAM)
//   $2000-$2FFF  Nametables (2KB internal VRAM, mirrored per the cartridge; $3000-$3EFF mirrors it)
//   $3F00-$3FFF  Palette RAM (32 bytes, mirrored)
class Ppu2C02 {
public:
    static constexpr int SCANLINES_PER_FRAME = 262; // 0-239 visible, 240 post-render, 241-260 VBlank, 261 pre-render
    static constexpr int DOTS_PER_SCANLINE = 341;
    static constexpr int VBLANK_SCANLINE = 241;
    static constexpr int PRE_RENDER_SCANLINE = 261;

    static constexpr int SCREEN_WIDTH = 256;
    static constexpr int SCREEN_HEIGHT = 240;
    // ARGB8888 pixels, row-major, ready for SDL_UpdateTexture.
    using FrameBuffer = std::array<std::uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT>;

    // Nametables: 32x30 tile indexes (960 bytes) followed by a 64-byte attribute table.
    static constexpr std::uint16_t NAMETABLE_BASE = 0x2000;
    static constexpr std::uint16_t NAMETABLE_BYTES = 0x0400;
    static constexpr std::uint16_t ATTRIBUTE_TABLE_OFFSET = 0x03C0;
    static constexpr int NAMETABLE_COLUMNS = 32;
    static constexpr int NAMETABLE_ROWS = 30;

    // Attribute tables hold one byte per 4x4-tile block (8 blocks per row). Each byte packs four
    // 2-bit background palette ids, one per 2x2-tile quadrant:
    //   bits 1-0 top-left, 3-2 top-right, 5-4 bottom-left, 7-6 bottom-right
    // Offset of the attribute byte for tile (column, row), relative to the nametable start:
    [[nodiscard]] static constexpr std::uint16_t attributeOffset(int column, int row) {
        return static_cast<std::uint16_t>(ATTRIBUTE_TABLE_OFFSET + ((row >> 2) << 3) + (column >> 2));
    }
    // Shift that brings the tile's quadrant down to bits 1-0: bit 1 of the column selects
    // right (+2), bit 1 of the row selects bottom (+4).
    [[nodiscard]] static constexpr int attributeShift(int column, int row) {
        return ((row & 0x02) << 1) | (column & 0x02);
    }

    // OAM: 64 sprites x 4 bytes, in this exact byte order.
    static constexpr int SPRITE_COUNT = 64;
    static constexpr int SPRITES_PER_SCANLINE = 8; // Hardware limit; extra sprites on a line are dropped

    struct ObjectAttributeEntry {
        std::uint8_t y;         // Top edge minus 1 (the sprite appears on scanline y + 1)
        std::uint8_t id;        // Tile index in the sprite pattern table (PPUCTRL bit 3)
        std::uint8_t attribute; // 76543210: V H P . . . p p  (V/H flip, P behind background, pp palette 4-7)
        std::uint8_t x;         // Left edge
    };
    static_assert(sizeof(ObjectAttributeEntry) == 4, "OAM entries must map 1:1 onto 4 OAM bytes");

    static constexpr std::uint8_t SPRITE_PALETTE_MASK = 0x03;
    static constexpr std::uint8_t SPRITE_BEHIND_BACKGROUND = 0x20;
    static constexpr std::uint8_t SPRITE_FLIP_HORIZONTAL = 0x40;
    static constexpr std::uint8_t SPRITE_FLIP_VERTICAL = 0x80;

    // Sprite i as a structured view of oamMemory[i*4 .. i*4+3].
    [[nodiscard]] ObjectAttributeEntry getOamEntry(int index) const {
        const auto base = static_cast<std::size_t>(index & (SPRITE_COUNT - 1)) * sizeof(ObjectAttributeEntry);
        return std::bit_cast<ObjectAttributeEntry>(
            std::array<std::uint8_t, 4>{oamMemory[base], oamMemory[base + 1], oamMemory[base + 2], oamMemory[base + 3]});
    }

    // Pattern tables: two 4KB tables ($0000, $1000) of 256 tiles; each tile is 8x8 pixels stored
    // as 16 bytes: 8 bytes of bit plane 0 followed by 8 bytes of bit plane 1.
    static constexpr int TILE_SIZE = 8;
    static constexpr int TILE_BYTES = 16;
    static constexpr int PATTERN_TABLE_TILES_PER_ROW = 16;
    static constexpr int PATTERN_TABLE_PIXELS = TILE_SIZE * PATTERN_TABLE_TILES_PER_ROW; // 128
    static constexpr std::uint16_t PATTERN_TABLE_BYTES = 0x1000;

    // Palette RAM ($3F00-$3F1F): 4 background palettes then 4 sprite palettes, 4 entries each.
    // Each entry is a 6-bit index into the 64-color system palette.
    static constexpr std::uint16_t PALETTE_RAM_START = 0x3F00;
    static constexpr int PALETTE_COUNT = 8;
    static constexpr int COLORS_PER_PALETTE = 4;
    static constexpr int SYSTEM_PALETTE_SIZE = 64;

    struct Color {
        std::uint8_t r;
        std::uint8_t g;
        std::uint8_t b;
        constexpr bool operator==(const Color&) const = default;
    };

    [[nodiscard]] static constexpr std::uint32_t toArgb(Color color) {
        return 0xFF000000u | (std::uint32_t{color.r} << 16) | (std::uint32_t{color.g} << 8) | color.b;
    }

    // Maps any palette address ($3F00-$3FFF) to its slot in the 32-byte palette RAM.
    //   - $3F20-$3FFF repeat $3F00-$3F1F every 32 bytes (only the low 5 bits are decoded).
    //   - Entry 0 of each sprite palette is shared with the matching background palette:
    //     $3F10/$3F14/$3F18/$3F1C are the same bytes as $3F00/$3F04/$3F08/$3F0C, for reads and writes.
    //     Those are exactly the indexes with bit 4 set and bits 0-1 clear, i.e. (index & 0x13) == 0x10.
    [[nodiscard]] static constexpr std::size_t paletteIndex(std::uint16_t addr) {
        std::size_t index = addr & 0x1F;
        if ((index & 0x13) == 0x10) {
            index &= 0x0F;
        }
        return index;
    }

    using TileRow = std::array<std::uint8_t, TILE_SIZE>;          // 2-bit color indexes, left to right
    using Tile = std::array<std::uint8_t, TILE_SIZE * TILE_SIZE>; // 2-bit color indexes, row-major
    // ARGB8888 pixels, row-major, ready for SDL_UpdateTexture.
    using PatternTableImage = std::array<std::uint32_t, PATTERN_TABLE_PIXELS * PATTERN_TABLE_PIXELS>;

    // Address of one row of one tile's low bit plane; the high plane is 8 bytes further.
    [[nodiscard]] static constexpr std::uint16_t patternRowAddress(int tableIndex, std::uint8_t tileIndex, int row) {
        return static_cast<std::uint16_t>((tableIndex & 1) * PATTERN_TABLE_BYTES + tileIndex * TILE_BYTES + row);
    }

    // Combines one row's two bit planes into 8 color indexes. Bit 7 is the leftmost pixel:
    //   lowPlane  = 0b0110'0001, highPlane = 0b0011'0001  ->  0 1 3 2 0 0 0 3
    [[nodiscard]] static constexpr TileRow decodeTileRow(std::uint8_t lowPlane, std::uint8_t highPlane) {
        TileRow row{};
        for (std::size_t x = 0; x < row.size(); ++x) {
            const auto bit = static_cast<int>(7 - x);
            row[x] = static_cast<std::uint8_t>((((highPlane >> bit) & 1) << 1) | ((lowPlane >> bit) & 1));
        }
        return row;
    }

    // Reads and decodes one 8x8 tile from pattern table 0 or 1.
    [[nodiscard]] Tile decodeTile(int tableIndex, std::uint8_t tileIndex);

    // Looks up color pixelIndex (0-3) of palette paletteId (0-3 background, 4-7 sprites) in palette
    // RAM and converts the stored system-palette index to RGB. This is a raw lookup: the renderer
    // is responsible for treating pixel index 0 as transparent / the universal backdrop ($3F00).
    [[nodiscard]] Color getColorFromPaletteRAM(std::uint8_t paletteId, std::uint8_t pixelIndex);

    using FrameSpan = std::span<std::uint32_t, SCREEN_WIDTH * SCREEN_HEIGHT>;

    // The frame being drawn. clock() renders each visible scanline into it at dot 1, so once the
    // PPU is past scanline 239 (VBlank / pollFrameComplete) it holds a complete picture.
    [[nodiscard]] const FrameBuffer& getFrameBuffer() const { return frameBuffer; }
    // Debug/test helper: renders a whole frame at once from the current scroll position (t, fine X)
    // and registers, as if nothing changed mid-frame. Does not touch v, the frame buffer or flags.
    void renderFrame(FrameSpan frameBuffer);

    // Debug view: renders a whole pattern table (16x16 tiles = 128x128 pixels) using palette
    // paletteId (0-7) from palette RAM.
    [[nodiscard]] const PatternTableImage& getPatternTable(int tableIndex, std::uint8_t paletteId);

    void connectCartridge(std::shared_ptr<Cartridge> newCartridge);
    void reset();

    // CPU side: addr is any address in $2000-$3FFF (mirrored every 8 bytes).
    // readOnly reads have no side effects (no VBlank clear, no latch reset, no buffer update).
    [[nodiscard]] std::uint8_t cpuRead(std::uint16_t addr, bool readOnly = false);
    void cpuWrite(std::uint16_t addr, std::uint8_t data);

    // PPU side: 14-bit PPU address space.
    [[nodiscard]] std::uint8_t ppuRead(std::uint16_t addr);
    void ppuWrite(std::uint16_t addr, std::uint8_t data);

    // Advances the PPU by one dot (3 dots per CPU cycle on NTSC).
    void clock();

    // True once per VBlank when PPUCTRL has NMI enabled; reading it clears the request.
    [[nodiscard]] bool pollNmi();
    // True once per frame after the last dot of the pre-render scanline; reading it clears it.
    [[nodiscard]] bool pollFrameComplete();

    [[nodiscard]] int getScanline() const { return scanline; }
    [[nodiscard]] int getCycle() const { return cycle; }
    // Loopy registers, exposed for debugging and tests.
    [[nodiscard]] std::uint16_t getVramAddress() const { return vramAddr; }
    [[nodiscard]] std::uint16_t getTempVramAddress() const { return tempVramAddr; }
    [[nodiscard]] std::uint8_t getFineX() const { return fineX; }

private:
    enum Ctrl : std::uint8_t {
        CTRL_NAMETABLE = 0x03,
        CTRL_INCREMENT_32 = 0x04,
        CTRL_SPRITE_TABLE = 0x08,
        CTRL_BACKGROUND_TABLE = 0x10,
        CTRL_SPRITE_SIZE_16 = 0x20,
        CTRL_NMI_ENABLE = 0x80,
    };

    enum Mask : std::uint8_t {
        MASK_GRAYSCALE = 0x01,
        MASK_SHOW_BACKGROUND_LEFT = 0x02,
        MASK_SHOW_SPRITES_LEFT = 0x04,
        MASK_SHOW_BACKGROUND = 0x08,
        MASK_SHOW_SPRITES = 0x10,
    };

    enum Status : std::uint8_t {
        STATUS_SPRITE_OVERFLOW = 0x20,
        STATUS_SPRITE_ZERO_HIT = 0x40,
        STATUS_VBLANK = 0x80,
    };

    // ARGB colors for all 8 palettes; entry 0 of every palette is the universal backdrop ($3F00).
    using ResolvedPalettes = std::array<std::array<std::uint32_t, COLORS_PER_PALETTE>, PALETTE_COUNT>;
    [[nodiscard]] ResolvedPalettes resolvePalettes() const;

    [[nodiscard]] std::uint16_t mirrorNametable(std::uint16_t addr) const;
    void incrementVramAddr();

    [[nodiscard]] bool isRenderingEnabled() const { return (mask & (MASK_SHOW_BACKGROUND | MASK_SHOW_SPRITES)) != 0; }

    // Draws one scanline: background from VRAM address lineAddr (coarse X/Y, nametable, fine Y)
    // shifted by fineX, then sprites. Returns the x of the first sprite 0 hit on the line, or -1.
    int renderScanline(std::span<std::uint32_t, SCREEN_WIDTH> line, int y, std::uint16_t lineAddr);

    // Loopy register updates performed by the rendering hardware.
    [[nodiscard]] static std::uint16_t incrementCoarseX(std::uint16_t addr);
    [[nodiscard]] static std::uint16_t incrementFineY(std::uint16_t addr);
    void copyHorizontalBits(); // v: ....F.. ...XXXXX <- t (dot 257)
    void copyVerticalBits();   // v: yyyF.YY YYY..... <- t (pre-render dots 280-304)

    std::shared_ptr<Cartridge> cartridge;

    FrameBuffer frameBuffer{};
    std::array<PatternTableImage, 2> patternTableImages{};

    std::array<std::uint8_t, 2048> tblName{};
    // Power-up contents are unspecified on real hardware; this is the commonly observed pattern
    // (nesdev wiki), so debug views show something before a game loads its own palettes.
    std::array<std::uint8_t, 32> tblPalette = {
        0x09, 0x01, 0x00, 0x01, 0x00, 0x02, 0x02, 0x0D, 0x08, 0x10, 0x08, 0x24, 0x00, 0x00, 0x04, 0x2C,
        0x09, 0x01, 0x34, 0x03, 0x00, 0x04, 0x00, 0x14, 0x08, 0x3A, 0x00, 0x02, 0x00, 0x20, 0x2C, 0x08,
    };
    std::array<std::uint8_t, 256> oamMemory{};

    // CPU-visible registers
    std::uint8_t ctrl = 0x00;    // $2000 PPUCTRL
    std::uint8_t mask = 0x00;    // $2001 PPUMASK
    std::uint8_t status = 0x00;  // $2002 PPUSTATUS (top 3 bits)
    std::uint8_t oamAddr = 0x00; // $2003 OAMADDR

    // Internal "loopy" registers shared by $2005/$2006 and later by rendering:
    //   v/t layout: yyy NN YYYYY XXXXX (fine Y, nametable, coarse Y, coarse X)
    std::uint16_t vramAddr = 0x0000;     // v: current VRAM address (15 bits)
    std::uint16_t tempVramAddr = 0x0000; // t: temporary VRAM address / top-left scroll position
    std::uint8_t fineX = 0x00;           // x: fine X scroll (3 bits)
    bool writeToggle = false;            // w: first/second write latch for $2005/$2006

    std::uint8_t readBuffer = 0x00; // $2007 reads below $3F00 are delayed by one read
    std::uint8_t ioLatch = 0x00;    // Last value on the PPU data bus; write-only registers read it back

    int scanline = 0;
    int cycle = 0;
    bool nmiRequested = false;
    bool frameComplete = false;
    bool oddFrame = false;
    int sprite0HitCycle = -1; // Dot on the current scanline where sprite 0 hit is raised, -1 if none
};
