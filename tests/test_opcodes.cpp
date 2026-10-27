// The opcode table is DATA, so we test it like data: count it, check its
// invariants, and cross-check it against the bit-pattern structure of the
// 6502 opcode map ("aaabbbcc", see Day 2 of the guide).
#include <gtest/gtest.h>

#include <array>

#include "mos6502/opcodes.hpp"

namespace mos6502 {
namespace {

constexpr unsigned count_legal() {
  unsigned n = 0;
  for (const Instruction& in : kInstructions) n += (in.op != Op::JAM) ? 1u : 0u;
  return n;
}

// Compile-time proof: the NMOS 6502 has exactly 151 documented opcodes [REF-3].
static_assert(count_legal() == 151);
static_assert(kMnemonics.size() == kOpCount);
static_assert(sizeof(Instruction) == 4, "table must stay compact: 256 * 4 = 1 KiB");

TEST(Opcodes, EveryLegalEntryHasSaneCycles) {
  for (unsigned i = 0; i < 256; ++i) {
    const Instruction& in = kInstructions[i];
    if (in.op == Op::JAM) {
      EXPECT_EQ(in.cycles, 0) << "opcode " << i;
    } else {
      EXPECT_GE(in.cycles, 2) << "opcode " << i;
      EXPECT_LE(in.cycles, 7) << "opcode " << i;
    }
  }
}

TEST(Opcodes, PagePenaltyOnlyForIndexedReads) {
  for (unsigned i = 0; i < 256; ++i) {
    const Instruction& in = kInstructions[i];
    if (!in.page_penalty) continue;
    const bool indexed = in.mode == Mode::ABX || in.mode == Mode::ABY || in.mode == Mode::IZY;
    EXPECT_TRUE(indexed) << "opcode " << i;
    // Stores and read-modify-write instructions always pay: no variable penalty.
    switch (in.op) {
      case Op::STA:
      case Op::ASL:
      case Op::LSR:
      case Op::ROL:
      case Op::ROR:
      case Op::INC:
      case Op::DEC: ADD_FAILURE() << "opcode " << i << " must not have a page penalty"; break;
      default: break;
    }
  }
}

TEST(Opcodes, BranchesAreTheEightRelativeOpcodes) {
  // Pattern xxy10000: xx picks the flag (N,V,C,Z), y is the value tested.
  constexpr std::array<Op, 8> expected = {Op::BPL, Op::BMI, Op::BVC, Op::BVS,
                                          Op::BCC, Op::BCS, Op::BNE, Op::BEQ};
  for (unsigned i = 0; i < 256; ++i) {
    const bool is_branch = (i & 0x1F) == 0x10;
    EXPECT_EQ(kInstructions[i].mode == Mode::REL, is_branch) << "opcode " << i;
    if (is_branch) {
      const unsigned index = ((i >> 6) << 1) | ((i >> 5) & 1);
      EXPECT_EQ(kInstructions[i].op, expected[index]) << "opcode " << i;
    }
  }
}

TEST(Opcodes, GroupOneFollowsTheAaaBbbPattern) {
  // Opcodes with cc = 01: aaa picks the operation, bbb the addressing mode.
  constexpr std::array<Op, 8> ops = {Op::ORA, Op::AND, Op::EOR, Op::ADC,
                                     Op::STA, Op::LDA, Op::CMP, Op::SBC};
  constexpr std::array<Mode, 8> modes = {Mode::IZX, Mode::ZP,  Mode::IMM, Mode::ABS,
                                         Mode::IZY, Mode::ZPX, Mode::ABY, Mode::ABX};
  for (unsigned aaa = 0; aaa < 8; ++aaa) {
    for (unsigned bbb = 0; bbb < 8; ++bbb) {
      const unsigned opcode = (aaa << 5) | (bbb << 2) | 0x01;
      const Instruction& in = kInstructions[opcode];
      if (ops[aaa] == Op::STA && modes[bbb] == Mode::IMM) {
        EXPECT_EQ(in.op, Op::JAM) << "STA #imm does not exist";  // opcode $89
        continue;
      }
      EXPECT_EQ(in.op, ops[aaa]) << "opcode " << opcode;
      EXPECT_EQ(in.mode, modes[bbb]) << "opcode " << opcode;
    }
  }
}

TEST(Opcodes, GroupTwoShiftsAndIncDecFollowThePattern) {
  // cc = 10, aaa 0..3 = ASL ROL LSR ROR; aaa 6..7 = DEC INC.
  constexpr std::array<Op, 4> shifts = {Op::ASL, Op::ROL, Op::LSR, Op::ROR};
  for (unsigned aaa = 0; aaa < 4; ++aaa) {
    EXPECT_EQ(kInstructions[(aaa << 5) | 0x06].mode, Mode::ZP);   // bbb = 1
    EXPECT_EQ(kInstructions[(aaa << 5) | 0x0A].mode, Mode::ACC);  // bbb = 2
    EXPECT_EQ(kInstructions[(aaa << 5) | 0x0E].mode, Mode::ABS);  // bbb = 3
    EXPECT_EQ(kInstructions[(aaa << 5) | 0x16].mode, Mode::ZPX);  // bbb = 5
    EXPECT_EQ(kInstructions[(aaa << 5) | 0x1E].mode, Mode::ABX);  // bbb = 7
    for (unsigned low : {0x06u, 0x0Au, 0x0Eu, 0x16u, 0x1Eu}) {
      EXPECT_EQ(kInstructions[(aaa << 5) | low].op, shifts[aaa]);
    }
  }
  for (unsigned aaa : {6u, 7u}) {
    const Op op = (aaa == 6) ? Op::DEC : Op::INC;
    for (unsigned low : {0x06u, 0x0Eu, 0x16u, 0x1Eu}) {
      EXPECT_EQ(kInstructions[(aaa << 5) | low].op, op);
    }
  }
}

TEST(Opcodes, InstructionLengthsMatchTheModes) {
  EXPECT_EQ(instruction_length(Mode::IMP), 1);
  EXPECT_EQ(instruction_length(Mode::ACC), 1);
  EXPECT_EQ(instruction_length(Mode::IMM), 2);
  EXPECT_EQ(instruction_length(Mode::REL), 2);
  EXPECT_EQ(instruction_length(Mode::IZY), 2);
  EXPECT_EQ(instruction_length(Mode::ABS), 3);
  EXPECT_EQ(instruction_length(Mode::IND), 3);
}

TEST(Opcodes, EveryOpHasAUniqueMnemonicExceptTheJamPlaceholder) {
  for (std::size_t i = 0; i < kOpCount; ++i) {
    for (std::size_t j = i + 1; j < kOpCount; ++j) {
      EXPECT_NE(kMnemonics[i], kMnemonics[j]);
    }
  }
}

}  // namespace
}  // namespace mos6502
