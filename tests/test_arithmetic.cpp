// ADC / SBC are where emulators go wrong, so they are tested EXHAUSTIVELY:
// every (A, operand, carry) combination, against an oracle written in a
// completely different style (plain signed/unsigned integer maths) from the
// bit tricks used in src/cpu.cpp.
#include <cstdint>

#include "test_support.hpp"

namespace mos6502 {
namespace {

using test::kOrigin;
using test::Machine;

struct Result {
  u8 a;
  u8 p;
};

// Executes ONE ADC #imm (opcode $69) or SBC #imm ($E9) on a reusable machine.
Result run_alu(Machine& m, u8 opcode, u8 a, u8 operand, bool carry, bool decimal) {
  m.cpu.regs.a = a;
  m.cpu.regs.p = static_cast<u8>(flag::U | (carry ? flag::C : 0) | (decimal ? flag::D : 0));
  m.poke(kOrigin, {opcode, operand});
  m.cpu.regs.pc = kOrigin;
  (void)m.step();
  return {m.cpu.regs.a, m.cpu.regs.p};
}

bool has(const Result& r, u8 mask) {
  return (r.p & mask) != 0;
}

// ------------------------------------------------------------------ binary --
TEST(Arithmetic, AdcBinaryExhaustive) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    for (int v = 0; v < 256; ++v) {
      for (int c = 0; c < 2; ++c) {
        const Result r = run_alu(m, 0x69, static_cast<u8>(a), static_cast<u8>(v), c != 0, false);
        const int unsigned_sum = a + v + c;
        const int signed_sum = static_cast<std::int8_t>(a) + static_cast<std::int8_t>(v) + c;
        const u8 expect = static_cast<u8>(unsigned_sum & 0xFF);
        ASSERT_EQ(r.a, expect) << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::C), unsigned_sum > 255) << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::V), signed_sum < -128 || signed_sum > 127)
            << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::Z), expect == 0);
        ASSERT_EQ(has(r, flag::N), (expect & 0x80) != 0);
      }
    }
  }
}

TEST(Arithmetic, SbcBinaryExhaustive) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    for (int v = 0; v < 256; ++v) {
      for (int c = 0; c < 2; ++c) {
        const Result r = run_alu(m, 0xE9, static_cast<u8>(a), static_cast<u8>(v), c != 0, false);
        const int borrow = 1 - c;
        const int unsigned_diff = a - v - borrow;
        const int signed_diff = static_cast<std::int8_t>(a) - static_cast<std::int8_t>(v) - borrow;
        const u8 expect = static_cast<u8>(unsigned_diff & 0xFF);
        ASSERT_EQ(r.a, expect) << a << "-" << v << "-" << borrow;
        ASSERT_EQ(has(r, flag::C), unsigned_diff >= 0) << a << "-" << v << "-" << borrow;
        ASSERT_EQ(has(r, flag::V), signed_diff < -128 || signed_diff > 127)
            << a << "-" << v << "-" << borrow;
        ASSERT_EQ(has(r, flag::Z), expect == 0);
        ASSERT_EQ(has(r, flag::N), (expect & 0x80) != 0);
      }
    }
  }
}

TEST(Arithmetic, WellKnownBinaryVectors) {
  Machine m;
  Result r = run_alu(m, 0x69, 0x50, 0x50, false, false);  // 80 + 80 = 160: signed overflow
  EXPECT_EQ(r.a, 0xA0);
  EXPECT_TRUE(has(r, flag::V));
  EXPECT_TRUE(has(r, flag::N));
  EXPECT_FALSE(has(r, flag::C));
  r = run_alu(m, 0x69, 0xFF, 0x01, false, false);  // wraps to zero, sets carry
  EXPECT_EQ(r.a, 0x00);
  EXPECT_TRUE(has(r, flag::C));
  EXPECT_TRUE(has(r, flag::Z));
  r = run_alu(m, 0xE9, 0x00, 0x01, true, false);  // 0 - 1 borrows
  EXPECT_EQ(r.a, 0xFF);
  EXPECT_FALSE(has(r, flag::C));
}

TEST(Arithmetic, AddingWithCarryChainsMultiByteNumbers) {
  // 16-bit add: $01FF + $0001 = $0200 using CLC / ADC / ADC.
  Machine m;
  m.poke(kOrigin, {0x18,                    // CLC
                   0xA9, 0xFF, 0x69, 0x01,  // LDA #$FF ; ADC #$01
                   0x85, 0x20,              // STA $20 (low result)
                   0xA9, 0x01, 0x69, 0x00,  // LDA #$01 ; ADC #$00 (+ carry)
                   0x85, 0x21});            // STA $21 (high result)
  for (int i = 0; i < 7; ++i) (void)m.step();
  EXPECT_EQ(m.peek(0x20), 0x00);
  EXPECT_EQ(m.peek(0x21), 0x02);
}

// ----------------------------------------------------------------- decimal --
int bcd_value(int v) {
  return (v >> 4) * 10 + (v & 0x0F);
}
u8 to_bcd(int n) {
  return static_cast<u8>(((n / 10) << 4) | (n % 10));
}
bool valid_bcd(int v) {
  return (v >> 4) <= 9 && (v & 0x0F) <= 9;
}

TEST(Decimal, AdcMatchesDecimalIntegerMathForValidBcd) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    if (!valid_bcd(a)) continue;
    for (int v = 0; v < 256; ++v) {
      if (!valid_bcd(v)) continue;
      for (int c = 0; c < 2; ++c) {
        const Result r = run_alu(m, 0x69, static_cast<u8>(a), static_cast<u8>(v), c != 0, true);
        const int sum = bcd_value(a) + bcd_value(v) + c;
        ASSERT_EQ(r.a, to_bcd(sum % 100)) << std::hex << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::C), sum >= 100) << std::hex << a << "+" << v << "+" << c;
      }
    }
  }
}

TEST(Decimal, SbcMatchesDecimalIntegerMathForValidBcd) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    if (!valid_bcd(a)) continue;
    for (int v = 0; v < 256; ++v) {
      if (!valid_bcd(v)) continue;
      for (int c = 0; c < 2; ++c) {
        const Result r = run_alu(m, 0xE9, static_cast<u8>(a), static_cast<u8>(v), c != 0, true);
        int diff = bcd_value(a) - bcd_value(v) - (1 - c);
        const bool no_borrow = diff >= 0;
        if (diff < 0) diff += 100;
        ASSERT_EQ(r.a, to_bcd(diff)) << std::hex << a << "-" << v << "-" << (1 - c);
        ASSERT_EQ(has(r, flag::C), no_borrow) << std::hex << a << "-" << v << "-" << (1 - c);
      }
    }
  }
}

TEST(Decimal, WellKnownVectors) {
  Machine m;
  Result r = run_alu(m, 0x69, 0x99, 0x01, false, true);  // 99 + 1 = 100
  EXPECT_EQ(r.a, 0x00);
  EXPECT_TRUE(has(r, flag::C));
  r = run_alu(m, 0x69, 0x09, 0x01, false, true);
  EXPECT_EQ(r.a, 0x10);
  r = run_alu(m, 0xE9, 0x00, 0x01, true, true);  // 0 - 1 = 99 with borrow
  EXPECT_EQ(r.a, 0x99);
  EXPECT_FALSE(has(r, flag::C));
}

// Bruce Clark's test program [REF-5] predicts the NMOS results for ANY operand
// bytes, valid BCD or not. This is a transcription of its predictor routines
// (A6502 / S6502) in the same nibble-by-nibble style as his assembly, so it is
// a differently-shaped formulation from the one in cpu.cpp.
struct Predicted {
  u8 a;
  bool c, n, v, z;
};

Predicted predict_adc(int n1, int n2, int carry) {
  const int n1l = n1 & 0x0F, n1h = n1 & 0xF0, n2l = n2 & 0x0F, n2h = n2 & 0xF0;
  int al = n1l + n2l + carry;
  int x = 0;
  if (al >= 0x0A) {
    x = 1;
    al = (al + 6) & 0x0F;
  }
  const int acc = al | n1h;
  const int operand = x ? (n2h | 0x0F) : n2h;  // N2H,x : N2H or N2H+$0F
  const int sum = acc + operand + x;           // carry in = x
  const int sum8 = sum & 0xFF;
  Predicted p{};
  p.n = (sum8 & 0x80) != 0;
  p.v = (~(acc ^ operand) & (acc ^ sum8) & 0x80) != 0;
  int result = sum8;
  p.c = false;
  if (sum > 0xFF || sum8 >= 0xA0) {
    result = (sum8 + 0x60) & 0xFF;
    p.c = true;
  }
  p.a = static_cast<u8>(result);
  p.z = ((n1 + n2 + carry) & 0xFF) == 0;  // Z comes from the binary result
  return p;
}

u8 predict_sbc_a(int n1, int n2, int carry) {
  const int n1l = n1 & 0x0F, n1h = n1 & 0xF0, n2h = n2 & 0xF0, n2l = n2 & 0x0F;
  int al = n1l - n2l - (1 - carry);
  int x = 0;
  if (al < 0) {
    x = 1;
    al = (al - 6) & 0x0F;
  }
  const int acc = al | n1h;
  int diff = acc - n2h - (x ? 0x10 : 0);
  if (diff < 0) diff -= 0x60;
  return static_cast<u8>(diff & 0xFF);
}

TEST(Decimal, AdcMatchesClarkPredictorForAllOperandsIncludingInvalidBcd) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    for (int v = 0; v < 256; ++v) {
      for (int c = 0; c < 2; ++c) {
        const Result r = run_alu(m, 0x69, static_cast<u8>(a), static_cast<u8>(v), c != 0, true);
        const Predicted p = predict_adc(a, v, c);
        ASSERT_EQ(r.a, p.a) << std::hex << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::C), p.c) << std::hex << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::N), p.n) << std::hex << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::V), p.v) << std::hex << a << "+" << v << "+" << c;
        ASSERT_EQ(has(r, flag::Z), p.z) << std::hex << a << "+" << v << "+" << c;
      }
    }
  }
}

TEST(Decimal, SbcMatchesClarkPredictorForAllOperandsIncludingInvalidBcd) {
  Machine m;
  for (int a = 0; a < 256; ++a) {
    for (int v = 0; v < 256; ++v) {
      for (int c = 0; c < 2; ++c) {
        const Result d = run_alu(m, 0xE9, static_cast<u8>(a), static_cast<u8>(v), c != 0, true);
        const Result b = run_alu(m, 0xE9, static_cast<u8>(a), static_cast<u8>(v), c != 0, false);
        ASSERT_EQ(d.a, predict_sbc_a(a, v, c)) << std::hex << a << "-" << v << "-" << c;
        // On the NMOS 6502 every SBC flag equals the BINARY subtraction's flag.
        const u8 mask = flag::N | flag::V | flag::Z | flag::C;
        ASSERT_EQ(d.p & mask, b.p & mask) << std::hex << a << "-" << v << "-" << c;
      }
    }
  }
}

TEST(Decimal, CycleCountIsUnchangedByDecimalMode) {
  Machine m;
  m.cpu.regs.p = flag::U | flag::D;
  EXPECT_EQ(m.run_one({0x69, 0x01}), 2u);
  EXPECT_EQ(m.run_one({0xE9, 0x01}), 2u);
}

}  // namespace
}  // namespace mos6502
