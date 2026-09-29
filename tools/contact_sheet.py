#!/usr/bin/env python3
"""Contact sheet of dumped PPM frames: contact_sheet.py DIR OUT.png FIRST LAST STEP [COLS]
(frame numbers; thumbnails at 1/2 size, labelled by frame number in the file name order)."""
import os, subprocess, sys

def read_ppm(p):
    d = open(p, 'rb').read()
    parts = d.split(b'\n', 3)
    w, h = map(int, parts[1].split())
    return w, h, parts[3]

def main(d, out, first, last, step, cols=4):
    frames = [f for f in range(first, last + 1, step) if os.path.exists(f'{d}/frame_{f:06d}.ppm')]
    tw, th = 256, 192
    rows = (len(frames) + cols - 1) // cols
    W, H = cols * tw, rows * th
    img = bytearray(W * H * 3)
    for k, f in enumerate(frames):
        w, h, px = read_ppm(f'{d}/frame_{f:06d}.ppm')
        ox, oy = (k % cols) * tw, (k // cols) * th
        for y in range(th):
            sy = y * h // th
            for x in range(tw):
                sx = x * w // tw
                s = 3 * (sy * w + sx)
                t = 3 * ((oy + y) * W + ox + x)
                img[t:t + 3] = px[s:s + 3]
    tmp = out + '.ppm'
    with open(tmp, 'wb') as fo:
        fo.write(b'P6\n%d %d\n255\n' % (W, H) + bytes(img))
    subprocess.run(['sips', '-s', 'format', 'png', tmp, '--out', out], capture_output=True)
    os.remove(tmp)
    print(out, [f for f in frames])

if __name__ == '__main__':
    a = sys.argv
    main(a[1], a[2], int(a[3]), int(a[4]), int(a[5]), int(a[6]) if len(a) > 6 else 4)
