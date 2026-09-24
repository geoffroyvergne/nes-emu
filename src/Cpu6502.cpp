#include "Cpu6502.hpp"

#include "Bus.hpp"

#include <format>
#include <iostream>

namespace {

constexpr std::uint8_t RESET_CYCLES = 7;
constexpr std::uint8_t INTERRUPT_CYCLES = 7;
constexpr std::uint16_t OAM_DMA_CYCLES = 513;
constexpr std::uint8_t RESET_STACK_POINTER = 0xFD;
constexpr std::uint16_t STACK_BASE = 0x0100;

constexpr std::uint8_t OPCODE_JSR = 0x20;
constexpr std::uint8_t OPCODE_JMP_ABS = 0x4C;

[[nodiscard]] constexpr bool isPageCrossed(std::uint16_t from, std::uint16_t to) {
    return (from & 0xFF00) != (to & 0xFF00);
}

[[nodiscard]] constexpr std::uint16_t makeWord(std::uint8_t lo, std::uint8_t hi) {
    return static_cast<std::uint16_t>((hi << 8) | lo);
}

[[nodiscard]] constexpr std::uint8_t instructionLength(Cpu6502::AddrMode mode) {
    using enum Cpu6502::AddrMode;
    switch (mode) {
    case IMP:
    case ACC:
        return 1;
    case ABS:
    case ABSX:
    case ABSY:
    case IND:
        return 3;
    default:
        return 2;
    }
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Opcode table

constexpr std::array<Cpu6502::Instruction, 256> Cpu6502::buildInstructionTable() {
    using C = Cpu6502;
    using enum AddrMode;

    constexpr auto modeHandler = [](AddrMode mode) -> Handler {
        switch (mode) {
        case IMP: return &C::implied;
        case ACC: return &C::accumulator;
        case IMM: return &C::immediate;
        case ZP: return &C::zeroPage;
        case ZPX: return &C::zeroPageX;
        case ZPY: return &C::zeroPageY;
        case REL: return &C::relative;
        case ABS: return &C::absolute;
        case ABSX: return &C::absoluteX;
        case ABSY: return &C::absoluteY;
        case IND: return &C::indirect;
        case INDX: return &C::indexedIndirect;
        case INDY: return &C::indirectIndexed;
        }
        return &C::implied;
    };
    constexpr auto op = [modeHandler](std::string_view mnemonic, Handler operate, AddrMode mode,
                                      std::uint8_t cycles) {
        return Instruction{mnemonic, operate, modeHandler(mode), mode, cycles, false};
    };
    constexpr auto unofficial = [modeHandler](std::string_view mnemonic, Handler operate,
                                              AddrMode mode, std::uint8_t cycles) {
        return Instruction{mnemonic, operate, modeHandler(mode), mode, cycles, true};
    };

    std::array<Instruction, 256> t{};

    t[0x00] = op("BRK", &C::opBrk, IMP, 7);
    t[0x01] = op("ORA", &C::opOra, INDX, 6);
    t[0x02] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x03] = unofficial("SLO", &C::opSlo, INDX, 8);
    t[0x04] = unofficial("NOP", &C::opNop, ZP, 3);
    t[0x05] = op("ORA", &C::opOra, ZP, 3);
    t[0x06] = op("ASL", &C::opAsl, ZP, 5);
    t[0x07] = unofficial("SLO", &C::opSlo, ZP, 5);
    t[0x08] = op("PHP", &C::opPhp, IMP, 3);
    t[0x09] = op("ORA", &C::opOra, IMM, 2);
    t[0x0A] = op("ASL", &C::opAsl, ACC, 2);
    t[0x0B] = unofficial("ANC", &C::opAnc, IMM, 2);
    t[0x0C] = unofficial("NOP", &C::opNop, ABS, 4);
    t[0x0D] = op("ORA", &C::opOra, ABS, 4);
    t[0x0E] = op("ASL", &C::opAsl, ABS, 6);
    t[0x0F] = unofficial("SLO", &C::opSlo, ABS, 6);

    t[0x10] = op("BPL", &C::opBpl, REL, 2);
    t[0x11] = op("ORA", &C::opOra, INDY, 5);
    t[0x12] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x13] = unofficial("SLO", &C::opSlo, INDY, 8);
    t[0x14] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0x15] = op("ORA", &C::opOra, ZPX, 4);
    t[0x16] = op("ASL", &C::opAsl, ZPX, 6);
    t[0x17] = unofficial("SLO", &C::opSlo, ZPX, 6);
    t[0x18] = op("CLC", &C::opClc, IMP, 2);
    t[0x19] = op("ORA", &C::opOra, ABSY, 4);
    t[0x1A] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0x1B] = unofficial("SLO", &C::opSlo, ABSY, 7);
    t[0x1C] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0x1D] = op("ORA", &C::opOra, ABSX, 4);
    t[0x1E] = op("ASL", &C::opAsl, ABSX, 7);
    t[0x1F] = unofficial("SLO", &C::opSlo, ABSX, 7);

    t[0x20] = op("JSR", &C::opJsr, ABS, 6);
    t[0x21] = op("AND", &C::opAnd, INDX, 6);
    t[0x22] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x23] = unofficial("RLA", &C::opRla, INDX, 8);
    t[0x24] = op("BIT", &C::opBit, ZP, 3);
    t[0x25] = op("AND", &C::opAnd, ZP, 3);
    t[0x26] = op("ROL", &C::opRol, ZP, 5);
    t[0x27] = unofficial("RLA", &C::opRla, ZP, 5);
    t[0x28] = op("PLP", &C::opPlp, IMP, 4);
    t[0x29] = op("AND", &C::opAnd, IMM, 2);
    t[0x2A] = op("ROL", &C::opRol, ACC, 2);
    t[0x2B] = unofficial("ANC", &C::opAnc, IMM, 2);
    t[0x2C] = op("BIT", &C::opBit, ABS, 4);
    t[0x2D] = op("AND", &C::opAnd, ABS, 4);
    t[0x2E] = op("ROL", &C::opRol, ABS, 6);
    t[0x2F] = unofficial("RLA", &C::opRla, ABS, 6);

    t[0x30] = op("BMI", &C::opBmi, REL, 2);
    t[0x31] = op("AND", &C::opAnd, INDY, 5);
    t[0x32] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x33] = unofficial("RLA", &C::opRla, INDY, 8);
    t[0x34] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0x35] = op("AND", &C::opAnd, ZPX, 4);
    t[0x36] = op("ROL", &C::opRol, ZPX, 6);
    t[0x37] = unofficial("RLA", &C::opRla, ZPX, 6);
    t[0x38] = op("SEC", &C::opSec, IMP, 2);
    t[0x39] = op("AND", &C::opAnd, ABSY, 4);
    t[0x3A] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0x3B] = unofficial("RLA", &C::opRla, ABSY, 7);
    t[0x3C] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0x3D] = op("AND", &C::opAnd, ABSX, 4);
    t[0x3E] = op("ROL", &C::opRol, ABSX, 7);
    t[0x3F] = unofficial("RLA", &C::opRla, ABSX, 7);

    t[0x40] = op("RTI", &C::opRti, IMP, 6);
    t[0x41] = op("EOR", &C::opEor, INDX, 6);
    t[0x42] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x43] = unofficial("SRE", &C::opSre, INDX, 8);
    t[0x44] = unofficial("NOP", &C::opNop, ZP, 3);
    t[0x45] = op("EOR", &C::opEor, ZP, 3);
    t[0x46] = op("LSR", &C::opLsr, ZP, 5);
    t[0x47] = unofficial("SRE", &C::opSre, ZP, 5);
    t[0x48] = op("PHA", &C::opPha, IMP, 3);
    t[0x49] = op("EOR", &C::opEor, IMM, 2);
    t[0x4A] = op("LSR", &C::opLsr, ACC, 2);
    t[0x4B] = unofficial("ALR", &C::opAlr, IMM, 2);
    t[0x4C] = op("JMP", &C::opJmp, ABS, 3);
    t[0x4D] = op("EOR", &C::opEor, ABS, 4);
    t[0x4E] = op("LSR", &C::opLsr, ABS, 6);
    t[0x4F] = unofficial("SRE", &C::opSre, ABS, 6);

    t[0x50] = op("BVC", &C::opBvc, REL, 2);
    t[0x51] = op("EOR", &C::opEor, INDY, 5);
    t[0x52] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x53] = unofficial("SRE", &C::opSre, INDY, 8);
    t[0x54] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0x55] = op("EOR", &C::opEor, ZPX, 4);
    t[0x56] = op("LSR", &C::opLsr, ZPX, 6);
    t[0x57] = unofficial("SRE", &C::opSre, ZPX, 6);
    t[0x58] = op("CLI", &C::opCli, IMP, 2);
    t[0x59] = op("EOR", &C::opEor, ABSY, 4);
    t[0x5A] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0x5B] = unofficial("SRE", &C::opSre, ABSY, 7);
    t[0x5C] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0x5D] = op("EOR", &C::opEor, ABSX, 4);
    t[0x5E] = op("LSR", &C::opLsr, ABSX, 7);
    t[0x5F] = unofficial("SRE", &C::opSre, ABSX, 7);

    t[0x60] = op("RTS", &C::opRts, IMP, 6);
    t[0x61] = op("ADC", &C::opAdc, INDX, 6);
    t[0x62] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x63] = unofficial("RRA", &C::opRra, INDX, 8);
    t[0x64] = unofficial("NOP", &C::opNop, ZP, 3);
    t[0x65] = op("ADC", &C::opAdc, ZP, 3);
    t[0x66] = op("ROR", &C::opRor, ZP, 5);
    t[0x67] = unofficial("RRA", &C::opRra, ZP, 5);
    t[0x68] = op("PLA", &C::opPla, IMP, 4);
    t[0x69] = op("ADC", &C::opAdc, IMM, 2);
    t[0x6A] = op("ROR", &C::opRor, ACC, 2);
    t[0x6B] = unofficial("ARR", &C::opArr, IMM, 2);
    t[0x6C] = op("JMP", &C::opJmp, IND, 5);
    t[0x6D] = op("ADC", &C::opAdc, ABS, 4);
    t[0x6E] = op("ROR", &C::opRor, ABS, 6);
    t[0x6F] = unofficial("RRA", &C::opRra, ABS, 6);

    t[0x70] = op("BVS", &C::opBvs, REL, 2);
    t[0x71] = op("ADC", &C::opAdc, INDY, 5);
    t[0x72] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x73] = unofficial("RRA", &C::opRra, INDY, 8);
    t[0x74] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0x75] = op("ADC", &C::opAdc, ZPX, 4);
    t[0x76] = op("ROR", &C::opRor, ZPX, 6);
    t[0x77] = unofficial("RRA", &C::opRra, ZPX, 6);
    t[0x78] = op("SEI", &C::opSei, IMP, 2);
    t[0x79] = op("ADC", &C::opAdc, ABSY, 4);
    t[0x7A] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0x7B] = unofficial("RRA", &C::opRra, ABSY, 7);
    t[0x7C] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0x7D] = op("ADC", &C::opAdc, ABSX, 4);
    t[0x7E] = op("ROR", &C::opRor, ABSX, 7);
    t[0x7F] = unofficial("RRA", &C::opRra, ABSX, 7);

    t[0x80] = unofficial("NOP", &C::opNop, IMM, 2);
    t[0x81] = op("STA", &C::opSta, INDX, 6);
    t[0x82] = unofficial("NOP", &C::opNop, IMM, 2);
    t[0x83] = unofficial("SAX", &C::opSax, INDX, 6);
    t[0x84] = op("STY", &C::opSty, ZP, 3);
    t[0x85] = op("STA", &C::opSta, ZP, 3);
    t[0x86] = op("STX", &C::opStx, ZP, 3);
    t[0x87] = unofficial("SAX", &C::opSax, ZP, 3);
    t[0x88] = op("DEY", &C::opDey, IMP, 2);
    t[0x89] = unofficial("NOP", &C::opNop, IMM, 2);
    t[0x8A] = op("TXA", &C::opTxa, IMP, 2);
    t[0x8B] = unofficial("XAA", &C::opXaa, IMM, 2);
    t[0x8C] = op("STY", &C::opSty, ABS, 4);
    t[0x8D] = op("STA", &C::opSta, ABS, 4);
    t[0x8E] = op("STX", &C::opStx, ABS, 4);
    t[0x8F] = unofficial("SAX", &C::opSax, ABS, 4);

    t[0x90] = op("BCC", &C::opBcc, REL, 2);
    t[0x91] = op("STA", &C::opSta, INDY, 6);
    t[0x92] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0x93] = unofficial("SHA", &C::opSha, INDY, 6);
    t[0x94] = op("STY", &C::opSty, ZPX, 4);
    t[0x95] = op("STA", &C::opSta, ZPX, 4);
    t[0x96] = op("STX", &C::opStx, ZPY, 4);
    t[0x97] = unofficial("SAX", &C::opSax, ZPY, 4);
    t[0x98] = op("TYA", &C::opTya, IMP, 2);
    t[0x99] = op("STA", &C::opSta, ABSY, 5);
    t[0x9A] = op("TXS", &C::opTxs, IMP, 2);
    t[0x9B] = unofficial("TAS", &C::opTas, ABSY, 5);
    t[0x9C] = unofficial("SHY", &C::opShy, ABSX, 5);
    t[0x9D] = op("STA", &C::opSta, ABSX, 5);
    t[0x9E] = unofficial("SHX", &C::opShx, ABSY, 5);
    t[0x9F] = unofficial("SHA", &C::opSha, ABSY, 5);

    t[0xA0] = op("LDY", &C::opLdy, IMM, 2);
    t[0xA1] = op("LDA", &C::opLda, INDX, 6);
    t[0xA2] = op("LDX", &C::opLdx, IMM, 2);
    t[0xA3] = unofficial("LAX", &C::opLax, INDX, 6);
    t[0xA4] = op("LDY", &C::opLdy, ZP, 3);
    t[0xA5] = op("LDA", &C::opLda, ZP, 3);
    t[0xA6] = op("LDX", &C::opLdx, ZP, 3);
    t[0xA7] = unofficial("LAX", &C::opLax, ZP, 3);
    t[0xA8] = op("TAY", &C::opTay, IMP, 2);
    t[0xA9] = op("LDA", &C::opLda, IMM, 2);
    t[0xAA] = op("TAX", &C::opTax, IMP, 2);
    t[0xAB] = unofficial("LXA", &C::opLxa, IMM, 2);
    t[0xAC] = op("LDY", &C::opLdy, ABS, 4);
    t[0xAD] = op("LDA", &C::opLda, ABS, 4);
    t[0xAE] = op("LDX", &C::opLdx, ABS, 4);
    t[0xAF] = unofficial("LAX", &C::opLax, ABS, 4);

    t[0xB0] = op("BCS", &C::opBcs, REL, 2);
    t[0xB1] = op("LDA", &C::opLda, INDY, 5);
    t[0xB2] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0xB3] = unofficial("LAX", &C::opLax, INDY, 5);
    t[0xB4] = op("LDY", &C::opLdy, ZPX, 4);
    t[0xB5] = op("LDA", &C::opLda, ZPX, 4);
    t[0xB6] = op("LDX", &C::opLdx, ZPY, 4);
    t[0xB7] = unofficial("LAX", &C::opLax, ZPY, 4);
    t[0xB8] = op("CLV", &C::opClv, IMP, 2);
    t[0xB9] = op("LDA", &C::opLda, ABSY, 4);
    t[0xBA] = op("TSX", &C::opTsx, IMP, 2);
    t[0xBB] = unofficial("LAS", &C::opLas, ABSY, 4);
    t[0xBC] = op("LDY", &C::opLdy, ABSX, 4);
    t[0xBD] = op("LDA", &C::opLda, ABSX, 4);
    t[0xBE] = op("LDX", &C::opLdx, ABSY, 4);
    t[0xBF] = unofficial("LAX", &C::opLax, ABSY, 4);

    t[0xC0] = op("CPY", &C::opCpy, IMM, 2);
    t[0xC1] = op("CMP", &C::opCmp, INDX, 6);
    t[0xC2] = unofficial("NOP", &C::opNop, IMM, 2);
    t[0xC3] = unofficial("DCP", &C::opDcp, INDX, 8);
    t[0xC4] = op("CPY", &C::opCpy, ZP, 3);
    t[0xC5] = op("CMP", &C::opCmp, ZP, 3);
    t[0xC6] = op("DEC", &C::opDec, ZP, 5);
    t[0xC7] = unofficial("DCP", &C::opDcp, ZP, 5);
    t[0xC8] = op("INY", &C::opIny, IMP, 2);
    t[0xC9] = op("CMP", &C::opCmp, IMM, 2);
    t[0xCA] = op("DEX", &C::opDex, IMP, 2);
    t[0xCB] = unofficial("AXS", &C::opAxs, IMM, 2);
    t[0xCC] = op("CPY", &C::opCpy, ABS, 4);
    t[0xCD] = op("CMP", &C::opCmp, ABS, 4);
    t[0xCE] = op("DEC", &C::opDec, ABS, 6);
    t[0xCF] = unofficial("DCP", &C::opDcp, ABS, 6);

    t[0xD0] = op("BNE", &C::opBne, REL, 2);
    t[0xD1] = op("CMP", &C::opCmp, INDY, 5);
    t[0xD2] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0xD3] = unofficial("DCP", &C::opDcp, INDY, 8);
    t[0xD4] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0xD5] = op("CMP", &C::opCmp, ZPX, 4);
    t[0xD6] = op("DEC", &C::opDec, ZPX, 6);
    t[0xD7] = unofficial("DCP", &C::opDcp, ZPX, 6);
    t[0xD8] = op("CLD", &C::opCld, IMP, 2);
    t[0xD9] = op("CMP", &C::opCmp, ABSY, 4);
    t[0xDA] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0xDB] = unofficial("DCP", &C::opDcp, ABSY, 7);
    t[0xDC] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0xDD] = op("CMP", &C::opCmp, ABSX, 4);
    t[0xDE] = op("DEC", &C::opDec, ABSX, 7);
    t[0xDF] = unofficial("DCP", &C::opDcp, ABSX, 7);

    t[0xE0] = op("CPX", &C::opCpx, IMM, 2);
    t[0xE1] = op("SBC", &C::opSbc, INDX, 6);
    t[0xE2] = unofficial("NOP", &C::opNop, IMM, 2);
    t[0xE3] = unofficial("ISB", &C::opIsb, INDX, 8);
    t[0xE4] = op("CPX", &C::opCpx, ZP, 3);
    t[0xE5] = op("SBC", &C::opSbc, ZP, 3);
    t[0xE6] = op("INC", &C::opInc, ZP, 5);
    t[0xE7] = unofficial("ISB", &C::opIsb, ZP, 5);
    t[0xE8] = op("INX", &C::opInx, IMP, 2);
    t[0xE9] = op("SBC", &C::opSbc, IMM, 2);
    t[0xEA] = op("NOP", &C::opNop, IMP, 2);
    t[0xEB] = unofficial("SBC", &C::opSbc, IMM, 2);
    t[0xEC] = op("CPX", &C::opCpx, ABS, 4);
    t[0xED] = op("SBC", &C::opSbc, ABS, 4);
    t[0xEE] = op("INC", &C::opInc, ABS, 6);
    t[0xEF] = unofficial("ISB", &C::opIsb, ABS, 6);

    t[0xF0] = op("BEQ", &C::opBeq, REL, 2);
    t[0xF1] = op("SBC", &C::opSbc, INDY, 5);
    t[0xF2] = unofficial("JAM", &C::opJam, IMP, 2);
    t[0xF3] = unofficial("ISB", &C::opIsb, INDY, 8);
    t[0xF4] = unofficial("NOP", &C::opNop, ZPX, 4);
    t[0xF5] = op("SBC", &C::opSbc, ZPX, 4);
    t[0xF6] = op("INC", &C::opInc, ZPX, 6);
    t[0xF7] = unofficial("ISB", &C::opIsb, ZPX, 6);
    t[0xF8] = op("SED", &C::opSed, IMP, 2);
    t[0xF9] = op("SBC", &C::opSbc, ABSY, 4);
    t[0xFA] = unofficial("NOP", &C::opNop, IMP, 2);
    t[0xFB] = unofficial("ISB", &C::opIsb, ABSY, 7);
    t[0xFC] = unofficial("NOP", &C::opNop, ABSX, 4);
    t[0xFD] = op("SBC", &C::opSbc, ABSX, 4);
    t[0xFE] = op("INC", &C::opInc, ABSX, 7);
    t[0xFF] = unofficial("ISB", &C::opIsb, ABSX, 7);

    return t;
}

constinit const std::array<Cpu6502::Instruction, 256> Cpu6502::INSTRUCTION_TABLE =
    Cpu6502::buildInstructionTable();

// ---------------------------------------------------------------------------------------------
// Execution

Cpu6502::Cpu6502(Bus& bus) : bus(bus) {}

void Cpu6502::reset() {
    pc = read16(RESET_VECTOR);
    a = 0x00;
    x = 0x00;
    y = 0x00;
    stp = RESET_STACK_POINTER;
    status = U | I;

    fetchedAddress = 0x0000;
    relAddr = 0x0000;
    fetchedData = 0x00;

    cyclesRemaining = RESET_CYCLES;
    totalCycles = 0;
    nmiPending = false;
    stallCycles = 0;
}

void Cpu6502::stallForOamDma() {
    // The halt starts once the current instruction's remaining cycles have elapsed.
    const std::uint64_t haltStartCycle = totalCycles + cyclesRemaining;
    stallCycles = static_cast<std::uint16_t>(stallCycles + OAM_DMA_CYCLES + (haltStartCycle & 1));
}

void Cpu6502::clock() {
    if (cyclesRemaining == 0 && stallCycles > 0) {
        --stallCycles; // Halted by DMA: the bus belongs to the DMA unit this cycle
        ++totalCycles;
        return;
    }

    if (cyclesRemaining == 0 && nmiPending) {
        serviceNmi();
    } else if (cyclesRemaining == 0) {
        opcode = read(pc++);
        status |= U;

        const Instruction& instruction = INSTRUCTION_TABLE[opcode];
        cyclesRemaining = instruction.cycles;
        operandIsAccumulator = false;

        const std::uint8_t addrModeExtra = (this->*instruction.addrModeHandler)();
        const std::uint8_t operateExtra = (this->*instruction.operate)();
        cyclesRemaining = static_cast<std::uint8_t>(cyclesRemaining + (addrModeExtra & operateExtra));
    }

    --cyclesRemaining;
    ++totalCycles;
}

// ---------------------------------------------------------------------------------------------
// Bus access and flag helpers

std::uint8_t Cpu6502::read(std::uint16_t addr) {
    return bus.cpuRead(addr);
}

void Cpu6502::write(std::uint16_t addr, std::uint8_t data) {
    bus.cpuWrite(addr, data);
}

std::uint16_t Cpu6502::read16(std::uint16_t addr) {
    const std::uint8_t lo = read(addr);
    const std::uint8_t hi = read(static_cast<std::uint16_t>(addr + 1));
    return makeWord(lo, hi);
}

std::uint8_t Cpu6502::peek(std::uint16_t addr) {
    return bus.cpuRead(addr, true);
}

void Cpu6502::setFlag(Flag flag, bool value) {
    status = value ? static_cast<std::uint8_t>(status | flag) : static_cast<std::uint8_t>(status & ~flag);
}

void Cpu6502::setZN(std::uint8_t value) {
    setFlag(Z, value == 0x00);
    setFlag(N, (value & 0x80) != 0);
}

std::uint8_t Cpu6502::fetch() {
    if (!operandIsAccumulator) {
        fetchedData = read(fetchedAddress);
    }
    return fetchedData;
}

void Cpu6502::serviceNmi() {
    nmiPending = false;
    push(static_cast<std::uint8_t>(pc >> 8));
    push(static_cast<std::uint8_t>(pc & 0xFF));
    // Hardware interrupts push status with B clear and U set.
    push(static_cast<std::uint8_t>((status & ~B) | U));
    setFlag(I, true);
    pc = read16(NMI_VECTOR);
    cyclesRemaining = INTERRUPT_CYCLES;
}

void Cpu6502::push(std::uint8_t data) {
    write(static_cast<std::uint16_t>(STACK_BASE + stp), data);
    --stp;
}

std::uint8_t Cpu6502::pop() {
    ++stp;
    return read(static_cast<std::uint16_t>(STACK_BASE + stp));
}

void Cpu6502::branchIf(bool condition) {
    if (!condition) {
        return;
    }
    const auto target = static_cast<std::uint16_t>(pc + relAddr);
    cyclesRemaining = static_cast<std::uint8_t>(cyclesRemaining + (isPageCrossed(pc, target) ? 2 : 1));
    pc = target;
}

void Cpu6502::writeModified(std::uint8_t original, std::uint8_t result) {
    // Real hardware writes the unmodified value back before the result. RAM doesn't care, but
    // mapper registers (e.g. MMC1) see both writes.
    write(fetchedAddress, original);
    write(fetchedAddress, result);
}

std::uint8_t Cpu6502::unimplemented() {
    if (!reportedUnimplemented.test(opcode)) {
        reportedUnimplemented.set(opcode);
        std::cerr << std::format("CPU: unimplemented opcode ${:02X} ({}) at ${:04X}, treated as NOP\n", opcode,
                                 INSTRUCTION_TABLE[opcode].mnemonic, static_cast<std::uint16_t>(pc - 1));
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Addressing modes

std::uint8_t Cpu6502::implied() {
    operandIsAccumulator = true;
    fetchedData = a;
    return 0;
}

std::uint8_t Cpu6502::accumulator() {
    operandIsAccumulator = true;
    fetchedData = a;
    return 0;
}

std::uint8_t Cpu6502::immediate() {
    fetchedAddress = pc++;
    return 0;
}

std::uint8_t Cpu6502::zeroPage() {
    fetchedAddress = read(pc++);
    return 0;
}

std::uint8_t Cpu6502::zeroPageX() {
    fetchedAddress = static_cast<std::uint8_t>(read(pc++) + x);
    return 0;
}

std::uint8_t Cpu6502::zeroPageY() {
    fetchedAddress = static_cast<std::uint8_t>(read(pc++) + y);
    return 0;
}

std::uint8_t Cpu6502::relative() {
    // Sign-extend the 8-bit branch offset.
    relAddr = static_cast<std::uint16_t>(static_cast<std::int8_t>(read(pc++)));
    return 0;
}

std::uint8_t Cpu6502::absolute() {
    fetchedAddress = read16(pc);
    pc = static_cast<std::uint16_t>(pc + 2);
    return 0;
}

std::uint8_t Cpu6502::absoluteX() {
    const std::uint16_t base = read16(pc);
    pc = static_cast<std::uint16_t>(pc + 2);
    fetchedAddress = static_cast<std::uint16_t>(base + x);
    return isPageCrossed(base, fetchedAddress) ? 1 : 0;
}

std::uint8_t Cpu6502::absoluteY() {
    const std::uint16_t base = read16(pc);
    pc = static_cast<std::uint16_t>(pc + 2);
    fetchedAddress = static_cast<std::uint16_t>(base + y);
    return isPageCrossed(base, fetchedAddress) ? 1 : 0;
}

std::uint8_t Cpu6502::indirect() {
    const std::uint16_t ptr = read16(pc);
    pc = static_cast<std::uint16_t>(pc + 2);
    // Hardware bug: the high byte is fetched without carrying into the pointer's page,
    // so JMP ($10FF) reads its high byte from $1000 rather than $1100.
    const auto hiAddr = static_cast<std::uint16_t>((ptr & 0xFF00) | ((ptr + 1) & 0x00FF));
    fetchedAddress = makeWord(read(ptr), read(hiAddr));
    return 0;
}

std::uint8_t Cpu6502::indexedIndirect() {
    const auto zp = static_cast<std::uint8_t>(read(pc++) + x);
    fetchedAddress = makeWord(read(zp), read(static_cast<std::uint8_t>(zp + 1)));
    return 0;
}

std::uint8_t Cpu6502::indirectIndexed() {
    const std::uint8_t zp = read(pc++);
    const std::uint16_t base = makeWord(read(zp), read(static_cast<std::uint8_t>(zp + 1)));
    fetchedAddress = static_cast<std::uint16_t>(base + y);
    return isPageCrossed(base, fetchedAddress) ? 1 : 0;
}

// ---------------------------------------------------------------------------------------------
// Disassembler / nestest trace

std::string Cpu6502::disassembleLine(std::uint16_t addr) {
    const std::uint8_t code = peek(addr);
    const Instruction& instruction = INSTRUCTION_TABLE[code];
    const std::uint8_t lo = peek(static_cast<std::uint16_t>(addr + 1));
    const std::uint8_t hi = peek(static_cast<std::uint16_t>(addr + 2));
    const std::uint16_t word = makeWord(lo, hi);

    std::string bytes = std::format("{:02X}", code);
    const std::uint8_t length = instructionLength(instruction.addrMode);
    if (length >= 2) {
        bytes += std::format(" {:02X}", lo);
    }
    if (length == 3) {
        bytes += std::format(" {:02X}", hi);
    }

    std::string operand;
    switch (instruction.addrMode) {
        using enum AddrMode;
    case IMP:
        break;
    case ACC:
        operand = "A";
        break;
    case IMM:
        operand = std::format("#${:02X}", lo);
        break;
    case ZP:
        operand = std::format("${:02X} = {:02X}", lo, peek(lo));
        break;
    case ZPX: {
        const auto target = static_cast<std::uint8_t>(lo + x);
        operand = std::format("${:02X},X @ {:02X} = {:02X}", lo, target, peek(target));
        break;
    }
    case ZPY: {
        const auto target = static_cast<std::uint8_t>(lo + y);
        operand = std::format("${:02X},Y @ {:02X} = {:02X}", lo, target, peek(target));
        break;
    }
    case REL: {
        const auto target = static_cast<std::uint16_t>(addr + 2 + static_cast<std::int8_t>(lo));
        operand = std::format("${:04X}", target);
        break;
    }
    case ABS:
        // Jumps show only the destination; data accesses also show the current memory value.
        if (code == OPCODE_JMP_ABS || code == OPCODE_JSR) {
            operand = std::format("${:04X}", word);
        } else {
            operand = std::format("${:04X} = {:02X}", word, peek(word));
        }
        break;
    case ABSX: {
        const auto target = static_cast<std::uint16_t>(word + x);
        operand = std::format("${:04X},X @ {:04X} = {:02X}", word, target, peek(target));
        break;
    }
    case ABSY: {
        const auto target = static_cast<std::uint16_t>(word + y);
        operand = std::format("${:04X},Y @ {:04X} = {:02X}", word, target, peek(target));
        break;
    }
    case IND: {
        const auto hiAddr = static_cast<std::uint16_t>((word & 0xFF00) | ((word + 1) & 0x00FF));
        operand = std::format("(${:04X}) = {:04X}", word, makeWord(peek(word), peek(hiAddr)));
        break;
    }
    case INDX: {
        const auto ptr = static_cast<std::uint8_t>(lo + x);
        const std::uint16_t target = makeWord(peek(ptr), peek(static_cast<std::uint8_t>(ptr + 1)));
        operand = std::format("(${:02X},X) @ {:02X} = {:04X} = {:02X}", lo, ptr, target, peek(target));
        break;
    }
    case INDY: {
        const std::uint16_t base = makeWord(peek(lo), peek(static_cast<std::uint8_t>(lo + 1)));
        const auto target = static_cast<std::uint16_t>(base + y);
        operand = std::format("(${:02X}),Y = {:04X} @ {:04X} = {:02X}", lo, base, target, peek(target));
        break;
    }
    }

    const std::string assembly =
        operand.empty() ? std::string(instruction.mnemonic) : std::format("{} {}", instruction.mnemonic, operand);

    // Column layout matches nestest.log: bytes at col 6, '*' marker for unofficial opcodes at
    // col 15, assembly at col 16, registers at col 48.
    return std::format("{:04X}  {:<8} {}{:<32}A:{:02X} X:{:02X} Y:{:02X} P:{:02X} SP:{:02X} CYC:{}", addr,
                       bytes, instruction.isUnofficial ? '*' : ' ', assembly, a, x, y, status, stp,
                       totalCycles);
}

// ---------------------------------------------------------------------------------------------
// Operations: load / store / transfer

std::uint8_t Cpu6502::opLda() {
    a = fetch();
    setZN(a);
    return 1;
}

std::uint8_t Cpu6502::opLdx() {
    x = fetch();
    setZN(x);
    return 1;
}

std::uint8_t Cpu6502::opLdy() {
    y = fetch();
    setZN(y);
    return 1;
}

std::uint8_t Cpu6502::opSta() {
    write(fetchedAddress, a);
    return 0;
}

std::uint8_t Cpu6502::opStx() {
    write(fetchedAddress, x);
    return 0;
}

std::uint8_t Cpu6502::opSty() {
    write(fetchedAddress, y);
    return 0;
}

std::uint8_t Cpu6502::opTax() {
    x = a;
    setZN(x);
    return 0;
}

std::uint8_t Cpu6502::opTay() {
    y = a;
    setZN(y);
    return 0;
}

std::uint8_t Cpu6502::opTxa() {
    a = x;
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opTya() {
    a = y;
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opTsx() {
    x = stp;
    setZN(x);
    return 0;
}

std::uint8_t Cpu6502::opTxs() {
    stp = x; // The only transfer that leaves the flags alone.
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Operations: jumps, calls, flag changes

std::uint8_t Cpu6502::opJmp() {
    pc = fetchedAddress;
    return 0;
}

std::uint8_t Cpu6502::opJsr() {
    // pc already points past the operand; the 6502 pushes the address of its last byte.
    const auto returnAddr = static_cast<std::uint16_t>(pc - 1);
    push(static_cast<std::uint8_t>(returnAddr >> 8));
    push(static_cast<std::uint8_t>(returnAddr & 0xFF));
    pc = fetchedAddress;
    return 0;
}

std::uint8_t Cpu6502::opRts() {
    const std::uint8_t lo = pop();
    const std::uint8_t hi = pop();
    pc = static_cast<std::uint16_t>(makeWord(lo, hi) + 1);
    return 0;
}

std::uint8_t Cpu6502::opClc() {
    setFlag(C, false);
    return 0;
}

std::uint8_t Cpu6502::opSec() {
    setFlag(C, true);
    return 0;
}

std::uint8_t Cpu6502::opCld() {
    setFlag(D, false);
    return 0;
}

std::uint8_t Cpu6502::opSed() {
    setFlag(D, true);
    return 0;
}

std::uint8_t Cpu6502::opCli() {
    setFlag(I, false);
    return 0;
}

std::uint8_t Cpu6502::opSei() {
    setFlag(I, true);
    return 0;
}

std::uint8_t Cpu6502::opClv() {
    setFlag(V, false);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Operations: branches

std::uint8_t Cpu6502::opBcc() {
    branchIf(!getFlag(C));
    return 0;
}

std::uint8_t Cpu6502::opBcs() {
    branchIf(getFlag(C));
    return 0;
}

std::uint8_t Cpu6502::opBne() {
    branchIf(!getFlag(Z));
    return 0;
}

std::uint8_t Cpu6502::opBeq() {
    branchIf(getFlag(Z));
    return 0;
}

std::uint8_t Cpu6502::opBpl() {
    branchIf(!getFlag(N));
    return 0;
}

std::uint8_t Cpu6502::opBmi() {
    branchIf(getFlag(N));
    return 0;
}

std::uint8_t Cpu6502::opBvc() {
    branchIf(!getFlag(V));
    return 0;
}

std::uint8_t Cpu6502::opBvs() {
    branchIf(getFlag(V));
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Operations: increment / decrement / NOP

std::uint8_t Cpu6502::opInc() {
    const std::uint8_t original = fetch();
    const auto result = static_cast<std::uint8_t>(original + 1);
    writeModified(original, result);
    setZN(result);
    return 0;
}

std::uint8_t Cpu6502::opDec() {
    const std::uint8_t original = fetch();
    const auto result = static_cast<std::uint8_t>(original - 1);
    writeModified(original, result);
    setZN(result);
    return 0;
}

std::uint8_t Cpu6502::opInx() {
    ++x;
    setZN(x);
    return 0;
}

std::uint8_t Cpu6502::opDex() {
    --x;
    setZN(x);
    return 0;
}

std::uint8_t Cpu6502::opIny() {
    ++y;
    setZN(y);
    return 0;
}

std::uint8_t Cpu6502::opDey() {
    --y;
    setZN(y);
    return 0;
}

std::uint8_t Cpu6502::opNop() {
    // Unofficial NOPs with an absolute,X operand take the page-cross cycle like a load does.
    return 1;
}

// ---------------------------------------------------------------------------------------------
// ALU helpers

void Cpu6502::addWithCarry(std::uint8_t value) {
    const unsigned sum = unsigned{a} + value + (getFlag(C) ? 1u : 0u);
    const auto result = static_cast<std::uint8_t>(sum);
    setFlag(C, sum > 0xFF);
    // Overflow: both inputs share a sign and the result's sign differs from it.
    setFlag(V, ((a ^ result) & (value ^ result) & 0x80) != 0);
    a = result;
    setZN(a);
}

void Cpu6502::compare(std::uint8_t reg, std::uint8_t value) {
    setFlag(C, reg >= value);
    setZN(static_cast<std::uint8_t>(reg - value));
}

std::uint8_t Cpu6502::shiftLeft(std::uint8_t value) {
    setFlag(C, (value & 0x80) != 0);
    const auto result = static_cast<std::uint8_t>(value << 1);
    setZN(result);
    return result;
}

std::uint8_t Cpu6502::shiftRight(std::uint8_t value) {
    setFlag(C, (value & 0x01) != 0);
    const auto result = static_cast<std::uint8_t>(value >> 1);
    setZN(result);
    return result;
}

std::uint8_t Cpu6502::rotateLeft(std::uint8_t value) {
    const auto result = static_cast<std::uint8_t>((value << 1) | (getFlag(C) ? 0x01 : 0x00));
    setFlag(C, (value & 0x80) != 0);
    setZN(result);
    return result;
}

std::uint8_t Cpu6502::rotateRight(std::uint8_t value) {
    const auto result = static_cast<std::uint8_t>((value >> 1) | (getFlag(C) ? 0x80 : 0x00));
    setFlag(C, (value & 0x01) != 0);
    setZN(result);
    return result;
}

void Cpu6502::storeShifted(std::uint8_t original, std::uint8_t result) {
    if (operandIsAccumulator) {
        a = result;
    } else {
        writeModified(original, result);
    }
}

// ---------------------------------------------------------------------------------------------
// Operations: arithmetic, logic, compare

std::uint8_t Cpu6502::opAdc() {
    addWithCarry(fetch());
    return 1;
}

std::uint8_t Cpu6502::opSbc() {
    // A - M - (1 - C) == A + ~M + C on the 6502 (no decimal mode on the 2A03).
    addWithCarry(static_cast<std::uint8_t>(~fetch()));
    return 1;
}

std::uint8_t Cpu6502::opAnd() {
    a &= fetch();
    setZN(a);
    return 1;
}

std::uint8_t Cpu6502::opOra() {
    a |= fetch();
    setZN(a);
    return 1;
}

std::uint8_t Cpu6502::opEor() {
    a ^= fetch();
    setZN(a);
    return 1;
}

std::uint8_t Cpu6502::opBit() {
    const std::uint8_t value = fetch();
    setFlag(Z, (a & value) == 0);
    setFlag(N, (value & 0x80) != 0);
    setFlag(V, (value & 0x40) != 0);
    return 0;
}

std::uint8_t Cpu6502::opCmp() {
    compare(a, fetch());
    return 1;
}

std::uint8_t Cpu6502::opCpx() {
    compare(x, fetch());
    return 0;
}

std::uint8_t Cpu6502::opCpy() {
    compare(y, fetch());
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Operations: shifts and rotates (accumulator or memory)

std::uint8_t Cpu6502::opAsl() {
    const std::uint8_t value = fetch();
    storeShifted(value, shiftLeft(value));
    return 0;
}

std::uint8_t Cpu6502::opLsr() {
    const std::uint8_t value = fetch();
    storeShifted(value, shiftRight(value));
    return 0;
}

std::uint8_t Cpu6502::opRol() {
    const std::uint8_t value = fetch();
    storeShifted(value, rotateLeft(value));
    return 0;
}

std::uint8_t Cpu6502::opRor() {
    const std::uint8_t value = fetch();
    storeShifted(value, rotateRight(value));
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Operations: stack and interrupts

std::uint8_t Cpu6502::opPha() {
    push(a);
    return 0;
}

std::uint8_t Cpu6502::opPhp() {
    // Software pushes (PHP/BRK) set B and U in the pushed copy; the register itself has no B bit.
    push(static_cast<std::uint8_t>(status | B | U));
    return 0;
}

std::uint8_t Cpu6502::opPla() {
    a = pop();
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opPlp() {
    status = static_cast<std::uint8_t>((pop() & ~B) | U);
    return 0;
}

std::uint8_t Cpu6502::opBrk() {
    // BRK is a 2-byte instruction: the byte after the opcode is skipped by the return address.
    ++pc;
    push(static_cast<std::uint8_t>(pc >> 8));
    push(static_cast<std::uint8_t>(pc & 0xFF));
    push(static_cast<std::uint8_t>(status | B | U));
    setFlag(I, true);
    pc = read16(IRQ_BRK_VECTOR);
    return 0;
}

std::uint8_t Cpu6502::opRti() {
    status = static_cast<std::uint8_t>((pop() & ~B) | U);
    const std::uint8_t lo = pop();
    const std::uint8_t hi = pop();
    pc = makeWord(lo, hi);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Unofficial operations exercised by nestest (combinations of official ones)

std::uint8_t Cpu6502::opLax() { // LDA + TAX
    a = fetch();
    x = a;
    setZN(a);
    return 1;
}

std::uint8_t Cpu6502::opSax() { // Store A & X, no flags
    write(fetchedAddress, static_cast<std::uint8_t>(a & x));
    return 0;
}

std::uint8_t Cpu6502::opDcp() { // DEC + CMP
    const std::uint8_t original = fetch();
    const auto result = static_cast<std::uint8_t>(original - 1);
    writeModified(original, result);
    compare(a, result);
    return 0;
}

std::uint8_t Cpu6502::opIsb() { // INC + SBC
    const std::uint8_t original = fetch();
    const auto result = static_cast<std::uint8_t>(original + 1);
    writeModified(original, result);
    addWithCarry(static_cast<std::uint8_t>(~result));
    return 0;
}

std::uint8_t Cpu6502::opSlo() { // ASL + ORA
    const std::uint8_t original = fetch();
    const std::uint8_t result = shiftLeft(original);
    writeModified(original, result);
    a |= result;
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opRla() { // ROL + AND
    const std::uint8_t original = fetch();
    const std::uint8_t result = rotateLeft(original);
    writeModified(original, result);
    a &= result;
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opSre() { // LSR + EOR
    const std::uint8_t original = fetch();
    const std::uint8_t result = shiftRight(original);
    writeModified(original, result);
    a ^= result;
    setZN(a);
    return 0;
}

std::uint8_t Cpu6502::opRra() { // ROR + ADC (ADC uses the carry produced by ROR)
    const std::uint8_t original = fetch();
    const std::uint8_t result = rotateRight(original);
    writeModified(original, result);
    addWithCarry(result);
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Not implemented: rarely used / unstable unofficial opcodes and JAM (reported once, then NOP)

std::uint8_t Cpu6502::opAnc() { return unimplemented(); }
std::uint8_t Cpu6502::opAlr() { return unimplemented(); }
std::uint8_t Cpu6502::opArr() { return unimplemented(); }
std::uint8_t Cpu6502::opAxs() { return unimplemented(); }
std::uint8_t Cpu6502::opXaa() { return unimplemented(); }
std::uint8_t Cpu6502::opLxa() { return unimplemented(); }
std::uint8_t Cpu6502::opLas() { return unimplemented(); }
std::uint8_t Cpu6502::opTas() { return unimplemented(); }
std::uint8_t Cpu6502::opSha() { return unimplemented(); }
std::uint8_t Cpu6502::opShx() { return unimplemented(); }
std::uint8_t Cpu6502::opShy() { return unimplemented(); }
std::uint8_t Cpu6502::opJam() { return unimplemented(); }
