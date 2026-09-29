#!/usr/bin/env python3
"""Tiny PowerPC (big-endian, 32-bit) disassembler: ppcdis.py FILE FILE_BASE_ADDR START_ADDR COUNT"""
import sys
from capstone import Cs, CS_ARCH_PPC, CS_MODE_32, CS_MODE_BIG_ENDIAN
data = open(sys.argv[1], 'rb').read()
base = int(sys.argv[2], 16); start = int(sys.argv[3], 16); n = int(sys.argv[4])
md = Cs(CS_ARCH_PPC, CS_MODE_32 | CS_MODE_BIG_ENDIAN)
off = start - base
for i in range(n):
    a = off + i*4
    w = data[a:a+4]
    if len(w) < 4: break
    ins = list(md.disasm(w, start + i*4))
    txt = f"{ins[0].mnemonic} {ins[0].op_str}" if ins else ".long"
    print(f"{start+i*4:08x}: {w.hex()}  {txt}")
