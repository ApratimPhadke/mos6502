#include "mos6502/disasm.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string_view>

#include "mos6502/opcodes.hpp"

namespace mos6502 {

Disassembly disassemble_bytes(u16 address, u8 b0, u8 b1, u8 b2) {
  const Instruction& in = kInstructions[b0];
  Disassembly out;

  if (in.op == Op::JAM) {
    std::array<char, 16> buf{};
    std::snprintf(buf.data(), buf.size(), ".byte $%02X", static_cast<unsigned>(b0));
    out.text = buf.data();
    return out;
  }

  out.length = instruction_length(in.mode);
  const unsigned lo = b1;
  const unsigned word = make_word(b1, b2);
  const std::string_view name = kMnemonics[static_cast<std::size_t>(in.op)];

  std::array<char, 24> operand{};
  switch (in.mode) {
    case Mode::IMP: break;
    case Mode::ACC: std::snprintf(operand.data(), operand.size(), " A"); break;
    case Mode::IMM: std::snprintf(operand.data(), operand.size(), " #$%02X", lo); break;
    case Mode::ZP: std::snprintf(operand.data(), operand.size(), " $%02X", lo); break;
    case Mode::ZPX: std::snprintf(operand.data(), operand.size(), " $%02X,X", lo); break;
    case Mode::ZPY: std::snprintf(operand.data(), operand.size(), " $%02X,Y", lo); break;
    case Mode::ABS: std::snprintf(operand.data(), operand.size(), " $%04X", word); break;
    case Mode::ABX: std::snprintf(operand.data(), operand.size(), " $%04X,X", word); break;
    case Mode::ABY: std::snprintf(operand.data(), operand.size(), " $%04X,Y", word); break;
    case Mode::IND: std::snprintf(operand.data(), operand.size(), " ($%04X)", word); break;
    case Mode::IZX: std::snprintf(operand.data(), operand.size(), " ($%02X,X)", lo); break;
    case Mode::IZY: std::snprintf(operand.data(), operand.size(), " ($%02X),Y", lo); break;
    case Mode::REL: {
      // Branch offset is relative to the address of the NEXT instruction.
      const auto offset = static_cast<std::int8_t>(b1);
      const auto target = static_cast<u16>(address + 2 + offset);
      std::snprintf(operand.data(), operand.size(), " $%04X", static_cast<unsigned>(target));
      break;
    }
  }
  out.text = std::string(name) + operand.data();
  return out;
}

Disassembly disassemble(const Bus& bus, u16 address) {
  return disassemble_bytes(address, bus.peek(address), bus.peek(static_cast<u16>(address + 1)),
                           bus.peek(static_cast<u16>(address + 2)));
}

std::string format_trace(const TraceEntry& e) {
  const Disassembly d = disassemble_bytes(e.regs.pc, e.bytes[0], e.bytes[1], e.bytes[2]);
  static constexpr std::string_view kDigits = "0123456789ABCDEF";
  std::string hex;
  for (u8 i = 0; i < d.length; ++i) {
    hex += kDigits[e.bytes[i] >> 4];
    hex += kDigits[e.bytes[i] & 0x0F];
    hex += ' ';
  }
  std::array<char, 128> line{};
  std::snprintf(line.data(), line.size(),
                "%04X  %-9s %-13s A:%02X X:%02X Y:%02X P:%02X SP:%02X CYC:%llu",
                static_cast<unsigned>(e.regs.pc), hex.c_str(), d.text.c_str(),
                static_cast<unsigned>(e.regs.a), static_cast<unsigned>(e.regs.x),
                static_cast<unsigned>(e.regs.y), static_cast<unsigned>(e.regs.p),
                static_cast<unsigned>(e.regs.sp), static_cast<unsigned long long>(e.cycle));
  return line.data();
}

}  // namespace mos6502
