#include "Apu2A03.hpp"

#include <numbers>

namespace {

constexpr std::uint16_t PULSE1_START = 0x4000;
constexpr std::uint16_t PULSE2_START = 0x4004;
constexpr std::uint16_t TRIANGLE_START = 0x4008;
constexpr std::uint16_t NOISE_START = 0x400C;
constexpr std::uint16_t NOISE_END = 0x400F;
constexpr std::uint16_t STATUS = 0x4015;
constexpr std::uint16_t FRAME_COUNTER = 0x4017;

constexpr std::uint8_t STATUS_PULSE1 = 0x01;
constexpr std::uint8_t STATUS_PULSE2 = 0x02;
constexpr std::uint8_t STATUS_TRIANGLE = 0x04;
constexpr std::uint8_t STATUS_NOISE = 0x08;
constexpr std::uint8_t STATUS_FRAME_IRQ = 0x40;

constexpr float HIGH_PASS_HZ = 90.0f;

} // namespace

Apu2A03::Apu2A03() {
    for (std::size_t i = 1; i < pulseMixTable.size(); ++i) {
        pulseMixTable[i] = static_cast<float>(95.88 / (8128.0 / static_cast<double>(i) + 100.0));
    }
    for (std::size_t t = 0; t < triangleNoiseMixTable.size(); ++t) {
        for (std::size_t n = 0; n < triangleNoiseMixTable[t].size(); ++n) {
            const double weighted = static_cast<double>(t) / 8227.0 + static_cast<double>(n) / 12241.0;
            triangleNoiseMixTable[t][n] = weighted > 0.0 ? static_cast<float>(159.79 / (1.0 / weighted + 100.0)) : 0.0f;
        }
    }
    setSampleRate(DEFAULT_SAMPLE_RATE);
}

void Apu2A03::setRegion(const RegionTiming& regionTiming) {
    timing = regionTiming;
    noise.setPeriodTable(timing.noisePeriods);
}

void Apu2A03::reset() {
    pulse1 = PulseChannel(PulseChannel::SweepNegate::OnesComplement);
    pulse2 = PulseChannel(PulseChannel::SweepNegate::TwosComplement);
    triangle = TriangleChannel();
    noise = NoiseChannel();
    noise.setPeriodTable(timing.noisePeriods);
    frameCycle = 0;
    fiveStepMode = false;
    irqInhibit = false;
    frameIrq = false;
    apuCycle = false;
    sampleClock = 0;
    sampleSum = 0.0;
    sampleCount = 0;
    highPassPrevIn = 0.0f;
    highPassPrevOut = 0.0f;
    samples.clear();
}

void Apu2A03::setSampleRate(int hz) {
    sampleRate = hz;
    // First-order RC high-pass: alpha = RC / (RC + dt)
    const float rc = 1.0f / (2.0f * std::numbers::pi_v<float> * HIGH_PASS_HZ);
    const float dt = 1.0f / static_cast<float>(hz);
    highPassAlpha = rc / (rc + dt);
}

// ---------------------------------------------------------------------------------------------
// Registers

void Apu2A03::cpuWrite(std::uint16_t addr, std::uint8_t data) {
    if (addr >= PULSE1_START && addr <= NOISE_END) {
        // Each channel owns 4 consecutive registers.
        const int reg = addr & 0x03;
        if (addr < PULSE2_START) {
            pulse1.write(reg, data);
        } else if (addr < TRIANGLE_START) {
            pulse2.write(reg, data);
        } else if (addr < NOISE_START) {
            triangle.write(reg, data);
        } else {
            noise.write(reg, data);
        }
    } else if (addr == STATUS) {
        pulse1.setEnabled((data & STATUS_PULSE1) != 0);
        pulse2.setEnabled((data & STATUS_PULSE2) != 0);
        triangle.setEnabled((data & STATUS_TRIANGLE) != 0);
        noise.setEnabled((data & STATUS_NOISE) != 0);
        // TODO: DMC enable (bit 4).
    } else if (addr == FRAME_COUNTER) {
        fiveStepMode = (data & 0x80) != 0;
        irqInhibit = (data & 0x40) != 0;
        if (irqInhibit) {
            frameIrq = false;
        }
        // The sequence restarts (hardware waits 3-4 cycles; ignored). 5-step mode also clocks
        // the quarter- and half-frame units immediately.
        frameCycle = 0;
        if (fiveStepMode) {
            clockQuarterFrame();
            clockHalfFrame();
        }
    }
    // TODO: $4010-$4013 (DMC).
}

std::uint8_t Apu2A03::readStatus(bool readOnly) {
    std::uint8_t status = 0x00;
    if (pulse1.isActive()) {
        status |= STATUS_PULSE1;
    }
    if (pulse2.isActive()) {
        status |= STATUS_PULSE2;
    }
    if (triangle.isActive()) {
        status |= STATUS_TRIANGLE;
    }
    if (noise.isActive()) {
        status |= STATUS_NOISE;
    }
    if (frameIrq) {
        status |= STATUS_FRAME_IRQ;
    }
    if (!readOnly) {
        frameIrq = false;
    }
    return status;
}

// ---------------------------------------------------------------------------------------------
// Timing

void Apu2A03::clockQuarterFrame() {
    pulse1.clockQuarterFrame();
    pulse2.clockQuarterFrame();
    triangle.clockQuarterFrame();
    noise.clockQuarterFrame();
}

void Apu2A03::clockHalfFrame() {
    pulse1.clockHalfFrame();
    pulse2.clockHalfFrame();
    triangle.clockHalfFrame();
    noise.clockHalfFrame();
}

void Apu2A03::clockFrameCounter() {
    // Step points are in CPU cycles since the sequence (re)started; they depend on the region.
    ++frameCycle;
    if (frameCycle == timing.apuStep1 || frameCycle == timing.apuStep3) {
        clockQuarterFrame();
    } else if (frameCycle == timing.apuStep2) {
        clockQuarterFrame();
        clockHalfFrame();
    } else if (!fiveStepMode) {
        if (frameCycle == timing.apuStep4) {
            clockQuarterFrame();
            clockHalfFrame();
            if (!irqInhibit) {
                frameIrq = true;
            }
        } else if (frameCycle >= timing.apuFourStepPeriod) {
            frameCycle = 0;
        }
    } else if (frameCycle == timing.apuStep5) {
        clockQuarterFrame();
        clockHalfFrame();
    } else if (frameCycle >= timing.apuFiveStepPeriod) {
        frameCycle = 0;
    }
}

void Apu2A03::clock() {
    clockFrameCounter();

    // Pulse timers run at half the CPU clock; triangle and noise (whose period table is in CPU
    // cycles) are clocked every CPU cycle.
    apuCycle = !apuCycle;
    if (apuCycle) {
        pulse1.clockTimer();
        pulse2.clockTimer();
    }
    triangle.clockTimer();
    noise.clockTimer();

    sampleSum += pulseMixTable[pulse1.output() + pulse2.output()] +
                 triangleNoiseMixTable[triangle.output()][noise.output()]; // + DMC once implemented
    ++sampleCount;

    // Emit one host sample every cpuClockHz / sampleRate CPU cycles (exact long-run rate:
    // 1789773 / 44100 = 40.58 on NTSC, 1662607 / 44100 = 37.70 on PAL).
    sampleClock += sampleRate;
    if (sampleClock >= timing.cpuClockHz) {
        sampleClock -= timing.cpuClockHz;
        emitSample(static_cast<float>(sampleSum / sampleCount));
        sampleSum = 0.0;
        sampleCount = 0;
    }
}

void Apu2A03::emitSample(float mixed) {
    const float filtered = highPassAlpha * (highPassPrevOut + mixed - highPassPrevIn);
    highPassPrevIn = mixed;
    highPassPrevOut = filtered;
    samples.push_back(filtered);
}
