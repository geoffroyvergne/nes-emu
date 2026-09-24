#pragma once

#include "ApuUnits.hpp"

#include <cstdint>

// APU pulse (square wave) channel. Pulse 1 ($4000-$4003) and Pulse 2 ($4004-$4007) are identical
// except for how the sweep unit negates, which is chosen at construction.
//
//   reg 0  DDLC VVVV  duty, length halt / envelope loop, constant volume, volume / envelope period
//   reg 1  EPPP NSSS  sweep enable, period, negate, shift
//   reg 2  TTTT TTTT  timer low 8 bits
//   reg 3  LLLL LTTT  length counter index, timer high 3 bits (also restarts envelope and phase)
class PulseChannel {
public:
    enum class SweepNegate {
        OnesComplement, // Pulse 1: target = period - change - 1
        TwosComplement, // Pulse 2: target = period - change
    };

    explicit PulseChannel(SweepNegate negateMode) : sweepNegateMode(negateMode) {}

    // Register writes (reg = 0-3, relative to the channel's base address).
    void write(int reg, std::uint8_t data);
    // $4015 enable bit.
    void setEnabled(bool enabled) { lengthCounter.setEnabled(enabled); }
    [[nodiscard]] bool isActive() const { return lengthCounter.isActive(); }

    void clockTimer();        // Every APU cycle (every 2nd CPU cycle)
    void clockQuarterFrame(); // Envelope
    void clockHalfFrame();    // Length counter and sweep

    // Current output level, 0-15.
    [[nodiscard]] std::uint8_t output() const;

private:
    [[nodiscard]] std::uint16_t sweepTargetPeriod() const;
    [[nodiscard]] bool isSweepMuting() const;

    SweepNegate sweepNegateMode;

    Envelope envelope;
    LengthCounter lengthCounter;

    std::uint8_t duty = 0;          // 0-3: 12.5%, 25%, 50%, 75%
    std::uint8_t sequenceStep = 0;  // 0-7 position in the duty waveform
    std::uint16_t timerPeriod = 0;  // 11-bit; frequency = CPU / (16 * (period + 1))
    std::uint16_t timerCounter = 0;

    bool sweepEnabled = false;
    std::uint8_t sweepPeriod = 0;
    bool sweepNegate = false;
    std::uint8_t sweepShift = 0;
    std::uint8_t sweepDivider = 0;
    bool sweepReload = false;
};
