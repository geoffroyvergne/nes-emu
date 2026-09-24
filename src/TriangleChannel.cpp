#include "TriangleChannel.hpp"

#include <array>

namespace {

// 15 down to 0, then 0 up to 15.
constexpr std::array<std::uint8_t, 32> TRIANGLE_SEQUENCE = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

// Periods 0 and 1 would produce ultrasonic output (~28-56 kHz) that real TVs filter out; stepping
// the sequencer there only produces aliasing and pops, so the channel holds its level instead.
constexpr std::uint16_t MIN_AUDIBLE_PERIOD = 2;

} // namespace

void TriangleChannel::write(int reg, std::uint8_t data) {
    switch (reg & 0x03) {
    case 0:
        controlFlag = (data & 0x80) != 0;
        lengthCounter.setHalted(controlFlag);
        linearReloadValue = data & 0x7F;
        break;
    case 2:
        timerPeriod = static_cast<std::uint16_t>((timerPeriod & 0x0700) | data);
        break;
    case 3:
        timerPeriod = static_cast<std::uint16_t>((timerPeriod & 0x00FF) | ((data & 0x07) << 8));
        lengthCounter.load(static_cast<std::uint8_t>(data >> 3));
        linearReload = true;
        break;
    default:
        break; // $4009 is unused
    }
}

void TriangleChannel::clockTimer() {
    if (timerCounter == 0) {
        timerCounter = timerPeriod;
        // The sequencer only advances while both counters are non-zero; otherwise the channel
        // keeps outputting its current level (it does not drop to 0, avoiding a click).
        if (linearCounter > 0 && lengthCounter.isActive() && timerPeriod >= MIN_AUDIBLE_PERIOD) {
            sequenceStep = static_cast<std::uint8_t>((sequenceStep + 1) & 0x1F);
        }
    } else {
        --timerCounter;
    }
}

void TriangleChannel::clockQuarterFrame() {
    if (linearReload) {
        linearCounter = linearReloadValue;
    } else if (linearCounter > 0) {
        --linearCounter;
    }
    if (!controlFlag) {
        linearReload = false;
    }
}

void TriangleChannel::clockHalfFrame() {
    lengthCounter.clockHalfFrame();
}

std::uint8_t TriangleChannel::output() const {
    return TRIANGLE_SEQUENCE[sequenceStep];
}
