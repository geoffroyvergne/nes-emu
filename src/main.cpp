#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "core/apu2A03.h"
#include "core/bus.h"
#include "core/cartridge.h"
#include "core/controller.h"
#include "core/cpu6502.h"
#include "core/ppu2C02.h"
#include "core/state_io.h"
#include "platform/sdl_app.h"

namespace {

// A video frame's real-world duration: NTSC generates 341*262 dots at
// 21.477272 MHz / 4, i.e. ~60.0988 Hz; PAL generates 341*312 dots at
// 26.601712 MHz / 5, i.e. ~50.0070 Hz (more scanlines AND a slower pixel
// clock - not just "50 vs 60").
constexpr double kNtscFrameSeconds = 341.0 * 262.0 * 4.0 / 21477272.0;
constexpr double kPalFrameSeconds = 341.0 * 312.0 * 5.0 / 26601712.0;
constexpr double kMinSpeed = 0.25;
constexpr double kMaxSpeed = 4.0;
constexpr double kSpeedStep = 0.25;

// Caps how much audio can be queued but unplayed, so a slow consumer (or a
// sustained mismatch between production and playback rate) can't grow
// playback latency unboundedly.
constexpr uint32_t kMaxQueuedAudioBytes =
    static_cast<uint32_t>(nes::Apu2A03::kSampleRate) * sizeof(int16_t) / 4; // ~250ms cap (mono)

// Playback doesn't start until this much audio is queued, so a stall or a
// slow frame doesn't immediately run the device dry (an underrun sounds like
// a sharp click, not silence - frequent ones sound like harsh distortion).
constexpr uint32_t kAudioPrebufferBytes =
    static_cast<uint32_t>(nes::Apu2A03::kSampleRate) * sizeof(int16_t) * 3 / 20; // ~150ms

// The last slice of each frame's wait is a busy-spin instead of a sleep:
// std::this_thread::sleep_for's OS-level granularity can overshoot by
// several ms on some platforms, which is enough to throw off both frame
// pacing and, downstream, how much audio is produced per real second.
constexpr double kPacerSpinMarginSeconds = 0.002;

// If the pacer falls behind by more than this (e.g. the window was minimized
// or the process was paused/debugged), give up trying to catch up and just
// resync to real time, rather than burning through a burst of unthrottled
// frames - see the pacer comment in the main loop for why simply resetting
// the reference time every frame (instead of accumulating it) matters.
constexpr double kPacerMaxCatchUpSeconds = 0.25;

// Left-stick deflection past this (of a possible +-32767) counts as a
// D-pad direction, for gamepads used as an analog-only controller.
constexpr int16_t kGamepadAxisThreshold = 16000;

// Player 1 is keyboard OR the first connected gamepad (either can be used,
// simultaneously if you like); player 2 is the second gamepad only, if
// connected - there's no keyboard mapping for a second player.
uint8_t buildControllerMask(const nes::SdlApp& app, int playerIndex) {
    using nes::Controller;
    uint8_t mask = 0;

    if (playerIndex == 0) {
        const uint8_t* keys = app.keyboardState();
        // Arrows and the dedicated Return/Shift keys are fixed physical keys
        // unaffected by keyboard layout, so scancodes (physical position) are
        // fine for them. A/B are letter keys, mapped by keycode (what the key
        // actually types) instead, so "A"/"B" mean the printed letter even on
        // non-QWERTY layouts (e.g. AZERTY, where SDL_SCANCODE_A is physically
        // the "Q" key).
        if (keys[SDL_SCANCODE_UP]) mask |= Controller::Up;
        if (keys[SDL_SCANCODE_DOWN]) mask |= Controller::Down;
        if (keys[SDL_SCANCODE_LEFT]) mask |= Controller::Left;
        if (keys[SDL_SCANCODE_RIGHT]) mask |= Controller::Right;
        if (app.keyHeld(SDLK_a)) mask |= Controller::A;
        if (app.keyHeld(SDLK_b)) mask |= Controller::B;
        if (keys[SDL_SCANCODE_RETURN]) mask |= Controller::Start;
        if (keys[SDL_SCANCODE_RSHIFT]) mask |= Controller::Select;
    }

    if (playerIndex < app.gamepadCount()) {
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_DPAD_UP) ||
            app.gamepadAxis(playerIndex, SDL_CONTROLLER_AXIS_LEFTY) < -kGamepadAxisThreshold) {
            mask |= Controller::Up;
        }
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_DPAD_DOWN) ||
            app.gamepadAxis(playerIndex, SDL_CONTROLLER_AXIS_LEFTY) > kGamepadAxisThreshold) {
            mask |= Controller::Down;
        }
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_DPAD_LEFT) ||
            app.gamepadAxis(playerIndex, SDL_CONTROLLER_AXIS_LEFTX) < -kGamepadAxisThreshold) {
            mask |= Controller::Left;
        }
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_DPAD_RIGHT) ||
            app.gamepadAxis(playerIndex, SDL_CONTROLLER_AXIS_LEFTX) > kGamepadAxisThreshold) {
            mask |= Controller::Right;
        }
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_A)) mask |= Controller::A;
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_B)) mask |= Controller::B;
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_START)) mask |= Controller::Start;
        if (app.gamepadButtonHeld(playerIndex, SDL_CONTROLLER_BUTTON_BACK)) mask |= Controller::Select;
    }

    return mask;
}

void updateTitle(nes::SdlApp& app, double speed, bool paused, double fps) {
    char title[96];
    std::snprintf(title, sizeof(title), "NES Emulator - Speed: %d%% - %.0f FPS%s",
                  static_cast<int>(speed * 100.0 + 0.5), fps, paused ? " - PAUSED" : "");
    app.setTitle(title);
}

// A save state is just the concatenation of each component's saveState()
// output, in this fixed order (loadStateFile() must read them back in the
// same order). Written next to the ROM as "<rom path>.state".
std::string stateFilePath(const std::string& romPath) { return romPath + ".state"; }

void saveStateToFile(const std::string& path, const nes::Bus& bus, const nes::Cpu6502& cpu, const nes::Ppu2C02& ppu,
                      const nes::Apu2A03& apu, const nes::Cartridge* cartridge) {
    nes::StateWriter w;
    bus.saveState(w);
    cpu.saveState(w);
    ppu.saveState(w);
    apu.saveState(w);
    if (cartridge) cartridge->saveState(w);

    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(w.buffer().data()), static_cast<std::streamsize>(w.buffer().size()));
    if (!out) throw std::runtime_error("failed to write " + path);
}

// Returns false if the file doesn't exist or couldn't be opened. A file that
// exists but is truncated/incompatible (e.g. from a different ROM or an
// older build) throws partway through StateReader and is caught here too,
// but note that's a best-effort check, not a transaction: components read
// before the failing one have already been mutated. In practice the only
// realistic source of such a file is hand-tampering, since these states are
// only ever produced by saveStateToFile() itself.
bool loadStateFromFile(const std::string& path, nes::Bus& bus, nes::Cpu6502& cpu, nes::Ppu2C02& ppu, nes::Apu2A03& apu,
                        nes::Cartridge* cartridge) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    try {
        nes::StateReader r(data);
        bus.loadState(r);
        cpu.loadState(r);
        ppu.loadState(r);
        apu.loadState(r);
        if (cartridge) cartridge->loadState(r);
    } catch (const std::exception&) {
        return false;
    }
    return true;
}

// Resamples (nearest-neighbor) a batch of samples produced at `inRate` Hz of
// NES-emulated audio time down/up to what the audio device actually needs
// for correct pitch, folding in two independent rate mismatches at once:
//  - `speed`: a batch that isn't 1x speed was produced in (1/speed)x the
//    real time a frame normally takes, so it needs proportionally fewer/more
//    output samples to play back in the right amount of real time (this is
//    also what keeps audio playing - pitched up/down like a tape - through
//    speed changes instead of muting).
//  - `deviceRate`: the audio device may not have been opened at exactly
//    `inRate` (some devices/drivers don't grant the exact rate requested),
//    which would otherwise pitch-shift every sample, not just during speed
//    changes.
// Best-effort fallback for when the ROM header (unreliably) claims NTSC:
// looks for common No-Intro/GoodNES-style region tags in the filename, e.g.
// "Game (Europe).nes" or "Game (E).nes". Conservative on purpose - a wrong
// guess here just swaps which direction the speed is off in, so this only
// fires on a clear, standard-looking tag rather than trying to be clever.
bool filenameSuggestsPal(const std::string& path) {
    static const char* kPalTags[] = {"(Europe)", "(E)", "(PAL)", "(Germany)", "(France)",
                                      "(Italy)", "(Spain)", "(UK)", "(Australia)"};
    for (const char* tag : kPalTags) {
        if (path.find(tag) != std::string::npos) return true;
    }
    return false;
}

std::vector<float> resampleAudio(const std::vector<float>& in, double inRate, double speed, double deviceRate) {
    double ratio = (inRate * speed) / deviceRate; // Input samples consumed per output sample.
    if (in.empty() || ratio == 1.0) return in;
    size_t outCount = static_cast<size_t>(static_cast<double>(in.size()) / ratio + 0.5);
    std::vector<float> out(outCount);
    for (size_t i = 0; i < outCount; i++) {
        size_t srcIndex = static_cast<size_t>(static_cast<double>(i) * ratio);
        if (srcIndex >= in.size()) srcIndex = in.size() - 1;
        out[i] = in[srcIndex];
    }
    return out;
}

} // namespace

int main(int argc, char** argv) {
    nes::Bus bus;
    nes::Ppu2C02 ppu;
    nes::Apu2A03 apu;
    nes::Cpu6502 cpu(bus);
    bus.connectCpu(&cpu);
    bus.connectPpu(&ppu);
    bus.connectApu(&apu);
    apu.connectBus(&bus);

    // --pal/--ntsc force the region regardless of the ROM header's claim,
    // since iNES 1.0 headers are notoriously unreliable about this (most
    // dumping tools left the TV-system byte at 0/NTSC regardless of the
    // ROM's actual region).
    bool forcePal = false;
    bool forceNtsc = false;
    for (int i = 2; i < argc; i++) {
        std::string arg = argv[i];
        if (arg == "--pal") forcePal = true;
        if (arg == "--ntsc") forceNtsc = true;
    }

    std::unique_ptr<nes::Cartridge> cartridge;
    bool isPal = false;
    if (argc > 1) {
        try {
            cartridge = std::make_unique<nes::Cartridge>(argv[1]);
            bus.insertCartridge(cartridge.get());
            ppu.connectCartridge(cartridge.get());

            const char* regionSource = "header";
            if (forcePal) {
                isPal = true;
                regionSource = "--pal";
            } else if (forceNtsc) {
                isPal = false;
                regionSource = "--ntsc";
            } else if (cartridge->tvSystem() == nes::TvSystem::PAL) {
                isPal = true;
                regionSource = "header";
            } else if (filenameSuggestsPal(argv[1])) {
                // The header claims NTSC, which is the common default value
                // unreliable dumps leave it at - but the filename has a
                // standard PAL region tag, which is a stronger signal.
                isPal = true;
                regionSource = "filename";
            } else {
                isPal = false;
                regionSource = "header (default)";
            }

            std::printf("Loaded ROM '%s' (mapper %u). Running as %s (source: %s).\n", argv[1], cartridge->mapperId(),
                        isPal ? "PAL (~50Hz)" : "NTSC (~60Hz)", regionSource);
            if (!forcePal && !forceNtsc) {
                std::printf(
                    "  Note: region auto-detection can still be wrong (unreliable header + a filename-based "
                    "guess). If this game sounds/plays at the wrong speed, relaunch with --pal or --ntsc to "
                    "override it, or press '-' in-game to slow down (and '+' / '0' to speed up / reset).\n");
            }
        } catch (const std::exception& e) {
            std::fprintf(stderr, "Failed to load ROM: %s\n", e.what());
            return 1;
        }
    } else {
        std::printf("No ROM specified (usage: %s <rom.nes> [--pal|--ntsc]). Running with no cartridge inserted.\n",
                    argv[0]);
    }

    bus.setRegion(isPal);
    ppu.setRegion(isPal);
    apu.setRegion(isPal);
    bus.reset();

    double nesFrameSeconds = isPal ? kPalFrameSeconds : kNtscFrameSeconds;

    try {
        nes::SdlApp app;
        app.init("NES Emulator", 256, 240, 3);
        app.initAudio(nes::Apu2A03::kSampleRate);
        std::printf("Audio device opened at %d Hz (requested %d Hz).\n", app.audioSampleRate(),
                    nes::Apu2A03::kSampleRate);

        double speed = 1.0;
        bool paused = false;
        double currentFps = isPal ? 1.0 / kPalFrameSeconds : 1.0 / kNtscFrameSeconds;
        updateTitle(app, speed, paused, currentFps);
        auto nextFrameTime = std::chrono::steady_clock::now();
        std::vector<float> audioSamples;
        bool audioStarted = false;

        int fpsFrameCount = 0;
        auto fpsWindowStart = std::chrono::steady_clock::now();

        const std::string statePath = argc > 1 ? stateFilePath(argv[1]) : std::string();

        while (app.pollEvents()) {
            if (app.keyPressed(SDL_SCANCODE_EQUALS) || app.keyPressed(SDL_SCANCODE_KP_PLUS)) {
                speed = std::min(kMaxSpeed, speed + kSpeedStep);
                updateTitle(app, speed, paused, currentFps);
            }
            if (app.keyPressed(SDL_SCANCODE_MINUS) || app.keyPressed(SDL_SCANCODE_KP_MINUS)) {
                speed = std::max(kMinSpeed, speed - kSpeedStep);
                updateTitle(app, speed, paused, currentFps);
            }
            if (app.keyPressed(SDL_SCANCODE_0) || app.keyPressed(SDL_SCANCODE_KP_0)) {
                speed = 1.0;
                updateTitle(app, speed, paused, currentFps);
            }
            if (app.keyPressed(SDL_SCANCODE_P)) {
                paused = !paused;
                updateTitle(app, speed, paused, currentFps);
            }
            if (app.keyPressed(SDL_SCANCODE_R)) {
                bus.reset();
                // The APU reset silences everything; drop any stale queued
                // audio and re-prebuffer rather than play a click into silence.
                app.pauseAudio();
                app.clearQueuedAudio();
                audioStarted = false;
            }
            if (app.keyPressed(SDL_SCANCODE_F)) {
                app.toggleFullscreen();
            }
            if (!statePath.empty() && app.keyPressed(SDL_SCANCODE_S)) {
                try {
                    saveStateToFile(statePath, bus, cpu, ppu, apu, cartridge.get());
                    std::printf("State saved to '%s'.\n", statePath.c_str());
                } catch (const std::exception& e) {
                    std::fprintf(stderr, "Failed to save state: %s\n", e.what());
                }
            }
            if (!statePath.empty() && app.keyPressed(SDL_SCANCODE_L)) {
                if (loadStateFromFile(statePath, bus, cpu, ppu, apu, cartridge.get())) {
                    std::printf("State loaded from '%s'.\n", statePath.c_str());
                    app.pauseAudio();
                    app.clearQueuedAudio();
                    audioStarted = false;
                } else {
                    std::printf("No (valid) save state found at '%s'.\n", statePath.c_str());
                }
            }

            bus.controller(0).setButtons(buildControllerMask(app, 0));
            bus.controller(1).setButtons(buildControllerMask(app, 1));

            if (!paused) {
                do {
                    bus.clock();
                } while (!ppu.frameComplete());
                ppu.clearFrameComplete();
            }

            app.presentFrame(ppu.frameBuffer().data());

            audioSamples.clear();
            if (!paused) apu.drainSamples(audioSamples);
            if (app.queuedAudioBytes() < kMaxQueuedAudioBytes) {
                app.queueAudio(resampleAudio(audioSamples, nes::Apu2A03::kSampleRate, speed, app.audioSampleRate()));
            }
            if (!audioStarted && app.queuedAudioBytes() >= kAudioPrebufferBytes) {
                app.resumeAudio();
                audioStarted = true;
            }

            fpsFrameCount++;
            double fpsElapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - fpsWindowStart).count();
            if (fpsElapsed >= 1.0) {
                currentFps = fpsFrameCount / fpsElapsed;
                updateTitle(app, speed, paused, currentFps);
                fpsFrameCount = 0;
                fpsWindowStart = std::chrono::steady_clock::now();
            }

            // Pace to the NES's real frame rate (scaled by `speed`) rather
            // than relying on vsync, so speed changes actually take effect
            // and playback isn't at the mercy of the display/driver's vsync
            // behavior. The final couple of ms are a busy-spin rather than a
            // sleep for precision (see kPacerSpinMarginSeconds) - imprecise
            // pacing here throws off audio production too, since samples are
            // generated in lockstep with emulated frames.
            //
            // nextFrameTime accumulates by exactly targetSeconds each
            // iteration rather than being reset to "now" after waiting: a
            // frame that runs even slightly over budget (OS scheduling
            // jitter, a slow frame) is compensated by a shorter wait next
            // frame instead of being permanently lost. Resetting to "now"
            // every frame silently loses that overrun time - individually
            // negligible, but it compounds into audio (generated in lockstep
            // with emulated frames) noticeably outrunning real time over a
            // play session, since 44100 Hz playback has no such slack.
            double targetSeconds = nesFrameSeconds / speed;
            nextFrameTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(targetSeconds));

            auto now = std::chrono::steady_clock::now();
            if (nextFrameTime < now - std::chrono::duration<double>(kPacerMaxCatchUpSeconds)) {
                nextFrameTime = now; // Fell too far behind (stall/pause): resync instead of catching up in a burst.
            }

            double remaining = std::chrono::duration<double>(nextFrameTime - now).count();
            if (remaining > kPacerSpinMarginSeconds) {
                std::this_thread::sleep_for(std::chrono::duration<double>(remaining - kPacerSpinMarginSeconds));
            }
            while (std::chrono::steady_clock::now() < nextFrameTime) {
                // Busy-spin out the last fraction of a millisecond for precise pacing.
            }
        }
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }

    return 0;
}
