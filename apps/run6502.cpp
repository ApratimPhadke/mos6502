// run6502 - load a 6502 program image, run it, report what happened.
//
//   run6502 --load 0x0000 --pc 0x0400 --trace 6502_functional_test.bin
//
// Stops when: the program traps (an instruction that jumps to itself, the
// classic "I am done / I have failed" idiom), a breakpoint is hit, the CPU
// jams on an undefined opcode, or the cycle budget runs out.
#include <bitset>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string_view>
#include <vector>

#include "mos6502/bus.hpp"
#include "mos6502/cpu.hpp"
#include "mos6502/disasm.hpp"
#include "mos6502/trace.hpp"

namespace {

using namespace mos6502;

constexpr int kExitOk = 0;
constexpr int kExitUsage = 1;
constexpr int kExitJam = 2;

struct Options {
  const char* image = nullptr;
  u16 load_address = 0;
  bool is_prg = false;
  bool have_pc = false;
  u16 pc = 0;
  u64 max_cycles = 1'000'000'000;
  bool trace = false;
  std::bitset<kAddressSpace> breakpoints;
  std::vector<u16> peeks;  // memory bytes to print after the run
};

void usage() {
  std::fputs(
      "usage: run6502 [options] <image>\n"
      "  --load ADDR      load a raw binary at ADDR (default 0)\n"
      "  --prg            image is a C64-style .prg (first 2 bytes = load address)\n"
      "  --pc ADDR        start at ADDR (default: read the reset vector at $FFFC)\n"
      "  --max-cycles N   stop after N cycles (default 1000000000)\n"
      "  --break ADDR     stop when PC reaches ADDR (repeatable)\n"
      "  --trace          print the last 32 executed instructions on exit\n"
      "  --peek ADDR      print the byte at ADDR after the run (repeatable)\n"
      "Numbers may be decimal, 0x1F or $1F.\n",
      stderr);
}

bool parse_number(const char* text, unsigned long long max, unsigned long long& out) {
  const int base = (text[0] == '$') ? 16 : 0;
  if (text[0] == '$') ++text;
  char* end = nullptr;
  out = std::strtoull(text, &end, base);
  return end != text && *end == '\0' && out <= max;
}

bool parse_args(int argc, char** argv, Options& o) {
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    auto number = [&](unsigned long long max, unsigned long long& v) {
      if (i + 1 >= argc) return false;
      ++i;  // consume the option's value
      return parse_number(argv[i], max, v);
    };
    unsigned long long v = 0;
    if (arg == "--load") {
      if (!number(0xFFFF, v)) return false;
      o.load_address = static_cast<u16>(v);
    } else if (arg == "--pc") {
      if (!number(0xFFFF, v)) return false;
      o.pc = static_cast<u16>(v);
      o.have_pc = true;
    } else if (arg == "--max-cycles") {
      if (!number(~0ULL, v)) return false;
      o.max_cycles = v;
    } else if (arg == "--break") {
      if (!number(0xFFFF, v)) return false;
      o.breakpoints.set(v);
    } else if (arg == "--peek") {
      if (!number(0xFFFF, v)) return false;
      o.peeks.push_back(static_cast<u16>(v));
    } else if (arg == "--prg") {
      o.is_prg = true;
    } else if (arg == "--trace") {
      o.trace = true;
    } else if (!arg.empty() && arg[0] != '-' && o.image == nullptr) {
      o.image = argv[i];
    } else {
      return false;
    }
  }
  return o.image != nullptr;
}

bool read_file(const char* path, std::vector<u8>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  Options opt;
  if (!parse_args(argc, argv, opt)) {
    usage();
    return kExitUsage;
  }

  std::vector<u8> image;
  if (!read_file(opt.image, image)) {
    std::fprintf(stderr, "run6502: cannot read '%s'\n", opt.image);
    return kExitUsage;
  }

  Bus bus;
  if (opt.is_prg) {
    const auto where = bus.load_prg(image);
    if (!where) {
      std::fputs("run6502: malformed .prg image\n", stderr);
      return kExitUsage;
    }
    opt.load_address = *where;
  } else if (!bus.load(opt.load_address, image)) {
    std::fputs("run6502: image does not fit in 64 KiB at that address\n", stderr);
    return kExitUsage;
  }

  Cpu cpu(bus);
  cpu.reset();
  if (opt.have_pc) {
    cpu.regs.pc = opt.pc;
  } else if (opt.is_prg) {
    cpu.regs.pc = opt.load_address;  // a .prg has no vectors of its own
  }

  TraceBuffer<1024> history;
  const char* reason = "cycle limit reached";
  int exit_code = kExitOk;
  const u64 start_cycles = cpu.total_cycles();

  const auto t0 = std::chrono::steady_clock::now();
  while (cpu.total_cycles() - start_cycles < opt.max_cycles) {
    const u16 pc = cpu.regs.pc;
    if (opt.breakpoints.test(pc)) {
      reason = "breakpoint";
      break;
    }
    if (opt.trace) {
      history.push(TraceEntry{.regs = cpu.regs,
                              .bytes = {bus.peek(pc), bus.peek(static_cast<u16>(pc + 1)),
                                        bus.peek(static_cast<u16>(pc + 2))},
                              .cycle = cpu.total_cycles()});
    }
    if (cpu.step() == 0) {
      reason = "CPU jammed on an undefined opcode";
      exit_code = kExitJam;
      break;
    }
    if (cpu.regs.pc == pc) {
      reason = "trap (instruction jumps to itself)";
      break;
    }
  }
  const auto t1 = std::chrono::steady_clock::now();

  if (opt.trace) {
    const std::size_t n = history.size();
    const std::size_t first = n > 32 ? n - 32 : 0;
    for (std::size_t i = first; i < n; ++i) {
      std::puts(format_trace(history.at(i)).c_str());
    }
  }

  const double seconds = std::chrono::duration<double>(t1 - t0).count();
  const u64 cycles = cpu.total_cycles() - start_cycles;
  std::printf("stopped: %s\n", reason);
  if (cpu.jammed()) std::printf("jam opcode: $%02X\n", static_cast<unsigned>(cpu.jam_opcode()));
  std::printf("PC=$%04X A=$%02X X=$%02X Y=$%02X SP=$%02X P=$%02X\n",
              static_cast<unsigned>(cpu.regs.pc), static_cast<unsigned>(cpu.regs.a),
              static_cast<unsigned>(cpu.regs.x), static_cast<unsigned>(cpu.regs.y),
              static_cast<unsigned>(cpu.regs.sp), static_cast<unsigned>(cpu.regs.p));
  std::printf("cycles: %llu  time: %.3f s  speed: %.1f MHz (real 6502: 1-2 MHz)\n",
              static_cast<unsigned long long>(cycles), seconds,
              seconds > 0.0 ? static_cast<double>(cycles) / seconds / 1e6 : 0.0);
  for (const u16 address : opt.peeks) {
    std::printf("[$%04X] = $%02X\n", static_cast<unsigned>(address),
                static_cast<unsigned>(bus.peek(address)));
  }
  return exit_code;
}
