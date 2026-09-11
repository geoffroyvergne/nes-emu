// Self-contained 6502 CPU core tests: small hand-assembled programs that
// exercise addressing modes, flag behavior, cycle counts (including
// page-crossing and branch timing), the stack, and the indirect-JMP
// page-boundary hardware bug. No external ROM files needed - see
// cpu_nestest_test.cpp for the golden-log comparison test.

#include <cstdio>

#include "core/bus.h"
#include "core/cpu6502.h"
#include "test_util.h"

using nes::Bus;
using nes::Cpu6502;

namespace {

void testLoadFlags() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0xA9); bus.write(0x0001, 0x00); // LDA #$00
    bus.write(0x0002, 0xA9); bus.write(0x0003, 0x80); // LDA #$80
    bus.write(0x0004, 0xA9); bus.write(0x0005, 0x05); // LDA #$05

    cpu.step();
    TEST_CHECK(cpu.getFlag(Cpu6502::Z));
    TEST_CHECK(!cpu.getFlag(Cpu6502::N));

    cpu.step();
    TEST_CHECK(!cpu.getFlag(Cpu6502::Z));
    TEST_CHECK(cpu.getFlag(Cpu6502::N));

    cpu.step();
    TEST_CHECK_EQ(cpu.a(), 0x05);
    TEST_CHECK(!cpu.getFlag(Cpu6502::Z));
    TEST_CHECK(!cpu.getFlag(Cpu6502::N));
}

void testAdcOverflowAndCarry() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0x18);                          // CLC
    bus.write(0x0001, 0xA9); bus.write(0x0002, 0x50);  // LDA #$50
    bus.write(0x0003, 0x69); bus.write(0x0004, 0x50);  // ADC #$50

    cpu.step();
    cpu.step();
    cpu.step();

    TEST_CHECK_EQ(cpu.a(), 0xA0);
    TEST_CHECK(cpu.getFlag(Cpu6502::V));  // positive + positive => negative result
    TEST_CHECK(cpu.getFlag(Cpu6502::N));
    TEST_CHECK(!cpu.getFlag(Cpu6502::C));
}

void testSbcBorrow() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0x38);                          // SEC (no borrow-in)
    bus.write(0x0001, 0xA9); bus.write(0x0002, 0x00);  // LDA #$00
    bus.write(0x0003, 0xE9); bus.write(0x0004, 0x01);  // SBC #$01

    cpu.step();
    cpu.step();
    cpu.step();

    TEST_CHECK_EQ(cpu.a(), 0xFF);
    TEST_CHECK(!cpu.getFlag(Cpu6502::C)); // borrow occurred
    TEST_CHECK(cpu.getFlag(Cpu6502::N));
}

void testZeroPageXWraparound() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0xA2); bus.write(0x0001, 0xFF);  // LDX #$FF
    bus.write(0x0002, 0xA9); bus.write(0x0003, 0x42);  // LDA #$42
    bus.write(0x0004, 0x95); bus.write(0x0005, 0x80);  // STA $80,X -> wraps to $7F

    cpu.step();
    cpu.step();
    cpu.step();

    TEST_CHECK_EQ(bus.read(0x007F), 0x42);
}

void testAbsoluteXPageCrossCycles() {
    {
        Bus bus;
        Cpu6502 cpu(bus);
        cpu.setPC(0x0000);
        bus.write(0x0000, 0xA2); bus.write(0x0001, 0x01);         // LDX #$01
        bus.write(0x0002, 0xBD); bus.write(0x0003, 0xFF); bus.write(0x0004, 0x01); // LDA $01FF,X (-> $0200, page cross)
        cpu.step();
        unsigned cycles = cpu.step();
        TEST_CHECK_EQ(cycles, 5u);
    }
    {
        Bus bus;
        Cpu6502 cpu(bus);
        cpu.setPC(0x0000);
        bus.write(0x0000, 0xA2); bus.write(0x0001, 0x01);         // LDX #$01
        bus.write(0x0002, 0xBD); bus.write(0x0003, 0x10); bus.write(0x0004, 0x00); // LDA $0010,X (no page cross)
        cpu.step();
        unsigned cycles = cpu.step();
        TEST_CHECK_EQ(cycles, 4u);
    }
}

void testBranchCycleCounts() {
    Bus bus;
    Cpu6502 cpu(bus);

    // Not taken: 2 cycles.
    cpu.setPC(0x0020);
    bus.write(0x0020, 0x18);                          // CLC (C=0)
    bus.write(0x0021, 0xB0); bus.write(0x0022, 0x10);  // BCS +16 (not taken)
    cpu.step();
    TEST_CHECK_EQ(cpu.step(), 2u);
    TEST_CHECK_EQ(cpu.pc(), 0x0023);

    // Taken, same page: 3 cycles.
    cpu.setPC(0x0010);
    bus.write(0x0010, 0xB0); bus.write(0x0011, 0x05);  // BCS +5 (C still 0 from above -> force C=1 first)
    bus.write(0x0012, 0x00);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0x38);                           // SEC
    cpu.step();
    cpu.setPC(0x0010);
    TEST_CHECK_EQ(cpu.step(), 3u);
    TEST_CHECK_EQ(cpu.pc(), 0x0017);

    // Taken, crosses a page boundary: 4 cycles.
    cpu.setPC(0x00F0);
    bus.write(0x00F0, 0xB0); bus.write(0x00F1, 0x20);  // BCS +32 -> $0112 (crosses from page 0 to page 1)
    TEST_CHECK_EQ(cpu.step(), 4u);
    TEST_CHECK_EQ(cpu.pc(), 0x0112);
}

void testJsrRts() {
    Bus bus;
    Cpu6502 cpu(bus);
    uint8_t startSp = cpu.sp();

    cpu.setPC(0x0300);
    bus.write(0x0300, 0x20); bus.write(0x0301, 0x10); bus.write(0x0302, 0x03); // JSR $0310
    bus.write(0x0310, 0x60);                                                  // RTS

    cpu.step();
    TEST_CHECK_EQ(cpu.pc(), 0x0310);
    TEST_CHECK_EQ(cpu.sp(), static_cast<uint8_t>(startSp - 2));

    cpu.step();
    TEST_CHECK_EQ(cpu.pc(), 0x0303);
    TEST_CHECK_EQ(cpu.sp(), startSp);
}

void testPhaPla() {
    Bus bus;
    Cpu6502 cpu(bus);
    uint8_t startSp = cpu.sp();

    cpu.setPC(0x0000);
    bus.write(0x0000, 0xA9); bus.write(0x0001, 0x77); // LDA #$77
    bus.write(0x0002, 0x48);                          // PHA
    bus.write(0x0003, 0xA9); bus.write(0x0004, 0x00); // LDA #$00
    bus.write(0x0005, 0x68);                          // PLA

    cpu.step();
    cpu.step();
    TEST_CHECK_EQ(cpu.sp(), static_cast<uint8_t>(startSp - 1));
    cpu.step();
    cpu.step();

    TEST_CHECK_EQ(cpu.a(), 0x77);
    TEST_CHECK_EQ(cpu.sp(), startSp);
}

void testIndirectJmpPageBoundaryBug() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0400);
    bus.write(0x0400, 0x6C); bus.write(0x0401, 0xFF); bus.write(0x0402, 0x02); // JMP ($02FF)
    bus.write(0x02FF, 0x34); // target low byte
    bus.write(0x0200, 0x12); // buggy high byte source (wraps to start of same page)
    bus.write(0x0300, 0x99); // correct high byte source - must NOT be used

    cpu.step();
    TEST_CHECK_EQ(cpu.pc(), 0x1234);
}

void testCompareAndIncDec() {
    Bus bus;
    Cpu6502 cpu(bus);
    cpu.setPC(0x0000);
    bus.write(0x0000, 0xA9); bus.write(0x0001, 0x10); // LDA #$10
    bus.write(0x0002, 0xC9); bus.write(0x0003, 0x10); // CMP #$10
    bus.write(0x0004, 0xA2); bus.write(0x0005, 0xFF); // LDX #$FF
    bus.write(0x0006, 0xE8);                          // INX

    cpu.step();
    cpu.step();
    TEST_CHECK(cpu.getFlag(Cpu6502::Z));
    TEST_CHECK(cpu.getFlag(Cpu6502::C));

    cpu.step();
    cpu.step();
    TEST_CHECK_EQ(cpu.x(), 0x00);
    TEST_CHECK(cpu.getFlag(Cpu6502::Z));
}

} // namespace

int main() {
    testLoadFlags();
    testAdcOverflowAndCarry();
    testSbcBorrow();
    testZeroPageXWraparound();
    testAbsoluteXPageCrossCycles();
    testBranchCycleCounts();
    testJsrRts();
    testPhaPla();
    testIndirectJmpPageBoundaryBug();
    testCompareAndIncDec();

    if (TEST_MAIN_RETURN() == 0) {
        std::printf("cpu_unit_test: all checks passed\n");
    }
    return TEST_MAIN_RETURN();
}
