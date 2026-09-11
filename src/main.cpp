#include <SDL.h>

#include <cstdio>
#include <exception>
#include <memory>

#include "core/bus.h"
#include "core/cartridge.h"
#include "core/controller.h"
#include "core/cpu6502.h"
#include "core/ppu2C02.h"
#include "platform/sdl_app.h"

namespace {

uint8_t buildControllerMask(const uint8_t* keys) {
    using nes::Controller;
    uint8_t mask = 0;
    if (keys[SDL_SCANCODE_UP]) mask |= Controller::Up;
    if (keys[SDL_SCANCODE_DOWN]) mask |= Controller::Down;
    if (keys[SDL_SCANCODE_LEFT]) mask |= Controller::Left;
    if (keys[SDL_SCANCODE_RIGHT]) mask |= Controller::Right;
    if (keys[SDL_SCANCODE_X]) mask |= Controller::A;
    if (keys[SDL_SCANCODE_Z]) mask |= Controller::B;
    if (keys[SDL_SCANCODE_RETURN]) mask |= Controller::Start;
    if (keys[SDL_SCANCODE_RSHIFT]) mask |= Controller::Select;
    return mask;
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

        while (app.pollEvents()) {
            bus.controller(0).setButtons(buildControllerMask(app.keyboardState()));

            do {
                bus.clock();
            } while (!ppu.frameComplete());
            ppu.clearFrameComplete();

            app.presentFrame(ppu.frameBuffer().data());
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }

    return 0;
}
