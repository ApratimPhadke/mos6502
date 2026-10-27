// RESET, BRK, IRQ, NMI and RTI [REF-3][REF-7][REF-8].
#include "test_support.hpp"

namespace mos6502 {
namespace {

using test::kOrigin;
using test::Machine;

// Vectors: reset -> $0300, NMI -> $0400, IRQ/BRK -> $0500.
void install_vectors(Machine& m) {
  m.poke(kResetVector, {0x00, 0x03});
  m.poke(kNmiVector, {0x00, 0x04});
  m.poke(kIrqVector, {0x00, 0x05});
}

TEST(Reset, LoadsPcFromVectorAndSetsDocumentedState) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.a = 0x55;  // garbage that reset must clear in this model
  m.cpu.reset();
  EXPECT_EQ(m.cpu.regs.pc, 0x0300);
  EXPECT_EQ(m.cpu.regs.sp, 0xFD);
  EXPECT_EQ(m.cpu.regs.p, flag::U | flag::I);
  EXPECT_EQ(m.cpu.regs.a, 0x00);
  EXPECT_EQ(m.cpu.total_cycles(), 7u);
}

TEST(Brk, PushesPcPlusTwoAndStatusWithBreakSetThenJumpsToIrqVector) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U | flag::C;        // I clear, D clear
  EXPECT_EQ(m.run_one({0x00, 0xEA}), 7u);  // BRK + padding byte
  EXPECT_EQ(m.cpu.regs.pc, 0x0500);
  EXPECT_EQ(m.cpu.regs.sp, 0xFC);
  EXPECT_EQ(m.peek(0x01FF), 0x02);  // return address = $0202 (skips padding)
  EXPECT_EQ(m.peek(0x01FE), 0x02);
  EXPECT_EQ(m.peek(0x01FD), flag::U | flag::B | flag::C);
  EXPECT_TRUE(m.flag(flag::I));
  EXPECT_FALSE(m.flag(flag::B));  // B only exists in the pushed copy
}

TEST(Brk, RtiRestoresFlagsAndReturnsWithoutAddingOne) {
  Machine m;
  install_vectors(m);
  m.poke(0x0500, 0x40);  // handler: RTI
  m.cpu.regs.p = flag::U | flag::C | flag::D;
  (void)m.run_one({0x00, 0xEA});
  EXPECT_EQ(m.step(), 6u);  // RTI
  EXPECT_EQ(m.cpu.regs.pc, 0x0202);
  EXPECT_EQ(m.cpu.regs.p, flag::U | flag::C | flag::D);
  EXPECT_EQ(m.cpu.regs.sp, 0xFF);
}

TEST(Brk, RtiForcesUnusedBitOnAndDropsBreak) {
  Machine m;
  m.poke(0x01FD, 0x10);  // P image with only B set
  m.poke(0x01FE, 0x34);  // return address $1234
  m.poke(0x01FF, 0x12);
  m.cpu.regs.sp = 0xFC;
  (void)m.run_one({0x40});  // RTI
  EXPECT_EQ(m.cpu.regs.p, flag::U);
  EXPECT_EQ(m.cpu.regs.pc, 0x1234);
}

TEST(Irq, IsIgnoredWhileTheInterruptDisableFlagIsSet) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U | flag::I;
  m.cpu.set_irq_line(true);
  EXPECT_EQ(m.run_one({0xEA}), 2u);  // the NOP runs normally
  EXPECT_EQ(m.cpu.regs.pc, kOrigin + 1);
}

TEST(Irq, IsTakenWhenEnabledAndPushesStatusWithBreakCLEAR) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U | flag::Z;
  m.poke(kOrigin, 0xEA);
  m.cpu.set_irq_line(true);
  EXPECT_EQ(m.step(), 7u);  // the interrupt entry replaces the next instruction
  EXPECT_EQ(m.cpu.regs.pc, 0x0500);
  EXPECT_EQ(m.peek(0x01FF), 0x02);  // return to the NOP at $0200 (no +2 here)
  EXPECT_EQ(m.peek(0x01FE), 0x00);
  EXPECT_EQ(m.peek(0x01FD), flag::U | flag::Z);  // B = 0 distinguishes IRQ from BRK
  EXPECT_TRUE(m.flag(flag::I));
}

TEST(Irq, IsLevelSensitiveSoItFiresAgainAfterRtiIfStillAsserted) {
  Machine m;
  install_vectors(m);
  m.poke(0x0500, 0x40);  // handler is just RTI
  m.poke(kOrigin, {0xEA, 0xEA});
  m.cpu.regs.p = flag::U;
  m.cpu.set_irq_line(true);
  (void)m.step();  // enter handler
  (void)m.step();  // RTI (restores I = 0)
  EXPECT_EQ(m.cpu.regs.pc, kOrigin);
  EXPECT_EQ(m.step(), 7u);  // line still low: taken again
  m.cpu.set_irq_line(false);
  (void)m.step();           // RTI
  EXPECT_EQ(m.step(), 2u);  // now the NOP finally executes
}

TEST(Nmi, IsEdgeTriggeredAndIgnoresTheInterruptDisableFlag) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U | flag::I;
  m.poke(kOrigin, 0xEA);
  m.cpu.trigger_nmi();
  EXPECT_EQ(m.step(), 7u);
  EXPECT_EQ(m.cpu.regs.pc, 0x0400);
  EXPECT_EQ(m.peek(0x01FD) & flag::B, 0);  // hardware interrupt: B clear
  m.poke(0x0400, 0xEA);
  EXPECT_EQ(m.step(), 2u);  // one trigger = one interrupt, not a stream
}

TEST(Nmi, WinsOverAnIrqThatArrivesAtTheSameTime) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U;
  m.cpu.set_irq_line(true);
  m.cpu.trigger_nmi();
  (void)m.step();
  EXPECT_EQ(m.cpu.regs.pc, 0x0400);
}

TEST(Interrupts, DecimalFlagSurvivesOnNmosParts) {
  Machine m;
  install_vectors(m);
  m.cpu.regs.p = flag::U | flag::D;
  m.cpu.trigger_nmi();
  (void)m.step();
  EXPECT_TRUE(m.flag(flag::D));  // the 65C02 would clear it; the NMOS part does not
}

}  // namespace
}  // namespace mos6502
