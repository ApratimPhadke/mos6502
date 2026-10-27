#!/usr/bin/env python3
"""Hand-rolled mutation testing for mos6502.

A mutation test asks the test suite an awkward question: "if I broke this line,
would you notice?" We inject ONE realistic bug at a time into src/cpu.cpp,
rebuild, run the tests, and record WHICH layer noticed:

  unit         the GoogleTest unit tests
  Klaus        Klaus Dormann's functional test, binary reached a wrong trap/jam
  Fingerprint  Klaus run finished, but the total cycle count changed
  Clark        Bruce Clark's decimal-mode program

usage:   python3 tools/mutation/mutate.py <build-dir> [--repo <repo-root>]
         (<build-dir> must already be configured and built with the tests;
          run it on a clean checkout: the script edits src/cpu.cpp and restores it)
exit:    0 = every mutant was killed, 1 = a mutant SURVIVED or did not compile
"""
import argparse
import os
import re
import subprocess
import sys

# (name, anchor, old, new, nth)
#   anchor: a substring that identifies the line to edit (the nth line containing it)
#   old/new: replaced within that line only; `old` must occur there
MUTANTS = [
    ("M01 ADC overflow: drop '~'", "set_flag(flag::V, (~(a ^ m) & (a ^ sum) & 0x80) != 0);", "(~(a ^ m)", "((a ^ m)", 1),
    ("M02 SBC overflow: add '~'", "set_flag(flag::V, ((a ^ m) & (a ^ sum) & 0x80) != 0);", "((a ^ m)", "(~(a ^ m)", 1),
    ("M03 JMP (ind): no page-wrap bug", "make_word(lo8(pointer + 1), hi8(pointer))", "make_word(lo8(pointer + 1), hi8(pointer))", "static_cast<u16>(pointer + 1)", 1),
    ("M04 (zp,X) pointer: no zero-page wrap", "const u8 hi = bus_.read(lo8(zp + 1));", "bus_.read(lo8(zp + 1))", "bus_.read(static_cast<u16>(zp + 1))", 1),
    ("M05 (zp),Y pointer: no zero-page wrap", "wraps in page 0 as well", "bus_.read(lo8(zp + 1))", "bus_.read(static_cast<u16>(zp + 1))", 1),
    ("M06 branch page-cross cost 2 -> 1", "crosses_page(next, regs.pc) ? 2u : 1u", "? 2u : 1u", "? 1u : 1u", 1),
    ("M07 decimal SBC: correction 6 -> 5", "low - 0x06", "0x06", "0x05", 1),
    ("M08 decimal ADC: carry test >= 0xA0 -> > 0xA0", "if (sum >= 0xA0) sum += 0x60;", ">= 0xA0", "> 0xA0", 1),
    ("M09 CMP carry: >= becomes >", "set_flag(flag::C, reg >= operand);", ">=", ">", 1),
    ("M10 PLP: forget to force U", "case Op::PLP:", "| flag::U", "", 1),
    ("M11 BIT: copy N but not V", "value & (flag::N | flag::V)", "(value & (flag::N | flag::V))", "(value & flag::N)", 1),
    ("M12 ROL: carry-in from wrong flag", "const int carry_in = regs.p & flag::C;", "regs.p & flag::C", "(regs.p & flag::Z) ? 1 : 0", 1),
    ("M13 JSR pushes PC, not PC-1", "push16(static_cast<u16>(regs.pc - 1));", "static_cast<u16>(regs.pc - 1)", "regs.pc", 1),
    ("M14 RTS forgets the +1", "case Op::RTS:", "pull16() + 1", "pull16() + 0", 1),
    ("M15 indexed read never pays page penalty", "if constexpr (in.page_penalty)", "in.page_penalty", "false", 1),
    ("M16 BRK pushes PC without skipping padding", "++regs.pc;  // BRK has a padding byte", "++regs.pc;", "", 1),
    ("M17 IRQ pushes B set (looks like BRK)", "(software ? flag::B : 0)", "(software ? flag::B : 0)", "(software ? flag::B : flag::B)", 1),
    ("M18 interrupt entry forgets to set I", "set_flag(flag::I, true);", "true", "false", 1),
]


def sh(cmd, cwd):
    r = subprocess.run(cmd, shell=True, cwd=cwd, capture_output=True, text=True)
    return r.returncode, r.stdout + r.stderr


def classify(build, repo):
    """Return the list of layers that noticed the current build's bug."""
    _, out = sh("ctest --test-dir %s" % build, repo)
    failed = re.findall(r"#\d+: (\S+) \.+\*+Failed", out)
    layers = []
    unit = [f for f in failed if not f.startswith("Functional") and f != "decimal_clark"]
    if unit:
        layers.append("unit(%d)" % len(unit))
    if any(f.startswith("Functional") for f in failed):
        _, k = sh("%s/tests/mos6502_tests --gtest_filter='Functional.*'" % build, repo)
        layers.append("Klaus" if "stopped at" in k else "Fingerprint")
    if "decimal_clark" in failed:
        layers.append("Clark")
    return layers


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("build_dir")
    ap.add_argument("--repo", default=".")
    args = ap.parse_args()
    repo = os.path.abspath(args.repo)
    build = os.path.abspath(args.build_dir)
    src = os.path.join(repo, "src", "cpu.cpp")
    original = open(src).read()
    lines = original.split("\n")

    rc, out = sh("ninja -C %s" % build, repo)
    if rc != 0:
        print("baseline does not build:\n" + out)
        return 1
    if classify(build, repo):
        print("baseline is not green; fix that before mutating")
        return 1

    survived, invalid = [], []
    try:
        for name, anchor, old, new, nth in MUTANTS:
            hits = [i for i, l in enumerate(lines) if anchor in l]
            if len(hits) < nth or old not in lines[hits[nth - 1]]:
                print("%-48s -> INVALID (pattern not found; the source changed?)" % name)
                invalid.append(name)
                continue
            mutated = lines[:]
            i = hits[nth - 1]
            mutated[i] = mutated[i].replace(old, new, 1)
            open(src, "w").write("\n".join(mutated))
            # Rebuild EVERYTHING (the decimal test uses the run6502 binary), and
            # treat a build failure as an invalid mutant. Never test a stale binary.
            rc, out = sh("ninja -C %s" % build, repo)
            if rc != 0:
                print("%-48s -> INVALID (does not compile)" % name)
                invalid.append(name)
                continue
            layers = classify(build, repo)
            print("%-48s -> %s" % (name, ", ".join(layers) if layers else "*** SURVIVED ***"))
            if not layers:
                survived.append(name)
    finally:
        open(src, "w").write(original)
        sh("ninja -C %s" % build, repo)

    total = len(MUTANTS)
    print("\n%d mutants: %d killed, %d survived, %d invalid" % (total, total - len(survived) - len(invalid), len(survived), len(invalid)))
    return 0 if not survived and not invalid else 1


if __name__ == "__main__":
    sys.exit(main())
