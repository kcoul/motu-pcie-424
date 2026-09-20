#!/usr/bin/env python3
"""Resolve %ebx-relative addresses in PIC i386 disassembly.

The 32-bit PIC idiom is:

    calll <next instruction>      ; pushes the address of the next instruction
    popl  %ebx                    ; ebx = that address

so every later `leal disp(%ebx), reg` names the absolute address ebx + disp.
llvm-objdump prints the displacement but not the target, which is why a plain
grep for a string address finds nothing.

Usage: picref.py <disasm.txt> <hex-target> [<hex-target> ...]
"""
import re, sys

asm = open(sys.argv[1], encoding="utf-8", errors="replace").read().splitlines()
targets = {int(t, 16) for t in sys.argv[2:]}

LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(\S+)\s*(.*)$")
FUNC = re.compile(r"^([0-9a-f]+)\s+<([^>]+)>:")
# leal 0x1234(%ebx), %eax   /  movl 0x1234(%ebx), %eax
EBX = re.compile(r"^(?:\$?(-?0x[0-9a-f]+))?\(%ebx\)")
EBXD = re.compile(r"(-?0x[0-9a-f]+)\(%ebx\)")

cur_fn = "?"
ebx = None
pending_call = None
hits = []

for ln in asm:
    m = FUNC.match(ln)
    if m:
        cur_fn = m.group(2)
        ebx = None
        continue
    m = LINE.match(ln)
    if not m:
        continue
    addr = int(m.group(1), 16)
    mnem, ops = m.group(3), m.group(4)

    # PIC base: calll to the immediately following instruction, then popl %ebx
    if mnem.startswith("call"):
        mm = re.match(r"0x([0-9a-f]+)", ops)
        if mm:
            pending_call = int(mm.group(1), 16)
        continue
    if mnem.startswith("pop") and "%ebx" in ops and pending_call == addr:
        ebx = addr
        pending_call = None
        continue
    pending_call = None

    if ebx is None:
        continue
    mm = EBXD.search(ops)
    if mm:
        disp = int(mm.group(1), 16)
        target = (ebx + disp) & 0xFFFFFFFF
        if target in targets:
            hits.append((addr, cur_fn, mnem, ops, target, ebx))

for addr, fn, mnem, ops, target, base in hits:
    print(f"0x{addr:06x}  {fn:44s}  {mnem:6s} {ops:28s} -> 0x{target:06x}  (ebx=0x{base:x})")
print(f"\n{len(hits)} reference(s)")
