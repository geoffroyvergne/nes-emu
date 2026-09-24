#pragma once

#include <cstdint>

// Standard NES controller: an 8-bit parallel-in/serial-out shift register (4021).
//
//   $4016 write, bit 0 = strobe. While strobe is 1 the register keeps reloading from the live
//   buttons (so reads always return A). The 1 -> 0 transition freezes the current buttons.
//   $4016/$4017 read: returns the next button in bit 0 and shifts, in the order
//   A, B, Select, Start, Up, Down, Left, Right. After 8 reads an official controller returns 1.
class Controller {
public:
    enum Button : std::uint8_t {
        A = 0x01,
        B = 0x02,
        SELECT = 0x04,
        START = 0x08,
        UP = 0x10,
        DOWN = 0x20,
        LEFT = 0x40,
        RIGHT = 0x80,
    };

    // Host side: live button state (bit layout = Button, i.e. the order the CPU reads them).
    void setButton(Button button, bool pressed);
    void setState(std::uint8_t buttons) { state = buttons; }
    [[nodiscard]] std::uint8_t getState() const { return state; }

    // CPU side.
    void writeStrobe(std::uint8_t data);
    // Bit 0 = button; bits 1-7 = open bus (the $40 high byte of the address lingers on the bus).
    // readOnly reads peek without shifting.
    [[nodiscard]] std::uint8_t read(bool readOnly = false);

private:
    static constexpr std::uint8_t OPEN_BUS_BITS = 0x40;

    std::uint8_t state = 0x00;         // Live buttons, updated by the host every frame
    std::uint8_t shiftRegister = 0x00; // Latched buttons being shifted out to the CPU
    bool strobe = false;
};
