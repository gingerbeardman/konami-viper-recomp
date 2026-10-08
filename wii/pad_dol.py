"""Append zeros needed for Dolphin's aligned DOL section reads."""
import struct
import sys
from pathlib import Path

p = Path(sys.argv[1])
data = p.read_bytes()
if len(data) < 256:
    raise ValueError('truncated DOL header')
h = struct.unpack('>64I', data[:256])
required = 256
for i in range(18):
    size = h[36 + i]
    if not size:
        continue
    if h[i] + size > len(data):
        raise ValueError('truncated DOL section')
    required = max(required, h[i] + ((size + 31) & ~31))
if required > len(data):
    p.write_bytes(data + bytes(required - len(data)))
