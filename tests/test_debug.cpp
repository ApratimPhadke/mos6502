#include <string>

#include "mos6502/disasm.hpp"
#include "mos6502/trace.hpp"
#include "test_support.hpp"

namespace mos6502 {
namespace {

using test::kOrigin;
using test::Machine;

std::string text(Machine& m, std::initializer_list<u8> code) {
  m.poke(kOrigin, code);
  return disassemble(m.bus, kOrigin).text;
}

TEST(Disasm, FormatsEveryAddressingMode) {
  Machine m;
  EXPECT_EQ(text(m, {0xE8}), "INX");
  EXPECT_EQ(text(m, {0x0A}), "ASL A");
  EXPECT_EQ(text(m, {0xA9, 0x42}), "LDA #$42");
  EXPECT_EQ(text(m, {0xA5, 0x10}), "LDA $10");
  EXPECT_EQ(text(m, {0xB5, 0x10}), "LDA $10,X");
  EXPECT_EQ(text(m, {0xB6, 0x10}), "LDX $10,Y");
  EXPECT_EQ(text(m, {0xAD, 0x34, 0x12}), "LDA $1234");
  EXPECT_EQ(text(m, {0xBD, 0x34, 0x12}), "LDA $1234,X");
  EXPECT_EQ(text(m, {0xB9, 0x34, 0x12}), "LDA $1234,Y");
  EXPECT_EQ(text(m, {0x6C, 0x34, 0x12}), "JMP ($1234)");
  EXPECT_EQ(text(m, {0xA1, 0x10}), "LDA ($10,X)");
  EXPECT_EQ(text(m, {0xB1, 0x10}), "LDA ($10),Y");
}

TEST(Disasm, BranchShowsTheResolvedTargetAddress) {
  Machine m;
  EXPECT_EQ(text(m, {0xD0, 0x04}), "BNE $0206");  // $0200 + 2 + 4
  EXPECT_EQ(text(m, {0xD0, 0xFE}), "BNE $0200");  // -2: jumps to itself
}

TEST(Disasm, ReportsLengthAndMarksUndefinedOpcodesAsData) {
  Machine m;
  m.poke(kOrigin, {0xAD, 0x00, 0x10});
  EXPECT_EQ(disassemble(m.bus, kOrigin).length, 3);
  EXPECT_EQ(text(m, {0x02}), ".byte $02");
  EXPECT_EQ(disassemble(m.bus, kOrigin).length, 1);
}

TEST(Trace, RingBufferKeepsTheNewestEntriesInOrder) {
  TraceBuffer<4> trace;
  EXPECT_EQ(trace.size(), 0u);
  for (u16 i = 0; i < 6; ++i) {
    TraceEntry e;
    e.cycle = i;
    trace.push(e);
  }
  ASSERT_EQ(trace.size(), 4u);  // capacity reached; two oldest were overwritten
  EXPECT_EQ(trace.at(0).cycle, 2u);
  EXPECT_EQ(trace.at(3).cycle, 5u);
  trace.clear();
  EXPECT_EQ(trace.size(), 0u);
}

TEST(Trace, PartiallyFilledBufferStartsAtTheFirstEntry) {
  TraceBuffer<8> trace;
  TraceEntry e;
  e.cycle = 42;
  trace.push(e);
  ASSERT_EQ(trace.size(), 1u);
  EXPECT_EQ(trace.at(0).cycle, 42u);
}

TEST(Trace, FormatsAClassicLogLine) {
  TraceEntry e;
  e.regs.pc = 0x0400;
  e.regs.a = 0x01;
  e.regs.x = 0x02;
  e.regs.y = 0x03;
  e.regs.p = 0x24;
  e.regs.sp = 0xFD;
  e.bytes = {0xA9, 0x42, 0x00};
  e.cycle = 7;
  EXPECT_EQ(format_trace(e), "0400  A9 42     LDA #$42      A:01 X:02 Y:03 P:24 SP:FD CYC:7");
}

}  // namespace
}  // namespace mos6502
