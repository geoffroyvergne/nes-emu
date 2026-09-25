#include "HostInput.hpp"

#include <algorithm>
#include <iostream>

#if defined(__APPLE__)
#include <IOKit/hidsystem/IOHIDLib.h>
#endif

namespace {

// Left stick deflection (of 32767) that counts as a D-pad press.
constexpr Sint16 STICK_THRESHOLD = 16000;

// macOS only delivers game controller input reports to SDL's HID driver when the app that launched
// the emulator (Terminal, VS Code, ...) has the "Input Monitoring" permission. Without it the pad is
// still detected but its buttons never arrive. Explains this once, and asks macOS to show its
// permission prompt if the user has never been asked.
void checkControllerInputPermission() {
#if defined(__APPLE__)
    static bool checked = false;
    if (checked) {
        return;
    }
    checked = true;
    const IOHIDAccessType access = IOHIDCheckAccess(kIOHIDRequestTypeListenEvent);
    if (access == kIOHIDAccessTypeGranted) {
        return;
    }
    if (access == kIOHIDAccessTypeUnknown) {
        IOHIDRequestAccess(kIOHIDRequestTypeListenEvent); // Shows the system prompt (non-blocking)
    }
    std::cerr << "Warning: macOS \"Input Monitoring\" permission is not granted, so controller buttons won't reach\n"
                 "         the emulator (the pad is detected but its input is blocked). Enable the app you launch\n"
                 "         the emulator from (Terminal, iTerm, Visual Studio Code, ...) in System Settings >\n"
                 "         Privacy & Security > Input Monitoring, then quit and reopen that app.\n";
#endif
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Keyboard

bool KeyboardPad::mapKeyToButton(SDL_Keycode key, Controller::Button& button) {
    switch (key) {
    case SDLK_z: button = Controller::A; return true;
    case SDLK_x: button = Controller::B; return true;
    case SDLK_RSHIFT:
    case SDLK_a: button = Controller::SELECT; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: button = Controller::START; return true;
    case SDLK_UP: button = Controller::UP; return true;
    case SDLK_DOWN: button = Controller::DOWN; return true;
    case SDLK_LEFT: button = Controller::LEFT; return true;
    case SDLK_RIGHT: button = Controller::RIGHT; return true;
    default: return false;
    }
}

bool KeyboardPad::handleKey(SDL_Keycode key, bool pressed) {
    Controller::Button button{};
    if (!mapKeyToButton(key, button)) {
        return false;
    }
    if (pressed) {
        held |= button; // Key repeat just sets the same bit again
    } else {
        held &= static_cast<std::uint8_t>(~button);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------
// Game controllers

GamepadManager::GamepadManager() {
    // Report face buttons by position (south/east/west/north) rather than by printed label, so the
    // layout is the same on Nintendo, Xbox and PlayStation pads. (SDL3/sdl2-compat is always positional.)
    SDL_SetHint(SDL_HINT_GAMECONTROLLER_USE_BUTTON_LABELS, "0");
    // Drivers are SDL's defaults. To try another backend without rebuilding, SDL reads hints from
    // the environment, e.g. SDL_JOYSTICK_HIDAPI=0 (macOS: use Apple's GameController framework).
    available = SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) == 0;
    if (!available) {
        std::cerr << "Warning: game controllers unavailable (" << SDL_GetError() << "); keyboard only\n";
    }
}

GamepadManager::~GamepadManager() {
    pads.clear(); // Close the pads before shutting the subsystem down
    if (available) {
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
    }
}

void GamepadManager::handleEvent(const SDL_Event& event) {
    if (!available) {
        return;
    }
    if (event.type == SDL_CONTROLLERDEVICEADDED) {
        // cdevice.which is a device index here (an instance id for the other controller events).
        const int deviceIndex = event.cdevice.which;
        const SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(deviceIndex);
        const bool alreadyOpen = std::any_of(pads.begin(), pads.end(), [&](const Pad& pad) { return pad.instanceId == instanceId; });
        if (alreadyOpen) {
            return;
        }
        SDL_GameController* handle = SDL_GameControllerOpen(deviceIndex);
        if (handle == nullptr) {
            std::cerr << "Warning: cannot open controller: " << SDL_GetError() << '\n';
            return;
        }
        pads.push_back({std::unique_ptr<SDL_GameController, PadCloser>(handle), instanceId});
        checkControllerInputPermission();
        std::cout << "Controller connected: " << getName(static_cast<int>(pads.size()) - 1) << " (player "
                  << pads.size() << ')' << std::endl;
    } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
        const auto removed = std::find_if(pads.begin(), pads.end(), [&](const Pad& pad) { return pad.instanceId == event.cdevice.which; });
        if (removed != pads.end()) {
            std::cout << "Controller disconnected: " << SDL_GameControllerName(removed->handle.get()) << std::endl;
            pads.erase(removed); // Remaining pads move up a player slot
        }
    }
}

std::uint8_t GamepadManager::getButtons(int player) const {
    if (player < 0 || player >= static_cast<int>(pads.size())) {
        return 0x00;
    }
    SDL_GameController* pad = pads[static_cast<std::size_t>(player)].handle.get();
    const auto button = [pad](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(pad, b) != 0; };
    const Sint16 stickX = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
    const Sint16 stickY = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);

    std::uint8_t buttons = 0x00;
    // Buttons are positional (see the constructor): SDL "A" = bottom face button, "B" = right,
    // "X" = left, "Y" = top. Bottom = NES A (jump); right or left = NES B (run/fire).
    if (button(SDL_CONTROLLER_BUTTON_A)) {
        buttons |= Controller::A;
    }
    if (button(SDL_CONTROLLER_BUTTON_B) || button(SDL_CONTROLLER_BUTTON_X)) {
        buttons |= Controller::B;
    }
    if (button(SDL_CONTROLLER_BUTTON_BACK)) {
        buttons |= Controller::SELECT;
    }
    if (button(SDL_CONTROLLER_BUTTON_START)) {
        buttons |= Controller::START;
    }
    if (button(SDL_CONTROLLER_BUTTON_DPAD_UP) || stickY < -STICK_THRESHOLD) {
        buttons |= Controller::UP;
    }
    if (button(SDL_CONTROLLER_BUTTON_DPAD_DOWN) || stickY > STICK_THRESHOLD) {
        buttons |= Controller::DOWN;
    }
    if (button(SDL_CONTROLLER_BUTTON_DPAD_LEFT) || stickX < -STICK_THRESHOLD) {
        buttons |= Controller::LEFT;
    }
    if (button(SDL_CONTROLLER_BUTTON_DPAD_RIGHT) || stickX > STICK_THRESHOLD) {
        buttons |= Controller::RIGHT;
    }
    return buttons;
}

std::string GamepadManager::getName(int player) const {
    if (player < 0 || player >= static_cast<int>(pads.size())) {
        return {};
    }
    const char* name = SDL_GameControllerName(pads[static_cast<std::size_t>(player)].handle.get());
    return name != nullptr ? name : "Unknown controller";
}
