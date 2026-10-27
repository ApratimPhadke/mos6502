#!/usr/bin/env python3
"""A deliberately tiny 6502 assembler - just enough for Bruce Clark's
6502_decimal_test.a65 (Klaus Dormann's repository, REF-5/REF-6).

Supported: `name = expr`, labels (no colon), org/ds/db/end, bss/code sections,
macro/endm (only `end_of_test`), if/else-less `if a = b` / `if a != b` / endif,
and the opcodes below. The opcode bytes are written from the MOS documentation,
independently of mos6502's C++ table, so a typo in either shows up as a failure.
"""
import re, sys

IMPLIED = dict(sed=0xF8, cld=0xD8, php=0x08, pla=0x68, inx=0xE8, dey=0x88, sec=0x38,
               clc=0x18, rts=0x60)
BRANCH = dict(bpl=0x10, bcc=0x90, bcs=0xB0, bne=0xD0, beq=0xF0)
#            imm   zp    zpx   abs
ALU = dict(adc=(0x69, 0x65, 0x75, 0x6D), sbc=(0xE9, 0xE5, 0xF5, 0xED),
           and_=(0x29, 0x25, 0x35, 0x2D), ora=(0x09, 0x05, 0x15, 0x0D),
           eor=(0x49, 0x45, 0x55, 0x4D), cmp=(0xC9, 0xC5, 0xD5, 0xCD),
           lda=(0xA9, 0xA5, 0xB5, 0xAD))
OTHER = {('ldy', 'imm'): 0xA0, ('ldx', 'imm'): 0xA2, ('cpy', 'imm'): 0xC0,
         ('cpx', 'imm'): 0xE0, ('sty', 'zp'): 0x84, ('sta', 'zp'): 0x85,
         ('inc', 'zp'): 0xE6, ('jsr', 'abs'): 0x20}

def assemble(text, defines):
    syms = dict(defines)
    lines = []
    for raw in text.splitlines():
        line = raw.split(';')[0].rstrip()
        if line.strip():
            lines.append((raw, line))

    def ev(expr, pc=None):
        expr = expr.strip()
        total = 0
        for sign, term in re.findall(r'([+-]?)\s*([^+-]+)', expr):
            term = term.strip()
            if term.startswith('$'): v = int(term[1:], 16)
            elif term.isdigit(): v = int(term)
            elif term in syms: v = syms[term]
            else: raise KeyError(term)
            total += -v if sign == '-' else v
        return total

    def run_pass(final):
        mem = bytearray(65536)
        pc, section, skip_stack, macro, entry = 0, 'code', [], False, None
        for raw, line in lines:
            toks = line.split()
            label = None
            if not line[0].isspace():
                label = toks.pop(0)
                if toks and toks[0] == '=':
                    syms[label] = ev(' '.join(toks[1:])); continue
                if toks and toks[0] == 'macro':
                    macro = True; continue
            if macro:
                if toks and toks[0] == 'endm': macro = False
                continue
            if toks and toks[0] == 'if':
                m = re.match(r'if\s+(\S+)\s*(!=|=)\s*(\S+)', line.strip())
                a, op, b = ev(m.group(1)), m.group(2), ev(m.group(3))
                skip_stack.append((a == b) if op == '=' else (a != b))
                continue
            if toks and toks[0] == 'endif':
                skip_stack.pop(); continue
            if skip_stack and not all(skip_stack):
                continue
            if label and not (toks and toks[0] in ('bss', 'code')):
                syms[label] = pc
            if not toks: continue
            m, rest = toks[0].lower(), ' '.join(toks[1:])
            if m in ('bss', 'code'): section = m; continue
            if m == 'org': pc = ev(rest); continue
            if m == 'end': entry = ev(rest); continue
            if m == 'ds':
                pc += ev(rest); continue
            if m == 'db':
                mem[pc] = ev(rest) & 0xFF; pc += 1; continue
            if m == 'end_of_test':
                mem[pc] = 0xDB; pc += 1; continue
            key = 'and_' if m == 'and' else m
            if m in IMPLIED:
                mem[pc] = IMPLIED[m]; pc += 1; continue
            if m in BRANCH:
                target = ev(rest) if final else pc + 2
                off = target - (pc + 2)
                if final and not -128 <= off <= 127: raise ValueError('branch range ' + raw)
                mem[pc] = BRANCH[m]; mem[pc + 1] = off & 0xFF; pc += 2; continue
            operand = rest.strip()
            mode = 'imm' if operand.startswith('#') else None
            indexed = None
            if mode == 'imm':
                val = ev(operand[1:]) if final else 0
            else:
                if ',' in operand:
                    operand, indexed = [x.strip() for x in operand.split(',')]
                val = ev(operand) if final else 0
                mode = 'zp' if (final and val < 256 and m != 'jsr') else 'abs'
                if not final and m != 'jsr': mode = 'zp'
            if key in ALU:
                imm, zp, zpx, ab = ALU[key]
                if mode == 'imm': mem[pc] = imm; mem[pc + 1] = val & 0xFF; pc += 2
                elif mode == 'zp' and indexed == 'x': mem[pc] = zpx; mem[pc + 1] = val & 0xFF; pc += 2
                elif mode == 'zp': mem[pc] = zp; mem[pc + 1] = val & 0xFF; pc += 2
                else: raise ValueError('unsupported ' + raw)
            elif (m, mode) in OTHER:
                op = OTHER[(m, mode)]
                mem[pc] = op
                if mode == 'abs': mem[pc + 1] = val & 0xFF; mem[pc + 2] = val >> 8; pc += 3
                else: mem[pc + 1] = val & 0xFF; pc += 2
            else:
                raise ValueError('unsupported instruction: ' + raw)
        return mem, entry

    run_pass(False)          # pass 1: collect labels
    mem, entry = run_pass(True)
    return mem, entry, syms

if __name__ == '__main__':
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument('source'); ap.add_argument('output')
    ap.add_argument('-D', action='append', default=[], help='name=value override')
    a = ap.parse_args()
    defs = {k: int(v, 0) for k, v in (d.split('=') for d in a.D)}
    text = open(a.source).read()
    # command-line -D overrides the equates written in the file
    text = '\n'.join(l for l in text.splitlines()
                     if not any(re.match(r'\s*%s\s*=' % k, l) for k in defs))
    image, entry, symbols = assemble(text, defs)
    open(a.output, 'wb').write(bytes(image))
    print('assembled; entry=$%04X ERROR=$%02X' % (entry, symbols['ERROR']))
