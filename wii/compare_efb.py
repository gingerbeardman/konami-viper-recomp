"""Exact comparison of row-major RGBA8 + Z32BE checkpoint captures."""
import argparse
import hashlib
from pathlib import Path

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('baseline', type=Path)
parser.add_argument('candidate', type=Path)
args = parser.parse_args()
images = [path.read_bytes() for path in (args.baseline, args.candidate)]
expected = 640 * 480 * 8
for path, data in zip((args.baseline, args.candidate), images):
    if len(data) != expected:
        parser.error(f'{path}: expected {expected} bytes, got {len(data)}')
    print(f'{path}: sha256={hashlib.sha256(data).hexdigest()}')
color = depth = maximum = 0
first = None
for offset in range(0, expected, 8):
    a, b = (data[offset:offset+8] for data in images)
    color += a[:4] != b[:4]
    depth += a[4:] != b[4:]
    maximum = max(maximum, *(abs(a[i]-b[i]) for i in range(4)))
    if first is None and a != b:
        pixel = offset // 8
        first = (pixel % 640, pixel // 640)
print(f'color_pixels_changed={color} depth_pixels_changed={depth} max_channel_error={maximum} first_difference={first}')
raise SystemExit(1 if color or depth else 0)
