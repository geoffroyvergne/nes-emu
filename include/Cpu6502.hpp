#pragma once

#include <array>
#include <bitset>
#include <cstdint>
#include <string>
#include <string_view>

class Bus;

// Ricoh 2A03 CPU core (a MOS 6502 without decimal mode).
// Timing is instruction-granular: an instruction executes entirely on its first clock() and the
// CPU then idles for the remaining cycles, so the total cycle count stays exact.
class Cpu6502 {
public:
    enum Flag : std::uint8_t {
        C = 1 << 0, // Carry
        Z = 1 << 1, // Zero
        I = 1 << 2, // Interrupt disable
        D = 1 << 3, // Decimal mode (flag exists, but the 2A03 ignores it)
        B = 1 << 4, // Break (only exists in the copy pushed to the stack)
        U = 1 << 5, // Unused, always reads as 1
        V = 1 << 6, // Overflow
        N = 1 << 7, // Negative
    };

    enum class AddrMode : std::uint8_t {
        IMP,  // Implied
        ACC,  // Accumulator
        IMM,  // #$nn
        ZP,   // $nn
        ZPX,  // $nn,X
        ZPY,  // $nn,Y
        REL,  // Branch offset
        ABS,  // $nnnn
        ABSX, // $nnnn,X
        ABSY, // $nnnn,Y
        IND,  // ($nnnn)       JMP only
        INDX, // ($nn,X)
        INDY, // ($nn),Y
    };

    static constexpr std::uint16_t NMI_VECTOR = 0xFFFA;
    static constexpr std::uint16_t RESET_VECTOR = 0xFFFC;
    static constexpr std::uint16_t IRQ_BRK_VECTOR = 0xFFFE;

    explicit Cpu6502(Bus& bus);
    Cpu6502(const Cpu6502&) = delete;
    Cpu6502& operator=(const Cpu6502&) = delete;

    void reset();
    // Advances the CPU by one CPU cycle.
    void clock();
    // Signals a non-maskable interrupt; it is serviced at the next instruction boundary.
    void nmi() { nmiPending = true; }
    // Halts the CPU after the current instruction for the duration of an OAM DMA transfer:
    // 513 cycles, or 514 when the halt begins on an odd CPU cycle (alignment to a read cycle).
    void stallForOamDma();

    // True when the current instruction has finished and the next clock() fetches a new opcode.
    [[nodiscard]] bool isInstructionComplete() const { return cyclesRemaining == 0 && stallCycles == 0; }
    [[nodiscard]] std::uint64_t getTotalCycles() const { return totalCycles; }
    [[nodiscard]] bool getFlag(Flag flag) const { return (status & flag) != 0; }

    // Formats the instruction at addr plus the current CPU state as a nestest.log line, e.g.
    //   C000  4C F5 C5  JMP $C5F5                       A:00 X:00 Y:00 P:24 SP:FD CYC:7
    // Memory annotations ("= 5A") use side-effect-free bus reads, so it is safe to call anytime.
    [[nodiscard]] std::string disassembleLine(std::uint16_t addr);

    // Registers are public so debuggers and tests can inspect and set them directly.
    std::uint8_t a = 0x00;       // Accumulator
    std::uint8_t x = 0x00;       // Index X
    std::uint8_t y = 0x00;       // Index Y
    std::uint8_t stp = 0xFD;     // Stack pointer (offset into page $01)
    std::uint16_t pc = 0x0000;   // Program counter
    std::uint8_t status = U | I; // Processor status

private:
    // Addressing modes and operations return 1 when they may need an extra cycle; the extra cycle
    // is only taken when both agree (e.g. a page-crossing read on LDA abs,X).
    using Handler = std::uint8_t (Cpu6502::*)();

    struct Instruction {
        std::string_view mnemonic;
        Handler operate;
        Handler addrModeHandler;
        AddrMode addrMode;
        std::uint8_t cycles;
        bool isUnofficial;
    };

    static constexpr std::array<Instruction, 256> buildInstructionTable();
    static const std::array<Instruction, 256> INSTRUCTION_TABLE;

    [[nodiscard]] std::uint8_t read(std::uint16_t addr);
    void write(std::uint16_t addr, std::uint8_t data);
    [[nodiscard]] std::uint16_t read16(std::uint16_t addr);
    // Side-effect-free read for the disassembler.
    [[nodiscard]] std::uint8_t peek(std::uint16_t addr);

    void setFlag(Flag flag, bool value);
    void setZN(std::uint8_t value);
    // Returns the operand for the current instruction (the accumulator for implied/accumulator).
    std::uint8_t fetch();

    // The stack lives in page $01; the stack pointer points at the next free slot.
    void push(std::uint8_t data);
    std::uint8_t pop();

    // Shared by all conditional branches: +1 cycle when taken, +1 more when crossing a page.
    void branchIf(bool condition);
    // Read-modify-write helper for INC/DEC (and later ASL/LSR/ROL/ROR on memory).
    void writeModified(std::uint8_t original, std::uint8_t result);

    // Shared ALU steps.
    void addWithCarry(std::uint8_t value); // ADC; SBC is ADC of the inverted operand
    void compare(std::uint8_t reg, std::uint8_t value);
    std::uint8_t shiftLeft(std::uint8_t value);   // ASL: C <- [76543210] <- 0
    std::uint8_t shiftRight(std::uint8_t value);  // LSR: 0 -> [76543210] -> C
    std::uint8_t rotateLeft(std::uint8_t value);  // ROL: C <- [76543210] <- C
    std::uint8_t rotateRight(std::uint8_t value); // ROR: C -> [76543210] -> C
    // Writes a shift/rotate result back to the accumulator or to memory (read-modify-write).
    void storeShifted(std::uint8_t original, std::uint8_t result);
    // Pushes PC and status, then jumps through the NMI vector (7 cycles).
    void serviceNmi();
    // Temporary handler for opcodes not implemented yet: reports each opcode once, then acts as NOP.
    std::uint8_t unimplemented();

    // Addressing modes: resolve the effective address into fetchedAddress (relAddr for branches).
    std::uint8_t implied();
    std::uint8_t accumulator();
    std::uint8_t immediate();
    std::uint8_t zeroPage();
    std::uint8_t zeroPageX();
    std::uint8_t zeroPageY();
    std::uint8_t relative();
    std::uint8_t absolute();
    std::uint8_t absoluteX();
    std::uint8_t absoluteY();
    std::uint8_t indirect();
    std::uint8_t indexedIndirect(); // (zp,X)
    std::uint8_t indirectIndexed(); // (zp),Y

    // Official operations.
    std::uint8_t opAdc();
    std::uint8_t opAnd();
    std::uint8_t opAsl();
    std::uint8_t opBcc();
    std::uint8_t opBcs();
    std::uint8_t opBeq();
    std::uint8_t opBit();
    std::uint8_t opBmi();
    std::uint8_t opBne();
    std::uint8_t opBpl();
    std::uint8_t opBrk();
    std::uint8_t opBvc();
    std::uint8_t opBvs();
    std::uint8_t opClc();
    std::uint8_t opCld();
    std::uint8_t opCli();
    std::uint8_t opClv();
    std::uint8_t opCmp();
    std::uint8_t opCpx();
    std::uint8_t opCpy();
    std::uint8_t opDec();
    std::uint8_t opDex();
    std::uint8_t opDey();
    std::uint8_t opEor();
    std::uint8_t opInc();
    std::uint8_t opInx();
    std::uint8_t opIny();
    std::uint8_t opJmp();
    std::uint8_t opJsr();
    std::uint8_t opLda();
    std::uint8_t opLdx();
    std::uint8_t opLdy();
    std::uint8_t opLsr();
    std::uint8_t opNop();
    std::uint8_t opOra();
    std::uint8_t opPha();
    std::uint8_t opPhp();
    std::uint8_t opPla();
    std::uint8_t opPlp();
    std::uint8_t opRol();
    std::uint8_t opRor();
    std::uint8_t opRti();
    std::uint8_t opRts();
    std::uint8_t opSbc();
    std::uint8_t opSec();
    std::uint8_t opSed();
    std::uint8_t opSei();
    std::uint8_t opSta();
    std::uint8_t opStx();
    std::uint8_t opSty();
    std::uint8_t opTax();
    std::uint8_t opTay();
    std::uint8_t opTsx();
    std::uint8_t opTxa();
    std::uint8_t opTxs();
    std::uint8_t opTya();

    // Unofficial operations (the ones nestest exercises come first).
    std::uint8_t opLax();
    std::uint8_t opSax();
    std::uint8_t opDcp();
    std::uint8_t opIsb();
    std::uint8_t opSlo();
    std::uint8_t opRla();
    std::uint8_t opSre();
    std::uint8_t opRra();
    std::uint8_t opAnc();
    std::uint8_t opAlr();
    std::uint8_t opArr();
    std::uint8_t opAxs();
    std::uint8_t opXaa();
    std::uint8_t opLxa();
    std::uint8_t opLas();
    std::uint8_t opTas();
    std::uint8_t opSha();
    std::uint8_t opShx();
    std::uint8_t opShy();
    std::uint8_t opJam();

    Bus& bus;
    std::bitset<256> reportedUnimplemented;

    std::uint8_t opcode = 0x00;
    std::uint8_t fetchedData = 0x00;
    std::uint16_t fetchedAddress = 0x0000;
    std::uint16_t relAddr = 0x0000;
    bool operandIsAccumulator = false;
    bool nmiPending = false;
    std::uint16_t stallCycles = 0; // DMA halt, consumed before the next instruction

    std::uint8_t cyclesRemaining = 0;
    std::uint64_t totalCycles = 0;
};
