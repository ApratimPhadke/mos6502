# Design notes

## Goal and success criteria

Build a **documented-NMOS-6502 CPU core** that is correct, fast enough, portable
to small targets, and easy to debug. "Done" means all of these are true:

| # | Criterion | How it is checked |
|---|-----------|-------------------|
| 1 | Passes Klaus Dormann's functional test | `ctest` -> `Functional.*` (trap at `$3469`) |
| 2 | Decimal mode is exact, incl. undefined N/V/Z | `ctest` -> `decimal_clark`, `Decimal.*`, `Arithmetic.*` |
| 3 | Cycle counts are exact | table from REF-3 + cycle fingerprint + differential test |
| 4 | No heap, no exceptions, no RTTI in the core | `nm` shows no `operator new`, `__cxa_*`, `typeinfo` |
| 5 | Warning-free, sanitizer-clean | `-Wall -Wextra -Wconversion ... -Werror`; ASan+UBSan run |
| 6 | Usable as a library | `cmake --install` + `examples/consumer` builds against it |

## Layers

```
 apps/run6502   bench/   tests/          <- users of the library
        |          |        |
        +----------+--------+
                   |
   mos6502_debug:  disasm  trace         <- host tooling (may use std::string)
                   |
   mos6502 (core): Cpu  --->  Bus        <- no heap / exceptions / I/O
                   |           |
              opcodes.hpp   Device (interface for memory-mapped peripherals)
              flags.hpp  types.hpp
```

Dependencies point downward only. The core never includes `<iostream>`, `<string>`
or `<vector>`.

## Files and their role

| File | Role |
|------|------|
| `types.hpp` | fixed-width types, vectors, `lo8/hi8/make_word/crosses_page` |
| `flags.hpp` | the 8 bit masks of the status register |
| `opcodes.hpp` | `Op`, `Mode`, and the 256-entry `constexpr` instruction table |
| `bus.hpp/.cpp` | 256-entry page table: RAM / ROM / device / unmapped |
| `cpu.hpp/.cpp` | registers, reset, interrupts, ALU, 256 specialised handlers |
| `disasm.*` | table-driven disassembler + trace-line formatter |
| `trace.hpp` | power-of-two ring buffer of recent instructions |

## Decisions (and what was measured)

**D1 - Opcode table as data.** One 151-line declarative table (op, mode, cycles,
page-penalty) instead of a 150-case `switch` with copy-pasted addressing code.
The disassembler reuses it, so CPU and disassembler cannot disagree.

**D2 - Compile-time specialised handlers.** `handler<Opcode>` is instantiated for
all 256 opcodes; inside it the mode, operation and cycle cost are constants, so the
compiler folds both `switch`es away. *Measured* (4 interleaved rounds, GCC 13, `-O3`):
~420 -> ~563 MHz-equivalent (+33%). Cost: ~34 KB of code instead of a few KB; on a tiny MCU you
might prefer the plain two-`switch` version.

**D3 - Page-table bus.** O(1) decode, supports ROM, MMIO and bank switching. Cost
measured by swapping in a flat array: removing it makes the core ~29% faster
(563 -> 726 MHz). We accept it:
a CPU core without a memory map cannot model real machines (REF-1).

**D4 - Things tried and rejected (no measurable gain => no extra complexity):**
struct-of-arrays page table; one packed "events" byte instead of three flags;
a single-lookup `read16`. All were within noise (~570 MHz, +/- 10% in that session) and were reverted.

**D5 - Error policy.** No exceptions. An undefined opcode *jams* the CPU
(`jammed()`, PC stays on the opcode, `step()` returns 0) like the real KIL
opcodes. Bus range errors return `false`. Preconditions that are programmer
errors (e.g. `TraceBuffer::at(i)` with `i >= size()`) are documented, not checked.

**D6 - P register.** A plain `u8`; bit 5 (U) is always 1, bit 4 (B) is never
stored (it exists only in the copy pushed by PHP/BRK) [REF-7].

**D7 - Licensing hygiene.** GPL test binaries are downloaded at configure time
with a pinned commit and SHA-256; nothing GPL is committed.

## Non-goals (and why)

* **Undocumented opcodes** - they jam the CPU here. Adding them is a table edit
  plus handlers; deliberately out of scope.
* **Cycle-exact bus activity** (dummy reads/writes, exact read-modify-write
  sequences). Matters only for peripherals with read side effects. The cycle
  *count* per instruction is exact.
* **Interrupt-polling quirks** (CLI/SEI/PLP latency, NMI hijacking BRK).
* **65C02 / 6510 variants** and any machine (C64, NES...). They belong in a layer
  *above* this core, built from `Bus` + `Device`.
* **Interrupt functional test** (Klaus's `6502_interrupt_test`): not run; interrupt
  behaviour is covered by our own unit tests only.

## Known limitations

* `Bus` is ~70 KB (64 KiB RAM + page tables). On a small MCU, place it in static
  storage; never on a thread stack.
* Not verified here: MSVC, macOS, 32-bit targets, big-endian hosts. The CI file
  lists them; treat the first runs as information.
