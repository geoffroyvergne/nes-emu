#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;
struct _SDL_GameController;
using SDL_GameController = _SDL_GameController;

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

    // Toggles borderless fullscreen (using the desktop's current resolution,
    // not an exclusive video mode switch).
    void toggleFullscreen();

    void beginFrame(uint8_t r, uint8_t g, uint8_t b);
    void endFrame();

    // Uploads a width*height array of packed 0x00RRGGBB pixels (row-major,
    // matching Ppu2C02::frameBuffer()) and presents it, replacing
    // beginFrame()/endFrame() for actual PPU output.
    void presentFrame(const uint32_t* pixels);

    // Opens the default audio output device as mono 16-bit PCM, requesting
    // the given sample rate. The device starts paused (see resumeAudio())
    // so playback doesn't begin - and start underrunning - before any
    // samples are queued. Throws std::runtime_error on failure.
    void initAudio(int sampleRate);

    // The sample rate the device was actually opened at - use this (not the
    // rate requested from initAudio()) when generating/resampling audio to
    // queue, since some devices/drivers don't grant the exact requested
    // rate even when SDL is asked not to allow changes.
    int audioSampleRate() const { return audioSampleRate_; }

    // Starts audio playback. Call once a small prebuffer is queued (see
    // kAudioPrebufferBytes in main.cpp) so playback has a cushion against
    // per-frame timing jitter instead of underrunning immediately.
    void resumeAudio();

    // Pauses audio playback (e.g. when fast-forwarding/slow-motion, where
    // audio isn't queued at all - see main.cpp). Safe to call repeatedly.
    void pauseAudio();

    // Queues samples (each in [0, 1], silence = 0) for playback, converting
    // to signed 16-bit PCM. No-op if initAudio() wasn't called.
    void queueAudio(const std::vector<float>& samples);

    // Bytes currently queued but not yet played - use to detect the audio
    // device falling behind (e.g. during fast-forward) and throttle/drop
    // accordingly, since queuing unboundedly would grow playback latency.
    uint32_t queuedAudioBytes() const;

    // Discards any currently-queued but unplayed audio (e.g. when changing
    // playback speed, to avoid a backlog of now-stale samples).
    void clearQueuedAudio();

    // Connected gamepads, opened/closed automatically (including hotplug)
    // by pollEvents(). Indices are stable only within a single "no
    // connect/disconnect happened" stretch - a controller disconnecting
    // shifts every later index down by one, same as a std::vector erase.
    int gamepadCount() const { return static_cast<int>(gamepads_.size()); }

    // `sdlButton` is an SDL_GameControllerButton value (e.g.
    // SDL_CONTROLLER_BUTTON_A); out-of-range `index` returns false.
    bool gamepadButtonHeld(int index, int sdlButton) const;

    // `sdlAxis` is an SDL_GameControllerAxis value (e.g.
    // SDL_CONTROLLER_AXIS_LEFTY); range [-32768, 32767], 0 if out of range.
    int16_t gamepadAxis(int index, int sdlAxis) const;

private:
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* frameTexture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    std::array<bool, 512> keyPressedEdge_{};
    std::unordered_map<int32_t, bool> keyHeld_;
    uint32_t audioDevice_ = 0;
    int audioSampleRate_ = 0;
    std::vector<SDL_GameController*> gamepads_;
    bool fullscreen_ = false;
};

} // namespace nes
