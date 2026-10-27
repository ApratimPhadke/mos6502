// opcodes.hpp - the instruction set as DATA.
//
// A CPU decodes an opcode byte into (operation, addressing mode, cycle cost).
// We store exactly that in a 256-entry table that is built at COMPILE time.
// Decoding is then one array lookup: O(1), no branching, and the table is
// auditable line-by-line against the published references.
//
// Source: [REF-3] for opcode values, modes, byte counts and cycle counts. The
// table is verified by tests/test_opcodes.cpp, by the Klaus Dormann functional
// test [REF-6] and by differential testing against another emulator [REF-9].
#pragma once

#include <array>
#include <cstddef>
#include <string_view>

#include "mos6502/types.hpp"

namespace mos6502 {

// The 56 documented NMOS 6502 operations, plus JAM for every opcode we do not
// implement (undocumented opcodes; on real silicon some of them freeze the CPU).
enum class Op : u8 {
  ADC,
  AND,
  ASL,
  BCC,
  BCS,
  BEQ,
  BIT,
  BMI,
  BNE,
  BPL,
  BRK,
  BVC,
  BVS,
  CLC,
  CLD,
  CLI,
  CLV,
  CMP,
  CPX,
  CPY,
  DEC,
  DEX,
  DEY,
  EOR,
  INC,
  INX,
  INY,
  JMP,
  JSR,
  LDA,
  LDX,
  LDY,
  LSR,
  NOP,
  ORA,
  PHA,
  PHP,
  PLA,
  PLP,
  ROL,
  ROR,
  RTI,
  RTS,
  SBC,
  SEC,
  SED,
  SEI,
  STA,
  STX,
  STY,
  TAX,
  TAY,
  TSX,
  TXA,
  TXS,
  TYA,
  JAM,
};

inline constexpr std::size_t kOpCount = static_cast<std::size_t>(Op::JAM) + 1;

inline constexpr std::array<std::string_view, kOpCount> kMnemonics = {
    "ADC", "AND", "ASL", "BCC", "BCS", "BEQ", "BIT", "BMI", "BNE", "BPL", "BRK", "BVC",
    "BVS", "CLC", "CLD", "CLI", "CLV", "CMP", "CPX", "CPY", "DEC", "DEX", "DEY", "EOR",
    "INC", "INX", "INY", "JMP", "JSR", "LDA", "LDX", "LDY", "LSR", "NOP", "ORA", "PHA",
    "PHP", "PLA", "PLP", "ROL", "ROR", "RTI", "RTS", "SBC", "SEC", "SED", "SEI", "STA",
    "STX", "STY", "TAX", "TAY", "TSX", "TXA", "TXS", "TYA", "???",
};

// How the operand bytes that follow the opcode are turned into an address.
enum class Mode : u8 {
  IMP,  // implied:            INX
  ACC,  // accumulator:        ASL A
  IMM,  // immediate:          LDA #$10
  ZP,   // zero page:          LDA $10
  ZPX,  // zero page,X:        LDA $10,X     (wraps inside page 0)
  ZPY,  // zero page,Y:        LDX $10,Y     (wraps inside page 0)
  ABS,  // absolute:           LDA $1234
  ABX,  // absolute,X:         LDA $1234,X
  ABY,  // absolute,Y:         LDA $1234,Y
  IND,  // indirect:           JMP ($1234)   (only JMP uses it)
  IZX,  // (indirect,X):       LDA ($10,X)
  IZY,  // (indirect),Y:       LDA ($10),Y
  REL,  // relative:           BNE label     (signed 8-bit offset)
};

// Total instruction length in bytes (opcode + operand bytes).
constexpr u8 instruction_length(Mode mode) noexcept {
  switch (mode) {
    case Mode::IMP:
    case Mode::ACC: return 1;
    case Mode::ABS:
    case Mode::ABX:
    case Mode::ABY:
    case Mode::IND: return 3;
    default: return 2;
  }
}

struct Instruction {
  Op op;
  Mode mode;
  u8 cycles;          // base cost, in clock cycles
  bool page_penalty;  // +1 cycle if indexing crosses a page boundary
};

namespace detail {

constexpr std::array<Instruction, 256> make_instruction_table() {
  std::array<Instruction, 256> t{};
  t.fill(Instruction{.op = Op::JAM, .mode = Mode::IMP, .cycles = 0, .page_penalty = false});

  auto def = [&t](u8 opcode, Op op, Mode mode, u8 cycles, bool penalty = false) {
    t[opcode] = Instruction{.op = op, .mode = mode, .cycles = cycles, .page_penalty = penalty};
  };

  // clang-format off
  // --- ADC ---------------------------------------------------------------
  def(0x69, Op::ADC, Mode::IMM, 2);        def(0x65, Op::ADC, Mode::ZP,  3);
  def(0x75, Op::ADC, Mode::ZPX, 4);        def(0x6D, Op::ADC, Mode::ABS, 4);
  def(0x7D, Op::ADC, Mode::ABX, 4, true);  def(0x79, Op::ADC, Mode::ABY, 4, true);
  def(0x61, Op::ADC, Mode::IZX, 6);        def(0x71, Op::ADC, Mode::IZY, 5, true);
  // --- AND ---------------------------------------------------------------
  def(0x29, Op::AND, Mode::IMM, 2);        def(0x25, Op::AND, Mode::ZP,  3);
  def(0x35, Op::AND, Mode::ZPX, 4);        def(0x2D, Op::AND, Mode::ABS, 4);
  def(0x3D, Op::AND, Mode::ABX, 4, true);  def(0x39, Op::AND, Mode::ABY, 4, true);
  def(0x21, Op::AND, Mode::IZX, 6);        def(0x31, Op::AND, Mode::IZY, 5, true);
  // --- ASL ---------------------------------------------------------------
  def(0x0A, Op::ASL, Mode::ACC, 2);        def(0x06, Op::ASL, Mode::ZP,  5);
  def(0x16, Op::ASL, Mode::ZPX, 6);        def(0x0E, Op::ASL, Mode::ABS, 6);
  def(0x1E, Op::ASL, Mode::ABX, 7);
  // --- Branches (2 cycles, +1 if taken, +1 more if the target is on another page)
  def(0x90, Op::BCC, Mode::REL, 2);        def(0xB0, Op::BCS, Mode::REL, 2);
  def(0xF0, Op::BEQ, Mode::REL, 2);        def(0x30, Op::BMI, Mode::REL, 2);
  def(0xD0, Op::BNE, Mode::REL, 2);        def(0x10, Op::BPL, Mode::REL, 2);
  def(0x50, Op::BVC, Mode::REL, 2);        def(0x70, Op::BVS, Mode::REL, 2);
  // --- BIT / BRK ---------------------------------------------------------
  def(0x24, Op::BIT, Mode::ZP,  3);        def(0x2C, Op::BIT, Mode::ABS, 4);
  def(0x00, Op::BRK, Mode::IMP, 7);
  // --- Flag clear / set --------------------------------------------------
  def(0x18, Op::CLC, Mode::IMP, 2);        def(0xD8, Op::CLD, Mode::IMP, 2);
  def(0x58, Op::CLI, Mode::IMP, 2);        def(0xB8, Op::CLV, Mode::IMP, 2);
  def(0x38, Op::SEC, Mode::IMP, 2);        def(0xF8, Op::SED, Mode::IMP, 2);
  def(0x78, Op::SEI, Mode::IMP, 2);
  // --- CMP ---------------------------------------------------------------
  def(0xC9, Op::CMP, Mode::IMM, 2);        def(0xC5, Op::CMP, Mode::ZP,  3);
  def(0xD5, Op::CMP, Mode::ZPX, 4);        def(0xCD, Op::CMP, Mode::ABS, 4);
  def(0xDD, Op::CMP, Mode::ABX, 4, true);  def(0xD9, Op::CMP, Mode::ABY, 4, true);
  def(0xC1, Op::CMP, Mode::IZX, 6);        def(0xD1, Op::CMP, Mode::IZY, 5, true);
  // --- CPX / CPY ---------------------------------------------------------
  def(0xE0, Op::CPX, Mode::IMM, 2);        def(0xE4, Op::CPX, Mode::ZP,  3);
  def(0xEC, Op::CPX, Mode::ABS, 4);
  def(0xC0, Op::CPY, Mode::IMM, 2);        def(0xC4, Op::CPY, Mode::ZP,  3);
  def(0xCC, Op::CPY, Mode::ABS, 4);
  // --- DEC / DEX / DEY ---------------------------------------------------
  def(0xC6, Op::DEC, Mode::ZP,  5);        def(0xD6, Op::DEC, Mode::ZPX, 6);
  def(0xCE, Op::DEC, Mode::ABS, 6);        def(0xDE, Op::DEC, Mode::ABX, 7);
  def(0xCA, Op::DEX, Mode::IMP, 2);        def(0x88, Op::DEY, Mode::IMP, 2);
  // --- EOR ---------------------------------------------------------------
  def(0x49, Op::EOR, Mode::IMM, 2);        def(0x45, Op::EOR, Mode::ZP,  3);
  def(0x55, Op::EOR, Mode::ZPX, 4);        def(0x4D, Op::EOR, Mode::ABS, 4);
  def(0x5D, Op::EOR, Mode::ABX, 4, true);  def(0x59, Op::EOR, Mode::ABY, 4, true);
  def(0x41, Op::EOR, Mode::IZX, 6);        def(0x51, Op::EOR, Mode::IZY, 5, true);
  // --- INC / INX / INY ---------------------------------------------------
  def(0xE6, Op::INC, Mode::ZP,  5);        def(0xF6, Op::INC, Mode::ZPX, 6);
  def(0xEE, Op::INC, Mode::ABS, 6);        def(0xFE, Op::INC, Mode::ABX, 7);
  def(0xE8, Op::INX, Mode::IMP, 2);        def(0xC8, Op::INY, Mode::IMP, 2);
  // --- JMP / JSR ---------------------------------------------------------
  def(0x4C, Op::JMP, Mode::ABS, 3);        def(0x6C, Op::JMP, Mode::IND, 5);
  def(0x20, Op::JSR, Mode::ABS, 6);
  // --- LDA ---------------------------------------------------------------
  def(0xA9, Op::LDA, Mode::IMM, 2);        def(0xA5, Op::LDA, Mode::ZP,  3);
  def(0xB5, Op::LDA, Mode::ZPX, 4);        def(0xAD, Op::LDA, Mode::ABS, 4);
  def(0xBD, Op::LDA, Mode::ABX, 4, true);  def(0xB9, Op::LDA, Mode::ABY, 4, true);
  def(0xA1, Op::LDA, Mode::IZX, 6);        def(0xB1, Op::LDA, Mode::IZY, 5, true);
  // --- LDX ---------------------------------------------------------------
  def(0xA2, Op::LDX, Mode::IMM, 2);        def(0xA6, Op::LDX, Mode::ZP,  3);
  def(0xB6, Op::LDX, Mode::ZPY, 4);        def(0xAE, Op::LDX, Mode::ABS, 4);
  def(0xBE, Op::LDX, Mode::ABY, 4, true);
  // --- LDY ---------------------------------------------------------------
  def(0xA0, Op::LDY, Mode::IMM, 2);        def(0xA4, Op::LDY, Mode::ZP,  3);
  def(0xB4, Op::LDY, Mode::ZPX, 4);        def(0xAC, Op::LDY, Mode::ABS, 4);
  def(0xBC, Op::LDY, Mode::ABX, 4, true);
  // --- LSR ---------------------------------------------------------------
  def(0x4A, Op::LSR, Mode::ACC, 2);        def(0x46, Op::LSR, Mode::ZP,  5);
  def(0x56, Op::LSR, Mode::ZPX, 6);        def(0x4E, Op::LSR, Mode::ABS, 6);
  def(0x5E, Op::LSR, Mode::ABX, 7);
  // --- NOP ---------------------------------------------------------------
  def(0xEA, Op::NOP, Mode::IMP, 2);
  // --- ORA ---------------------------------------------------------------
  def(0x09, Op::ORA, Mode::IMM, 2);        def(0x05, Op::ORA, Mode::ZP,  3);
  def(0x15, Op::ORA, Mode::ZPX, 4);        def(0x0D, Op::ORA, Mode::ABS, 4);
  def(0x1D, Op::ORA, Mode::ABX, 4, true);  def(0x19, Op::ORA, Mode::ABY, 4, true);
  def(0x01, Op::ORA, Mode::IZX, 6);        def(0x11, Op::ORA, Mode::IZY, 5, true);
  // --- Stack -------------------------------------------------------------
  def(0x48, Op::PHA, Mode::IMP, 3);        def(0x08, Op::PHP, Mode::IMP, 3);
  def(0x68, Op::PLA, Mode::IMP, 4);        def(0x28, Op::PLP, Mode::IMP, 4);
  // --- ROL / ROR ---------------------------------------------------------
  def(0x2A, Op::ROL, Mode::ACC, 2);        def(0x26, Op::ROL, Mode::ZP,  5);
  def(0x36, Op::ROL, Mode::ZPX, 6);        def(0x2E, Op::ROL, Mode::ABS, 6);
  def(0x3E, Op::ROL, Mode::ABX, 7);
  def(0x6A, Op::ROR, Mode::ACC, 2);        def(0x66, Op::ROR, Mode::ZP,  5);
  def(0x76, Op::ROR, Mode::ZPX, 6);        def(0x6E, Op::ROR, Mode::ABS, 6);
  def(0x7E, Op::ROR, Mode::ABX, 7);
  // --- RTI / RTS ---------------------------------------------------------
  def(0x40, Op::RTI, Mode::IMP, 6);        def(0x60, Op::RTS, Mode::IMP, 6);
  // --- SBC ---------------------------------------------------------------
  def(0xE9, Op::SBC, Mode::IMM, 2);        def(0xE5, Op::SBC, Mode::ZP,  3);
  def(0xF5, Op::SBC, Mode::ZPX, 4);        def(0xED, Op::SBC, Mode::ABS, 4);
  def(0xFD, Op::SBC, Mode::ABX, 4, true);  def(0xF9, Op::SBC, Mode::ABY, 4, true);
  def(0xE1, Op::SBC, Mode::IZX, 6);        def(0xF1, Op::SBC, Mode::IZY, 5, true);
  // --- STA (stores never get the page-cross penalty: indexed ones always pay it)
  def(0x85, Op::STA, Mode::ZP,  3);        def(0x95, Op::STA, Mode::ZPX, 4);
  def(0x8D, Op::STA, Mode::ABS, 4);        def(0x9D, Op::STA, Mode::ABX, 5);
  def(0x99, Op::STA, Mode::ABY, 5);        def(0x81, Op::STA, Mode::IZX, 6);
  def(0x91, Op::STA, Mode::IZY, 6);
  // --- STX / STY ---------------------------------------------------------
  def(0x86, Op::STX, Mode::ZP,  3);        def(0x96, Op::STX, Mode::ZPY, 4);
  def(0x8E, Op::STX, Mode::ABS, 4);
  def(0x84, Op::STY, Mode::ZP,  3);        def(0x94, Op::STY, Mode::ZPX, 4);
  def(0x8C, Op::STY, Mode::ABS, 4);
  // --- Transfers ---------------------------------------------------------
  def(0xAA, Op::TAX, Mode::IMP, 2);        def(0xA8, Op::TAY, Mode::IMP, 2);
  def(0xBA, Op::TSX, Mode::IMP, 2);        def(0x8A, Op::TXA, Mode::IMP, 2);
  def(0x9A, Op::TXS, Mode::IMP, 2);        def(0x98, Op::TYA, Mode::IMP, 2);
  // clang-format on

  return t;
}

}  // namespace detail

inline constexpr std::array<Instruction, 256> kInstructions = detail::make_instruction_table();

}  // namespace mos6502
