// cpu.hpp - the NMOS 6502 processor core.
//
// Design rules for this class (they make it portable to small targets):
//   * no heap allocation, no exceptions, no RTTI, no I/O, no global state;
//   * deterministic: same bus contents + same registers => same result;
//   * cycle COUNT is exact per instruction; individual bus cycles are not
//     modelled (see docs/DESIGN.md, "Non-goals").
#pragma once

#include "mos6502/bus.hpp"
#include "mos6502/flags.hpp"
#include "mos6502/opcodes.hpp"
#include "mos6502/types.hpp"

namespace mos6502 {

namespace detail {
struct HandlerTable;  // builds the opcode -> handler dispatch table (cpu.cpp)
}

class Cpu {
 public:
  // The architectural registers [REF-3]. P never has the B bit set: B only
  // exists in the copy of P that is pushed on the stack [REF-7].
  struct Registers {
    u16 pc = 0;
    u8 sp = 0;
    u8 a = 0;
    u8 x = 0;
    u8 y = 0;
    u8 p = flag::U | flag::I;
  };

  explicit Cpu(Bus& bus) noexcept : bus_(bus) {}

  Registers regs;  // public on purpose: debuggers and tests poke registers

  // Power-on / RESET: load PC from the vector at $FFFC, SP = $FD, I = 1.
  void reset() noexcept;

  // Execute exactly one instruction (or one interrupt entry sequence).
  // Returns the number of clock cycles it took, or 0 if the CPU is jammed.
  [[nodiscard]] unsigned step() noexcept;

  // Keep stepping until at least `cycle_budget` cycles were used or the CPU
  // jams. Returns the cycles actually used (may overshoot by < 8).
  u64 run(u64 cycle_budget) noexcept;

  // Interrupt inputs. IRQ is level-sensitive and maskable by the I flag.
  // NMI is edge-triggered (one call = one edge) and cannot be masked.
  void set_irq_line(bool asserted) noexcept { irq_line_ = asserted; }
  void trigger_nmi() noexcept { nmi_pending_ = true; }

  // An undefined opcode freezes the CPU (like the real KIL opcodes do). PC
  // stays on the offending opcode so a debugger can show it.
  [[nodiscard]] bool jammed() const noexcept { return jammed_; }
  [[nodiscard]] u8 jam_opcode() const noexcept { return jam_opcode_; }

  [[nodiscard]] u64 total_cycles() const noexcept { return cycles_; }

 private:
  // --- memory helpers ----------------------------------------------------
  u8 fetch8() noexcept;
  u16 fetch16() noexcept;
  u16 read16(u16 address) noexcept;
  void push(u8 value) noexcept;
  u8 pull() noexcept;
  void push16(u16 value) noexcept;
  u16 pull16() noexcept;

  // --- flag helpers --------------------------------------------------------
  void set_flag(u8 mask, bool on) noexcept;
  u8 set_nz(u8 value) noexcept;  // sets N and Z from value, returns value

  // --- decode / execute ----------------------------------------------------
  // One specialised handler is generated per opcode at compile time, so the
  // addressing mode, operation, and cycle cost are all constants inside it.
  friend struct detail::HandlerTable;
  template <unsigned Opcode>
  static unsigned handler(Cpu& cpu) noexcept;  // returns cycles; 0 = jammed
  template <Mode M>
  u16 resolve(bool& page_crossed) noexcept;
  template <Op O, Mode M>
  unsigned execute(u16 address) noexcept;  // returns EXTRA cycles
  void service_interrupt(u16 vector, bool software) noexcept;

  // --- ALU -----------------------------------------------------------------
  void adc(u8 operand) noexcept;
  void sbc(u8 operand) noexcept;
  void adc_decimal(u8 operand) noexcept;
  void sbc_decimal(u8 operand) noexcept;
  void compare(u8 reg, u8 operand) noexcept;
  u8 asl(u8 v) noexcept;
  u8 lsr(u8 v) noexcept;
  u8 rol(u8 v) noexcept;
  u8 ror(u8 v) noexcept;
  unsigned branch(bool taken, u16 offset_address) noexcept;

  Bus& bus_;
  u64 cycles_ = 0;
  bool irq_line_ = false;
  bool nmi_pending_ = false;
  bool jammed_ = false;
  u8 jam_opcode_ = 0;
};

}  // namespace mos6502
