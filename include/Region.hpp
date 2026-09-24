#pragma once

#include <array>
#include <cstdint>
#include <string_view>

enum class Region { Ntsc, Pal };

// Everything that differs between an NTSC console (2A03 CPU/APU + 2C02 PPU) and a PAL console
// (2A07 + 2C07). The 6502 core itself (instruction timing, DMA) is identical in both.
struct RegionTiming {
    std::string_view name;
    int cpuClockHz;
    // PPU dots per CPU cycle as a fraction: NTSC 3/1, PAL 16/5 = 3.2.
    int ppuDotsPerCpuCycleNum;
    int ppuDotsPerCpuCycleDen;
    int scanlinesPerFrame;  // 262 / 312 (VBlank always starts at 241; the pre-render line is the last one)
    bool skipsOddFrameDot;  // NTSC drops one dot every other frame while rendering; PAL doesn't
    // APU frame counter points in CPU cycles: quarter frames 1-3, 4-step last step (+ IRQ),
    // 4-step period, 5-step last step, 5-step period.
    int apuStep1, apuStep2, apuStep3, apuStep4, apuFourStepPeriod, apuStep5, apuFiveStepPeriod;
    std::array<std::uint16_t, 16> noisePeriods; // Noise LFSR periods in CPU cycles

    [[nodiscard]] constexpr double cpuCyclesPerFrame() const {
        const double dotsPerFrame = scanlinesPerFrame * 341.0 - (skipsOddFrameDot ? 0.5 : 0.0);
        return dotsPerFrame * ppuDotsPerCpuCycleDen / ppuDotsPerCpuCycleNum;
    }
    [[nodiscard]] constexpr double frameRate() const { return cpuClockHz / cpuCyclesPerFrame(); }
};

inline constexpr RegionTiming NTSC_TIMING{
    "NTSC", 1789773, 3, 1, 262, true,
    7457, 14913, 22371, 29829, 29830, 37281, 37282,
    {4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068},
};

inline constexpr RegionTiming PAL_TIMING{
    "PAL", 1662607, 16, 5, 312, false,
    8313, 16627, 24939, 33253, 33254, 41565, 41566,
    {4, 8, 14, 30, 60, 88, 118, 148, 188, 236, 354, 472, 708, 944, 1890, 3778},
};

[[nodiscard]] constexpr const RegionTiming& timingFor(Region region) {
    return region == Region::Pal ? PAL_TIMING : NTSC_TIMING;
}

// 29780.5 CPU cycles -> 60.0988 fps; 33247.5 CPU cycles -> 50.0070 fps.
static_assert(NTSC_TIMING.cpuCyclesPerFrame() == 29780.5);
static_assert(PAL_TIMING.cpuCyclesPerFrame() == 33247.5);
