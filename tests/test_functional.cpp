// Ground truth: Klaus Dormann's 6502 functional test [REF-6].
//
// The binary exercises every documented opcode and addressing mode and checks
// results AND flags. It has no I/O: the program signals the outcome by looping
// forever on one address. Loaded at $0000 and started at $0400:
//   * success trap: $3469  (listing: "jmp *  ;test passed, no errors")
//   * any other self-loop is a failure; the byte at $0200 ("test_case") holds
//     the number of the test that was running.
#include <fstream>
#include <iterator>
#include <sstream>
#include <vector>

#include "mos6502/disasm.hpp"
#include "mos6502/trace.hpp"
#include "test_support.hpp"

namespace mos6502 {
namespace {

constexpr u16 kStartAddress = 0x0400;
constexpr u16 kSuccessTrap = 0x3469;
constexpr u16 kTestCaseByte = 0x0200;

TEST(Functional, KlausDormannFunctionalTestReachesTheSuccessTrap) {
  std::ifstream file(MOS6502_KLAUS_BIN, std::ios::binary);
  if (!file) GTEST_SKIP() << "6502_functional_test.bin was not downloaded (offline?)";
  const std::vector<u8> image((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());
  ASSERT_EQ(image.size(), 65536u);

  test::Machine m;
  ASSERT_TRUE(m.bus.load(0x0000, image));
  m.cpu.regs.pc = kStartAddress;

  TraceBuffer<32> history;
  constexpr u64 kMaxInstructions = 200'000'000;  // the real run needs ~30 million
  u64 executed = 0;
  bool trapped = false;
  while (executed < kMaxInstructions) {
    const u16 pc = m.cpu.regs.pc;
    history.push({m.cpu.regs,
                  {m.bus.peek(pc), m.bus.peek(static_cast<u16>(pc + 1)),
                   m.bus.peek(static_cast<u16>(pc + 2))},
                  m.cpu.total_cycles()});
    if (m.cpu.step() == 0) break;  // jammed
    ++executed;
    if (m.cpu.regs.pc == pc) {
      trapped = true;
      break;
    }
  }

  if (!trapped || m.cpu.regs.pc != kSuccessTrap) {
    std::ostringstream report;
    report << "stopped at $" << std::hex << m.cpu.regs.pc << " (expected $" << kSuccessTrap
           << "), test_case = $" << static_cast<int>(m.bus.peek(kTestCaseByte)) << ", after "
           << std::dec << executed << " instructions\nlast instructions:\n";
    for (std::size_t i = 0; i < history.size(); ++i) {
      report << "  " << format_trace(history.at(i)) << '\n';
    }
    FAIL() << report.str();
  }
  // Regression fingerprint: the cycle total we MEASURED for this exact binary.
  // It is not an external truth, but any change to a cycle count in the opcode
  // table or in the page-crossing / branch rules will move this number.
  EXPECT_EQ(m.cpu.total_cycles(), 96'241'367u);
  SUCCEED() << executed << " instructions, " << m.cpu.total_cycles() << " cycles";
}

}  // namespace
}  // namespace mos6502
