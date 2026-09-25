#pragma once

#include "Controller.hpp"

#include <SDL.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

// Host-side input: turns the keyboard and SDL game controllers into NES button bytes
// (bit layout = Controller::Button: A, B, Select, Start, Up, Down, Left, Right = bits 0-7).

// A real D-pad can't press opposite directions and games aren't written to expect it (Super Mario
// Bros. can corrupt memory on Left+Right), so opposite pairs cancel out. Applied to the combined
// input of all sources for a player.
[[nodiscard]] constexpr std::uint8_t withoutOpposingDirections(std::uint8_t buttons) {
    constexpr std::uint8_t upDown = Controller::UP | Controller::DOWN;
    constexpr std::uint8_t leftRight = Controller::LEFT | Controller::RIGHT;
    if ((buttons & upDown) == upDown) {
        buttons &= static_cast<std::uint8_t>(~upDown);
    }
    if ((buttons & leftRight) == leftRight) {
        buttons &= static_cast<std::uint8_t>(~leftRight);
    }
    return buttons;
}

// Player 1 on the keyboard: Z = A, X = B, Right Shift / A = Select, Enter = Start, arrows = D-pad.
// Tracks the keys physically held; the caller cleans the combined state with
// withoutOpposingDirections(), so releasing one of two opposite keys brings the other back.
class KeyboardPad {
public:
    // Returns false for keys that aren't mapped to a button.
    [[nodiscard]] static bool mapKeyToButton(SDL_Keycode key, Controller::Button& button);

    // Handles a key press/release; returns false if the key isn't a controller key.
    bool handleKey(SDL_Keycode key, bool pressed);
    void releaseAll() { held = 0x00; }

    [[nodiscard]] std::uint8_t getHeld() const { return held; }
    // Held buttons with opposite directions removed.
    [[nodiscard]] std::uint8_t getState() const { return withoutOpposingDirections(held); }

private:
    std::uint8_t held = 0x00;
};

// SDL game controllers (Switch Pro, Xbox, PlayStation, 8BitDo, ...), hot-pluggable. Controllers are
// assigned to players in connection order. Buttons are mapped by physical position: NES A = bottom
// face button, NES B = right or left face button, Start/+ = Start, Back/Select/- = Select, D-pad and
// left stick = D-pad.
class GamepadManager {
public:
    // Initialises SDL's game controller subsystem. Already-connected pads are opened when SDL
    // delivers their "device added" events, so call handleEvent() for every event.
    GamepadManager();
    ~GamepadManager();
    GamepadManager(const GamepadManager&) = delete;
    GamepadManager& operator=(const GamepadManager&) = delete;

    [[nodiscard]] bool isAvailable() const { return available; }

    // Opens/closes pads on SDL_CONTROLLERDEVICEADDED / SDL_CONTROLLERDEVICEREMOVED.
    void handleEvent(const SDL_Event& event);

    // Current NES buttons of the player's pad (0 if that player has none), read live from SDL.
    [[nodiscard]] std::uint8_t getButtons(int player) const;
    [[nodiscard]] int getConnectedCount() const { return static_cast<int>(pads.size()); }
    [[nodiscard]] std::string getName(int player) const;

private:
    struct PadCloser {
        void operator()(SDL_GameController* pad) const { SDL_GameControllerClose(pad); }
    };
    struct Pad {
        std::unique_ptr<SDL_GameController, PadCloser> handle;
        SDL_JoystickID instanceId;
    };

    bool available = false;
    std::vector<Pad> pads; // Index = player
};
