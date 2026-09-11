#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <memory>
#include <thread>

#include "core/bus.h"
#include "core/cartridge.h"
#include "core/controller.h"
#include "core/cpu6502.h"
#include "core/ppu2C02.h"
#include "platform/sdl_app.h"

namespace {

// The NES's NTSC PPU generates a new frame every 341*262 dots at 21.477272
// MHz / 4, i.e. ~60.0988 Hz - use that rather than a flat 60 for pacing.
constexpr double kNesFrameSeconds = 341.0 * 262.0 * 4.0 / 21477272.0;
constexpr double kMinSpeed = 0.25;
constexpr double kMaxSpeed = 4.0;
constexpr double kSpeedStep = 0.25;

uint8_t buildControllerMask(const nes::SdlApp& app) {
    using nes::Controller;
    const uint8_t* keys = app.keyboardState();
    uint8_t mask = 0;
    // Arrows and the dedicated Return/Shift keys are fixed physical keys
    // unaffected by keyboard layout, so scancodes (physical position) are
    // fine for them. A/B are letter keys, mapped by keycode (what the key
    // actually types) instead, so "X"/"Z" mean the printed letter even on
    // non-QWERTY layouts (e.g. AZERTY, where SDL_SCANCODE_Z is physically
    // the "W" key).
    if (keys[SDL_SCANCODE_UP]) mask |= Controller::Up;
    if (keys[SDL_SCANCODE_DOWN]) mask |= Controller::Down;
    if (keys[SDL_SCANCODE_LEFT]) mask |= Controller::Left;
    if (keys[SDL_SCANCODE_RIGHT]) mask |= Controller::Right;
    if (app.keyHeld(SDLK_x)) mask |= Controller::A;
    if (app.keyHeld(SDLK_z)) mask |= Controller::B;
    if (keys[SDL_SCANCODE_RETURN]) mask |= Controller::Start;
    if (keys[SDL_SCANCODE_RSHIFT]) mask |= Controller::Select;
    return mask;
}

void updateTitle(nes::SdlApp& app, double speed) {
    char title[64];
    std::snprintf(title, sizeof(title), "NES Emulator - Speed: %d%%", static_cast<int>(speed * 100.0 + 0.5));
    app.setTitle(title);
}

} // namespace

int main(int argc, char** argv) {
    nes::Bus bus;
    nes::Ppu2C02 ppu;
    nes::Cpu6502 cpu(bus);
    bus.connectCpu(&cpu);
    bus.connectPpu(&ppu);

    std::unique_ptr<nes::Cartridge> cartridge;
    if (argc > 1) {
        try {
            cartridge = std::make_unique<nes::Cartridge>(argv[1]);
            bus.insertCartridge(cartridge.get());
            ppu.connectCartridge(cartridge.get());
            std::printf("Loaded ROM '%s' (mapper %u).\n", argv[1], cartridge->mapperId());
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to load ROM: %s\n", e.what());
            return 1;
        }
    } else {
        std::printf("No ROM specified (usage: %s <rom.nes>). Running with no cartridge inserted.\n", argv[0]);
    }

    bus.reset();

    try {
        nes::SdlApp app;
        app.init("NES Emulator", 256, 240, 3);

        double speed = 1.0;
        updateTitle(app, speed);
        auto frameStart = std::chrono::steady_clock::now();

        while (app.pollEvents()) {
            if (app.keyPressed(SDL_SCANCODE_EQUALS) || app.keyPressed(SDL_SCANCODE_KP_PLUS)) {
                speed = std::min(kMaxSpeed, speed + kSpeedStep);
                updateTitle(app, speed);
            }
            if (app.keyPressed(SDL_SCANCODE_MINUS) || app.keyPressed(SDL_SCANCODE_KP_MINUS)) {
                speed = std::max(kMinSpeed, speed - kSpeedStep);
                updateTitle(app, speed);
            }
            if (app.keyPressed(SDL_SCANCODE_0) || app.keyPressed(SDL_SCANCODE_KP_0)) {
                speed = 1.0;
                updateTitle(app, speed);
            }

            bus.controller(0).setButtons(buildControllerMask(app));

            do {
                bus.clock();
            } while (!ppu.frameComplete());
            ppu.clearFrameComplete();

            app.presentFrame(ppu.frameBuffer().data());

            // Pace to the NES's real frame rate (scaled by `speed`) rather
            // than relying on vsync, so speed changes actually take effect
            // and playback isn't at the mercy of the display/driver's vsync
            // behavior.
            double targetSeconds = kNesFrameSeconds / speed;
            std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - frameStart;
            if (elapsed.count() < targetSeconds) {
                std::this_thread::sleep_for(std::chrono::duration<double>(targetSeconds - elapsed.count()));
            }
            frameStart = std::chrono::steady_clock::now();
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }

    return 0;
}
