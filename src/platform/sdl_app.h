#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>

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
// keyboardState()/keyHeld() just expose SDL's live key state so the
// application layer decides what each key means, keeping this class a
// generic platform shim.
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
    // the window be closed. Also refreshes the key-just-pressed edges that
    // keyPressed() reports for this frame.
    bool pollEvents();

    // Returns SDL's live keyboard state array, indexed by SDL_Scancode (a
    // fixed physical key position on a US QWERTY reference layout,
    // regardless of the OS keyboard layout actually in effect). Good for
    // things like arrow keys where physical position is what matters.
    const uint8_t* keyboardState() const;

    // True if the key producing this SDL_Keycode (a layout-aware *character*,
    // e.g. SDLK_z - what actually gets typed, not a fixed physical position)
    // is currently held. Use this for letter-key mappings so they match the
    // printed keycap regardless of keyboard layout (e.g. AZERTY, where the
    // physical position of SDL_SCANCODE_Z prints "W", not "Z").
    bool keyHeld(int32_t keycode) const;

    // True if this key transitioned from up to down since the last
    // pollEvents() call (i.e. a single edge, not auto-repeat) - use this for
    // one-shot actions like adjusting speed, as opposed to keyboardState()
    // which is for continuously-held buttons like the D-pad.
    bool keyPressed(int scancode) const;

    void setTitle(const std::string& title);

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
    std::array<bool, 512> keyPressedEdge_{};
    std::unordered_map<int32_t, bool> keyHeld_;
};

} // namespace nes
