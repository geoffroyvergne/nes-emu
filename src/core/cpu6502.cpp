#include "core/cpu6502.h"

#include "core/state_io.h"

namespace nes {

Cpu6502::Cpu6502(Bus& bus) : bus_(bus) {
    buildLookupTable();
    reset();
}

void Cpu6502::saveState(StateWriter& w) const {
    w.write(a_);
    w.write(x_);
    w.write(y_);
    w.write(sp_);
    w.write(pc_);
    w.write(status_);
    w.write(fetched_);
    w.write(addrAbs_);
    w.write(addrRel_);
    w.write(opcode_);
    w.write(cyclesLeft_);
    w.write(clockCount_);
}

void Cpu6502::loadState(StateReader& r) {
    a_ = r.read<uint8_t>();
    x_ = r.read<uint8_t>();
    y_ = r.read<uint8_t>();
    sp_ = r.read<uint8_t>();
    pc_ = r.read<uint16_t>();
    status_ = r.read<uint8_t>();
    fetched_ = r.read<uint8_t>();
    addrAbs_ = r.read<uint16_t>();
    addrRel_ = r.read<uint16_t>();
    opcode_ = r.read<uint8_t>();
    cyclesLeft_ = r.read<uint8_t>();
    clockCount_ = r.read<uint64_t>();
}

void Cpu6502::setFlag(Flag f, bool value) {
    if (value) {
        status_ |= f;
    } else {
        status_ &= static_cast<uint8_t>(~f);
    }
}

void Cpu6502::reset() {
    a_ = 0;
    x_ = 0;
    y_ = 0;
    sp_ = 0xFD;
    status_ = 0;
    setFlag(U, true);
    setFlag(I, true);

    addrAbs_ = 0xFFFC;
    uint16_t lo = read(addrAbs_);
    uint16_t hi = read(addrAbs_ + 1);
    pc_ = static_cast<uint16_t>((hi << 8) | lo);

    addrAbs_ = 0;
    addrRel_ = 0;
    fetched_ = 0;
    cyclesLeft_ = 8;
}

void Cpu6502::irq() {
    if (getFlag(I)) {
        return;
    }
    write(0x0100 + sp_, static_cast<uint8_t>((pc_ >> 8) & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(pc_ & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(status_ | U));
    sp_--;
    setFlag(I, true);

    addrAbs_ = 0xFFFE;
    uint16_t lo = read(addrAbs_);
    uint16_t hi = read(addrAbs_ + 1);
    pc_ = static_cast<uint16_t>((hi << 8) | lo);
    cyclesLeft_ = 7;
}

void Cpu6502::nmi() {
    write(0x0100 + sp_, static_cast<uint8_t>((pc_ >> 8) & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(pc_ & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(status_ | U));
    sp_--;
    setFlag(I, true);

    addrAbs_ = 0xFFFA;
    uint16_t lo = read(addrAbs_);
    uint16_t hi = read(addrAbs_ + 1);
    pc_ = static_cast<uint16_t>((hi << 8) | lo);
    cyclesLeft_ = 8;
}

void Cpu6502::clock() {
    if (cyclesLeft_ == 0) {
        opcode_ = read(pc_);
        pc_++;

        const Instruction& instr = lookup_[opcode_];
        cyclesLeft_ = instr.cycles;

        uint8_t extra1 = (this->*instr.addrmode)();
        uint8_t extra2 = (this->*instr.operate)();
        cyclesLeft_ = static_cast<uint8_t>(cyclesLeft_ + (extra1 & extra2));

        setFlag(U, true);
    }
    clockCount_++;
    cyclesLeft_--;
}

unsigned Cpu6502::step() {
    // Drain any cycles still owed by a prior operation (e.g. the 8 cycles
    // reset() or the 7/8 cycles irq()/nmi() charge before the CPU is ready
    // to fetch again) before timing the next instruction, so the returned
    // count always reflects exactly one instruction regardless of what ran
    // immediately before this call.
    while (cyclesLeft_ > 0) {
        clock();
    }
    uint64_t start = clockCount_;
    do {
        clock();
    } while (cyclesLeft_ > 0);
    return static_cast<unsigned>(clockCount_ - start);
}

uint8_t Cpu6502::fetch() {
    if (lookup_[opcode_].addrmode != &Cpu6502::IMP) {
        fetched_ = read(addrAbs_);
    }
    return fetched_;
}

// ---------------------------------------------------------------------------
// Addressing modes
// ---------------------------------------------------------------------------

uint8_t Cpu6502::IMP() {
    fetched_ = a_;
    return 0;
}

uint8_t Cpu6502::IMM() {
    addrAbs_ = pc_++;
    return 0;
}

uint8_t Cpu6502::ZP0() {
    addrAbs_ = read(pc_++) & 0x00FF;
    return 0;
}

uint8_t Cpu6502::ZPX() {
    addrAbs_ = static_cast<uint16_t>((read(pc_++) + x_) & 0x00FF);
    return 0;
}

uint8_t Cpu6502::ZPY() {
    addrAbs_ = static_cast<uint16_t>((read(pc_++) + y_) & 0x00FF);
    return 0;
}

uint8_t Cpu6502::REL() {
    addrRel_ = read(pc_++);
    if (addrRel_ & 0x80) {
        addrRel_ |= 0xFF00;
    }
    return 0;
}

uint8_t Cpu6502::ABS() {
    uint16_t lo = read(pc_++);
    uint16_t hi = read(pc_++);
    addrAbs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t Cpu6502::ABX() {
    uint16_t lo = read(pc_++);
    uint16_t hi = read(pc_++);
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + x_);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t Cpu6502::ABY() {
    uint16_t lo = read(pc_++);
    uint16_t hi = read(pc_++);
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + y_);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

uint8_t Cpu6502::IND() {
    uint16_t ptrLo = read(pc_++);
    uint16_t ptrHi = read(pc_++);
    uint16_t ptr = static_cast<uint16_t>((ptrHi << 8) | ptrLo);

    if (ptrLo == 0x00FF) {
        // Reproduce the famous 6502 page-boundary bug: the high byte is
        // fetched from the start of the same page instead of the next page.
        addrAbs_ = static_cast<uint16_t>((read(ptr & 0xFF00) << 8) | read(ptr));
    } else {
        addrAbs_ = static_cast<uint16_t>((read(ptr + 1) << 8) | read(ptr));
    }
    return 0;
}

uint8_t Cpu6502::IZX() {
    uint16_t t = read(pc_++);
    uint16_t lo = read(static_cast<uint16_t>((t + x_) & 0x00FF));
    uint16_t hi = read(static_cast<uint16_t>((t + x_ + 1) & 0x00FF));
    addrAbs_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t Cpu6502::IZY() {
    uint16_t t = read(pc_++);
    uint16_t lo = read(t & 0x00FF);
    uint16_t hi = read(static_cast<uint16_t>((t + 1) & 0x00FF));
    addrAbs_ = static_cast<uint16_t>(((hi << 8) | lo) + y_);
    return ((addrAbs_ & 0xFF00) != (hi << 8)) ? 1 : 0;
}

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

uint8_t Cpu6502::ADC() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(a_) + fetched_ + (getFlag(C) ? 1 : 0);
    setFlag(C, temp > 255);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(V, (~(static_cast<uint16_t>(a_) ^ fetched_) & (static_cast<uint16_t>(a_) ^ temp)) & 0x0080);
    setFlag(N, temp & 0x0080);
    a_ = temp & 0x00FF;
    return 1;
}

uint8_t Cpu6502::SBC() {
    fetch();
    uint16_t value = static_cast<uint16_t>(fetched_) ^ 0x00FF;
    uint16_t temp = static_cast<uint16_t>(a_) + value + (getFlag(C) ? 1 : 0);
    setFlag(C, temp & 0xFF00);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(V, (temp ^ static_cast<uint16_t>(a_)) & (temp ^ value) & 0x0080);
    setFlag(N, temp & 0x0080);
    a_ = temp & 0x00FF;
    return 1;
}

uint8_t Cpu6502::AND() {
    fetch();
    a_ = a_ & fetched_;
    setFlag(Z, a_ == 0);
    setFlag(N, a_ & 0x80);
    return 1;
}

uint8_t Cpu6502::ASL() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(fetched_) << 1;
    setFlag(C, (temp & 0xFF00) != 0);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    if (lookup_[opcode_].addrmode == &Cpu6502::IMP) {
        a_ = temp & 0x00FF;
    } else {
        write(addrAbs_, temp & 0x00FF);
    }
    return 0;
}

uint8_t Cpu6502::BCC() {
    if (!getFlag(C)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BCS() {
    if (getFlag(C)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BEQ() {
    if (getFlag(Z)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BIT() {
    fetch();
    uint16_t temp = a_ & fetched_;
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, fetched_ & (1 << 7));
    setFlag(V, fetched_ & (1 << 6));
    return 0;
}

uint8_t Cpu6502::BMI() {
    if (getFlag(N)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BNE() {
    if (!getFlag(Z)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BPL() {
    if (!getFlag(N)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BRK() {
    pc_++;
    write(0x0100 + sp_, static_cast<uint8_t>((pc_ >> 8) & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(pc_ & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(status_ | B | U));
    sp_--;
    setFlag(I, true);
    pc_ = static_cast<uint16_t>(read(0xFFFE) | (read(0xFFFF) << 8));
    return 0;
}

uint8_t Cpu6502::BVC() {
    if (!getFlag(V)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::BVS() {
    if (getFlag(V)) {
        cyclesLeft_++;
        addrAbs_ = static_cast<uint16_t>(pc_ + addrRel_);
        if ((addrAbs_ & 0xFF00) != (pc_ & 0xFF00)) cyclesLeft_++;
        pc_ = addrAbs_;
    }
    return 0;
}

uint8_t Cpu6502::CLC() { setFlag(C, false); return 0; }
uint8_t Cpu6502::CLD() { setFlag(D, false); return 0; }
uint8_t Cpu6502::CLI() { setFlag(I, false); return 0; }
uint8_t Cpu6502::CLV() { setFlag(V, false); return 0; }

uint8_t Cpu6502::CMP() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(a_) - fetched_;
    setFlag(C, a_ >= fetched_);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    return 1;
}

uint8_t Cpu6502::CPX() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(x_) - fetched_;
    setFlag(C, x_ >= fetched_);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    return 0;
}

uint8_t Cpu6502::CPY() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(y_) - fetched_;
    setFlag(C, y_ >= fetched_);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    return 0;
}

uint8_t Cpu6502::DEC() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(fetched_ - 1);
    write(addrAbs_, temp & 0x00FF);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    return 0;
}

uint8_t Cpu6502::DEX() { x_--; setFlag(Z, x_ == 0); setFlag(N, x_ & 0x80); return 0; }
uint8_t Cpu6502::DEY() { y_--; setFlag(Z, y_ == 0); setFlag(N, y_ & 0x80); return 0; }

uint8_t Cpu6502::EOR() {
    fetch();
    a_ = a_ ^ fetched_;
    setFlag(Z, a_ == 0);
    setFlag(N, a_ & 0x80);
    return 1;
}

uint8_t Cpu6502::INC() {
    fetch();
    uint16_t temp = static_cast<uint16_t>(fetched_ + 1);
    write(addrAbs_, temp & 0x00FF);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    return 0;
}

uint8_t Cpu6502::INX() { x_++; setFlag(Z, x_ == 0); setFlag(N, x_ & 0x80); return 0; }
uint8_t Cpu6502::INY() { y_++; setFlag(Z, y_ == 0); setFlag(N, y_ & 0x80); return 0; }

uint8_t Cpu6502::JMP() { pc_ = addrAbs_; return 0; }

uint8_t Cpu6502::JSR() {
    pc_--;
    write(0x0100 + sp_, static_cast<uint8_t>((pc_ >> 8) & 0xFF));
    sp_--;
    write(0x0100 + sp_, static_cast<uint8_t>(pc_ & 0xFF));
    sp_--;
    pc_ = addrAbs_;
    return 0;
}

uint8_t Cpu6502::LDA() { fetch(); a_ = fetched_; setFlag(Z, a_ == 0); setFlag(N, a_ & 0x80); return 1; }
uint8_t Cpu6502::LDX() { fetch(); x_ = fetched_; setFlag(Z, x_ == 0); setFlag(N, x_ & 0x80); return 1; }
uint8_t Cpu6502::LDY() { fetch(); y_ = fetched_; setFlag(Z, y_ == 0); setFlag(N, y_ & 0x80); return 1; }

uint8_t Cpu6502::LSR() {
    fetch();
    setFlag(C, fetched_ & 0x0001);
    uint16_t temp = fetched_ >> 1;
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    if (lookup_[opcode_].addrmode == &Cpu6502::IMP) {
        a_ = temp & 0x00FF;
    } else {
        write(addrAbs_, temp & 0x00FF);
    }
    return 0;
}

uint8_t Cpu6502::NOP() {
    // A handful of illegal NOP variants use absolute,X addressing and can
    // take an extra cycle on a page cross; returning 1 lets that combine
    // with the addressing mode's result. Official NOP (0xEA) is implied-mode
    // so the addressing mode always contributes 0 anyway.
    return 1;
}

uint8_t Cpu6502::ORA() { fetch(); a_ = a_ | fetched_; setFlag(Z, a_ == 0); setFlag(N, a_ & 0x80); return 1; }

uint8_t Cpu6502::PHA() { write(0x0100 + sp_, a_); sp_--; return 0; }

uint8_t Cpu6502::PHP() {
    write(0x0100 + sp_, static_cast<uint8_t>(status_ | B | U));
    sp_--;
    return 0;
}

uint8_t Cpu6502::PLA() {
    sp_++;
    a_ = read(0x0100 + sp_);
    setFlag(Z, a_ == 0);
    setFlag(N, a_ & 0x80);
    return 0;
}

uint8_t Cpu6502::PLP() {
    sp_++;
    status_ = read(0x0100 + sp_);
    setFlag(U, true);
    return 0;
}

uint8_t Cpu6502::ROL() {
    fetch();
    uint16_t temp = static_cast<uint16_t>((fetched_ << 1) | (getFlag(C) ? 1 : 0));
    setFlag(C, temp & 0xFF00);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    if (lookup_[opcode_].addrmode == &Cpu6502::IMP) {
        a_ = temp & 0x00FF;
    } else {
        write(addrAbs_, temp & 0x00FF);
    }
    return 0;
}

uint8_t Cpu6502::ROR() {
    fetch();
    uint16_t temp = static_cast<uint16_t>((getFlag(C) ? (1 << 7) : 0) | (fetched_ >> 1));
    setFlag(C, fetched_ & 0x01);
    setFlag(Z, (temp & 0x00FF) == 0);
    setFlag(N, temp & 0x0080);
    if (lookup_[opcode_].addrmode == &Cpu6502::IMP) {
        a_ = temp & 0x00FF;
    } else {
        write(addrAbs_, temp & 0x00FF);
    }
    return 0;
}

uint8_t Cpu6502::RTI() {
    sp_++;
    status_ = read(0x0100 + sp_);
    setFlag(U, true);

    sp_++;
    uint16_t lo = read(0x0100 + sp_);
    sp_++;
    uint16_t hi = read(0x0100 + sp_);
    pc_ = static_cast<uint16_t>((hi << 8) | lo);
    return 0;
}

uint8_t Cpu6502::RTS() {
    sp_++;
    uint16_t lo = read(0x0100 + sp_);
    sp_++;
    uint16_t hi = read(0x0100 + sp_);
    pc_ = static_cast<uint16_t>((hi << 8) | lo);
    pc_++;
    return 0;
}

uint8_t Cpu6502::SEC() { setFlag(C, true); return 0; }
uint8_t Cpu6502::SED() { setFlag(D, true); return 0; }
uint8_t Cpu6502::SEI() { setFlag(I, true); return 0; }

uint8_t Cpu6502::STA() { write(addrAbs_, a_); return 0; }
uint8_t Cpu6502::STX() { write(addrAbs_, x_); return 0; }
uint8_t Cpu6502::STY() { write(addrAbs_, y_); return 0; }

uint8_t Cpu6502::TAX() { x_ = a_; setFlag(Z, x_ == 0); setFlag(N, x_ & 0x80); return 0; }
uint8_t Cpu6502::TAY() { y_ = a_; setFlag(Z, y_ == 0); setFlag(N, y_ & 0x80); return 0; }
uint8_t Cpu6502::TSX() { x_ = sp_; setFlag(Z, x_ == 0); setFlag(N, x_ & 0x80); return 0; }
uint8_t Cpu6502::TXA() { a_ = x_; setFlag(Z, a_ == 0); setFlag(N, a_ & 0x80); return 0; }
uint8_t Cpu6502::TXS() { sp_ = x_; return 0; }
uint8_t Cpu6502::TYA() { a_ = y_; setFlag(Z, a_ == 0); setFlag(N, a_ & 0x80); return 0; }

uint8_t Cpu6502::XXX() { return 0; }

// ---------------------------------------------------------------------------
// Opcode table (all 151 official 6502 opcodes; unused slots are illegal
// opcodes and fall back to a NOP-like no-op via XXX/IMP, except $EB which is
// the one illegal opcode NES software occasionally relies on - it behaves
// exactly like the official SBC immediate).
// ---------------------------------------------------------------------------

void Cpu6502::buildLookupTable() {
    using I = Instruction;
    lookup_ = {{
        // 0x0_
        I{"BRK", &Cpu6502::BRK, &Cpu6502::IMM, 7}, I{"ORA", &Cpu6502::ORA, &Cpu6502::IZX, 6},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 3}, I{"ORA", &Cpu6502::ORA, &Cpu6502::ZP0, 3},
        I{"ASL", &Cpu6502::ASL, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"PHP", &Cpu6502::PHP, &Cpu6502::IMP, 3}, I{"ORA", &Cpu6502::ORA, &Cpu6502::IMM, 2},
        I{"ASL", &Cpu6502::ASL, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"ORA", &Cpu6502::ORA, &Cpu6502::ABS, 4},
        I{"ASL", &Cpu6502::ASL, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0x1_
        I{"BPL", &Cpu6502::BPL, &Cpu6502::REL, 2}, I{"ORA", &Cpu6502::ORA, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"ORA", &Cpu6502::ORA, &Cpu6502::ZPX, 4},
        I{"ASL", &Cpu6502::ASL, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"CLC", &Cpu6502::CLC, &Cpu6502::IMP, 2}, I{"ORA", &Cpu6502::ORA, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"ORA", &Cpu6502::ORA, &Cpu6502::ABX, 4},
        I{"ASL", &Cpu6502::ASL, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        // 0x2_
        I{"JSR", &Cpu6502::JSR, &Cpu6502::ABS, 6}, I{"AND", &Cpu6502::AND, &Cpu6502::IZX, 6},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"BIT", &Cpu6502::BIT, &Cpu6502::ZP0, 3}, I{"AND", &Cpu6502::AND, &Cpu6502::ZP0, 3},
        I{"ROL", &Cpu6502::ROL, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"PLP", &Cpu6502::PLP, &Cpu6502::IMP, 4}, I{"AND", &Cpu6502::AND, &Cpu6502::IMM, 2},
        I{"ROL", &Cpu6502::ROL, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"BIT", &Cpu6502::BIT, &Cpu6502::ABS, 4}, I{"AND", &Cpu6502::AND, &Cpu6502::ABS, 4},
        I{"ROL", &Cpu6502::ROL, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0x3_
        I{"BMI", &Cpu6502::BMI, &Cpu6502::REL, 2}, I{"AND", &Cpu6502::AND, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"AND", &Cpu6502::AND, &Cpu6502::ZPX, 4},
        I{"ROL", &Cpu6502::ROL, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"SEC", &Cpu6502::SEC, &Cpu6502::IMP, 2}, I{"AND", &Cpu6502::AND, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"AND", &Cpu6502::AND, &Cpu6502::ABX, 4},
        I{"ROL", &Cpu6502::ROL, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        // 0x4_
        I{"RTI", &Cpu6502::RTI, &Cpu6502::IMP, 6}, I{"EOR", &Cpu6502::EOR, &Cpu6502::IZX, 6},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 3}, I{"EOR", &Cpu6502::EOR, &Cpu6502::ZP0, 3},
        I{"LSR", &Cpu6502::LSR, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"PHA", &Cpu6502::PHA, &Cpu6502::IMP, 3}, I{"EOR", &Cpu6502::EOR, &Cpu6502::IMM, 2},
        I{"LSR", &Cpu6502::LSR, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"JMP", &Cpu6502::JMP, &Cpu6502::ABS, 3}, I{"EOR", &Cpu6502::EOR, &Cpu6502::ABS, 4},
        I{"LSR", &Cpu6502::LSR, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0x5_
        I{"BVC", &Cpu6502::BVC, &Cpu6502::REL, 2}, I{"EOR", &Cpu6502::EOR, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"EOR", &Cpu6502::EOR, &Cpu6502::ZPX, 4},
        I{"LSR", &Cpu6502::LSR, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"CLI", &Cpu6502::CLI, &Cpu6502::IMP, 2}, I{"EOR", &Cpu6502::EOR, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"EOR", &Cpu6502::EOR, &Cpu6502::ABX, 4},
        I{"LSR", &Cpu6502::LSR, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        // 0x6_
        I{"RTS", &Cpu6502::RTS, &Cpu6502::IMP, 6}, I{"ADC", &Cpu6502::ADC, &Cpu6502::IZX, 6},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 3}, I{"ADC", &Cpu6502::ADC, &Cpu6502::ZP0, 3},
        I{"ROR", &Cpu6502::ROR, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"PLA", &Cpu6502::PLA, &Cpu6502::IMP, 4}, I{"ADC", &Cpu6502::ADC, &Cpu6502::IMM, 2},
        I{"ROR", &Cpu6502::ROR, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"JMP", &Cpu6502::JMP, &Cpu6502::IND, 5}, I{"ADC", &Cpu6502::ADC, &Cpu6502::ABS, 4},
        I{"ROR", &Cpu6502::ROR, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0x7_
        I{"BVS", &Cpu6502::BVS, &Cpu6502::REL, 2}, I{"ADC", &Cpu6502::ADC, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"ADC", &Cpu6502::ADC, &Cpu6502::ZPX, 4},
        I{"ROR", &Cpu6502::ROR, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"SEI", &Cpu6502::SEI, &Cpu6502::IMP, 2}, I{"ADC", &Cpu6502::ADC, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"ADC", &Cpu6502::ADC, &Cpu6502::ABX, 4},
        I{"ROR", &Cpu6502::ROR, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        // 0x8_
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMM, 2}, I{"STA", &Cpu6502::STA, &Cpu6502::IZX, 6},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMM, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"STY", &Cpu6502::STY, &Cpu6502::ZP0, 3}, I{"STA", &Cpu6502::STA, &Cpu6502::ZP0, 3},
        I{"STX", &Cpu6502::STX, &Cpu6502::ZP0, 3}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 3},
        I{"DEY", &Cpu6502::DEY, &Cpu6502::IMP, 2}, I{"NOP", &Cpu6502::NOP, &Cpu6502::IMM, 2},
        I{"TXA", &Cpu6502::TXA, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"STY", &Cpu6502::STY, &Cpu6502::ABS, 4}, I{"STA", &Cpu6502::STA, &Cpu6502::ABS, 4},
        I{"STX", &Cpu6502::STX, &Cpu6502::ABS, 4}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        // 0x9_
        I{"BCC", &Cpu6502::BCC, &Cpu6502::REL, 2}, I{"STA", &Cpu6502::STA, &Cpu6502::IZY, 6},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"STY", &Cpu6502::STY, &Cpu6502::ZPX, 4}, I{"STA", &Cpu6502::STA, &Cpu6502::ZPX, 4},
        I{"STX", &Cpu6502::STX, &Cpu6502::ZPY, 4}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        I{"TYA", &Cpu6502::TYA, &Cpu6502::IMP, 2}, I{"STA", &Cpu6502::STA, &Cpu6502::ABY, 5},
        I{"TXS", &Cpu6502::TXS, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 5}, I{"STA", &Cpu6502::STA, &Cpu6502::ABX, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        // 0xA_
        I{"LDY", &Cpu6502::LDY, &Cpu6502::IMM, 2}, I{"LDA", &Cpu6502::LDA, &Cpu6502::IZX, 6},
        I{"LDX", &Cpu6502::LDX, &Cpu6502::IMM, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"LDY", &Cpu6502::LDY, &Cpu6502::ZP0, 3}, I{"LDA", &Cpu6502::LDA, &Cpu6502::ZP0, 3},
        I{"LDX", &Cpu6502::LDX, &Cpu6502::ZP0, 3}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 3},
        I{"TAY", &Cpu6502::TAY, &Cpu6502::IMP, 2}, I{"LDA", &Cpu6502::LDA, &Cpu6502::IMM, 2},
        I{"TAX", &Cpu6502::TAX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"LDY", &Cpu6502::LDY, &Cpu6502::ABS, 4}, I{"LDA", &Cpu6502::LDA, &Cpu6502::ABS, 4},
        I{"LDX", &Cpu6502::LDX, &Cpu6502::ABS, 4}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        // 0xB_
        I{"BCS", &Cpu6502::BCS, &Cpu6502::REL, 2}, I{"LDA", &Cpu6502::LDA, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"LDY", &Cpu6502::LDY, &Cpu6502::ZPX, 4}, I{"LDA", &Cpu6502::LDA, &Cpu6502::ZPX, 4},
        I{"LDX", &Cpu6502::LDX, &Cpu6502::ZPY, 4}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        I{"CLV", &Cpu6502::CLV, &Cpu6502::IMP, 2}, I{"LDA", &Cpu6502::LDA, &Cpu6502::ABY, 4},
        I{"TSX", &Cpu6502::TSX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        I{"LDY", &Cpu6502::LDY, &Cpu6502::ABX, 4}, I{"LDA", &Cpu6502::LDA, &Cpu6502::ABX, 4},
        I{"LDX", &Cpu6502::LDX, &Cpu6502::ABY, 4}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 4},
        // 0xC_
        I{"CPY", &Cpu6502::CPY, &Cpu6502::IMM, 2}, I{"CMP", &Cpu6502::CMP, &Cpu6502::IZX, 6},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMM, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"CPY", &Cpu6502::CPY, &Cpu6502::ZP0, 3}, I{"CMP", &Cpu6502::CMP, &Cpu6502::ZP0, 3},
        I{"DEC", &Cpu6502::DEC, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"INY", &Cpu6502::INY, &Cpu6502::IMP, 2}, I{"CMP", &Cpu6502::CMP, &Cpu6502::IMM, 2},
        I{"DEX", &Cpu6502::DEX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2},
        I{"CPY", &Cpu6502::CPY, &Cpu6502::ABS, 4}, I{"CMP", &Cpu6502::CMP, &Cpu6502::ABS, 4},
        I{"DEC", &Cpu6502::DEC, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0xD_
        I{"BNE", &Cpu6502::BNE, &Cpu6502::REL, 2}, I{"CMP", &Cpu6502::CMP, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"CMP", &Cpu6502::CMP, &Cpu6502::ZPX, 4},
        I{"DEC", &Cpu6502::DEC, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"CLD", &Cpu6502::CLD, &Cpu6502::IMP, 2}, I{"CMP", &Cpu6502::CMP, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"CMP", &Cpu6502::CMP, &Cpu6502::ABX, 4},
        I{"DEC", &Cpu6502::DEC, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        // 0xE_
        I{"CPX", &Cpu6502::CPX, &Cpu6502::IMM, 2}, I{"SBC", &Cpu6502::SBC, &Cpu6502::IZX, 6},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMM, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"CPX", &Cpu6502::CPX, &Cpu6502::ZP0, 3}, I{"SBC", &Cpu6502::SBC, &Cpu6502::ZP0, 3},
        I{"INC", &Cpu6502::INC, &Cpu6502::ZP0, 5}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 5},
        I{"INX", &Cpu6502::INX, &Cpu6502::IMP, 2}, I{"SBC", &Cpu6502::SBC, &Cpu6502::IMM, 2},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"SBC", &Cpu6502::SBC, &Cpu6502::IMM, 2},
        I{"CPX", &Cpu6502::CPX, &Cpu6502::ABS, 4}, I{"SBC", &Cpu6502::SBC, &Cpu6502::ABS, 4},
        I{"INC", &Cpu6502::INC, &Cpu6502::ABS, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        // 0xF_
        I{"BEQ", &Cpu6502::BEQ, &Cpu6502::REL, 2}, I{"SBC", &Cpu6502::SBC, &Cpu6502::IZY, 5},
        I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 8},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"SBC", &Cpu6502::SBC, &Cpu6502::ZPX, 4},
        I{"INC", &Cpu6502::INC, &Cpu6502::ZPX, 6}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 6},
        I{"SED", &Cpu6502::SED, &Cpu6502::IMP, 2}, I{"SBC", &Cpu6502::SBC, &Cpu6502::ABY, 4},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 2}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
        I{"NOP", &Cpu6502::NOP, &Cpu6502::IMP, 4}, I{"SBC", &Cpu6502::SBC, &Cpu6502::ABX, 4},
        I{"INC", &Cpu6502::INC, &Cpu6502::ABX, 7}, I{"???", &Cpu6502::XXX, &Cpu6502::IMP, 7},
    }};
}

} // namespace nes
