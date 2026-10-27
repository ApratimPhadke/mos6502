#include "mos6502/cpu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace mos6502 {

// ===========================================================================
// Memory helpers
// ===========================================================================

u8 Cpu::fetch8() noexcept {
  return bus_.read(regs.pc++);
}

u16 Cpu::fetch16() noexcept {
  const u8 lo = fetch8();  // little-endian: low byte first
  const u8 hi = fetch8();
  return make_word(lo, hi);
}

u16 Cpu::read16(u16 address) noexcept {
  const u8 lo = bus_.read(address);
  const u8 hi = bus_.read(static_cast<u16>(address + 1));
  return make_word(lo, hi);
}

// The stack lives in page 1 and grows DOWNWARD. SP points at the next free
// slot, so push = write then decrement; pull = increment then read. SP is a
// u8, so it wraps inside page 1 exactly like the hardware register.
void Cpu::push(u8 value) noexcept {
  bus_.write(static_cast<u16>(kStackBase | regs.sp), value);
  --regs.sp;
}

u8 Cpu::pull() noexcept {
  ++regs.sp;
  return bus_.read(static_cast<u16>(kStackBase | regs.sp));
}

void Cpu::push16(u16 value) noexcept {
  push(hi8(value));  // high byte first, so the low byte ends up at the
  push(lo8(value));  // lower address (little-endian once pulled back)
}

u16 Cpu::pull16() noexcept {
  const u8 lo = pull();
  const u8 hi = pull();
  return make_word(lo, hi);
}

// ===========================================================================
// Flag helpers
// ===========================================================================

void Cpu::set_flag(u8 mask, bool on) noexcept {
  regs.p = on ? lo8(regs.p | mask) : lo8(regs.p & ~mask);
}

// N is simply a copy of bit 7 of the result, so no branch is needed for it.
u8 Cpu::set_nz(u8 value) noexcept {
  regs.p = lo8((regs.p & ~(flag::N | flag::Z)) | (value & flag::N) | (value == 0 ? flag::Z : 0));
  return value;
}

// ===========================================================================
// Reset and interrupts
// ===========================================================================

void Cpu::reset() noexcept {
  regs = Registers{};
  regs.sp = 0xFD;  // [REF-8] reset performs three suppressed pushes: $00 - 3
  regs.pc = read16(kResetVector);
  nmi_pending_ = false;
  jammed_ = false;
  jam_opcode_ = 0;
  cycles_ += 7;
}

// Shared by IRQ, NMI and BRK [REF-3]. The pushed copy of P gets bit 5 = 1 and
// bit 4 (B) = 1 only for BRK [REF-7]. D is NOT cleared on NMOS parts.
void Cpu::service_interrupt(u16 vector, bool software) noexcept {
  push16(regs.pc);
  push(lo8(regs.p | flag::U | (software ? flag::B : 0)));
  set_flag(flag::I, true);
  regs.pc = read16(vector);
}

// ===========================================================================
// Addressing modes: turn the operand bytes into an effective address.
// ===========================================================================

template <Mode M>
u16 Cpu::resolve(bool& page_crossed) noexcept {
  switch (M) {
    case Mode::IMP:
    case Mode::ACC: return 0;  // no operand address
    case Mode::IMM:
    case Mode::REL: return regs.pc++;  // the operand IS the next byte of the program
    case Mode::ZP: return fetch8();
    case Mode::ZPX: return lo8(fetch8() + regs.x);  // wraps inside page 0
    case Mode::ZPY: return lo8(fetch8() + regs.y);
    case Mode::ABS: return fetch16();
    case Mode::ABX: {
      const u16 base = fetch16();
      const u16 address = static_cast<u16>(base + regs.x);
      page_crossed = crosses_page(base, address);
      return address;
    }
    case Mode::ABY: {
      const u16 base = fetch16();
      const u16 address = static_cast<u16>(base + regs.y);
      page_crossed = crosses_page(base, address);
      return address;
    }
    case Mode::IND: {
      // Famous NMOS bug [REF-3]: the pointer's high byte is never incremented,
      // so a vector at $xxFF takes its high byte from $xx00, not $(xx+1)00.
      const u16 pointer = fetch16();
      const u8 lo = bus_.read(pointer);
      const u8 hi = bus_.read(make_word(lo8(pointer + 1), hi8(pointer)));
      return make_word(lo, hi);
    }
    case Mode::IZX: {
      const u8 zp = lo8(fetch8() + regs.x);  // pointer table wraps in page 0
      const u8 lo = bus_.read(zp);
      const u8 hi = bus_.read(lo8(zp + 1));
      return make_word(lo, hi);
    }
    case Mode::IZY: {
      const u8 zp = fetch8();
      const u8 lo = bus_.read(zp);
      const u8 hi = bus_.read(lo8(zp + 1));  // wraps in page 0 as well
      const u16 base = make_word(lo, hi);
      const u16 address = static_cast<u16>(base + regs.y);
      page_crossed = crosses_page(base, address);
      return address;
    }
  }
  return 0;  // unreachable: every enumerator is handled above
}

// ===========================================================================
// ALU
// ===========================================================================

// Binary add with carry. Overflow (V) means "the signed result is wrong": it
// happens exactly when both inputs have the same sign and the result's sign
// differs. In bit terms: ~(a ^ m) & (a ^ sum) & 0x80.
void Cpu::adc(u8 operand) noexcept {
  if ((regs.p & flag::D) != 0) {
    adc_decimal(operand);
    return;
  }
  const int a = regs.a;
  const int m = operand;
  const int sum = a + m + (regs.p & flag::C);
  set_flag(flag::C, sum > 0xFF);
  set_flag(flag::V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);
  regs.a = set_nz(lo8(sum));
}

// Subtract with carry (carry = NOT borrow). In two's complement,
// a - m - (1 - c) == a + ~m + c, so SBC is ADC of the complemented operand.
void Cpu::sbc(u8 operand) noexcept {
  if ((regs.p & flag::D) != 0) {
    sbc_decimal(operand);
    return;
  }
  const int a = regs.a;
  const int m = operand;
  const int sum = a + (m ^ 0xFF) + (regs.p & flag::C);
  set_flag(flag::C, sum > 0xFF);
  set_flag(flag::V, ((a ^ m) & (a ^ sum) & 0x80) != 0);
  regs.a = set_nz(lo8(sum));
}

// Decimal (BCD) add, NMOS behaviour, derived from Bruce Clark's test program
// [REF-5]: process the low nibble, then the high nibble; N and V come from the
// intermediate sum, Z from the plain binary sum, C from the corrected sum.
void Cpu::adc_decimal(u8 operand) noexcept {
  const int a = regs.a;
  const int m = operand;
  const int carry = regs.p & flag::C;

  int low = (a & 0x0F) + (m & 0x0F) + carry;
  if (low >= 0x0A) low = ((low + 0x06) & 0x0F) + 0x10;
  int sum = (a & 0xF0) + (m & 0xF0) + low;

  set_flag(flag::Z, ((a + m + carry) & 0xFF) == 0);
  set_flag(flag::N, (sum & 0x80) != 0);
  set_flag(flag::V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);
  if (sum >= 0xA0) sum += 0x60;
  set_flag(flag::C, sum > 0xFF);
  regs.a = lo8(sum);
}

// Decimal subtract: every flag equals the BINARY subtraction's flag [REF-5];
// only the accumulator value is decimal-adjusted.
void Cpu::sbc_decimal(u8 operand) noexcept {
  const int a = regs.a;
  const int m = operand;
  const int carry = regs.p & flag::C;

  const int binary = a + (m ^ 0xFF) + carry;
  set_flag(flag::C, binary > 0xFF);
  set_flag(flag::V, ((a ^ m) & (a ^ binary) & 0x80) != 0);
  (void)set_nz(lo8(binary));

  int low = (a & 0x0F) - (m & 0x0F) - (1 - carry);
  if (low < 0) low = ((low - 0x06) & 0x0F) - 0x10;
  int result = (a & 0xF0) - (m & 0xF0) + low;
  if (result < 0) result -= 0x60;
  regs.a = lo8(result);
}

// CMP/CPX/CPY: a subtraction whose result is thrown away. C = "no borrow".
void Cpu::compare(u8 reg, u8 operand) noexcept {
  set_flag(flag::C, reg >= operand);
  (void)set_nz(lo8(reg - operand));
}

u8 Cpu::asl(u8 v) noexcept {
  set_flag(flag::C, (v & 0x80) != 0);
  return set_nz(lo8(v << 1));
}

u8 Cpu::lsr(u8 v) noexcept {
  set_flag(flag::C, (v & 0x01) != 0);
  return set_nz(static_cast<u8>(v >> 1));
}

u8 Cpu::rol(u8 v) noexcept {
  const int carry_in = regs.p & flag::C;
  set_flag(flag::C, (v & 0x80) != 0);
  return set_nz(lo8((v << 1) | carry_in));
}

u8 Cpu::ror(u8 v) noexcept {
  const int carry_in = (regs.p & flag::C) << 7;
  set_flag(flag::C, (v & 0x01) != 0);
  return set_nz(lo8((v >> 1) | carry_in));
}

// Relative branch. Cost: +1 when taken, +1 more when the target is on a
// different page than the next instruction [REF-3].
unsigned Cpu::branch(bool taken, u16 offset_address) noexcept {
  const auto offset = static_cast<std::int8_t>(bus_.read(offset_address));
  if (!taken) return 0;
  const u16 next = regs.pc;
  regs.pc = static_cast<u16>(next + offset);
  return crosses_page(next, regs.pc) ? 2u : 1u;
}

// ===========================================================================
// Instruction execution. Returns EXTRA cycles beyond the table's base cost.
// ===========================================================================

template <Op O, Mode M>
unsigned Cpu::execute(u16 address) noexcept {
  // Read-modify-write helper: operate on A or on memory, decided at COMPILE time.
  auto rmw = [&](auto&& fn) {
    if constexpr (M == Mode::ACC) {
      regs.a = fn(regs.a);
    } else {
      bus_.write(address, fn(bus_.read(address)));
    }
  };

  switch (O) {
    // ---- loads and stores -------------------------------------------------
    case Op::LDA: regs.a = set_nz(bus_.read(address)); break;
    case Op::LDX: regs.x = set_nz(bus_.read(address)); break;
    case Op::LDY: regs.y = set_nz(bus_.read(address)); break;
    case Op::STA: bus_.write(address, regs.a); break;
    case Op::STX: bus_.write(address, regs.x); break;
    case Op::STY: bus_.write(address, regs.y); break;

    // ---- register transfers ----------------------------------------------
    case Op::TAX: regs.x = set_nz(regs.a); break;
    case Op::TAY: regs.y = set_nz(regs.a); break;
    case Op::TXA: regs.a = set_nz(regs.x); break;
    case Op::TYA: regs.a = set_nz(regs.y); break;
    case Op::TSX: regs.x = set_nz(regs.sp); break;
    case Op::TXS:
      regs.sp = regs.x;
      break;  // the only transfer without flags

    // ---- stack ---------------------------------------------------------------
    case Op::PHA: push(regs.a); break;
    case Op::PLA: regs.a = set_nz(pull()); break;
    case Op::PHP: push(lo8(regs.p | flag::B | flag::U)); break;
    case Op::PLP: regs.p = lo8((pull() & ~flag::B) | flag::U); break;

    // ---- logic -----------------------------------------------------------------
    case Op::AND: regs.a = set_nz(lo8(regs.a & bus_.read(address))); break;
    case Op::ORA: regs.a = set_nz(lo8(regs.a | bus_.read(address))); break;
    case Op::EOR: regs.a = set_nz(lo8(regs.a ^ bus_.read(address))); break;
    case Op::BIT: {
      const u8 value = bus_.read(address);
      set_flag(flag::Z, (regs.a & value) == 0);
      regs.p = lo8((regs.p & ~(flag::N | flag::V)) | (value & (flag::N | flag::V)));
      break;
    }

    // ---- arithmetic ----------------------------------------------------------
    case Op::ADC: adc(bus_.read(address)); break;
    case Op::SBC: sbc(bus_.read(address)); break;
    case Op::CMP: compare(regs.a, bus_.read(address)); break;
    case Op::CPX: compare(regs.x, bus_.read(address)); break;
    case Op::CPY: compare(regs.y, bus_.read(address)); break;

    // ---- increments, decrements -----------------------------------------------
    case Op::INC: rmw([this](u8 v) { return set_nz(lo8(v + 1)); }); break;
    case Op::DEC: rmw([this](u8 v) { return set_nz(lo8(v - 1)); }); break;
    case Op::INX: regs.x = set_nz(lo8(regs.x + 1)); break;
    case Op::INY: regs.y = set_nz(lo8(regs.y + 1)); break;
    case Op::DEX: regs.x = set_nz(lo8(regs.x - 1)); break;
    case Op::DEY: regs.y = set_nz(lo8(regs.y - 1)); break;

    // ---- shifts and rotates --------------------------------------------------
    case Op::ASL: rmw([this](u8 v) { return asl(v); }); break;
    case Op::LSR: rmw([this](u8 v) { return lsr(v); }); break;
    case Op::ROL: rmw([this](u8 v) { return rol(v); }); break;
    case Op::ROR: rmw([this](u8 v) { return ror(v); }); break;

    // ---- jumps and calls -----------------------------------------------------
    case Op::JMP: regs.pc = address; break;
    case Op::JSR:  // pushes the address of the LAST byte of the JSR instruction
      push16(static_cast<u16>(regs.pc - 1));
      regs.pc = address;
      break;
    case Op::RTS: regs.pc = static_cast<u16>(pull16() + 1); break;
    case Op::BRK:
      ++regs.pc;  // BRK has a padding byte: the return address skips it
      service_interrupt(kIrqVector, true);
      break;
    case Op::RTI:
      regs.p = lo8((pull() & ~flag::B) | flag::U);
      regs.pc = pull16();
      break;

    // ---- branches (return their extra cycles) -----------------------------------
    case Op::BCC: return branch((regs.p & flag::C) == 0, address);
    case Op::BCS: return branch((regs.p & flag::C) != 0, address);
    case Op::BNE: return branch((regs.p & flag::Z) == 0, address);
    case Op::BEQ: return branch((regs.p & flag::Z) != 0, address);
    case Op::BPL: return branch((regs.p & flag::N) == 0, address);
    case Op::BMI: return branch((regs.p & flag::N) != 0, address);
    case Op::BVC: return branch((regs.p & flag::V) == 0, address);
    case Op::BVS: return branch((regs.p & flag::V) != 0, address);

    // ---- flag instructions ------------------------------------------------------
    case Op::CLC: set_flag(flag::C, false); break;
    case Op::SEC: set_flag(flag::C, true); break;
    case Op::CLI: set_flag(flag::I, false); break;
    case Op::SEI: set_flag(flag::I, true); break;
    case Op::CLD: set_flag(flag::D, false); break;
    case Op::SED: set_flag(flag::D, true); break;
    case Op::CLV: set_flag(flag::V, false); break;

    case Op::NOP:
    case Op::JAM:  // JAM never gets here: it has its own handler
      break;
  }
  return 0;
}

// ===========================================================================
// Dispatch: one compile-time-specialised handler per opcode.
// ===========================================================================

// Everything about opcode `Opcode` (operation, mode, cycles, page penalty) is a
// constant here, so the compiler folds away both switches and the penalty test.
template <unsigned Opcode>
unsigned Cpu::handler(Cpu& cpu) noexcept {
  constexpr Instruction in = kInstructions[Opcode];
  if constexpr (in.op == Op::JAM) {
    cpu.regs.pc = static_cast<u16>(cpu.regs.pc - 1);  // PC back onto the opcode
    cpu.jammed_ = true;
    cpu.jam_opcode_ = static_cast<u8>(Opcode);
    return 0;
  } else {
    bool page_crossed = false;
    const u16 address = cpu.resolve<in.mode>(page_crossed);
    unsigned cycles = in.cycles;
    if constexpr (in.page_penalty) cycles += page_crossed ? 1u : 0u;
    return cycles + cpu.execute<in.op, in.mode>(address);
  }
}

namespace detail {

using Handler = unsigned (*)(Cpu&) noexcept;

struct HandlerTable {
  template <std::size_t... I>
  static constexpr std::array<Handler, 256> make(std::index_sequence<I...>) {
    return {&Cpu::handler<static_cast<unsigned>(I)>...};
  }
};

constexpr std::array<Handler, 256> kHandlers = HandlerTable::make(std::make_index_sequence<256>{});

}  // namespace detail

// ===========================================================================
// The main loop: one call = one instruction.
// ===========================================================================

unsigned Cpu::step() noexcept {
  if (jammed_) return 0;

  unsigned cycles = 0;
  if (nmi_pending_) {
    nmi_pending_ = false;
    service_interrupt(kNmiVector, false);
    cycles = 7;
  } else if (irq_line_ && (regs.p & flag::I) == 0) {
    service_interrupt(kIrqVector, false);
    cycles = 7;
  } else {
    const u8 opcode = fetch8();
    cycles = detail::kHandlers[opcode](*this);
    if (cycles == 0) return 0;  // the handler jammed the CPU
  }
  cycles_ += cycles;
  return cycles;
}

u64 Cpu::run(u64 cycle_budget) noexcept {
  u64 used = 0;
  while (used < cycle_budget) {
    const unsigned c = step();
    if (c == 0) break;  // jammed
    used += c;
  }
  return used;
}

}  // namespace mos6502
