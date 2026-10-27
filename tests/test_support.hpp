// Shared helpers for the unit tests: a bus + CPU pair and tiny utilities.
#pragma once

#include <gtest/gtest.h>

#include <initializer_list>

#include "mos6502/bus.hpp"
#include "mos6502/cpu.hpp"
#include "mos6502/flags.hpp"

namespace mos6502::test {

inline constexpr u16 kOrigin = 0x0200;  // where test programs are placed

class Machine {
 public:
  Machine() {
    cpu.regs.pc = kOrigin;
    cpu.regs.sp = 0xFF;  // empty stack
  }

  void poke(u16 address, u8 value) { bus.write(address, value); }
  void poke(u16 address, std::initializer_list<u8> bytes) {
    u16 a = address;
    for (u8 b : bytes) bus.write(a++, b);
  }
  [[nodiscard]] u8 peek(u16 address) { return bus.read(address); }

  // Put `code` at the origin, point PC at it, execute ONE instruction and
  // return how many cycles it took.
  unsigned run_one(std::initializer_list<u8> code) {
    poke(kOrigin, code);
    cpu.regs.pc = kOrigin;
    return cpu.step();
  }

  // Execute the next instruction wherever PC currently is.
  unsigned step() { return cpu.step(); }

  [[nodiscard]] bool flag(u8 mask) const { return (cpu.regs.p & mask) != 0; }

  Bus bus;
  Cpu cpu{bus};
};

}  // namespace mos6502::test
