// Direct unit tests for the bank-switching arithmetic and register protocols
// of the mapper 007 additions (MMC1, UxROM, CNROM, MMC3), driven straight
// through the Mapper interface - no ROM files needed.

#include <cstdio>

#include "core/mappers/mapper_cnrom.h"
#include "core/mappers/mapper_mmc1.h"
#include "core/mappers/mapper_mmc3.h"
#include "core/mappers/mapper_uxrom.h"
#include "test_util.h"

using namespace nes;

namespace {

void testUxrom() {
    MapperUxrom mapper(4, 0, Mirroring::Horizontal); // 4 x 16KB PRG banks.
    uint32_t mapped = 0;

    // Default bank 0 at $8000; $C000 always the last bank (3).
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 0x0000u);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x4000);

    // Selecting bank 2 only affects $8000-$BFFF; write is fully consumed (no PRG-RAM passthrough).
    TEST_CHECK(!mapper.cpuMapWrite(0x8000, mapped, 2));
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x4000);
    TEST_CHECK(mapper.cpuMapRead(0xBFFF, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x4000 + 0x3FFF);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x4000); // Still fixed to the last bank.
}

void testCnrom() {
    MapperCnrom mapper(1, 4, Mirroring::Vertical); // 16KB PRG (mirrored), 32KB CHR (4 banks).
    uint32_t mapped = 0;

    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 0x0000u);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped)); // 16KB PRG mirrors into the top half too.
    TEST_CHECK_EQ(mapped, 0x0000u);

    TEST_CHECK(!mapper.cpuMapWrite(0x8000, mapped, 3));
    TEST_CHECK(mapper.ppuMapRead(0x0000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x2000);
    TEST_CHECK(mapper.ppuMapRead(0x1FFF, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x2000 + 0x1FFF);
    TEST_CHECK(!mapper.ppuMapWrite(0x0000, mapped)); // CHR ROM: read-only.
}

// Feeds a 5-bit value into MMC1's serial shift register one bit per write
// (LSB first), which is how real software programs it.
void mmc1WriteSerial(MapperMmc1& mapper, uint16_t addr, uint8_t value) {
    uint32_t mapped = 0;
    for (int i = 0; i < 5; i++) {
        mapper.cpuMapWrite(addr, mapped, (value >> i) & 0x01);
    }
}

void testMmc1() {
    MapperMmc1 mapper(8, 0, Mirroring::Horizontal); // 8 x 16KB PRG, CHR RAM.
    uint32_t mapped = 0;

    TEST_CHECK(mapper.mirroring() == Mirroring::SingleScreenLow); // Default control = 0x0C.

    mmc1WriteSerial(mapper, 0x8000, 0x03); // mirroring=3 (horizontal), prg mode=0, chr mode=0.
    TEST_CHECK(mapper.mirroring() == Mirroring::Horizontal);

    // PRG mode 3: switchable at $8000, fixed to the last bank at $C000.
    mmc1WriteSerial(mapper, 0x8000, 0x0F); // prg mode=3 (bits 2-3), mirroring=3.
    mmc1WriteSerial(mapper, 0xE000, 5);    // PRG bank register = 5.
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 5u * 0x4000);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 7u * 0x4000); // prgBanks16k - 1.

    // PRG mode 2: fixed bank 0 at $8000, switchable at $C000.
    mmc1WriteSerial(mapper, 0x8000, 0x08); // prg mode=2, mirroring=0.
    mmc1WriteSerial(mapper, 0xE000, 3);
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 0x0000u);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x4000);

    // PRG mode 0/1: 32KB mode, low bank bit ignored.
    mmc1WriteSerial(mapper, 0x8000, 0x00);
    mmc1WriteSerial(mapper, 0xE000, 4); // bank32 = 4 >> 1 = 2.
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x8000);
    TEST_CHECK(mapper.cpuMapRead(0xFFFF, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x8000 + 0x7FFF);

    // Reset (bit 7 set) forces PRG mode to 3, mid-sequence or not.
    mapper.cpuMapWrite(0x8000, mapped, 1); // Partial write (1 of 5 bits) ...
    mapper.cpuMapWrite(0x8000, mapped, 0x80); // ... then a reset write.
    mmc1WriteSerial(mapper, 0xE000, 0);       // PRG bank register = 0.
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 7u * 0x4000); // Fixed-last-bank behavior of PRG mode 3.

    // CHR banking on a second instance with real CHR ROM.
    MapperMmc1 chrMapper(2, 4, Mirroring::Horizontal); // 32KB CHR = 8 x 4KB units.
    mmc1WriteSerial(chrMapper, 0x8000, 0x10); // chr mode=1 (4KB banks).
    mmc1WriteSerial(chrMapper, 0xA000, 3);    // CHR bank 0 = 3.
    mmc1WriteSerial(chrMapper, 0xC000, 5);    // CHR bank 1 = 5.
    TEST_CHECK(chrMapper.ppuMapRead(0x0000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x1000);
    TEST_CHECK(chrMapper.ppuMapRead(0x1000, mapped));
    TEST_CHECK_EQ(mapped, 5u * 0x1000);
    TEST_CHECK(chrMapper.ppuMapRead(0x1FFF, mapped));
    TEST_CHECK_EQ(mapped, 5u * 0x1000 + 0x0FFF);

    mmc1WriteSerial(chrMapper, 0x8000, 0x00); // chr mode=0 (8KB banks), low bit ignored.
    mmc1WriteSerial(chrMapper, 0xA000, 6);    // bank8 = 6 >> 1 = 3.
    TEST_CHECK(chrMapper.ppuMapRead(0x0000, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x2000);
    TEST_CHECK(chrMapper.ppuMapRead(0x1FFF, mapped));
    TEST_CHECK_EQ(mapped, 3u * 0x2000 + 0x1FFF);
}

void testMmc3PrgAndChr() {
    MapperMmc3 mapper(4, 2, Mirroring::Horizontal); // 4x16KB PRG (8x8KB), 16KB CHR (16x1KB).
    uint32_t mapped = 0;

    TEST_CHECK(mapper.cpuMapRead(0xE000, mapped)); // Always fixed to the last 8KB bank.
    TEST_CHECK_EQ(mapped, 7u * 0x2000);
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped)); // Default mode: fixed to second-last.
    TEST_CHECK_EQ(mapped, 6u * 0x2000);
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped)); // Default mode: switchable via R6 (0).
    TEST_CHECK_EQ(mapped, 0x0000u);

    mapper.cpuMapWrite(0x8000, mapped, 6); // Select R6 register.
    mapper.cpuMapWrite(0x8001, mapped, 2); // R6 = 2.
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x2000);

    mapper.cpuMapWrite(0x8000, mapped, 7); // Select R7.
    mapper.cpuMapWrite(0x8001, mapped, 5); // R7 = 5.
    TEST_CHECK(mapper.cpuMapRead(0xA000, mapped)); // $A000 always switchable via R7.
    TEST_CHECK_EQ(mapped, 5u * 0x2000);

    // Flip PRG mode (bit 6): $8000/$C000 swap roles.
    mapper.cpuMapWrite(0x8000, mapped, 0x46); // Select R6, mode bit set.
    TEST_CHECK(mapper.cpuMapRead(0x8000, mapped));
    TEST_CHECK_EQ(mapped, 6u * 0x2000); // Now fixed to second-last.
    TEST_CHECK(mapper.cpuMapRead(0xC000, mapped));
    TEST_CHECK_EQ(mapped, 2u * 0x2000); // Now switchable via R6 (still 2).

    // CHR: R0/R1 are 2KB (low bit ignored), R2-R5 are 1KB.
    mapper.cpuMapWrite(0x8000, mapped, 0);
    mapper.cpuMapWrite(0x8001, mapped, 4); // R0 = 4 (& 0xFE = 4).
    mapper.cpuMapWrite(0x8000, mapped, 2);
    mapper.cpuMapWrite(0x8001, mapped, 10); // R2 = 10.
    TEST_CHECK(mapper.ppuMapRead(0x0000, mapped));
    TEST_CHECK_EQ(mapped, 4u * 0x400);
    TEST_CHECK(mapper.ppuMapRead(0x07FF, mapped));
    TEST_CHECK_EQ(mapped, 4u * 0x400 + 0x7FF);
    TEST_CHECK(mapper.ppuMapRead(0x1000, mapped));
    TEST_CHECK_EQ(mapped, 10u * 0x400);

    // CHR invert (bit 7): the two halves swap layout.
    mapper.cpuMapWrite(0x8000, mapped, 0x82); // Select R2, invert bit set.
    TEST_CHECK(mapper.ppuMapRead(0x0000, mapped)); // Now uses R2 (was R0's territory).
    TEST_CHECK_EQ(mapped, 10u * 0x400);
    TEST_CHECK(mapper.ppuMapRead(0x1000, mapped)); // Now uses R0 (was R2's territory).
    TEST_CHECK_EQ(mapped, 4u * 0x400);

    // Mirroring register ($A000 even).
    TEST_CHECK(mapper.mirroring() == Mirroring::Vertical); // Default.
    mapper.cpuMapWrite(0xA000, mapped, 1);
    TEST_CHECK(mapper.mirroring() == Mirroring::Horizontal);
    mapper.cpuMapWrite(0xA000, mapped, 0);
    TEST_CHECK(mapper.mirroring() == Mirroring::Vertical);
}

void testMmc3Irq() {
    MapperMmc3 mapper(4, 2, Mirroring::Horizontal);
    uint32_t mapped = 0;

    mapper.cpuMapWrite(0xC000, mapped, 4); // IRQ latch = 4.
    mapper.cpuMapWrite(0xC001, mapped, 0); // Request a reload on the next clock.
    mapper.cpuMapWrite(0xE001, mapped, 0); // Enable IRQs.

    TEST_CHECK(!mapper.irqPending());
    mapper.scanlineTick(); // Reloads counter to 4 (no decrement this tick).
    TEST_CHECK(!mapper.irqPending());
    mapper.scanlineTick(); // 4 -> 3
    mapper.scanlineTick(); // 3 -> 2
    mapper.scanlineTick(); // 2 -> 1
    TEST_CHECK(!mapper.irqPending());
    mapper.scanlineTick(); // 1 -> 0: fires.
    TEST_CHECK(mapper.irqPending());

    // $E000 (even) disables AND acknowledges.
    mapper.cpuMapWrite(0xE000, mapped, 0);
    TEST_CHECK(!mapper.irqPending());
    mapper.scanlineTick(); // Reloads to 4 again, but IRQs are disabled now.
    TEST_CHECK(!mapper.irqPending());

    // Latch = 0 fires immediately on reload.
    MapperMmc3 zeroLatch(4, 2, Mirroring::Horizontal);
    zeroLatch.cpuMapWrite(0xC000, mapped, 0);
    zeroLatch.cpuMapWrite(0xC001, mapped, 0);
    zeroLatch.cpuMapWrite(0xE001, mapped, 0);
    zeroLatch.scanlineTick();
    TEST_CHECK(zeroLatch.irqPending());
}

} // namespace

int main() {
    testUxrom();
    testCnrom();
    testMmc1();
    testMmc3PrgAndChr();
    testMmc3Irq();

    if (TEST_MAIN_RETURN() == 0) {
        std::printf("mapper_unit_test: all checks passed\n");
    }
    return TEST_MAIN_RETURN();
}
