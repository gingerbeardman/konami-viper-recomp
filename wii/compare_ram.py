"""Locate drift between two complete guest RAM snapshots (big-endian words)."""
import argparse
import hashlib
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('baseline', type=Path)
p.add_argument('candidate', type=Path)
a = p.parse_args()
old, new = a.baseline.read_bytes(), a.candidate.read_bytes()
if len(old) != 0x1000000 or len(new) != len(old):
    p.error('both snapshots must contain exactly 16 MiB of guest RAM')
for name, data in ((a.baseline, old), (a.candidate, new)):
    print(f'{name}: sha256={hashlib.sha256(data).hexdigest()}')
changed = sum(x != y for x, y in zip(old, new))
words = [i for i in range(0, len(old), 4) if old[i:i+4] != new[i:i+4]]
print(f'Changed bytes={changed}, aligned words={len(words)}')
print('Changed 64-KiB regions:')
for base in range(0, len(old), 65536):
    n = sum(x != y for x, y in zip(old[base:base+65536], new[base:base+65536]))
    if n:
        print(f'  {base:08x}-{base+65535:08x}: {n} bytes')
print('First 32 changed words (float interpretations are not type evidence):')
for i in words[:32]:
    x, y = old[i:i+4], new[i:i+4]
    xf, yf = struct.unpack('>f', x)[0], struct.unpack('>f', y)[0]
    print(f'  {i:08x}: {x.hex()} -> {y.hex()}  floats {xf:.9g} -> {yf:.9g}')
