#include "core/apu2A03.h"

#include <algorithm>
#include <array>

#include "core/bus.h"
#include "core/state_io.h"

namespace nes {

namespace {

constexpr double kCpuClockHzNtsc = 1789773.0;
constexpr double kCpuClockHzPal = 1662607.0;

// Frame sequencer quarter/half-frame cycle counts. NTSC's are exact
// (well-documented, widely cross-checked against real hardware). PAL's are
// derived from PAL's own ~50.007Hz frame rate divided into quarters (a
// quarter-frame period isn't simply the NTSC value scaled by the CPU clock
// ratio - PAL's video frame rate reduction is bigger than its CPU clock
// reduction, since PAL also has more scanlines per frame), consistent with
// PAL's documented ~8313-cycle quarter-frame period, but not independently
// verified against real hardware the way the NTSC values are.
constexpr std::array<uint32_t, 5> kFrameStepsNtsc = {7457, 14913, 22371, 29829, 37281};
constexpr std::array<uint32_t, 5> kFrameStepsPal = {8313, 16627, 24939, 33254, 41565};

constexpr std::array<uint8_t, 32> kLengthTable = {
    10, 254, 20, 2,  40, 4,  80, 6,  160, 8,  60, 10, 14, 12, 26, 14,
    12, 16,  24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30,
};

constexpr std::array<uint16_t, 16> kNoisePeriodTable = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068,
};

constexpr std::array<uint16_t, 16> kDmcRateTable = {
    428, 380, 340, 320, 286, 254, 226, 214, 190, 160, 142, 128, 106, 84, 72, 54,
};

constexpr std::array<std::array<uint8_t, 8>, 4> kDutyTable = {{
    {0, 1, 0, 0, 0, 0, 0, 0}, // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0}, // 25%
    {0, 1, 1, 1, 1, 0, 0, 0}, // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}, // 75%
}};

constexpr std::array<uint8_t, 32> kTriangleSequence = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0,  1,  2,  3,  4,  5,  6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
};

struct Pulse {
    bool isPulse1 = true; // Selects the sweep unit's negate quirk (one's vs two's complement).
    uint8_t duty = 0;
    bool lengthHalt = false; // Also doubles as the envelope loop flag.
    bool constantVolume = false;
    uint8_t volume = 0; // Constant volume level, or envelope divider period.

    bool envStart = false;
    uint8_t envDecay = 0;
    uint8_t envDivider = 0;

    bool sweepEnabled = false;
    uint8_t sweepPeriod = 0;
    bool sweepNegate = false;
    uint8_t sweepShift = 0;
    bool sweepReload = false;
    uint8_t sweepDivider = 0;

    uint16_t timerPeriod = 0;
    uint16_t timerValue = 0;
    uint8_t dutyStep = 0;

    uint8_t lengthCounter = 0;
    bool enabled = false;

    void writeReg0(uint8_t d) {
        duty = (d >> 6) & 0x03;
        lengthHalt = (d >> 5) & 0x01;
        constantVolume = (d >> 4) & 0x01;
        volume = d & 0x0F;
    }
    void writeReg1(uint8_t d) {
        sweepEnabled = (d >> 7) & 0x01;
        sweepPeriod = (d >> 4) & 0x07;
        sweepNegate = (d >> 3) & 0x01;
        sweepShift = d & 0x07;
        sweepReload = true;
    }
    void writeReg2(uint8_t d) { timerPeriod = static_cast<uint16_t>((timerPeriod & 0x0700) | d); }
    void writeReg3(uint8_t d) {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x00FF) | ((d & 0x07) << 8));
        dutyStep = 0;
        envStart = true;
        if (enabled) lengthCounter = kLengthTable[(d >> 3) & 0x1F];
    }

    void clockTimer() {
        if (timerValue == 0) {
            timerValue = timerPeriod;
            dutyStep = (dutyStep + 1) & 0x07;
        } else {
            timerValue--;
        }
    }

    void clockEnvelope() {
        if (envStart) {
            envStart = false;
            envDecay = 15;
            envDivider = volume;
        } else if (envDivider == 0) {
            envDivider = volume;
            if (envDecay > 0) {
                envDecay--;
            } else if (lengthHalt) {
                envDecay = 15;
            }
        } else {
            envDivider--;
        }
    }

    int32_t sweepTargetPeriod() const {
        int32_t change = timerPeriod >> sweepShift;
        if (sweepNegate) change = isPulse1 ? -change - 1 : -change;
        int32_t target = static_cast<int32_t>(timerPeriod) + change;
        return target < 0 ? 0 : target;
    }

    bool sweepMuting() const { return timerPeriod < 8 || sweepTargetPeriod() > 0x7FF; }

    void clockSweep() {
        int32_t target = sweepTargetPeriod();
        if (sweepDivider == 0 && sweepEnabled && sweepShift != 0 && !sweepMuting()) {
            timerPeriod = static_cast<uint16_t>(target);
        }
        if (sweepDivider == 0 || sweepReload) {
            sweepDivider = sweepPeriod;
            sweepReload = false;
        } else {
            sweepDivider--;
        }
    }

    void clockLength() {
        if (!lengthHalt && lengthCounter > 0) lengthCounter--;
    }

    uint8_t output() const {
        if (!enabled || lengthCounter == 0 || sweepMuting()) return 0;
        if (!kDutyTable[duty][dutyStep]) return 0;
        return constantVolume ? volume : envDecay;
    }
};

struct Triangle {
    bool lengthHalt = false; // Also the linear counter's "control" flag.
    uint8_t linearCounterReload = 0;
    uint16_t timerPeriod = 0;
    uint16_t timerValue = 0;
    uint8_t sequenceStep = 0;
    uint8_t linearCounter = 0;
    bool linearReloadFlag = false;
    uint8_t lengthCounter = 0;
    bool enabled = false;

    void writeReg0(uint8_t d) {
        lengthHalt = (d >> 7) & 0x01;
        linearCounterReload = d & 0x7F;
    }
    void writeReg2(uint8_t d) { timerPeriod = static_cast<uint16_t>((timerPeriod & 0x0700) | d); }
    void writeReg3(uint8_t d) {
        timerPeriod = static_cast<uint16_t>((timerPeriod & 0x00FF) | ((d & 0x07) << 8));
        linearReloadFlag = true;
        if (enabled) lengthCounter = kLengthTable[(d >> 3) & 0x1F];
    }

    void clockTimer() {
        if (timerValue == 0) {
            timerValue = timerPeriod;
            if (lengthCounter > 0 && linearCounter > 0) sequenceStep = (sequenceStep + 1) & 0x1F;
        } else {
            timerValue--;
        }
    }

    void clockLinear() {
        if (linearReloadFlag) {
            linearCounter = linearCounterReload;
        } else if (linearCounter > 0) {
            linearCounter--;
        }
        if (!lengthHalt) linearReloadFlag = false;
    }

    void clockLength() {
        if (!lengthHalt && lengthCounter > 0) lengthCounter--;
    }

    uint8_t output() const { return enabled ? kTriangleSequence[sequenceStep] : 0; }
};

struct Noise {
    bool lengthHalt = false;
    bool constantVolume = false;
    uint8_t volume = 0;

    bool envStart = false;
    uint8_t envDecay = 0;
    uint8_t envDivider = 0;

    bool modeFlag = false;
    uint16_t timerPeriod = kNoisePeriodTable[0];
    uint16_t timerValue = 0;
    uint16_t shiftRegister = 1;

    uint8_t lengthCounter = 0;
    bool enabled = false;

    void writeReg0(uint8_t d) {
        lengthHalt = (d >> 5) & 0x01;
        constantVolume = (d >> 4) & 0x01;
        volume = d & 0x0F;
    }
    void writeReg2(uint8_t d) {
        modeFlag = (d >> 7) & 0x01;
        timerPeriod = kNoisePeriodTable[d & 0x0F];
    }
    void writeReg3(uint8_t d) {
        envStart = true;
        if (enabled) lengthCounter = kLengthTable[(d >> 3) & 0x1F];
    }

    void clockTimer() {
        if (timerValue == 0) {
            timerValue = timerPeriod;
            uint8_t feedbackBit = modeFlag ? ((shiftRegister >> 6) & 1) : ((shiftRegister >> 1) & 1);
            uint16_t feedback = (shiftRegister & 1) ^ feedbackBit;
            shiftRegister >>= 1;
            shiftRegister |= static_cast<uint16_t>(feedback << 14);
        } else {
            timerValue--;
        }
    }

    void clockEnvelope() {
        if (envStart) {
            envStart = false;
            envDecay = 15;
            envDivider = volume;
        } else if (envDivider == 0) {
            envDivider = volume;
            if (envDecay > 0) {
                envDecay--;
            } else if (lengthHalt) {
                envDecay = 15;
            }
        } else {
            envDivider--;
        }
    }

    void clockLength() {
        if (!lengthHalt && lengthCounter > 0) lengthCounter--;
    }

    uint8_t output() const {
        if (!enabled || lengthCounter == 0 || (shiftRegister & 1)) return 0;
        return constantVolume ? volume : envDecay;
    }
};

struct Dmc {
    bool irqEnable = false;
    bool loop = false;
    uint16_t timerPeriod = kDmcRateTable[0];
    uint16_t timerValue = 0;

    uint16_t sampleAddress = 0xC000;
    uint16_t sampleLength = 1;
    uint16_t currentAddress = 0;
    uint16_t bytesRemaining = 0;

    uint8_t shiftRegister = 0;
    uint8_t bitsRemaining = 8;
    bool silence = true;
    uint8_t outputLevel = 0;

    bool sampleBufferFilled = false;
    uint8_t sampleBuffer = 0;

    bool irqFlag = false;

    void writeReg0(uint8_t d) {
        irqEnable = (d >> 7) & 0x01;
        loop = (d >> 6) & 0x01;
        timerPeriod = kDmcRateTable[d & 0x0F];
        if (!irqEnable) irqFlag = false;
    }
    void writeReg1(uint8_t d) { outputLevel = d & 0x7F; }
    void writeReg2(uint8_t d) { sampleAddress = static_cast<uint16_t>(0xC000 + d * 64); }
    void writeReg3(uint8_t d) { sampleLength = static_cast<uint16_t>(d * 16 + 1); }

    void restart() {
        currentAddress = sampleAddress;
        bytesRemaining = sampleLength;
    }

    // Approximates real hardware's separate "memory reader" and "output
    // unit": doesn't model CPU DMA-stall cycles, just performs the read
    // once the buffer empties.
    void clockTimer(Bus* bus) {
        if (!sampleBufferFilled && bytesRemaining > 0 && bus) {
            sampleBuffer = bus->read(currentAddress);
            sampleBufferFilled = true;
            currentAddress = currentAddress == 0xFFFF ? 0x8000 : static_cast<uint16_t>(currentAddress + 1);
            bytesRemaining--;
            if (bytesRemaining == 0) {
                if (loop) {
                    restart();
                } else if (irqEnable) {
                    irqFlag = true;
                }
            }
        }

        if (timerValue == 0) {
            timerValue = timerPeriod;
            if (!silence) {
                if (shiftRegister & 1) {
                    if (outputLevel <= 125) outputLevel = static_cast<uint8_t>(outputLevel + 2);
                } else {
                    if (outputLevel >= 2) outputLevel = static_cast<uint8_t>(outputLevel - 2);
                }
            }
            shiftRegister = static_cast<uint8_t>(shiftRegister >> 1);
            bitsRemaining--;
            if (bitsRemaining == 0) {
                bitsRemaining = 8;
                if (!sampleBufferFilled) {
                    silence = true;
                } else {
                    silence = false;
                    shiftRegister = sampleBuffer;
                    sampleBufferFilled = false;
                }
            }
        } else {
            timerValue--;
        }
    }

    uint8_t output() const { return outputLevel; }
};

} // namespace

struct Apu2A03::Impl {
    Bus* bus = nullptr;

    Pulse pulse1;
    Pulse pulse2;
    Triangle triangle;
    Noise noise;
    Dmc dmc;

    uint32_t frameCounter = 0;
    bool fiveStepMode = false;
    bool irqInhibit = false;
    bool frameIrqFlag = false;

    bool halfClockToggle = false;
    double sampleAccumulator = 0.0;
    std::vector<float> sampleBuffer;

    double cpuClockHz = kCpuClockHzNtsc;
    std::array<uint32_t, 5> frameSteps = kFrameStepsNtsc;

    Impl() { pulse1.isPulse1 = true; pulse2.isPulse1 = false; }

    void setRegion(bool isPal) {
        cpuClockHz = isPal ? kCpuClockHzPal : kCpuClockHzNtsc;
        frameSteps = isPal ? kFrameStepsPal : kFrameStepsNtsc;
    }

    void quarterFrameClock() {
        pulse1.clockEnvelope();
        pulse2.clockEnvelope();
        noise.clockEnvelope();
        triangle.clockLinear();
    }

    void halfFrameClock() {
        pulse1.clockLength();
        pulse2.clockLength();
        triangle.clockLength();
        noise.clockLength();
        pulse1.clockSweep();
        pulse2.clockSweep();
    }

    void writeFrameCounterReg(uint8_t d) {
        fiveStepMode = (d >> 7) & 0x01;
        irqInhibit = (d >> 6) & 0x01;
        if (irqInhibit) frameIrqFlag = false;
        frameCounter = 0;
        if (fiveStepMode) {
            quarterFrameClock();
            halfFrameClock();
        }
    }

    void clockFrameSequencer() {
        frameCounter++;
        if (!fiveStepMode) {
            if (frameCounter == frameSteps[0]) {
                quarterFrameClock();
            } else if (frameCounter == frameSteps[1]) {
                quarterFrameClock();
                halfFrameClock();
            } else if (frameCounter == frameSteps[2]) {
                quarterFrameClock();
            } else if (frameCounter == frameSteps[3]) {
                quarterFrameClock();
                halfFrameClock();
                if (!irqInhibit) frameIrqFlag = true;
                frameCounter = 0;
            }
        } else {
            if (frameCounter == frameSteps[0]) {
                quarterFrameClock();
            } else if (frameCounter == frameSteps[1]) {
                quarterFrameClock();
                halfFrameClock();
            } else if (frameCounter == frameSteps[2]) {
                quarterFrameClock();
            } else if (frameCounter == frameSteps[4]) {
                quarterFrameClock();
                halfFrameClock();
                frameCounter = 0;
            }
        }
    }

    void writeStatus(uint8_t d) {
        pulse1.enabled = d & 0x01;
        if (!pulse1.enabled) pulse1.lengthCounter = 0;
        pulse2.enabled = (d >> 1) & 0x01;
        if (!pulse2.enabled) pulse2.lengthCounter = 0;
        triangle.enabled = (d >> 2) & 0x01;
        if (!triangle.enabled) triangle.lengthCounter = 0;
        noise.enabled = (d >> 3) & 0x01;
        if (!noise.enabled) noise.lengthCounter = 0;

        bool dmcEnable = (d >> 4) & 0x01;
        if (!dmcEnable) {
            dmc.bytesRemaining = 0;
        } else if (dmc.bytesRemaining == 0) {
            dmc.restart();
        }
        dmc.irqFlag = false;
    }

    uint8_t readStatus() {
        uint8_t result = 0;
        if (pulse1.lengthCounter > 0) result |= 0x01;
        if (pulse2.lengthCounter > 0) result |= 0x02;
        if (triangle.lengthCounter > 0) result |= 0x04;
        if (noise.lengthCounter > 0) result |= 0x08;
        if (dmc.bytesRemaining > 0) result |= 0x10;
        if (frameIrqFlag) result |= 0x40;
        if (dmc.irqFlag) result |= 0x80;
        frameIrqFlag = false;
        return result;
    }

    void emitSample() {
        uint8_t p1 = pulse1.output();
        uint8_t p2 = pulse2.output();
        uint8_t tri = triangle.output();
        uint8_t noi = noise.output();
        uint8_t dmcOut = dmc.output();

        double pulseSum = p1 + p2;
        double pulseOut = pulseSum == 0.0 ? 0.0 : 95.88 / (8128.0 / pulseSum + 100.0);

        double tndDenom = (tri == 0 && noi == 0 && dmcOut == 0)
                               ? 0.0
                               : (tri / 8227.0 + noi / 12241.0 + dmcOut / 22638.0);
        double tndOut = tndDenom == 0.0 ? 0.0 : 159.79 / (1.0 / tndDenom + 100.0);

        // Standard NES mixer output: naturally 0 at silence (no artificial
        // centering needed), positive otherwise. The audible waveform comes
        // from the channels' audio-frequency toggling, not from any DC bias.
        double mix = std::clamp(pulseOut + tndOut, 0.0, 1.0);
        sampleBuffer.push_back(static_cast<float>(mix));
    }

    void clock() {
        halfClockToggle = !halfClockToggle;
        if (halfClockToggle) {
            pulse1.clockTimer();
            pulse2.clockTimer();
            noise.clockTimer();
            dmc.clockTimer(bus);
        }
        triangle.clockTimer();

        clockFrameSequencer();

        sampleAccumulator += Apu2A03::kSampleRate;
        if (sampleAccumulator >= cpuClockHz) {
            sampleAccumulator -= cpuClockHz;
            emitSample();
        }
    }
};

Apu2A03::Apu2A03() : impl_(std::make_unique<Impl>()) {}
Apu2A03::~Apu2A03() = default;

void Apu2A03::connectBus(Bus* bus) { impl_->bus = bus; }

void Apu2A03::setRegion(bool isPal) { impl_->setRegion(isPal); }

void Apu2A03::reset() {
    Bus* bus = impl_->bus;
    double cpuClockHz = impl_->cpuClockHz;
    std::array<uint32_t, 5> frameSteps = impl_->frameSteps;
    *impl_ = Impl();
    impl_->bus = bus;
    impl_->cpuClockHz = cpuClockHz;
    impl_->frameSteps = frameSteps;
}

void Apu2A03::cpuWrite(uint16_t addr, uint8_t data) {
    switch (addr) {
        case 0x4000: impl_->pulse1.writeReg0(data); break;
        case 0x4001: impl_->pulse1.writeReg1(data); break;
        case 0x4002: impl_->pulse1.writeReg2(data); break;
        case 0x4003: impl_->pulse1.writeReg3(data); break;
        case 0x4004: impl_->pulse2.writeReg0(data); break;
        case 0x4005: impl_->pulse2.writeReg1(data); break;
        case 0x4006: impl_->pulse2.writeReg2(data); break;
        case 0x4007: impl_->pulse2.writeReg3(data); break;
        case 0x4008: impl_->triangle.writeReg0(data); break;
        case 0x400A: impl_->triangle.writeReg2(data); break;
        case 0x400B: impl_->triangle.writeReg3(data); break;
        case 0x400C: impl_->noise.writeReg0(data); break;
        case 0x400E: impl_->noise.writeReg2(data); break;
        case 0x400F: impl_->noise.writeReg3(data); break;
        case 0x4010: impl_->dmc.writeReg0(data); break;
        case 0x4011: impl_->dmc.writeReg1(data); break;
        case 0x4012: impl_->dmc.writeReg2(data); break;
        case 0x4013: impl_->dmc.writeReg3(data); break;
        case 0x4015: impl_->writeStatus(data); break;
        case 0x4017: impl_->writeFrameCounterReg(data); break;
        default: break;
    }
}

uint8_t Apu2A03::cpuRead(uint16_t addr) {
    if (addr == 0x4015) return impl_->readStatus();
    return 0;
}

void Apu2A03::clock() { impl_->clock(); }

bool Apu2A03::irqPending() const { return impl_->frameIrqFlag || impl_->dmc.irqFlag; }

void Apu2A03::drainSamples(std::vector<float>& out) {
    out.insert(out.end(), impl_->sampleBuffer.begin(), impl_->sampleBuffer.end());
    impl_->sampleBuffer.clear();
}

// Pulse/Triangle/Noise/Dmc are plain structs of trivial fields (no
// pointers/containers), so each is saved as a single trivially-copyable
// blob rather than enumerating every field.
void Apu2A03::saveState(StateWriter& w) const {
    w.write(impl_->pulse1);
    w.write(impl_->pulse2);
    w.write(impl_->triangle);
    w.write(impl_->noise);
    w.write(impl_->dmc);
    w.write(impl_->frameCounter);
    w.write(impl_->fiveStepMode);
    w.write(impl_->irqInhibit);
    w.write(impl_->frameIrqFlag);
    w.write(impl_->halfClockToggle);
    w.write(impl_->sampleAccumulator);
}

void Apu2A03::loadState(StateReader& r) {
    impl_->pulse1 = r.read<Pulse>();
    impl_->pulse2 = r.read<Pulse>();
    impl_->triangle = r.read<Triangle>();
    impl_->noise = r.read<Noise>();
    impl_->dmc = r.read<Dmc>();
    impl_->frameCounter = r.read<uint32_t>();
    impl_->fiveStepMode = r.read<bool>();
    impl_->irqInhibit = r.read<bool>();
    impl_->frameIrqFlag = r.read<bool>();
    impl_->halfClockToggle = r.read<bool>();
    impl_->sampleAccumulator = r.read<double>();
}

} // namespace nes
