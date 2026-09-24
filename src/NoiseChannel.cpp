#include "NoiseChannel.hpp"

void NoiseChannel::write(int reg, std::uint8_t data) {
    switch (reg & 0x03) {
    case 0:
        lengthCounter.setHalted((data & 0x20) != 0);
        envelope.write(data);
        break;
    case 2:
        shortMode = (data & 0x80) != 0;
        periodIndex = data & 0x0F;
        timerPeriod = periodTable[periodIndex];
        break;
    case 3:
        lengthCounter.load(static_cast<std::uint8_t>(data >> 3));
        envelope.restart();
        break;
    default:
        break; // $400D is unused
    }
}

void NoiseChannel::setPeriodTable(const std::array<std::uint16_t, 16>& table) {
    periodTable = table;
    timerPeriod = periodTable[periodIndex];
}

void NoiseChannel::clockTimer() {
    if (timerCounter == 0) {
        timerCounter = static_cast<std::uint16_t>(timerPeriod - 1); // Shift exactly every timerPeriod cycles
        // Feedback = bit 0 XOR (bit 6 in short mode, else bit 1); shift right, feedback into bit 14.
        const int tap = shortMode ? 6 : 1;
        const auto feedback = static_cast<std::uint16_t>((shiftRegister ^ (shiftRegister >> tap)) & 0x01);
        shiftRegister = static_cast<std::uint16_t>((shiftRegister >> 1) | (feedback << 14));
    } else {
        --timerCounter;
    }
}

void NoiseChannel::clockQuarterFrame() {
    envelope.clockQuarterFrame();
}

void NoiseChannel::clockHalfFrame() {
    lengthCounter.clockHalfFrame();
}

std::uint8_t NoiseChannel::output() const {
    // Silent when bit 0 of the LFSR is set or the length counter has expired.
    if ((shiftRegister & 0x01) != 0 || !lengthCounter.isActive()) {
        return 0;
    }
    return envelope.volume();
}
