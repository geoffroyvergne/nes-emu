#pragma once

#include <cstdint>
#include <memory>
#include <vector>

namespace nes {

class Bus;
class StateWriter;
class StateReader;

// The NES's 2A03 APU: 2 pulse channels, triangle, noise, and DMC, mixed
// through the standard non-linear NES mixer formula. Internal channel state
// is hidden behind a Pimpl (defined in apu2A03.cpp) since it's pure
// implementation detail with no reuse value outside this one class.
//
// clock() must be called once per CPU cycle (the 2A03's internal timers are
// derived from the CPU clock, some at full rate, some halved) - the owning
// Bus does this alongside Cpu6502::clock(). Output samples are generated at
// a fixed downsampled rate (see kSampleRate in the .cpp) and buffered
// internally; the frontend drains them each video frame and queues them to
// the audio device.
class Apu2A03 {
public:
    // Output sample rate for drainSamples(); the frontend's audio device
    // should be opened at this same rate.
    static constexpr int kSampleRate = 44100;

    Apu2A03();
    ~Apu2A03();

    Apu2A03(const Apu2A03&) = delete;
    Apu2A03& operator=(const Apu2A03&) = delete;

    // Needed for the DMC channel, which reads sample bytes directly from CPU
    // memory. Not owned; the caller must keep it alive while connected.
    void connectBus(Bus* bus);

    // Configures NTSC (1.789773 MHz CPU clock - the default) or PAL
    // (1.662607 MHz) timing: the downsampler's clock rate and the frame
    // sequencer's quarter/half-frame cycle counts. Noise and DMC period
    // tables are NOT region-switched (a known simplification - PAL's real
    // tables differ modestly from NTSC's, affecting only those two
    // channels' exact pitch/rate, not overall tempo). Call before reset().
    void setRegion(bool isPal);

    void reset();

    // CPU-facing registers: $4000-$4013 (channels), $4015 (status, R/W),
    // $4017 (frame counter, write-only from the APU's perspective).
    void cpuWrite(uint16_t addr, uint8_t data);
    uint8_t cpuRead(uint16_t addr);

    void clock();

    // True if the frame counter or DMC currently wants to assert IRQ.
    bool irqPending() const;

    // Appends all samples generated since the last call to `out` (each in
    // [0, ~1], silence = 0) and clears the internal buffer.
    void drainSamples(std::vector<float>& out);

    // Save-state support: all channel/frame-sequencer state. Does not
    // include the region config from setRegion() (expected to already be
    // set correctly before loadState()) or the in-flight sample buffer
    // (transient - drained every video frame).
    void saveState(StateWriter& w) const;
    void loadState(StateReader& r);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nes
