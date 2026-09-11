#pragma once

#include <array>
#include <cstdint>

#include "core/bus.h"

namespace nes {

// A cycle-accurate (in total cycle count, not per-cycle internal state)
// emulation of the MOS 6502 core used by the NES (the NES's 2A03 omits BCD
// mode but we don't implement BCD math anyway since ADC/SBC are only ever
// used in binary mode by NES software).
//
// Modeled after the well-established public-domain 6502 emulation pattern
// used by many NES emulator projects: each opcode is dispatched through a
// 256-entry table of {addressing mode, operation, base cycle count}, and the
// whole instruction executes atomically on the cycle it's fetched, with the
// correct *total* cycle count charged afterwards. This keeps the CPU/PPU
// cycle budget accurate for timing purposes without needing true
// per-cycle-step internal state, which the 6502 rarely needs for
// emulation correctness.
class Cpu6502 {
public:
    enum Flag : uint8_t {
        C = 1 << 0, // Carry
        Z = 1 << 1, // Zero
        I = 1 << 2, // Interrupt disable
        D = 1 << 3, // Decimal mode (unused on the NES's 2A03)
        B = 1 << 4, // Break (only meaningful in the byte pushed by BRK/PHP)
        U = 1 << 5, // Unused, always reads as 1
        V = 1 << 6, // Overflow
        N = 1 << 7, // Negative
    };

    explicit Cpu6502(Bus& bus);

    // Sets registers to their documented post-reset state and loads PC from
    // the reset vector at $FFFC/$FFFD. Takes 8 cycles, matching hardware.
    void reset();

    // Maskable/non-maskable interrupts. irq() is a no-op if the I flag is set.
    void irq();
    void nmi();

    // Advances exactly one clock cycle. Instructions execute atomically on
    // the cycle they're fetched; clock() spends the rest of their declared
    // cycle count "catching up" before fetching the next opcode.
    void clock();

    // Runs clock() until the in-flight instruction (if any) finishes, then
    // executes exactly one full instruction. Returns the number of cycles it
    // took. Convenient for tests and for trace/step debugging.
    unsigned step();

    bool instructionComplete() const { return cyclesLeft_ == 0; }

    uint8_t a() const { return a_; }
    uint8_t x() const { return x_; }
    uint8_t y() const { return y_; }
    uint8_t sp() const { return sp_; }
    uint16_t pc() const { return pc_; }
    uint8_t status() const { return status_; }
    uint64_t totalCycles() const { return clockCount_; }

    // Test/debug hook: force PC, e.g. to jump straight to nestest's
    // automation entry point at $C000 without going through reset.
    void setPC(uint16_t pc) { pc_ = pc; }

    bool getFlag(Flag f) const { return (status_ & f) != 0; }

private:
    void setFlag(Flag f, bool value);

    uint8_t read(uint16_t addr) { return bus_.read(addr); }
    void write(uint16_t addr, uint8_t value) { bus_.write(addr, value); }
    uint8_t fetch();

    // Addressing modes. Each computes addr_abs_ (or addr_rel_ for branches)
    // and returns 1 if it MAY require an extra cycle (page-cross), else 0.
    uint8_t IMP();
    uint8_t IMM();
    uint8_t ZP0();
    uint8_t ZPX();
    uint8_t ZPY();
    uint8_t REL();
    uint8_t ABS();
    uint8_t ABX();
    uint8_t ABY();
    uint8_t IND();
    uint8_t IZX();
    uint8_t IZY();

    // Operations. Each returns 1 if it can combine with the addressing
    // mode's potential extra cycle, else 0.
    uint8_t ADC(); uint8_t AND(); uint8_t ASL(); uint8_t BCC(); uint8_t BCS();
    uint8_t BEQ(); uint8_t BIT(); uint8_t BMI(); uint8_t BNE(); uint8_t BPL();
    uint8_t BRK(); uint8_t BVC(); uint8_t BVS(); uint8_t CLC(); uint8_t CLD();
    uint8_t CLI(); uint8_t CLV(); uint8_t CMP(); uint8_t CPX(); uint8_t CPY();
    uint8_t DEC(); uint8_t DEX(); uint8_t DEY(); uint8_t EOR(); uint8_t INC();
    uint8_t INX(); uint8_t INY(); uint8_t JMP(); uint8_t JSR(); uint8_t LDA();
    uint8_t LDX(); uint8_t LDY(); uint8_t LSR(); uint8_t NOP(); uint8_t ORA();
    uint8_t PHA(); uint8_t PHP(); uint8_t PLA(); uint8_t PLP(); uint8_t ROL();
    uint8_t ROR(); uint8_t RTI(); uint8_t RTS(); uint8_t SBC(); uint8_t SEC();
    uint8_t SED(); uint8_t SEI(); uint8_t STA(); uint8_t STX(); uint8_t STY();
    uint8_t TAX(); uint8_t TAY(); uint8_t TSX(); uint8_t TXA(); uint8_t TXS();
    uint8_t TYA();
    uint8_t XXX(); // Catch-all for unimplemented/illegal opcodes: acts as a NOP.

    using AddrModeFn = uint8_t (Cpu6502::*)();
    using OperateFn = uint8_t (Cpu6502::*)();

    struct Instruction {
        const char* name;
        OperateFn operate;
        AddrModeFn addrmode;
        uint8_t cycles;
    };

    void buildLookupTable();

    Bus& bus_;
    std::array<Instruction, 256> lookup_{};

    uint8_t a_ = 0;
    uint8_t x_ = 0;
    uint8_t y_ = 0;
    uint8_t sp_ = 0;
    uint16_t pc_ = 0;
    uint8_t status_ = 0;

    uint8_t fetched_ = 0;
    uint16_t addrAbs_ = 0;
    uint16_t addrRel_ = 0;
    uint8_t opcode_ = 0;
    uint8_t cyclesLeft_ = 0;
    uint64_t clockCount_ = 0;
};

} // namespace nes
