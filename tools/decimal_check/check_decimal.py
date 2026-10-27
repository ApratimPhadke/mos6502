#!/usr/bin/env python3
"""Run Bruce Clark's decimal-mode test program on run6502 (CTest driver).

usage: check_decimal.py <path-to-run6502> <6502_decimal_test.a65>

The program computes the NMOS 6502 decimal-mode ADC/SBC results it EXPECTS with
ordinary binary instructions, compares them with what the decimal-mode
instructions actually produced, and stores 0 in ERROR ($000B) when all
2 x 2 x 65536 operand/carry combinations agree.
"""
import os, re, subprocess, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm65 import assemble

ERROR_ADDRESS = 0x0B
CONFIGS = {
    "A and C checked (Clark's default)": {},
    "A, N, V, Z and C all checked": {"chk_n": 1, "chk_v": 1, "chk_z": 1},
}

def main(run6502, source):
    text = open(source).read()
    status = 0
    for name, overrides in CONFIGS.items():
        body = "\n".join(l for l in text.splitlines()
                         if not any(re.match(r"\s*%s\s*=" % k, l) for k in overrides))
        image, entry, _ = assemble(body, overrides)
        with tempfile.NamedTemporaryFile(suffix=".bin", delete=False) as f:
            f.write(bytes(image))
        try:
            out = subprocess.run(
                [run6502, "--load", "0", "--pc", "0x%X" % entry, "--peek", "0x%X" % ERROR_ADDRESS, f.name],
                capture_output=True, text=True, timeout=300).stdout
        finally:
            os.unlink(f.name)
        m = re.search(r"\[\$000B\] = \$([0-9A-F]{2})", out)
        ok = bool(m) and m.group(1) == "00" and "jam opcode: $DB" in out
        print("%-40s %s" % (name, "PASS (ERROR=0)" if ok else "FAIL\n" + out))
        status |= 0 if ok else 1
    return status

if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
