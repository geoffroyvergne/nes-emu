#include "platform/sdl_app.h"

#include <SDL.h>

#include <stdexcept>

namespace nes {

SdlApp::~SdlApp() {
    if (frameTexture_) SDL_DestroyTexture(frameTexture_);
    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_) SDL_DestroyWindow(window_);
    SDL_Quit();
}

void SdlApp::init(const std::string& title, int width, int height, int scale) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
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

} // namespace nes
