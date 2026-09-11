#pragma once

#include <cstdint>

namespace nes {

// Emulates one NES controller's shift-register protocol. The frontend calls
// setButtons() each frame with the live button state (from keyboard or
// gamepad); the Bus calls write()/read() to service $4016/$4017 the way the
// CPU would talk to a real controller.
class Controller {
public:
    enum Button : uint8_t {
        A = 0x80,
        B = 0x40,
        Select = 0x20,
        Start = 0x10,
        Up = 0x08,
        Down = 0x04,
        Left = 0x02,
        Right = 0x01,
    };

    void setButtons(uint8_t mask) { liveState_ = mask; }

    void write(uint8_t data) {
        strobing_ = (data & 0x01) != 0;
        if (strobing_) shiftReg_ = liveState_;
    }

    uint8_t read() {
        if (strobing_) {
            shiftReg_ = liveState_;
            return (shiftReg_ & 0x80) ? 1 : 0;
        }
        uint8_t bit = (shiftReg_ & 0x80) ? 1 : 0;
        shiftReg_ = static_cast<uint8_t>((shiftReg_ << 1) | 1);
        return bit;
    }

private:
    uint8_t liveState_ = 0;
    uint8_t shiftReg_ = 0;
    bool strobing_ = false;
};

} // namespace nes
