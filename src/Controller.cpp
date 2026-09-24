#include "Controller.hpp"

void Controller::setButton(Button button, bool pressed) {
    state = pressed ? static_cast<std::uint8_t>(state | button) : static_cast<std::uint8_t>(state & ~button);
}

void Controller::writeStrobe(std::uint8_t data) {
    // While strobe is high the register continuously reloads, so whatever the buttons are at the
    // moment strobe drops (1 -> 0) is what gets shifted out. Reloading on both the rising write and
    // the falling write reproduces that without polling every cycle.
    if (strobe || (data & 0x01) != 0) {
        shiftRegister = state;
    }
    strobe = (data & 0x01) != 0;
}

std::uint8_t Controller::read(bool readOnly) {
    if (strobe) {
        // Still reloading: every read reports the live A button and nothing shifts.
        return static_cast<std::uint8_t>(OPEN_BUS_BITS | (state & 0x01));
    }

    const auto bit = static_cast<std::uint8_t>(shiftRegister & 0x01);
    if (!readOnly) {
        // Shift towards bit 0 and feed 1s in from the top: reads 9+ return 1 on official pads.
        shiftRegister = static_cast<std::uint8_t>((shiftRegister >> 1) | 0x80);
    }
    return static_cast<std::uint8_t>(OPEN_BUS_BITS | bit);
}
