#pragma once

#include "ApuUnits.hpp"
#include "Region.hpp"

#include <array>
#include <cstdint>

// APU noise channel ($400C-$400F): a 15-bit linear feedback shift register clocked at one of 16
// preset rates, gated by an envelope and a length counter.
//
//   $400C  --LC VVVV  length halt / envelope loop, constant volume, volume / envelope period
//   $400D  ---- ----  unused
//   $400E  M--- PPPP  mode (short 93-step sequence), period index into the rate table
//   $400F  LLLL L---  length counter index (also restarts the envelope)
class NoiseChannel {
public:
    void write(int reg, std::uint8_t data);
    // The rate table differs between NTSC and PAL.
    void setPeriodTable(const std::array<std::uint16_t, 16>& table);
    void setEnabled(bool enabled) { lengthCounter.setEnabled(enabled); }
    [[nodiscard]] bool isActive() const { return lengthCounter.isActive(); }

    void clockTimer();        // Every CPU cycle; the period table is in CPU cycles
    void clockQuarterFrame(); // Envelope
    void clockHalfFrame();    // Length counter

    // Current output level, 0-15.
    [[nodiscard]] std::uint8_t output() const;

private:
    Envelope envelope;
    LengthCounter lengthCounter;

    std::array<std::uint16_t, 16> periodTable = NTSC_TIMING.noisePeriods;
    std::uint8_t periodIndex = 0;
    bool shortMode = false;          // Feedback from bit 6 instead of bit 1: metallic, periodic tone
    std::uint16_t timerPeriod = 4;   // CPU cycles between LFSR shifts
    std::uint16_t timerCounter = 0;
    std::uint16_t shiftRegister = 1; // 15-bit LFSR; loaded with 1 at power-up
};
