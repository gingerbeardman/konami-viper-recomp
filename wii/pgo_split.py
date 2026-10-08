"""Split sd:/viper/pgo.bin (wii/pgo_dump.c) into .gcda files for -fprofile-use.
Each object's gcda path was fixed at compile time inside the build container
(/src = repository root); it is mapped back onto this checkout.
Run: python3 wii/pgo_split.py pgo.bin [--root REPO]"""
import argparse
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('stream', type=Path)
p.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
a = p.parse_args()
data = a.stream.read_bytes()
pos, name, chunks, written = 0, None, [], 0
while pos < len(data):
    tag = data[pos:pos + 1]
    n = struct.unpack_from('>I', data, pos + 1)[0]
    body = data[pos + 5:pos + 5 + n]
    pos += 5 + n
    if tag == b'F':
        name, chunks = body.decode(), []
    elif tag == b'D':
        chunks.append(body)
    elif tag == b'E':
        if not name:
            raise ValueError('object without a filename')
        rel = name[5:] if name.startswith('/src/') else name.lstrip('/')
        out = a.root / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(b''.join(chunks))
        written += 1
        name = None
    else:
        raise ValueError(f'bad record tag {tag!r} at {pos}')
print(f'{written} gcda files written under {a.root}')
