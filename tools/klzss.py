#!/usr/bin/env python3
"""Konami Viper LZSS decompressor (reimplementation of BIOS routine @ 0xFFF01708).

Stream: flag byte, consumed LSB first. bit=1 -> literal byte.
bit=0 -> 2-byte match: b0,b1; len=(b0&0xF)+3, dist=((b0&0xF0)<<4)|b1 ; dist==0 -> end.
"""
import sys

def decompress(src, pos=0, limit=None):
    out = bytearray()
    while True:
        flags = src[pos]; pos += 1
        for _ in range(8):
            if flags & 1:
                out.append(src[pos]); pos += 1
            else:
                b0, b1 = src[pos], src[pos+1]; pos += 2
                dist = ((b0 & 0xF0) << 4) | b1
                if dist == 0:
                    return bytes(out), pos
                n = (b0 & 0xF) + 3
                s = len(out) - dist
                for i in range(n):
                    out.append(out[s+i])
            flags >>= 1
            if limit and len(out) >= limit:
                return bytes(out), pos

if __name__ == '__main__':
    data = open(sys.argv[1], 'rb').read()
    off = int(sys.argv[2], 16)
    out, end = decompress(data, off)
    open(sys.argv[3], 'wb').write(out)
    print(f"in {off:#x}..{end:#x} ({end-off} bytes) -> out {len(out)} bytes")
