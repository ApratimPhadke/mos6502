// bench_cpu - how fast does the emulated CPU run?
//
// Method (see Day 6 of the guide): fixed workload, several repetitions, report
// the BEST time (the least disturbed by the OS), and print a result checksum so
// the compiler cannot delete the work.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>

#include "mos6502/bus.hpp"
#include "mos6502/cpu.hpp"

namespace {

// A tiny mixed workload: abs,X load/store, ADC, INC zp, branches, JMP.
//   0200  LDX #$00
//   0202  LDA $0300,X
//   0205  CLC
//   0206  ADC #$01
//   0208  STA $0300,X
//   020B  INX
//   020C  BNE $0202
//   020E  INC $10
//   0210  LDY $10
//   0212  JMP $0200
constexpr std::array<mos6502::u8, 21> kProgram = {0xA2, 0x00, 0xBD, 0x00, 0x03, 0x18, 0x69,
                                                  0x01, 0x9D, 0x00, 0x03, 0xE8, 0xD0, 0xF4,
                                                  0xE6, 0x10, 0xA4, 0x10, 0x4C, 0x00, 0x02};

}  // namespace

int main() {
  using namespace mos6502;
  constexpr u64 kCycles = 200'000'000;
  constexpr int kRepeats = 5;

  double best = 1e30;
  u64 checksum = 0;
  for (int r = 0; r < kRepeats; ++r) {
    Bus bus;
    if (!bus.load(0x0200, kProgram)) return 1;
    Cpu cpu(bus);
    cpu.regs.pc = 0x0200;

    const auto t0 = std::chrono::steady_clock::now();
    const u64 used = cpu.run(kCycles);
    const auto t1 = std::chrono::steady_clock::now();

    best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    checksum += used + cpu.regs.a + cpu.regs.y + bus.peek(0x0300) + bus.peek(0x0010);
  }

  std::printf("best of %d: %.3f s for %llu cycles = %.1f MHz emulated (checksum %llu)\n", kRepeats,
              best, static_cast<unsigned long long>(kCycles),
              static_cast<double>(kCycles) / best / 1e6, static_cast<unsigned long long>(checksum));
  return 0;
}
