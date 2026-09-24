#pragma once

#include <array>
#include <cstdint>

// Building blocks shared by several APU channels.

// Length counter: silences a channel after a programmed duration. Loaded from a table indexed by
// the top 5 bits of the channel's 4th register; decremented on every half frame unless halted.
class LengthCounter {
public:
    static constexpr std::array<std::uint8_t, 32> LENGTH_TABLE = {
        10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
        12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
    };

    void setEnabled(bool isEnabled) {
        enabled = isEnabled;
        if (!enabled) {
            value = 0; // Disabling via $4015 clears the counter immediately
        }
    }
    void setHalted(bool isHalted) { halted = isHalted; }
    void load(std::uint8_t tableIndex) {
        if (enabled) {
            value = LENGTH_TABLE[tableIndex & 0x1F];
        }
    }
    void clockHalfFrame() {
        if (!halted && value > 0) {
            --value;
        }
    }
    [[nodiscard]] bool isActive() const { return value > 0; }

private:
    std::uint8_t value = 0;
    bool enabled = false;
    bool halted = false;
};

// Envelope: either a constant volume, or a decay from 15 to 0 at a programmable rate (optionally
// looping). Clocked on every quarter frame.
class Envelope {
public:
    // Register bits: --LC VVVV (L = loop, C = constant volume, V = volume / decay period).
    void write(std::uint8_t data) {
        loop = (data & 0x20) != 0;
        constantVolume = (data & 0x10) != 0;
        volumeOrPeriod = data & 0x0F;
    }
    void restart() { start = true; } // Written on the channel's 4th register
    void clockQuarterFrame() {
        if (start) {
            start = false;
            decayLevel = 15;
            divider = volumeOrPeriod;
        } else if (divider == 0) {
            divider = volumeOrPeriod;
            if (decayLevel > 0) {
                --decayLevel;
            } else if (loop) {
                decayLevel = 15;
            }
        } else {
            --divider;
        }
    }
    [[nodiscard]] std::uint8_t volume() const { return constantVolume ? volumeOrPeriod : decayLevel; }

private:
    bool start = false;
    bool loop = false;
    bool constantVolume = false;
    std::uint8_t volumeOrPeriod = 0;
    std::uint8_t divider = 0;
    std::uint8_t decayLevel = 0;
};
