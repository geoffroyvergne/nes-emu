#include "platform/sdl_app.h"

#include <SDL.h>

#include <algorithm>
#include <stdexcept>

namespace nes {

SdlApp::~SdlApp() {
    for (SDL_GameController* gc : gamepads_) SDL_GameControllerClose(gc);
    if (audioDevice_) SDL_CloseAudioDevice(audioDevice_);
    if (frameTexture_) SDL_DestroyTexture(frameTexture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SdlApp::init(const std::string& title, int width, int height, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }

    width_ = width;
    height_ = height;

    window_ = SDL_CreateWindow(title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                width * scale, height * scale, SDL_WINDOW_SHOWN);
    if (!window_) {
        throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
    }

    // No SDL_RENDERER_PRESENTVSYNC: frame pacing (including speed adjustment)
    // is handled explicitly in main.cpp's loop instead, since vsync would
    // lock playback to the display's refresh rate and defeat speed changes.
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_) {
        throw std::runtime_error(std::string("SDL_CreateRenderer failed: ") + SDL_GetError());
    }
    SDL_RenderSetLogicalSize(renderer_, width, height);

    frameTexture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!frameTexture_) {
        throw std::runtime_error(std::string("SDL_CreateTexture failed: ") + SDL_GetError());
    }
}

const uint8_t* SdlApp::keyboardState() const {
    return SDL_GetKeyboardState(nullptr);
}

bool SdlApp::pollEvents() {
    keyPressedEdge_.fill(false);

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_QUIT) {
            return false;
        }
        if (event.type == SDL_KEYDOWN) {
            if (event.key.keysym.sym == SDLK_ESCAPE) {
                return false;
            }
            if (!event.key.repeat && event.key.keysym.scancode < keyPressedEdge_.size()) {
                keyPressedEdge_[event.key.keysym.scancode] = true;
            }
            keyHeld_[event.key.keysym.sym] = true;
        } else if (event.type == SDL_KEYUP) {
            keyHeld_[event.key.keysym.sym] = false;
        } else if (event.type == SDL_CONTROLLERDEVICEADDED) {
            // `which` is a device *index* for this event only.
            SDL_GameController* gc = SDL_GameControllerOpen(event.cdevice.which);
            if (gc) gamepads_.push_back(gc);
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            // `which` is the joystick *instance ID* here, not a device index.
            SDL_JoystickID instanceId = event.cdevice.which;
            for (auto it = gamepads_.begin(); it != gamepads_.end(); ++it) {
                if (SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(*it)) == instanceId) {
                    SDL_GameControllerClose(*it);
                    gamepads_.erase(it);
                    break;
                }
            }
        }
    }
    return true;
}

bool SdlApp::keyHeld(int32_t keycode) const {
    auto it = keyHeld_.find(keycode);
    return it != keyHeld_.end() && it->second;
}

bool SdlApp::keyPressed(int scancode) const {
    return scancode >= 0 && static_cast<size_t>(scancode) < keyPressedEdge_.size() && keyPressedEdge_[scancode];
}

void SdlApp::setTitle(const std::string& title) {
    SDL_SetWindowTitle(window_, title.c_str());
}

void SdlApp::toggleFullscreen() {
    fullscreen_ = !fullscreen_;
    SDL_SetWindowFullscreen(window_, fullscreen_ ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}

void SdlApp::beginFrame(uint8_t r, uint8_t g, uint8_t b) {
    SDL_SetRenderDrawColor(renderer_, r, g, b, 255);
    SDL_RenderClear(renderer_);
}

void SdlApp::endFrame() {
    SDL_RenderPresent(renderer_);
}

void SdlApp::presentFrame(const uint32_t* pixels) {
    SDL_UpdateTexture(frameTexture_, nullptr, pixels, width_ * static_cast<int>(sizeof(uint32_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, frameTexture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

void SdlApp::initAudio(int sampleRate) {
    SDL_AudioSpec desired{};
    desired.freq = sampleRate;
    desired.format = AUDIO_S16SYS;
    desired.channels = 1;
    // A larger hardware buffer than the ~735 samples/frame we produce gives
    // playback more cushion against per-frame timing jitter before it runs
    // dry (which sounds like harsh clicking/distortion, not just silence).
    desired.samples = 4096;

    // allowed_changes=0 asks SDL to convert internally so `obtained` matches
    // `desired` - but audioSampleRate() reads back the real value rather
    // than assuming that held, since some devices/drivers (aggregate/
    // multi-output devices in particular) can be inconsistent about it.
    SDL_AudioSpec obtained{};
    audioDevice_ = SDL_OpenAudioDevice(nullptr, 0, &desired, &obtained, 0);
    if (audioDevice_ == 0) {
        throw std::runtime_error(std::string("SDL_OpenAudioDevice failed: ") + SDL_GetError());
    }
    audioSampleRate_ = obtained.freq;
    // Left paused: playback starts via resumeAudio() once a prebuffer is
    // queued, rather than immediately underrunning against an empty queue.
}

void SdlApp::resumeAudio() {
    if (audioDevice_) SDL_PauseAudioDevice(audioDevice_, 0);
}

void SdlApp::pauseAudio() {
    if (audioDevice_) SDL_PauseAudioDevice(audioDevice_, 1);
}

void SdlApp::queueAudio(const std::vector<float>& samples) {
    if (audioDevice_ == 0 || samples.empty()) return;

    std::vector<int16_t> pcm(samples.size());
    for (size_t i = 0; i < samples.size(); i++) {
        pcm[i] = static_cast<int16_t>(std::clamp(samples[i], 0.0f, 1.0f) * 32767.0f);
    }
    SDL_QueueAudio(audioDevice_, pcm.data(), static_cast<Uint32>(pcm.size() * sizeof(int16_t)));
}

uint32_t SdlApp::queuedAudioBytes() const {
    return audioDevice_ ? SDL_GetQueuedAudioSize(audioDevice_) : 0;
}

void SdlApp::clearQueuedAudio() {
    if (audioDevice_) SDL_ClearQueuedAudio(audioDevice_);
}

bool SdlApp::gamepadButtonHeld(int index, int sdlButton) const {
    if (index < 0 || index >= static_cast<int>(gamepads_.size())) return false;
    return SDL_GameControllerGetButton(gamepads_[static_cast<size_t>(index)],
                                        static_cast<SDL_GameControllerButton>(sdlButton)) != 0;
}

int16_t SdlApp::gamepadAxis(int index, int sdlAxis) const {
    if (index < 0 || index >= static_cast<int>(gamepads_.size())) return 0;
    return SDL_GameControllerGetAxis(gamepads_[static_cast<size_t>(index)],
                                      static_cast<SDL_GameControllerAxis>(sdlAxis));
}

} // namespace nes
