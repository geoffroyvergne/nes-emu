#pragma once

#include "NoiseChannel.hpp"
#include "PulseChannel.hpp"
#include "Region.hpp"
#include "TriangleChannel.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

// Ricoh 2A03 / 2A07 APU (NTSC / PAL). clock() is called once per CPU cycle; it runs the channels and the frame
// counter and resamples the mixed output to the host sample rate into an internal buffer.
//
//   $4000-$4003  Pulse 1          $4010-$4013  DMC (not implemented yet)
//   $4004-$4007  Pulse 2          $4015        Status (write: channel enables, read: length/IRQ flags)
//   $4008-$400B  Triangle         $4017        Frame counter (write only; reads are controller 2)
//   $400C-$400F  Noise
class Apu2A03 {
public:
    static constexpr int DEFAULT_SAMPLE_RATE = 44100;

    Apu2A03();
    void reset();
    // CPU clock (resampling ratio), frame counter step points and noise rates for the region.
    void setRegion(const RegionTiming& regionTiming);

    void cpuWrite(std::uint16_t addr, std::uint8_t data);
    // $4015: bits 0-3 = pulse 1, pulse 2, triangle, noise length counters > 0;
    // bit 6 = frame IRQ (cleared by the read).
    [[nodiscard]] std::uint8_t readStatus(bool readOnly = false);

    void clock();

    // Output: mono float samples in [-1, 1] at the host rate, accumulated until cleared.
    void setSampleRate(int hz);
    [[nodiscard]] std::span<const float> getSamples() const { return samples; }
    void clearSamples() { samples.clear(); }

    // Frame counter IRQ (4-step mode, not inhibited). Not wired to the CPU yet.
    [[nodiscard]] bool isFrameIrqPending() const { return frameIrq; }

private:
    void clockQuarterFrame();
    void clockHalfFrame();
    void clockFrameCounter();
    void emitSample(float mixed);

    PulseChannel pulse1{PulseChannel::SweepNegate::OnesComplement};
    PulseChannel pulse2{PulseChannel::SweepNegate::TwosComplement};
    TriangleChannel triangle;
    NoiseChannel noise;

    RegionTiming timing = NTSC_TIMING;

    // Frame counter: sequences quarter frames (envelopes) and half frames (length, sweep) at ~240 Hz.
    int frameCycle = 0;
    bool fiveStepMode = false;
    bool irqInhibit = false;
    bool frameIrq = false;
    bool apuCycle = false; // Channel timers run at half the CPU clock

    // Mixer: the 2A03's nonlinear DAC response (nesdev "APU Mixer"), precomputed exactly.
    //   pulse: 95.88 / (8128 / (p1 + p2) + 100)                        indexed by p1 + p2 (0-30)
    //   tnd:   159.79 / (1 / (t / 8227 + n / 12241 + d / 22638) + 100)  indexed by [t][n] (d = 0 until
    //          the DMC exists; it will need a third dimension or a direct evaluation)
    std::array<float, 31> pulseMixTable{};
    std::array<std::array<float, 16>, 16> triangleNoiseMixTable{};

    // Resampling: box-filter average of the per-cycle output over each host sample period, then a
    // 90 Hz high-pass (the NES output stage) to remove the DC offset.
    int sampleRate = DEFAULT_SAMPLE_RATE;
    long long sampleClock = 0;
    double sampleSum = 0.0;
    int sampleCount = 0;
    float highPassAlpha = 0.0f;
    float highPassPrevIn = 0.0f;
    float highPassPrevOut = 0.0f;
    std::vector<float> samples;
};
