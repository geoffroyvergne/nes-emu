#include "PulseChannel.hpp"

#include <array>

namespace {

// Waveforms for the 4 duty settings, one bit per sequencer step.
constexpr std::array<std::array<std::uint8_t, 8>, 4> DUTY_SEQUENCES = {{
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}, // 75% (25% inverted)
}};

constexpr std::uint16_t MIN_AUDIBLE_PERIOD = 8;   // Periods < 8 (> ~12.4 kHz) are silenced
constexpr std::uint16_t MAX_TIMER_PERIOD = 0x7FF; // A sweep target above 11 bits silences the channel

} // namespace

void PulseChannel::write(int reg, std::uint8_t data) {
    switch (reg & 0x03) {
    case 0:
        duty = static_cast<std::uint8_t>(data >> 6);
        lengthCounter.setHalted((data & 0x20) != 0); // Same bit as the envelope loop flag
        envelope.write(data);
        break;
    case 1:
        sweepEnabled = (data & 0x80) != 0;
        sweepPeriod = static_cast<std::uint8_t>((data >> 4) & 0x07);
        sweepNegate = (data & 0x08) != 0;
        sweepShift = data & 0x07;
        sweepReload = true;
        break;
    case 2:
        timerPeriod = static_cast<std::uint16_t>((timerPeriod & 0x0700) | data);
        break;
    case 3:
        timerPeriod = static_cast<std::uint16_t>((timerPeriod & 0x00FF) | ((data & 0x07) << 8));
        lengthCounter.load(static_cast<std::uint8_t>(data >> 3));
        envelope.restart();
        sequenceStep = 0; // Restarts the waveform phase (the classic pulse "click")
        break;
    default:
        break;
    }
}

void PulseChannel::clockTimer() {
    if (timerCounter == 0) {
        timerCounter = timerPeriod;
        sequenceStep = static_cast<std::uint8_t>((sequenceStep + 1) & 0x07);
    } else {
        --timerCounter;
    }
}

void PulseChannel::clockQuarterFrame() {
    envelope.clockQuarterFrame();
}

void PulseChannel::clockHalfFrame() {
    lengthCounter.clockHalfFrame();

    if (sweepDivider == 0 && sweepEnabled && sweepShift > 0 && !isSweepMuting()) {
        timerPeriod = sweepTargetPeriod();
    }
    if (sweepDivider == 0 || sweepReload) {
        sweepDivider = sweepPeriod;
        sweepReload = false;
    } else {
        --sweepDivider;
    }
}

std::uint16_t PulseChannel::sweepTargetPeriod() const {
    const auto change = static_cast<std::uint16_t>(timerPeriod >> sweepShift);
    if (!sweepNegate) {
        return static_cast<std::uint16_t>(timerPeriod + change);
    }
    const int extra = sweepNegateMode == SweepNegate::OnesComplement ? 1 : 0;
    const int target = timerPeriod - change - extra;
    return static_cast<std::uint16_t>(target < 0 ? 0 : target);
}

bool PulseChannel::isSweepMuting() const {
    // Evaluated continuously, even when the sweep is disabled.
    return timerPeriod < MIN_AUDIBLE_PERIOD || sweepTargetPeriod() > MAX_TIMER_PERIOD;
}

std::uint8_t PulseChannel::output() const {
    if (!lengthCounter.isActive() || isSweepMuting() || DUTY_SEQUENCES[duty][sequenceStep] == 0) {
        return 0;
    }
    return envelope.volume();
}
