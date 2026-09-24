#pragma once

#include "ApuUnits.hpp"

#include <cstdint>

// APU triangle channel ($4008-$400B): a fixed 32-step triangle wave with no volume control. It is
// gated by two counters: the length counter (like the other channels) and a finer linear counter.
//
//   $4008  CRRR RRRR  control (length halt + linear counter control), linear counter reload value
//   $4009  ---- ----  unused
//   $400A  TTTT TTTT  timer low 8 bits
//   $400B  LLLL LTTT  length counter index, timer high 3 bits (also sets the linear reload flag)
class TriangleChannel {
public:
    void write(int reg, std::uint8_t data);
    void setEnabled(bool enabled) { lengthCounter.setEnabled(enabled); }
    [[nodiscard]] bool isActive() const { return lengthCounter.isActive(); }

    void clockTimer();        // Every CPU cycle (unlike the pulse/noise timers)
    void clockQuarterFrame(); // Linear counter
    void clockHalfFrame();    // Length counter

    // Current output level, 0-15.
    [[nodiscard]] std::uint8_t output() const;

private:
    LengthCounter lengthCounter;

    bool controlFlag = false; // Halts the length counter and keeps reloading the linear counter
    std::uint8_t linearReloadValue = 0;
    std::uint8_t linearCounter = 0;
    bool linearReload = false;

    std::uint16_t timerPeriod = 0; // frequency = CPU / (32 * (period + 1))
    std::uint16_t timerCounter = 0;
    std::uint8_t sequenceStep = 0; // 0-31
};
