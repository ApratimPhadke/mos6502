// compare_reference - run mos6502 side by side with the original reference
// emulator (davepoo/6502Emulator) [REF-9].
//
//   compare_reference diff  [trials]       random single-instruction fuzzing
//   compare_reference klaus <image.bin>    run Klaus's test on the reference
//   compare_reference bench                same workload as bench_cpu
//
// "diff" does NOT say who is right - it says where the two disagree. The
// ground truth (Klaus's test, REF-6) decides. See Day 6 of the guide.
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "m6502.h"  // the reference
#include "mos6502/bus.hpp"
#include "mos6502/cpu.hpp"
#include "mos6502/flags.hpp"
#include "mos6502/opcodes.hpp"

namespace {

using namespace mos6502;

constexpr u8 kIgnoredBits = flag::B | flag::U;  // do not exist in the real register

// --------------------------------------------------------------------- diff --
struct Mismatch {
  unsigned count = 0;
  std::string first;
};

// Every disagreement we have investigated is one of four real hardware
// behaviours that the reference gets wrong. Classify a mismatch so the tool can
// report how many are "explained" and, more importantly, how many are NOT.
const char* explain(u8 opcode, u8 operand1, u8 x, u8 sp) {
  const Instruction& in = kInstructions[opcode];
  if (in.mode == Mode::IZX && lo8(operand1 + x) == 0xFF)
    return "(zp,X) pointer at $FF must wrap to $00";
  if (in.mode == Mode::IZY && operand1 == 0xFF) return "(zp),Y pointer at $FF must wrap to $00";
  if (in.mode == Mode::IND && operand1 == 0xFF) return "JMP ($xxFF) page-boundary bug";
  if (in.op == Op::RTS && sp >= 0xFE) return "RTS: SP must wrap inside page 1";
  if (in.op == Op::RTI && sp >= 0xFD) return "RTI: SP must wrap inside page 1";
  return nullptr;
}

int run_diff(unsigned trials) {
  std::mt19937_64 rng(20260101);
  auto ref_mem = std::make_unique<m6502::Mem>();
  auto bus = std::make_unique<Bus>();
  m6502::CPU ref{};
  Cpu cpu(*bus);

  std::vector<u8> legal;
  for (unsigned i = 0; i < 256; ++i) {
    if (kInstructions[i].op != Op::JAM) legal.push_back(static_cast<u8>(i));
  }

  std::array<u8, kAddressSpace> base{};
  std::map<unsigned, Mismatch> by_opcode;
  unsigned total_bad = 0;
  std::map<std::string, unsigned> classes;
  unsigned unexplained = 0;

  for (unsigned t = 0; t < trials; ++t) {
    if (t % 256 == 0) {
      for (auto& b : base) b = static_cast<u8>(rng());
    }
    const u8 opcode = legal[rng() % legal.size()];
    const bool is_arith =
        kInstructions[opcode].op == Op::ADC || kInstructions[opcode].op == Op::SBC;
    const u16 pc = static_cast<u16>(0x0300 + rng() % 0xE000);
    std::array<u8, kAddressSpace> image = base;
    image[pc] = opcode;
    image[pc + 1] = static_cast<u8>(rng());
    image[pc + 2] = static_cast<u8>(rng());

    const u8 a = static_cast<u8>(rng()), x = static_cast<u8>(rng()), y = static_cast<u8>(rng());
    const u8 sp = static_cast<u8>(rng());
    u8 p = static_cast<u8>(rng());
    if (is_arith) p &= static_cast<u8>(~flag::D);  // the reference rejects decimal mode

    // reference
    std::memcpy(ref_mem->Data, image.data(), kAddressSpace);
    ref.PC = pc;
    ref.A = a;
    ref.X = x;
    ref.Y = y;
    ref.SP = sp;
    ref.PS = p;
    int ref_cycles = -1;
    bool ref_threw = false;
    try {
      ref_cycles = ref.Execute(1, *ref_mem);
    } catch (...) {
      ref_threw = true;
    }

    // mine
    (void)bus->load(0, image);
    cpu.regs = {pc, sp, a, x, y, static_cast<u8>(p | flag::U)};
    const int my_cycles = static_cast<int>(cpu.step());

    std::string why;
    char buf[160];
    if (ref_threw) {
      why = "reference threw";
    } else {
      if (cpu.regs.pc != ref.PC) {
        std::snprintf(buf, sizeof buf, "PC mine=%04X ref=%04X", cpu.regs.pc, ref.PC);
        why += buf;
      }
      if (cpu.regs.a != ref.A) {
        std::snprintf(buf, sizeof buf, " A mine=%02X ref=%02X", cpu.regs.a, ref.A);
        why += buf;
      }
      if (cpu.regs.x != ref.X) {
        std::snprintf(buf, sizeof buf, " X mine=%02X ref=%02X", cpu.regs.x, ref.X);
        why += buf;
      }
      if (cpu.regs.y != ref.Y) {
        std::snprintf(buf, sizeof buf, " Y mine=%02X ref=%02X", cpu.regs.y, ref.Y);
        why += buf;
      }
      if (cpu.regs.sp != ref.SP) {
        std::snprintf(buf, sizeof buf, " SP mine=%02X ref=%02X", cpu.regs.sp, ref.SP);
        why += buf;
      }
      if ((cpu.regs.p & ~kIgnoredBits) != (ref.PS & ~kIgnoredBits)) {
        std::snprintf(buf, sizeof buf, " P mine=%02X ref=%02X", cpu.regs.p & ~kIgnoredBits,
                      ref.PS & ~kIgnoredBits);
        why += buf;
      }
      if (my_cycles != ref_cycles) {
        std::snprintf(buf, sizeof buf, " cycles mine=%d ref=%d", my_cycles, ref_cycles);
        why += buf;
      }
      const auto mine = bus->backing_store();
      if (std::memcmp(mine.data(), ref_mem->Data, kAddressSpace) != 0) {
        for (unsigned i = 0; i < kAddressSpace; ++i) {
          if (mine[i] != ref_mem->Data[i]) {
            std::snprintf(buf, sizeof buf, " MEM[%04X] mine=%02X ref=%02X", i, mine[i],
                          ref_mem->Data[i]);
            why += buf;
            break;
          }
        }
      }
    }
    if (!why.empty()) {
      ++total_bad;
      if (const char* c = explain(opcode, image[pc + 1], x, sp)) {
        ++classes[c];
      } else {
        ++unexplained;
        std::printf("UNEXPLAINED opcode $%02X: %s\n", opcode, why.c_str());
      }
      Mismatch& m = by_opcode[opcode];
      if (m.count++ == 0) {
        std::snprintf(buf, sizeof buf, "[a=%02X x=%02X y=%02X sp=%02X p=%02X b1=%02X b2=%02X] ", a,
                      x, y, sp, p, image[pc + 1], image[pc + 2]);
        m.first = std::string(buf) + why;
      }
    }
  }

  std::printf("%u trials, %u mismatching, %u unexplained\n", trials, total_bad, unexplained);
  for (const auto& [name, n] : classes) std::printf("  %5u x %s\n", n, name.c_str());
  for (const auto& [op, m] : by_opcode) {
    const Instruction& in = kInstructions[op];
    std::printf("  $%02X %s %-3u mismatches, first: %s\n", op,
                std::string(kMnemonics[static_cast<std::size_t>(in.op)]).c_str(), m.count,
                m.first.c_str());
  }
  return 0;
}

// -------------------------------------------------------------------- klaus --
int run_klaus(const char* path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    std::fputs("cannot open image\n", stderr);
    return 1;
  }
  const std::vector<char> image((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
  auto mem = std::make_unique<m6502::Mem>();
  m6502::CPU cpu{};
  std::memcpy(mem->Data, image.data(), image.size());
  cpu.PC = 0x0400;
  cpu.SP = 0xFF;
  cpu.A = cpu.X = cpu.Y = 0;
  cpu.PS = 0;
  unsigned long long n = 0;
  try {
    for (; n < 200'000'000ULL; ++n) {
      const m6502::Word pc = cpu.PC;
      (void)cpu.Execute(1, *mem);
      if (cpu.PC == pc) break;
    }
  } catch (...) {
    std::printf("reference THREW after %llu instructions at PC=$%04X, test_case=$%02X\n", n, cpu.PC,
                mem->Data[0x0200]);
    return 0;
  }
  std::printf(
      "reference stopped after %llu instructions at PC=$%04X, test_case=$%02X (success is $3469)\n",
      n, cpu.PC, mem->Data[0x0200]);
  return 0;
}

// -------------------------------------------------------------------- bench --
int run_bench() {
  constexpr std::array<u8, 21> program = {0xA2, 0x00, 0xBD, 0x00, 0x03, 0x18, 0x69,
                                          0x01, 0x9D, 0x00, 0x03, 0xE8, 0xD0, 0xF4,
                                          0xE6, 0x10, 0xA4, 0x10, 0x4C, 0x00, 0x02};
  double best = 1e30;
  for (int r = 0; r < 5; ++r) {
    auto mem = std::make_unique<m6502::Mem>();
    m6502::CPU cpu{};
    cpu.Reset(0x0200, *mem);  // note: Reset() wipes memory, so load AFTER it
    std::memcpy(mem->Data + 0x0200, program.data(), program.size());
    const auto t0 = std::chrono::steady_clock::now();
    const int used = cpu.Execute(200'000'000, *mem);
    const auto t1 = std::chrono::steady_clock::now();
    best = std::min(best, std::chrono::duration<double>(t1 - t0).count());
    if (used < 200'000'000) std::puts("warning: reference stopped early");
  }
  std::printf("reference best of 5: %.3f s for 200000000 cycles = %.1f MHz emulated\n", best,
              200e6 / best / 1e6);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc >= 2 && std::strcmp(argv[1], "diff") == 0) {
    return run_diff(argc >= 3 ? static_cast<unsigned>(std::strtoul(argv[2], nullptr, 10))
                              : 300000u);
  }
  if (argc >= 3 && std::strcmp(argv[1], "klaus") == 0) return run_klaus(argv[2]);
  if (argc >= 2 && std::strcmp(argv[1], "bench") == 0) return run_bench();
  std::fputs("usage: compare_reference diff [trials] | klaus <image.bin> | bench\n", stderr);
  return 1;
}
