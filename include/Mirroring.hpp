#pragma once

#include <string_view>

// How the PPU's 4 logical nametables map onto the console's 2KB of VRAM. Fixed by the board for
// simple cartridges (iNES header), switchable at runtime by mappers such as MMC1.
enum class Mirroring {
    Horizontal,        // $2000 = $2400, $2800 = $2C00 (vertical scrolling games)
    Vertical,          // $2000 = $2800, $2400 = $2C00 (horizontal scrolling games)
    FourScreen,        // 4 separate tables (needs extra VRAM on the cartridge)
    SingleScreenLower, // All 4 tables show the first 1KB of VRAM
    SingleScreenUpper, // All 4 tables show the second 1KB of VRAM
};

[[nodiscard]] std::string_view toString(Mirroring mirroring);
