// disasm.hpp - turn machine code back into assembly text.
//
// The disassembler is driven by the SAME kInstructions table as the CPU, so the
// two can never disagree about instruction lengths or addressing modes.
#pragma once

#include <string>

#include "mos6502/bus.hpp"
#include "mos6502/trace.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {

struct Disassembly {
  std::string text;  // e.g. "LDA ($10),Y"
  u8 length = 1;     // bytes consumed (1..3)
};

// Decode from three already-fetched bytes (used by the trace formatter).
[[nodiscard]] Disassembly disassemble_bytes(u16 address, u8 b0, u8 b1, u8 b2);

// Decode from the bus using peek(), which never triggers device side effects.
[[nodiscard]] Disassembly disassemble(const Bus& bus, u16 address);

// One log line, e.g.
//   "0400  A9 42     LDA #$42        A:00 X:00 Y:00 P:24 SP:FD CYC:7"
[[nodiscard]] std::string format_trace(const TraceEntry& entry);

}  // namespace mos6502
