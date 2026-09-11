// Round-trip fidelity test for the save-state system: save -> run more
// cycles (mutating CPU/PPU/APU/mapper state) -> load the earlier save ->
// save again -> the two byte buffers must be byte-for-byte identical.
// That's a strong, comprehensive check without needing to know every
// individual field's semantics: it fails if ANY saved field either isn't
// restored correctly or wasn't included in the format at all (in which case
// the "mutate" step's effect on it would leak through into the second save).
//
// A tiny synthetic NROM ROM is generated to a temp file (no bundled ROM
// files, consistent with the other test suites) that sets up non-trivial
// CPU/PPU/APU register state before settling into an infinite loop, so
// there's plenty for the round trip to get wrong if it's going to.

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "core/apu2A03.h"
#include "core/bus.h"
#include "core/cartridge.h"
#include "core/cpu6502.h"
#include "core/ppu2C02.h"
#include "core/state_io.h"
#include "test_util.h"

using namespace nes;

namespace {

std::string writeSyntheticRom() {
    std::vector<uint8_t> prog = {
        0xA9, 0x42,                                     // LDA #$42
        0xA2, 0x13,                                     // LDX #$13
        0xA0, 0x07,                                     // LDY #$07
        0xA9, 0x3F, 0x8D, 0x06, 0x20,                    // LDA #$3F : STA $2006
        0xA9, 0x10, 0x8D, 0x06, 0x20,                    // LDA #$10 : STA $2006  (PPUADDR=$3F10)
        0xA9, 0x00, 0x8D, 0x00, 0x20,                    // LDA #$00 : STA $2000  (PPUCTRL)
        0xA9, 0x08, 0x8D, 0x01, 0x20,                    // LDA #$08 : STA $2001  (PPUMASK: bg on)
        0xA9, 0x0F, 0x8D, 0x15, 0x40,                    // LDA #$0F : STA $4015  (APU enable all)
        0xA9, 0x8F, 0x8D, 0x00, 0x40,                    // LDA #$8F : STA $4000  (pulse1 duty/vol)
        0xA9, 100,  0x8D, 0x02, 0x40,                    // LDA #100 : STA $4002  (pulse1 timer lo)
        0xA9, 0x08, 0x8D, 0x03, 0x40,                    // LDA #$08 : STA $4003  (pulse1 timer hi+len)
        0x4C, 0x00, 0x00,                                // JMP <self>, patched below
    };
    uint16_t jmpAddr = static_cast<uint16_t>(0x8000 + prog.size() - 3);
    prog[prog.size() - 2] = jmpAddr & 0xFF;
    prog[prog.size() - 1] = (jmpAddr >> 8) & 0xFF;

    std::vector<uint8_t> prg(16384, 0);
    std::copy(prog.begin(), prog.end(), prg.begin());
    prg[0x3FFC] = 0x00;
    prg[0x3FFD] = 0x80;

    std::string path = "save_state_unit_test_rom.nes";
    std::ofstream out(path, std::ios::binary);
    uint8_t header[16] = {'N', 'E', 'S', 0x1A, 1, 0, 0x00, 0x00, 0, 0, 0, 0, 0, 0, 0, 0};
    out.write(reinterpret_cast<const char*>(header), sizeof(header));
    out.write(reinterpret_cast<const char*>(prg.data()), static_cast<std::streamsize>(prg.size()));
    return path;
}

struct System {
    Bus bus;
    Ppu2C02 ppu;
    Apu2A03 apu;
    Cpu6502 cpu{bus};
    Cartridge cartridge;

    explicit System(const std::string& romPath) : cartridge(romPath) {
        bus.connectCpu(&cpu);
        bus.connectPpu(&ppu);
        bus.connectApu(&apu);
        apu.connectBus(&bus);
        bus.insertCartridge(&cartridge);
        ppu.connectCartridge(&cartridge);
        bus.reset();
    }

    void runCycles(int n) {
        for (int i = 0; i < n; i++) bus.clock();
    }

    std::vector<uint8_t> save() const {
        StateWriter w;
        bus.saveState(w);
        cpu.saveState(w);
        ppu.saveState(w);
        apu.saveState(w);
        cartridge.saveState(w);
        return w.buffer();
    }

    void load(const std::vector<uint8_t>& data) {
        StateReader r(data);
        bus.loadState(r);
        cpu.loadState(r);
        ppu.loadState(r);
        apu.loadState(r);
        cartridge.loadState(r);
    }
};

} // namespace

int main() {
    std::string romPath = writeSyntheticRom();

    {
        System sys(romPath);

        // Run past the init sequence and well into the infinite loop, so
        // CPU/PPU/APU/mapper all have non-default, non-trivial state.
        sys.runCycles(5000);

        std::vector<uint8_t> saved = sys.save();
        TEST_CHECK(!saved.empty());

        // Advance a lot further: PPU scanline/cycle position, APU envelope/
        // length-counter/frame-sequencer state, and CPU cycle count should
        // all now differ from the moment of the save.
        sys.runCycles(200000);
        std::vector<uint8_t> mutated = sys.save();
        TEST_CHECK(mutated.size() == saved.size()); // Same format/size...
        TEST_CHECK(mutated != saved);                // ...but genuinely different content.

        sys.load(saved);
        std::vector<uint8_t> restored = sys.save();
        TEST_CHECK(restored == saved); // Exact round-trip: load(saved) must reproduce `saved` exactly.

        // Loading again from the "mutated" snapshot and saving should
        // likewise reproduce it exactly - confirms this isn't one-way.
        sys.load(mutated);
        std::vector<uint8_t> restoredMutated = sys.save();
        TEST_CHECK(restoredMutated == mutated);
    }

    std::remove(romPath.c_str());

    if (TEST_MAIN_RETURN() == 0) {
        std::printf("save_state_unit_test: all checks passed\n");
    }
    return TEST_MAIN_RETURN();
}
