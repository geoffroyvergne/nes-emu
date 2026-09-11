#pragma once

#include <cstdint>
#include <string>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace nes {

// Thin SDL2 wrapper: owns the window/renderer/texture and the frame loop
// plumbing. This is the only part of the project allowed to depend on SDL2 -
// the core emulator (Bus/Cpu6502/Ppu2C02/Cartridge/...) knows nothing about
// it.
//
// Key-to-controller mapping deliberately lives in main.cpp, not here:
// keyboardState() just exposes SDL's live key state so the application layer
// decides what each key means, keeping this class a generic platform shim.
class SdlApp {
public:
    SdlApp() = default;
    ~SdlApp();

    SdlApp(const SdlApp&) = delete;
    SdlApp& operator=(const SdlApp&) = delete;

    // Throws std::runtime_error on any SDL initialization failure. width/height
    // are the logical (pre-scale) render resolution, e.g. 256x240 for the NES
    // PPU framebuffer.
    void init(const std::string& title, int width, int height, int scale);

    // Pumps the SDL event queue; returns false once the user has requested
    // the window be closed.
    bool pollEvents();

    // Returns SDL's live keyboard state array, indexed by SDL_Scancode.
    const uint8_t* keyboardState() const;

    void beginFrame(uint8_t r, uint8_t g, uint8_t b);
    void endFrame();

    // Uploads a width*height array of packed 0x00RRGGBB pixels (row-major,
    // matching Ppu2C02::frameBuffer()) and presents it, replacing
    // beginFrame()/endFrame() for actual PPU output.
    void presentFrame(const uint32_t* pixels);

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* frameTexture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

} // namespace nes
