#include <cstdio>

#include "mos6502/cpu.hpp"
#include "mos6502/disasm.hpp"
int main() {
  mos6502::Bus bus;
  const mos6502::u8 program[] = {0xA9, 0x2A, 0x69, 0x08,
                                 0x8D, 0x00, 0x30};  // LDA #42; ADC #8; STA $3000
  if (!bus.load(0x0200, program)) return 1;
  mos6502::Cpu cpu(bus);
  cpu.regs.pc = 0x0200;
  for (int i = 0; i < 3; ++i) {
    std::printf("%s\n", mos6502::disassemble(bus, cpu.regs.pc).text.c_str());
    (void)cpu.step();
  }
  std::printf("[$3000] = %u (expected 50)\n", static_cast<unsigned>(bus.peek(0x3000)));
  return bus.peek(0x3000) == 50 ? 0 : 1;
}
