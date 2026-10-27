# mos6502

A small, fast, well-tested emulator **core** for the documented NMOS 6502 CPU,
written in C++20. No heap, no exceptions, no I/O in the core; memory-mapped
devices plug in through a page-table `Bus`.

```cpp
#include "mos6502/cpu.hpp"

mos6502::Bus bus;
const mos6502::u8 program[] = {0xA9, 0x2A, 0x69, 0x08, 0x8D, 0x00, 0x30};  // LDA #42 / ADC #8 / STA $3000
(void)bus.load(0x0200, program);

mos6502::Cpu cpu(bus);
cpu.regs.pc = 0x0200;
for (int i = 0; i < 3; ++i) (void)cpu.step();   // returns the cycles each instruction took
// bus.peek(0x3000) == 50
```

## Build, test, run

```sh
cmake --preset release
cmake --build --preset release
ctest --preset release                      # 87 tests, ~1 s
./out/release/run6502 --load 0 --pc 0x400 --peek 0x200 \
    out/release/tests/6502_functional_test.bin
```

Requirements: CMake >= 3.20, a C++20 compiler (GCC 13 and Clang 22 verified),
Python 3 (for the decimal-mode test), and internet access at the first configure
(GoogleTest and the test programs are downloaded, pinned and hash-checked).

Other presets: `debug`, `asan` (AddressSanitizer + UBSan).

## What is verified

| Check | Result |
|-------|--------|
| Klaus Dormann functional test | passes, 96,241,367 cycles |
| Bruce Clark decimal-mode program (A, N, V, Z, C checked) | `ERROR = 0` |
| Unit tests (incl. exhaustive ADC/SBC, binary and decimal) | 86 pass |
| ASan + UBSan | clean |
| GCC 13 and Clang 22, `-Wall -Wextra -Wconversion -Werror` | clean |
| clang-tidy (curated checks) | 0 findings |
| Line coverage of `src/` | 99.7% |
| Hand-made mutation test (18 injected bugs, `tools/mutation/mutate.py`) | 18 / 18 caught |
| Differential test vs. davepoo/6502Emulator, 1M random instructions | 505 mismatches, all explained |

## Speed

About 560 MHz-equivalent on the bundled benchmark (GCC 13, `-O3`; about 420 with
Clang 22) - hundreds of times faster than a real 1 MHz 6502. See `docs/DESIGN.md` for what was measured
and what the page-table bus costs.

## Layout

```
include/mos6502/   public headers (types, flags, opcodes, bus, cpu, disasm, trace)
src/               bus.cpp cpu.cpp disasm.cpp
apps/run6502.cpp   command-line runner
tests/             unit tests + functional-test harness
bench/             benchmark
tools/             decimal_check (assembler + driver), mutation (mutation tester),
                   reference_compare (optional side-by-side fuzzing)
examples/consumer  how to use the installed package
docs/              REFERENCES.md  DESIGN.md
```

## Scope

Documented opcodes only; undefined opcodes jam the CPU. Cycle *counts* are exact,
individual bus cycles are not modelled. See `docs/DESIGN.md` ("Non-goals").

## License

MIT (see `LICENSE`). Downloaded test programs are GPL-3.0 and are not included.
