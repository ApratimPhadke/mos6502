# References

Every non-obvious fact in this code base traces back to one of the sources below.
In code a fact is tagged `[REF-n]`; this file says what `n` is, how far we trust it,
and exactly what we took from it.

All pages were accessed on **2026-10-15**.

## Rules we follow

1. **Cite the thing you actually read.** If a source was only located, not read, it
   is listed as *pointer* and no code comment leans on it.
2. **Rank sources.** Hardware behaviour beats a document about it; a document beats
   an emulator; an emulator beats a blog post. When two sources disagree, a
   *running test against real behaviour* (REF-6) decides.
3. **Separate CPU facts from machine facts.** A memory map belongs to a *machine*
   (CPU + chips + wiring), not to the CPU.
4. **Cross-check numbers.** Even good pages have typos (see REF-1).
5. **Pin what you download.** Exact commit + SHA-256, so a build is reproducible
   and a changed upstream cannot silently change our tests.

## Index

| ID | Source | Trust | Status |
|----|--------|-------|--------|
| REF-1 | Commodore 64 memory map - <https://sta.c64.org/cbm64mem.html> | machine-specific | read in full |
| REF-2 | MOS MCS6500 Hardware / Programming Manuals - <https://bio.nic.funet.fi/pub/cbm/manuals/mos/index.html> | primary | pointer (index located, PDFs not opened) |
| REF-3 | Obelisk 6502 guide (archived) - <https://www.nesdev.org/obelisk-6502-guide/reference.html>, `addressing.html` | secondary | instruction reference read in full |
| REF-4 | masswerk 6502 instruction set - <https://masswerk.at/6502/6502_instruction_set.html> | secondary | pointer (not used for any claim) |
| REF-5 | Decimal mode: Bruce Clark's tutorial (<http://www.6502.org/tutorials/decimal_mode.html>) and his test program; NESdev mirror <https://nesdev.org/wiki/Visual6502wiki/6502DecimalMode> | secondary / test oracle | test program read in full; tutorial URL taken from its header, page not opened |
| REF-6 | Klaus Dormann, 6502/65C02 functional tests - <https://github.com/Klaus2m5/6502_65C02_functional_tests> | ground truth (executable) | used, pinned |
| REF-7 | NESdev wiki, Status flags - <https://nesdev.org/wiki/Status_flag> | secondary | search excerpt read |
| REF-8 | NESdev wiki, CPU power up state - <https://nesdev.org/wiki/CPU_power_up_state> | secondary | search excerpt read |
| REF-9 | davepoo's 6502 emulator - <https://github.com/davepoo/6502Emulator>; video <https://youtu.be/qJgsuQoy9bc> (named in its README) | reference implementation | source read in full |
| REF-10 | GoogleTest - <https://github.com/google/googletest> | tool | pinned v1.17.0 |

## Details

### REF-1 - Commodore 64 memory map (supplied by the project owner)

*What it is:* a table of every address range of a **Commodore 64**, whose CPU is a
6510 (a 6502 with an on-chip I/O port).

*What we took:*

* the three regions the **CPU itself** defines, confirmed there: zero page
  `$0000-$00FF`, the stack page `$0100-$01FF` ("Processor stack"), and the
  hardware vectors `$FFFA-$FFFF` (NMI `$FFFA`, reset `$FFFC`, IRQ `$FFFE`);
* the *idea* of run-time bank switching: bits 0-2 of the port at `$0001` choose
  whether RAM, ROM or I/O is visible at `$A000`, `$D000` and `$E000`. This is why
  our `Bus` is a remappable page table rather than a fixed array;
* the `.prg` convention (a 2-byte load address; BASIC starts at `$0801`) for
  `Bus::load_prg`.

*What we did NOT take:* anything from `$D000` upward (VIC-II, SID, CIA, colour RAM
are chips of that machine) and `$0000/$0001` (the 6510's port; a 6502 has none).

*Cross-check note:* the row for `$D800-$DFFF` prints its decimal start as `55295`,
but `$D800` = 13 x 4096 + 8 x 256 = **55296** (the row above ends at 55295). A typo,
harmless here, and a good reason to convert hex yourself instead of trusting a
decimal column. The page also shows BASIC using `$0100-$013D` and `$00FF-$010A`
inside the "stack" page: regions are *conventions of the software*, not walls.

### REF-2 - MOS manuals (pointer)

The original 1975-76 hardware and programming manuals are the top of the trust
ranking. I located the index page that lists them but did not open the PDFs, so no
claim in the code relies on them. Read them yourself when a question is not settled
by REF-3 + REF-6.

### REF-3 - Obelisk 6502 guide

Used for: opcode values, byte counts, base cycle counts and the "+1 if page
crossed" rule for every documented instruction; branch cost ("2, +1 if the branch
succeeds, +2 if to a new page"); the `JMP ($xxFF)` bug ("fetches the LSB from
`$xxFF` ... takes the MSB from `$xx00`"); that BRK jumps through `$FFFE/F`; that
the decimal flag "is not reset when an interrupt is generated".

### REF-5 - Decimal mode

Bruce Clark's program computes what an NMOS 6502 *should* produce for ADC/SBC in
decimal mode (for **any** operand bytes, not only valid BCD) using only binary
instructions, then compares. We run **his actual program** on our CPU
(`tools/decimal_check/`). Result on NMOS 6502: ADC's N and V come from the
intermediate sum, Z from the binary sum; every SBC flag equals the binary SBC flag.

### REF-6 - Klaus Dormann's functional tests

GPL-3.0, so **never copied into this repository**. `tests/CMakeLists.txt` downloads
commit `7954e2dbb49c469ea286070bf46cdd71aeb29e4b` and refuses any file whose
SHA-256 differs:

* `6502_functional_test.bin` `fa12bfc761e6f9057e4cc01a665a7b800ff01ae91f598af1e39a1201d01953fd`
* `6502_decimal_test.a65`   `dfbe4b907c5821d47d9f7f74eeb51197cf4b9f5260375bc1c6c48c008220e1a0`

Facts read from the listing: load at `$0000`, start at `$0400`, success trap at
`$3469` (`jmp *`), current test number in the byte at `$0200`.

What it does **not** cover (proved by mutation testing, see the guide): JMP-indirect
page bug, zero-page pointer wrap, cycle counts, IRQ/NMI behaviour.

### REF-9 - the reference emulator

Read in full. We wrote our own code; `tools/reference_compare` runs both side by
side. Differences found (1,000,000 random instructions, 0 unexplained): `(zp,X)`
and `(zp),Y` pointer at `$FF`; `RTS`/`RTI` when SP wraps; `JMP ($xxFF)`. The
reference also refuses decimal mode and reaches the functional test's decimal
section before throwing.
