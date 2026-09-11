// Black-box tests for the APU, driven purely through its CPU-facing
// register interface (cpuWrite/cpuRead) plus clock()/drainSamples() - the
// same surface real 6502 software uses. Internal channel state is
// deliberately hidden behind Apu2A03's Pimpl, so this is the intended
// testing seam.

#include <cstdio>
#include <vector>

#include "core/apu2A03.h"
#include "test_util.h"

using nes::Apu2A03;

namespace {

void testLengthCounterAndStatus() {
    Apu2A03 apu;
    apu.reset();

    apu.cpuWrite(0x4015, 0x01); // Enable pulse 1.
    apu.cpuWrite(0x4000, 0x1F); // duty=0, halt=0, constant volume=1, volume=15.
    apu.cpuWrite(0x4002, 0x00); // Timer low.
    apu.cpuWrite(0x4003, 0x00); // Timer high=0, length load index 0 -> table[0] = 10.

    TEST_CHECK((apu.cpuRead(0x4015) & 0x01) != 0);

    // Half-frame clocks (which decrement length counters) land roughly every
    // ~14913-14916 CPU cycles in 4-step mode; well past 6 full frame-counter
    // cycles is more than enough for 10 decrements to have happened.
    for (int i = 0; i < 6 * 29829; i++) apu.clock();

    TEST_CHECK((apu.cpuRead(0x4015) & 0x01) == 0); // Length counter hit 0: channel silenced.
}

void testFrameSequencerIrqFourStep() {
    Apu2A03 apu;
    apu.reset(); // Default: 4-step mode, IRQ not inhibited.

    TEST_CHECK(!apu.irqPending());
    for (int i = 0; i < 29830; i++) apu.clock(); // Just past step 4 (cycle 29829).
    TEST_CHECK(apu.irqPending());

    uint8_t status = apu.cpuRead(0x4015);
    TEST_CHECK((status & 0x40) != 0);  // Frame IRQ flag was set.
    TEST_CHECK(!apu.irqPending());     // Reading $4015 acknowledges it.
}

void testFiveStepModeNeverIrqs() {
    Apu2A03 apu;
    apu.reset();
    apu.cpuWrite(0x4017, 0x80); // 5-step mode, IRQ not inhibited.

    for (int i = 0; i < 40000; i++) apu.clock(); // Past a full 5-step cycle (37281).
    TEST_CHECK(!apu.irqPending());                // 5-step mode never raises the frame IRQ.
}

void testMixerSilenceThenSound() {
    Apu2A03 apu;
    apu.reset();

    for (int i = 0; i < 10000; i++) apu.clock();
    std::vector<float> samples;
    apu.drainSamples(samples);
    TEST_CHECK(!samples.empty());
    bool allZero = true;
    for (float s : samples) {
        if (s != 0.0f) { allZero = false; break; }
    }
    TEST_CHECK(allZero); // Nothing enabled: mixer output must be silence.

    apu.cpuWrite(0x4015, 0x01); // Enable pulse 1.
    apu.cpuWrite(0x4000, 0x8F); // duty=2 (50%), halt=0, constant volume=1, volume=15.
    // Timer period 100 (>= 8: pulse channels self-mute below that regardless
    // of sweep settings, which is correct real-hardware behavior).
    apu.cpuWrite(0x4002, 100);
    apu.cpuWrite(0x4003, 0x08); // length load index 1 -> table[1] = 254 (plenty).

    for (int i = 0; i < 10000; i++) apu.clock();
    samples.clear();
    apu.drainSamples(samples);
    bool anyNonZero = false;
    for (float s : samples) {
        if (s > 0.0f) { anyNonZero = true; break; }
    }
    TEST_CHECK(anyNonZero); // An audible constant-volume tone must produce nonzero samples.
}

// Plays a pulse1 tone at a known timer period, clocks the APU for exactly 1
// second of emulated CPU time (a pure clock()-call count, with no real-time
// pacing involved), and counts waveform rising edges to measure the actual
// output frequency against the exact NESDev formula. Guards against pitch
// bugs in the channel timers/downsampler specifically (as opposed to the
// separate concern of whether real-time playback is paced correctly).
void testPulseFrequencyAccuracy() {
    Apu2A03 apu;
    apu.reset();

    constexpr int kPeriod = 253; // 0xFD
    apu.cpuWrite(0x4015, 0x01);
    apu.cpuWrite(0x4000, 0x8F); // duty=2 (50%), halt=0, constant volume=1, volume=15.
    apu.cpuWrite(0x4002, kPeriod & 0xFF);
    apu.cpuWrite(0x4003, static_cast<uint8_t>((1 << 3) | ((kPeriod >> 8) & 0x07))); // length idx=1, timer hi.

    constexpr long kCpuClockHz = 1789773;
    for (long i = 0; i < kCpuClockHz; i++) apu.clock();

    std::vector<float> samples;
    apu.drainSamples(samples);

    int crossings = 0;
    bool wasHigh = false;
    for (float s : samples) {
        bool isHigh = s > 0.01f;
        if (isHigh && !wasHigh) crossings++;
        wasHigh = isHigh;
    }

    double expectedHz = kCpuClockHz / (16.0 * (kPeriod + 1)); // NESDev pulse frequency formula.
    TEST_CHECK(crossings > static_cast<int>(expectedHz) - 3 && crossings < static_cast<int>(expectedHz) + 3);
}

} // namespace

int main() {
    testLengthCounterAndStatus();
    testFrameSequencerIrqFourStep();
    testFiveStepModeNeverIrqs();
    testMixerSilenceThenSound();
    testPulseFrequencyAccuracy();

    if (TEST_MAIN_RETURN() == 0) {
        std::printf("apu_unit_test: all checks passed\n");
    }
    return TEST_MAIN_RETURN();
}
