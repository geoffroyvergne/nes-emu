#include "Bus.hpp"
#include "Cartridge.hpp"
#include "Controller.hpp"
#include "Cpu6502.hpp"
#include "Ppu2C02.hpp"

#include <SDL.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <memory>

namespace {

constexpr int NES_WIDTH = 256;
constexpr int NES_HEIGHT = 240;
constexpr int WINDOW_SCALE = 3;

// RAII deleters so SDL resources are released automatically in reverse order.
struct SdlWindowDeleter {
    void operator()(SDL_Window* window) const { SDL_DestroyWindow(window); }
};

struct SdlRendererDeleter {
    void operator()(SDL_Renderer* renderer) const { SDL_DestroyRenderer(renderer); }
};

struct SdlTextureDeleter {
    void operator()(SDL_Texture* texture) const { SDL_DestroyTexture(texture); }
};

using WindowPtr = std::unique_ptr<SDL_Window, SdlWindowDeleter>;
using RendererPtr = std::unique_ptr<SDL_Renderer, SdlRendererDeleter>;
using TexturePtr = std::unique_ptr<SDL_Texture, SdlTextureDeleter>;

// Owns SDL subsystem initialisation for the lifetime of the object.
class SdlContext {
public:
    explicit SdlContext(std::uint32_t flags) : initialized(SDL_Init(flags) == 0) {}
    ~SdlContext() {
        if (initialized) {
            SDL_Quit();
        }
    }
    SdlContext(const SdlContext&) = delete;
    SdlContext& operator=(const SdlContext&) = delete;

    [[nodiscard]] bool isInitialized() const { return initialized; }

private:
    bool initialized;
};

void printCartridgeInfo(const Cartridge& cartridge) {
    std::cout << "PRG-ROM:   " << +cartridge.getPrgBankCount() << " x 16KB ("
              << cartridge.getPrgBankCount() * Cartridge::PRG_BANK_SIZE / 1024 << " KB)\n"
              << "CHR-ROM:   " << +cartridge.getChrBankCount() << " x 8KB ("
              << cartridge.getChrBankCount() * Cartridge::CHR_BANK_SIZE / 1024 << " KB)"
              << (cartridge.usesChrRam() ? " -> uses 8KB CHR-RAM" : "") << '\n'
              << "Mapper:    " << +cartridge.getMapperId() << '\n'
              << "Mirroring: " << toString(cartridge.getMirroring()) << '\n'
              << "Trainer:   " << (cartridge.hasTrainer() ? "yes" : "no") << '\n'
              << "Battery:   " << (cartridge.hasBatteryRam() ? "yes" : "no") << std::endl;
}

// Debug view: both 128x128 pattern tables side by side, vertically centred in the 256x240 screen,
// with the 32 palette RAM entries as 8x8 swatches underneath.
constexpr int PATTERN_TABLE_COUNT = 2;
constexpr int PATTERN_VIEW_Y = (NES_HEIGHT - Ppu2C02::PATTERN_TABLE_PIXELS) / 2;
constexpr int SWATCH_SIZE = 8;
constexpr int SWATCH_VIEW_Y = PATTERN_VIEW_Y + Ppu2C02::PATTERN_TABLE_PIXELS + SWATCH_SIZE;
constexpr int PALETTE_VIEW_WIDTH = SWATCH_SIZE * Ppu2C02::COLORS_PER_PALETTE;

TexturePtr createPatternTableTexture(SDL_Renderer* renderer) {
    return TexturePtr(SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                        Ppu2C02::PATTERN_TABLE_PIXELS, Ppu2C02::PATTERN_TABLE_PIXELS));
}

void drawPatternTables(SDL_Renderer* renderer, Ppu2C02& ppu, const std::array<TexturePtr, PATTERN_TABLE_COUNT>& textures,
                       std::uint8_t paletteId) {
    constexpr int pitch = Ppu2C02::PATTERN_TABLE_PIXELS * static_cast<int>(sizeof(std::uint32_t));
    for (int table = 0; table < PATTERN_TABLE_COUNT; ++table) {
        SDL_Texture* texture = textures[static_cast<std::size_t>(table)].get();
        SDL_UpdateTexture(texture, nullptr, ppu.getPatternTable(table, paletteId).data(), pitch);

        const SDL_Rect dest{table * Ppu2C02::PATTERN_TABLE_PIXELS, PATTERN_VIEW_Y, Ppu2C02::PATTERN_TABLE_PIXELS,
                            Ppu2C02::PATTERN_TABLE_PIXELS};
        SDL_RenderCopy(renderer, texture, nullptr, &dest);
    }
}

void drawPaletteSwatches(SDL_Renderer* renderer, Ppu2C02& ppu, std::uint8_t selectedPalette) {
    for (int palette = 0; palette < Ppu2C02::PALETTE_COUNT; ++palette) {
        for (int entry = 0; entry < Ppu2C02::COLORS_PER_PALETTE; ++entry) {
            const Ppu2C02::Color color =
                ppu.getColorFromPaletteRAM(static_cast<std::uint8_t>(palette), static_cast<std::uint8_t>(entry));
            SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, 255);
            const SDL_Rect swatch{palette * PALETTE_VIEW_WIDTH + entry * SWATCH_SIZE, SWATCH_VIEW_Y, SWATCH_SIZE,
                                  SWATCH_SIZE};
            SDL_RenderFillRect(renderer, &swatch);
        }
    }
    // Outline the palette the pattern tables are drawn with.
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    const SDL_Rect outline{selectedPalette * PALETTE_VIEW_WIDTH, SWATCH_VIEW_Y - 1, PALETTE_VIEW_WIDTH, SWATCH_SIZE + 2};
    SDL_RenderDrawRect(renderer, &outline);
}

void updateWindowTitle(SDL_Window* window, bool showDebugView, std::uint8_t paletteId) {
    const std::string title =
        showDebugView ? std::format("NES Emulator - pattern tables, palette {} ({}) - P: next palette, Tab: game",
                                    paletteId, paletteId < Ppu2C02::PALETTE_COUNT / 2 ? "background" : "sprite")
                      : std::string("NES Emulator - Tab: debug view");
    SDL_SetWindowTitle(window, title.c_str());
}

void drawFrame(SDL_Renderer* renderer, SDL_Texture* texture, const Ppu2C02::FrameBuffer& frame) {
    constexpr int pitch = Ppu2C02::SCREEN_WIDTH * static_cast<int>(sizeof(std::uint32_t));
    SDL_UpdateTexture(texture, nullptr, frame.data(), pitch);
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);
}

// Keyboard layout for player 1. Returns false for keys that aren't mapped to a button.
bool mapKeyToButton(SDL_Keycode key, Controller::Button& button) {
    switch (key) {
    case SDLK_z: button = Controller::A; return true;
    case SDLK_x: button = Controller::B; return true;
    case SDLK_SPACE: button = Controller::SELECT; return true;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: button = Controller::START; return true;
    case SDLK_UP: button = Controller::UP; return true;
    case SDLK_DOWN: button = Controller::DOWN; return true;
    case SDLK_LEFT: button = Controller::LEFT; return true;
    case SDLK_RIGHT: button = Controller::RIGHT; return true;
    default: return false;
    }
}

// NTSC: the PPU draws 3 dots for every CPU cycle.
constexpr int PPU_DOTS_PER_CPU_CYCLE = 3;

// Runs the system until the PPU finishes a frame (~29781 CPU cycles).
void runFrame(Cpu6502& cpu, Ppu2C02& ppu, Bus& bus) {
    do {
        cpu.clock();
        if (bus.pollOamDma()) {
            cpu.stallForOamDma();
        }
        for (int dot = 0; dot < PPU_DOTS_PER_CPU_CYCLE; ++dot) {
            ppu.clock();
        }
        if (ppu.pollNmi()) {
            cpu.nmi();
        }
    } while (!ppu.pollFrameComplete());
}

// nestest.nes runs its full automated suite when started at $C000 instead of the reset vector.
constexpr std::uint16_t NESTEST_ENTRY_POINT = 0xC000;
// Length of the reference nestest.log; the ROM's final RTS then returns into RAM at $0001.
constexpr int NESTEST_INSTRUCTION_COUNT = 8991;
constexpr const char* TRACE_LOG_PATH = "emulator_execution.log";

// Headless CPU run for nestest.nes: writes one nestest.log-format line per executed instruction.
int runCpuTrace(Cpu6502& cpu) {
    std::ofstream log(TRACE_LOG_PATH);
    if (!log) {
        std::cerr << "Cannot write " << TRACE_LOG_PATH << '\n';
        return EXIT_FAILURE;
    }

    cpu.pc = NESTEST_ENTRY_POINT;
    int instructionCount = 0;
    while (instructionCount < NESTEST_INSTRUCTION_COUNT) {
        if (cpu.isInstructionComplete()) {
            log << cpu.disassembleLine(cpu.pc) << '\n';
            ++instructionCount;
        }
        cpu.clock();
    }

    std::cout << "Wrote " << instructionCount << " instructions to " << TRACE_LOG_PATH << '\n';
    return EXIT_SUCCESS;
}

} // namespace

int main(int argc, char* argv[]) {
    bool testMode = false;
    const char* romPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "--test-mode") {
            testMode = true;
        } else if (romPath == nullptr && !arg.starts_with("--")) {
            romPath = argv[i];
        } else {
            romPath = nullptr;
            break;
        }
    }
    if (romPath == nullptr) {
        std::cerr << "Usage: " << argv[0] << " [--test-mode] <path/to/game.nes>\n"
                  << "  --test-mode  run the CPU headless from $C000 (nestest.nes) and write "
                  << TRACE_LOG_PATH << '\n';
        return EXIT_FAILURE;
    }

    std::shared_ptr<Cartridge> cartridge;
    try {
        cartridge = std::make_shared<Cartridge>(romPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load ROM: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
    printCartridgeInfo(*cartridge);
    if (cartridge->getMapperId() != 0) {
        std::cerr << "Warning: mapper " << +cartridge->getMapperId()
                  << " is not supported yet; using mapper 0 (NROM) layout\n";
    }

    // Declared before the bus, which keeps a non-owning pointer to it.
    Ppu2C02 ppu;
    ppu.connectCartridge(cartridge);

    Bus bus;
    bus.insertCartridge(cartridge);
    bus.connectPpu(ppu);

    Cpu6502 cpu(bus);
    cpu.reset();
    std::cout << std::format("Reset vector: ${:04X}", cpu.pc) << std::endl;

    if (testMode) {
        return runCpuTrace(cpu);
    }

    SdlContext sdl(SDL_INIT_VIDEO | SDL_INIT_EVENTS);
    if (!sdl.isInitialized()) {
        std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    WindowPtr window(SDL_CreateWindow("NES Emulator",
                                      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                      NES_WIDTH * WINDOW_SCALE, NES_HEIGHT * WINDOW_SCALE,
                                      SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI));
    if (!window) {
        std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    RendererPtr renderer(SDL_CreateRenderer(window.get(), -1,
                                            SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC));
    if (!renderer) {
        std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    // Render in native NES coordinates; SDL scales to the window with crisp pixels.
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
    SDL_RenderSetLogicalSize(renderer.get(), NES_WIDTH, NES_HEIGHT);
    SDL_RenderSetIntegerScale(renderer.get(), SDL_TRUE);

    const std::array<TexturePtr, PATTERN_TABLE_COUNT> patternTextures = {
        createPatternTableTexture(renderer.get()), createPatternTableTexture(renderer.get())};
    if (!patternTextures[0] || !patternTextures[1]) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    const TexturePtr frameTexture(SDL_CreateTexture(renderer.get(), SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
                                                    Ppu2C02::SCREEN_WIDTH, Ppu2C02::SCREEN_HEIGHT));
    if (!frameTexture) {
        std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
        return EXIT_FAILURE;
    }

    bool showDebugView = false;
    std::uint8_t debugPaletteId = 0;
    updateWindowTitle(window.get(), showDebugView, debugPaletteId);

    bool isRunning = true;
    while (isRunning) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
            case SDL_QUIT:
                isRunning = false;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                Controller::Button button{};
                if (mapKeyToButton(event.key.keysym.sym, button)) {
                    // Level-triggered: key repeat just re-sets the same bit.
                    bus.getController(0).setButton(button, event.type == SDL_KEYDOWN);
                    break;
                }
                if (event.type == SDL_KEYUP) {
                    break;
                }
                if (event.key.keysym.sym == SDLK_ESCAPE) {
                    isRunning = false;
                } else if (event.key.keysym.sym == SDLK_TAB) {
                    showDebugView = !showDebugView;
                    updateWindowTitle(window.get(), showDebugView, debugPaletteId);
                } else if (event.key.keysym.sym == SDLK_p && showDebugView) {
                    debugPaletteId = static_cast<std::uint8_t>((debugPaletteId + 1) % Ppu2C02::PALETTE_COUNT);
                    updateWindowTitle(window.get(), showDebugView, debugPaletteId);
                }
                break;
            }
            case SDL_WINDOWEVENT:
                // Key-up events are lost while the window is unfocused; release everything.
                if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    bus.getController(0).setState(0x00);
                }
                break;
            default:
                break;
            }
        }

        runFrame(cpu, ppu, bus);

        SDL_SetRenderDrawColor(renderer.get(), 0, 0, 0, 255);
        SDL_RenderClear(renderer.get());
        if (showDebugView) {
            drawPatternTables(renderer.get(), ppu, patternTextures, debugPaletteId);
            drawPaletteSwatches(renderer.get(), ppu, debugPaletteId);
        } else {
            // runFrame() stops at the end of the frame, so this is the frame rendered at VBlank start.
            drawFrame(renderer.get(), frameTexture.get(), ppu.getFrameBuffer());
        }
        SDL_RenderPresent(renderer.get());
    }

    return EXIT_SUCCESS;
}
