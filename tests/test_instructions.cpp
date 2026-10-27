// Per-instruction behaviour: results, flags, cycle counts and the awkward edge
// cases (page crossing, zero-page wrap-around, stack wrap, the JMP bug).
#include <array>

#include "test_support.hpp"

namespace mos6502 {
namespace {

using test::kOrigin;
using test::Machine;

// ============================================================ loads/stores ===
TEST(Load, ImmediateSetsValueAndClearsFlags) {
  Machine m;
  EXPECT_EQ(m.run_one({0xA9, 0x42}), 2u);  // LDA #$42
  EXPECT_EQ(m.cpu.regs.a, 0x42);
  EXPECT_FALSE(m.flag(flag::Z));
  EXPECT_FALSE(m.flag(flag::N));
  EXPECT_EQ(m.cpu.regs.pc, kOrigin + 2);
}

TEST(Load, SetsZeroAndNegativeFlags) {
  Machine m;
  (void)m.run_one({0xA9, 0x00});
  EXPECT_TRUE(m.flag(flag::Z));
  EXPECT_FALSE(m.flag(flag::N));
  (void)m.run_one({0xA9, 0x80});
  EXPECT_FALSE(m.flag(flag::Z));
  EXPECT_TRUE(m.flag(flag::N));
}

TEST(Load, LdxAndLdyImmediate) {
  Machine m;
  EXPECT_EQ(m.run_one({0xA2, 0x7F}), 2u);
  EXPECT_EQ(m.cpu.regs.x, 0x7F);
  EXPECT_EQ(m.run_one({0xA0, 0xFF}), 2u);
  EXPECT_EQ(m.cpu.regs.y, 0xFF);
  EXPECT_TRUE(m.flag(flag::N));
}

TEST(Addressing, ZeroPageIndexedWrapsInsidePageZero) {
  Machine m;
  m.cpu.regs.x = 0xFF;
  m.poke(0x007F, 0x11);                    // ($80 + $FF) & $FF
  m.poke(0x017F, 0x22);                    // what a non-wrapping CPU would read
  EXPECT_EQ(m.run_one({0xB5, 0x80}), 4u);  // LDA $80,X
  EXPECT_EQ(m.cpu.regs.a, 0x11);
}

TEST(Addressing, ZeroPageYUsedByLdxAndStx) {
  Machine m;
  m.cpu.regs.y = 0x10;
  m.poke(0x0030, 0x5A);
  EXPECT_EQ(m.run_one({0xB6, 0x20}), 4u);  // LDX $20,Y
  EXPECT_EQ(m.cpu.regs.x, 0x5A);
  EXPECT_EQ(m.run_one({0x96, 0xF8}), 4u);  // STX $F8,Y -> wraps to $08
  EXPECT_EQ(m.peek(0x0008), 0x5A);
}

TEST(Addressing, AbsoluteIndexedPaysOneCycleOnPageCross) {
  Machine m;
  m.cpu.regs.x = 1;
  m.poke(0x1300, 0x77);
  m.poke(0x1201, 0x66);
  EXPECT_EQ(m.run_one({0xBD, 0xFF, 0x12}), 5u);  // LDA $12FF,X -> $1300, crossed
  EXPECT_EQ(m.cpu.regs.a, 0x77);
  EXPECT_EQ(m.run_one({0xBD, 0x00, 0x12}), 4u);  // LDA $1200,X -> $1201, same page
  EXPECT_EQ(m.cpu.regs.a, 0x66);
}

TEST(Addressing, AbsoluteYAndStoresNeverVary) {
  Machine m;
  m.cpu.regs.y = 1;
  m.cpu.regs.a = 0x3C;
  EXPECT_EQ(m.run_one({0x99, 0xFF, 0x12}), 5u);  // STA $12FF,Y : always 5
  EXPECT_EQ(m.peek(0x1300), 0x3C);
  EXPECT_EQ(m.run_one({0x99, 0x00, 0x12}), 5u);  // same cost without a cross
  EXPECT_EQ(m.run_one({0xB9, 0xFF, 0x12}), 5u);  // LDA $12FF,Y crossed
}

TEST(Addressing, IndexedIndirectUsesAWrappingPointerTable) {
  Machine m;
  m.cpu.regs.x = 1;
  m.poke(0x00FF, 0x34);  // pointer low byte at $FF ...
  m.poke(0x0000, 0x12);  // ... high byte WRAPS to $00, not $0100
  m.poke(0x0100, 0xEE);  // a non-wrapping CPU would use this
  m.poke(0x1234, 0x99);
  EXPECT_EQ(m.run_one({0xA1, 0xFE}), 6u);  // LDA ($FE,X) -> pointer at $FF
  EXPECT_EQ(m.cpu.regs.a, 0x99);
}

TEST(Addressing, IndirectIndexedPointerAlsoWrapsInPageZero) {
  Machine m;
  m.cpu.regs.y = 1;
  m.poke(0x00FF, 0x34);
  m.poke(0x0000, 0x12);
  m.poke(0x0100, 0xEE);
  m.poke(0x1235, 0x88);
  EXPECT_EQ(m.run_one({0xB1, 0xFF}), 5u);  // LDA ($FF),Y
  EXPECT_EQ(m.cpu.regs.a, 0x88);
}

TEST(Addressing, IndirectIndexedPaysOnPageCross) {
  Machine m;
  m.cpu.regs.y = 1;
  m.poke(0x0020, {0xFF, 0x12});  // base $12FF
  m.poke(0x1300, 0x42);
  EXPECT_EQ(m.run_one({0xB1, 0x20}), 6u);
  EXPECT_EQ(m.cpu.regs.a, 0x42);
  EXPECT_EQ(m.run_one({0x91, 0x20}), 6u);  // STA (zp),Y is 6 either way
}

TEST(Store, StoresDoNotChangeFlags) {
  Machine m;
  m.cpu.regs.a = 0x00;
  m.cpu.regs.p = flag::U | flag::N;
  EXPECT_EQ(m.run_one({0x8D, 0x00, 0x30}), 4u);  // STA $3000
  EXPECT_EQ(m.cpu.regs.p, flag::U | flag::N);
  EXPECT_EQ(m.peek(0x3000), 0x00);
}

// ============================================================== transfers ===
TEST(Transfer, TaxTayTxaTyaSetFlags) {
  Machine m;
  m.cpu.regs.a = 0x80;
  EXPECT_EQ(m.run_one({0xAA}), 2u);  // TAX
  EXPECT_EQ(m.cpu.regs.x, 0x80);
  EXPECT_TRUE(m.flag(flag::N));
  m.cpu.regs.a = 0;
  (void)m.run_one({0xA8});  // TAY
  EXPECT_TRUE(m.flag(flag::Z));
  m.cpu.regs.x = 0x11;
  (void)m.run_one({0x8A});  // TXA
  EXPECT_EQ(m.cpu.regs.a, 0x11);
  m.cpu.regs.y = 0x22;
  (void)m.run_one({0x98});  // TYA
  EXPECT_EQ(m.cpu.regs.a, 0x22);
}

TEST(Transfer, TxsDoesNotTouchFlagsButTsxDoes) {
  Machine m;
  m.cpu.regs.x = 0x00;
  m.cpu.regs.p = flag::U;   // Z clear
  (void)m.run_one({0x9A});  // TXS
  EXPECT_EQ(m.cpu.regs.sp, 0x00);
  EXPECT_FALSE(m.flag(flag::Z));  // unchanged although the value was zero
  (void)m.run_one({0xBA});        // TSX
  EXPECT_TRUE(m.flag(flag::Z));
}

// ================================================================== stack ===
TEST(Stack, PhaPlaRoundTripAndCycles) {
  Machine m;
  m.cpu.regs.a = 0xA5;
  EXPECT_EQ(m.run_one({0x48}), 3u);  // PHA
  EXPECT_EQ(m.peek(0x01FF), 0xA5);
  EXPECT_EQ(m.cpu.regs.sp, 0xFE);
  m.cpu.regs.a = 0;
  EXPECT_EQ(m.run_one({0x68}), 4u);  // PLA
  EXPECT_EQ(m.cpu.regs.a, 0xA5);
  EXPECT_EQ(m.cpu.regs.sp, 0xFF);
  EXPECT_TRUE(m.flag(flag::N));
}

TEST(Stack, PhpPushesBreakAndUnusedBitsPlpIgnoresBreak) {
  Machine m;
  m.cpu.regs.p = flag::U | flag::C | flag::Z;
  EXPECT_EQ(m.run_one({0x08}), 3u);  // PHP
  EXPECT_EQ(m.peek(0x01FF), flag::U | flag::B | flag::C | flag::Z);
  EXPECT_FALSE(m.flag(flag::B));  // B never lives in the register itself
  m.poke(0x01FF, 0xFF);           // every bit set
  m.cpu.regs.sp = 0xFE;
  EXPECT_EQ(m.run_one({0x28}), 4u);  // PLP
  EXPECT_EQ(m.cpu.regs.p, 0xEF);     // B dropped, U forced on
}

TEST(Stack, PlpForcesUnusedBitOnEvenWhenTheStackByteHasItClear) {
  Machine m;
  m.poke(0x01FF, 0x00);  // every bit clear, including bits 4 and 5
  m.cpu.regs.sp = 0xFE;
  (void)m.run_one({0x28});  // PLP
  EXPECT_EQ(m.cpu.regs.p, flag::U);
}

TEST(Stack, PointerWrapsInsidePageOne) {
  Machine m;
  m.cpu.regs.sp = 0x00;
  m.cpu.regs.a = 0x5C;
  (void)m.run_one({0x48});  // PHA writes $0100 then SP wraps to $FF
  EXPECT_EQ(m.peek(0x0100), 0x5C);
  EXPECT_EQ(m.cpu.regs.sp, 0xFF);
  m.cpu.regs.a = 0;
  (void)m.run_one({0x68});  // PLA: SP -> $00, read $0100
  EXPECT_EQ(m.cpu.regs.a, 0x5C);
  EXPECT_EQ(m.cpu.regs.sp, 0x00);
}

// ================================================================== logic ===
TEST(Logic, AndOraEorAndTheirFlags) {
  Machine m;
  m.cpu.regs.a = 0xF0;
  (void)m.run_one({0x29, 0x0F});  // AND #$0F
  EXPECT_EQ(m.cpu.regs.a, 0x00);
  EXPECT_TRUE(m.flag(flag::Z));
  (void)m.run_one({0x09, 0x81});  // ORA #$81
  EXPECT_EQ(m.cpu.regs.a, 0x81);
  EXPECT_TRUE(m.flag(flag::N));
  (void)m.run_one({0x49, 0xFF});  // EOR #$FF
  EXPECT_EQ(m.cpu.regs.a, 0x7E);
  EXPECT_FALSE(m.flag(flag::N));
}

TEST(Logic, BitCopiesBits7And6AndTestsAnd) {
  Machine m;
  m.cpu.regs.a = 0x0F;
  m.poke(0x0010, 0xC0);
  EXPECT_EQ(m.run_one({0x24, 0x10}), 3u);  // BIT $10
  EXPECT_TRUE(m.flag(flag::Z));            // 0x0F & 0xC0 == 0
  EXPECT_TRUE(m.flag(flag::N));
  EXPECT_TRUE(m.flag(flag::V));
  EXPECT_EQ(m.cpu.regs.a, 0x0F);  // A is not modified
  m.poke(0x3000, 0x3F);
  EXPECT_EQ(m.run_one({0x2C, 0x00, 0x30}), 4u);  // BIT $3000
  EXPECT_FALSE(m.flag(flag::Z));
  EXPECT_FALSE(m.flag(flag::N));
  EXPECT_FALSE(m.flag(flag::V));
}

// ============================================================ inc / dec ===
TEST(IncDec, RegistersWrapAndSetFlags) {
  Machine m;
  m.cpu.regs.x = 0xFF;
  EXPECT_EQ(m.run_one({0xE8}), 2u);  // INX
  EXPECT_EQ(m.cpu.regs.x, 0x00);
  EXPECT_TRUE(m.flag(flag::Z));
  m.cpu.regs.y = 0x00;
  (void)m.run_one({0x88});  // DEY
  EXPECT_EQ(m.cpu.regs.y, 0xFF);
  EXPECT_TRUE(m.flag(flag::N));
}

TEST(IncDec, MemoryReadModifyWriteCycles) {
  Machine m;
  m.poke(0x0010, 0x7F);
  EXPECT_EQ(m.run_one({0xE6, 0x10}), 5u);  // INC $10
  EXPECT_EQ(m.peek(0x0010), 0x80);
  EXPECT_TRUE(m.flag(flag::N));
  m.cpu.regs.x = 1;
  m.poke(0x1301, 0x01);
  EXPECT_EQ(m.run_one({0xDE, 0x00, 0x13}), 7u);  // DEC $1300,X: 7, no variable penalty
  EXPECT_EQ(m.peek(0x1301), 0x00);
  EXPECT_TRUE(m.flag(flag::Z));
}

// ============================================================ shift/rotate ===
TEST(Shift, AslAccumulatorMovesBit7IntoCarry) {
  Machine m;
  m.cpu.regs.a = 0x81;
  EXPECT_EQ(m.run_one({0x0A}), 2u);
  EXPECT_EQ(m.cpu.regs.a, 0x02);
  EXPECT_TRUE(m.flag(flag::C));
  EXPECT_FALSE(m.flag(flag::N));
}

TEST(Shift, LsrMovesBit0IntoCarryAndClearsN) {
  Machine m;
  m.cpu.regs.a = 0x01;
  m.cpu.regs.p |= flag::N;
  (void)m.run_one({0x4A});
  EXPECT_EQ(m.cpu.regs.a, 0x00);
  EXPECT_TRUE(m.flag(flag::C));
  EXPECT_TRUE(m.flag(flag::Z));
  EXPECT_FALSE(m.flag(flag::N));
}

TEST(Shift, RolAndRorRotateThroughCarry) {
  Machine m;
  m.cpu.regs.a = 0x80;
  m.cpu.regs.p |= flag::C;
  (void)m.run_one({0x2A});  // ROL A
  EXPECT_EQ(m.cpu.regs.a, 0x01);
  EXPECT_TRUE(m.flag(flag::C));
  m.cpu.regs.a = 0x01;
  m.cpu.regs.p |= flag::C;
  (void)m.run_one({0x6A});  // ROR A
  EXPECT_EQ(m.cpu.regs.a, 0x80);
  EXPECT_TRUE(m.flag(flag::C));
  EXPECT_TRUE(m.flag(flag::N));
}

TEST(Shift, MemoryFormsWriteTheResultBack) {
  Machine m;
  m.poke(0x0010, 0x40);
  EXPECT_EQ(m.run_one({0x06, 0x10}), 5u);  // ASL $10
  EXPECT_EQ(m.peek(0x0010), 0x80);
  m.cpu.regs.x = 2;
  m.poke(0x1202, 0x03);
  EXPECT_EQ(m.run_one({0x5E, 0x00, 0x12}), 7u);  // LSR $1200,X
  EXPECT_EQ(m.peek(0x1202), 0x01);
  EXPECT_TRUE(m.flag(flag::C));
}

// ================================================================ compare ===
TEST(Compare, CmpSetsCarryZeroNegativeLikeASubtraction) {
  Machine m;
  m.cpu.regs.a = 0x40;
  (void)m.run_one({0xC9, 0x40});  // equal
  EXPECT_TRUE(m.flag(flag::C));
  EXPECT_TRUE(m.flag(flag::Z));
  EXPECT_FALSE(m.flag(flag::N));
  (void)m.run_one({0xC9, 0x30});  // A > M
  EXPECT_TRUE(m.flag(flag::C));
  EXPECT_FALSE(m.flag(flag::Z));
  (void)m.run_one({0xC9, 0x50});  // A < M: 0x40 - 0x50 = 0xF0
  EXPECT_FALSE(m.flag(flag::C));
  EXPECT_TRUE(m.flag(flag::N));
  EXPECT_EQ(m.cpu.regs.a, 0x40);  // A is untouched
}

TEST(Compare, CpxAndCpyHaveTheirOwnRegisters) {
  Machine m;
  m.cpu.regs.x = 5;
  m.cpu.regs.y = 9;
  (void)m.run_one({0xE0, 0x05});  // CPX #5
  EXPECT_TRUE(m.flag(flag::Z));
  (void)m.run_one({0xC0, 0x0A});  // CPY #10
  EXPECT_FALSE(m.flag(flag::C));
}

// ================================================================== flags ===
TEST(FlagOps, SetAndClearEachFlag) {
  Machine m;
  EXPECT_EQ(m.run_one({0x38}), 2u);  // SEC
  EXPECT_TRUE(m.flag(flag::C));
  (void)m.run_one({0x18});  // CLC
  EXPECT_FALSE(m.flag(flag::C));
  (void)m.run_one({0xF8});  // SED
  EXPECT_TRUE(m.flag(flag::D));
  (void)m.run_one({0xD8});  // CLD
  EXPECT_FALSE(m.flag(flag::D));
  (void)m.run_one({0x58});  // CLI
  EXPECT_FALSE(m.flag(flag::I));
  (void)m.run_one({0x78});  // SEI
  EXPECT_TRUE(m.flag(flag::I));
  m.cpu.regs.p |= flag::V;
  (void)m.run_one({0xB8});  // CLV
  EXPECT_FALSE(m.flag(flag::V));
}

TEST(Misc, NopOnlyAdvancesPcAndTime) {
  Machine m;
  const Cpu::Registers before = m.cpu.regs;
  EXPECT_EQ(m.run_one({0xEA}), 2u);
  EXPECT_EQ(m.cpu.regs.a, before.a);
  EXPECT_EQ(m.cpu.regs.p, before.p);
  EXPECT_EQ(m.cpu.regs.pc, kOrigin + 1);
}

// ================================================================= jumps ===
TEST(Jump, AbsoluteJump) {
  Machine m;
  EXPECT_EQ(m.run_one({0x4C, 0x34, 0x12}), 3u);
  EXPECT_EQ(m.cpu.regs.pc, 0x1234);
}

TEST(Jump, IndirectJump) {
  Machine m;
  m.poke(0x1000, {0x78, 0x56});
  EXPECT_EQ(m.run_one({0x6C, 0x00, 0x10}), 5u);
  EXPECT_EQ(m.cpu.regs.pc, 0x5678);
}

TEST(Jump, IndirectJumpReproducesTheNmosPageBoundaryBug) {
  Machine m;
  m.poke(0x10FF, 0x34);  // low byte
  m.poke(0x1000, 0x12);  // high byte is fetched from $1000, NOT $1100
  m.poke(0x1100, 0x99);
  (void)m.run_one({0x6C, 0xFF, 0x10});
  EXPECT_EQ(m.cpu.regs.pc, 0x1234);
}

TEST(Jump, JsrPushesReturnAddressMinusOneAndRtsComesBack) {
  Machine m;
  m.cpu.regs.pc = 0x1234;
  m.poke(0x1234, {0x20, 0x00, 0x30});  // JSR $3000
  m.poke(0x3000, 0x60);                // RTS
  EXPECT_EQ(m.step(), 6u);
  EXPECT_EQ(m.cpu.regs.pc, 0x3000);
  EXPECT_EQ(m.peek(0x01FF), 0x12);  // high byte of $1236 pushed first
  EXPECT_EQ(m.peek(0x01FE), 0x36);  // $1236 = address of JSR's last byte
  EXPECT_EQ(m.cpu.regs.sp, 0xFD);
  EXPECT_EQ(m.step(), 6u);           // RTS
  EXPECT_EQ(m.cpu.regs.pc, 0x1237);  // the instruction after the JSR
  EXPECT_EQ(m.cpu.regs.sp, 0xFF);
}

// ============================================================== branches ===
TEST(Branch, EightConditionsTakenAndNotTaken) {
  struct Case {
    u8 opcode;
    u8 mask;
    bool taken_when_set;
  };
  constexpr std::array<Case, 8> cases = {{
      {0x10, flag::N, false},  // BPL
      {0x30, flag::N, true},   // BMI
      {0x50, flag::V, false},  // BVC
      {0x70, flag::V, true},   // BVS
      {0x90, flag::C, false},  // BCC
      {0xB0, flag::C, true},   // BCS
      {0xD0, flag::Z, false},  // BNE
      {0xF0, flag::Z, true},   // BEQ
  }};
  for (const Case& c : cases) {
    for (bool flag_set : {false, true}) {
      Machine m;
      m.cpu.regs.p = static_cast<u8>(flag::U | (flag_set ? c.mask : 0));
      const bool taken = (flag_set == c.taken_when_set);
      const unsigned cycles = m.run_one({c.opcode, 0x10});
      EXPECT_EQ(cycles, taken ? 3u : 2u) << "opcode " << int{c.opcode};
      EXPECT_EQ(m.cpu.regs.pc, taken ? kOrigin + 2 + 0x10 : kOrigin + 2)
          << "opcode " << int{c.opcode};
    }
  }
}

TEST(Branch, BackwardOffsetIsSigned) {
  Machine m;
  m.cpu.regs.p = flag::U;                  // Z clear -> BNE taken
  EXPECT_EQ(m.run_one({0xD0, 0xFE}), 3u);  // BNE -2 : jumps to itself
  EXPECT_EQ(m.cpu.regs.pc, kOrigin);
}

TEST(Branch, CrossingAPageCostsOneMoreCycle) {
  Machine m;
  m.cpu.regs.pc = 0x02F0;
  m.poke(0x02F0, {0xD0, 0x20});  // next = $02F2, target = $0312: other page
  EXPECT_EQ(m.step(), 4u);
  EXPECT_EQ(m.cpu.regs.pc, 0x0312);
  m.cpu.regs.pc = 0x0300;
  m.poke(0x0300, {0xD0, 0xFC});  // next = $0302, target = $02FE: other page
  EXPECT_EQ(m.step(), 4u);
  EXPECT_EQ(m.cpu.regs.pc, 0x02FE);
}

TEST(Branch, NotTakenNeverPaysPageCrossing) {
  Machine m;
  m.cpu.regs.pc = 0x02F0;
  m.cpu.regs.p = flag::U | flag::Z;
  m.poke(0x02F0, {0xD0, 0x20});  // BNE with Z set: not taken
  EXPECT_EQ(m.step(), 2u);
}

// ================================================================== jam ===
TEST(Jam, UndefinedOpcodeFreezesTheCpuUntilReset) {
  Machine m;
  m.poke(0xFFFC, {0x00, 0x03});
  EXPECT_EQ(m.run_one({0x02}), 0u);
  EXPECT_TRUE(m.cpu.jammed());
  EXPECT_EQ(m.cpu.jam_opcode(), 0x02);
  EXPECT_EQ(m.cpu.regs.pc, kOrigin);  // PC stays on the offending opcode
  EXPECT_EQ(m.step(), 0u);
  m.cpu.reset();
  EXPECT_FALSE(m.cpu.jammed());
  EXPECT_EQ(m.cpu.regs.pc, 0x0300);
}

TEST(Run, StopsAtTheCycleBudgetAndMayOvershootByOneInstruction) {
  Machine m;
  for (u16 a = kOrigin; a < kOrigin + 20; ++a) m.poke(a, 0xEA);  // NOPs
  EXPECT_EQ(m.cpu.run(10), 10u);                                 // five 2-cycle NOPs
  EXPECT_EQ(m.cpu.run(1), 2u);  // one more instruction: overshoot by 1
  EXPECT_EQ(m.cpu.total_cycles(), 12u);
}

}  // namespace
}  // namespace mos6502
