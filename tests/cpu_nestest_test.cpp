// Optional CPU accuracy validation against Kevin Horton's nestest.nes,
// comparing our execution trace to the widely-distributed golden log.
//
// nestest.nes and its golden log are copyrighted test tools, not part of
// this project, so they aren't bundled here - see tests/roms/README.md for
// where to get them. If they're absent, this test prints a notice and
// passes (skipped) rather than failing the build.

#include <cstdio>
#include <fstream>
#include <regex>
#include <string>

#include "core/bus.h"
#include "core/cartridge.h"
#include "core/cpu6502.h"
#include "test_util.h"

#ifndef TESTS_ROMS_DIR
#define TESTS_ROMS_DIR "."
#endif

using nes::Bus;
using nes::Cartridge;
using nes::Cpu6502;

namespace {

struct GoldenLine {
    uint16_t pc;
    uint8_t a, x, y, p, sp;
};

bool parseLine(const std::string& line, GoldenLine& out) {
    static const std::regex pattern(
        R"(^([0-9A-Fa-f]{4}).*A:([0-9A-Fa-f]{2}) X:([0-9A-Fa-f]{2}) Y:([0-9A-Fa-f]{2}) )"
        R"(P:([0-9A-Fa-f]{2}) SP:([0-9A-Fa-f]{2}))");
    std::smatch m;
    if (!std::regex_search(line, m, pattern)) {
        return false;
    }
    out.pc = static_cast<uint16_t>(std::stoul(m[1], nullptr, 16));
    out.a = static_cast<uint8_t>(std::stoul(m[2], nullptr, 16));
    out.x = static_cast<uint8_t>(std::stoul(m[3], nullptr, 16));
    out.y = static_cast<uint8_t>(std::stoul(m[4], nullptr, 16));
    out.p = static_cast<uint8_t>(std::stoul(m[5], nullptr, 16));
    out.sp = static_cast<uint8_t>(std::stoul(m[6], nullptr, 16));
    return true;
}

} // namespace

int main() {
    const std::string romPath = std::string(TESTS_ROMS_DIR) + "/nestest.nes";
    const std::string logPath = std::string(TESTS_ROMS_DIR) + "/nestest.log";

    std::ifstream logFile(logPath);
    std::ifstream romProbe(romPath, std::ios::binary);
    if (!logFile || !romProbe) {
        std::printf(
            "cpu_nestest_test: SKIPPED (nestest.nes/.log not found in %s - see tests/roms/README.md)\n",
            TESTS_ROMS_DIR);
        return 0;
    }
    romProbe.close();

    Cartridge cart(romPath);
    Bus bus;
    bus.insertCartridge(&cart);
    Cpu6502 cpu(bus);
    cpu.setPC(0xC000); // nestest's automated (no PPU/controller needed) entry point

    std::string line;
    int lineNo = 0;
    int compared = 0;
    while (std::getline(logFile, line)) {
        ++lineNo;
        GoldenLine expected{};
        if (!parseLine(line, expected)) {
            continue;
        }

        bool mismatch = false;
        if (cpu.pc() != expected.pc) mismatch = true;
        if (cpu.a() != expected.a) mismatch = true;
        if (cpu.x() != expected.x) mismatch = true;
        if (cpu.y() != expected.y) mismatch = true;
        if (cpu.status() != expected.p) mismatch = true;
        if (cpu.sp() != expected.sp) mismatch = true;

        if (mismatch) {
            std::fprintf(stderr,
                         "FAIL nestest.log:%d: expected PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X, "
                         "got PC=%04X A=%02X X=%02X Y=%02X P=%02X SP=%02X\n",
                         lineNo, expected.pc, expected.a, expected.x, expected.y, expected.p, expected.sp,
                         cpu.pc(), cpu.a(), cpu.x(), cpu.y(), cpu.status(), cpu.sp());
            ++testutil::failureCount();
            break; // First divergence is all we need; later lines cascade from it.
        }

        cpu.step();
        ++compared;
    }

    if (compared == 0) {
        std::fprintf(stderr, "cpu_nestest_test: no golden log lines matched the expected format\n");
        ++testutil::failureCount();
    } else if (TEST_MAIN_RETURN() == 0) {
        std::printf("cpu_nestest_test: %d instructions matched nestest.log\n", compared);
    }

    return TEST_MAIN_RETURN();
}
