"""Homebrew Channel icon for a Wii build variant: the base icon
(wii/icons/<game>.png, 128x48) with a short variant label stamped in its
bottom-left corner, on the baseline of the Japanese title and in its blue,
using a built-in 4x7 pixel font (no system fonts needed).
Run: python3 wii/make_icon.py LABEL OUT.png [--base wii/icons/gticlub2.png] [--bold] [--left N]
     e.g. python3 wii/make_icon.py "1:1 N" apps/viper-gticlub2-1to1-nearest/icon.png"""
import argparse
from pathlib import Path

from PIL import Image

# 4x7 glyphs, rows top to bottom, '#' = ink.
GLYPHS = {
    'A': [' ## ', '#  #', '#  #', '####', '#  #', '#  #', '#  #'],
    'B': ['### ', '#  #', '#  #', '### ', '#  #', '#  #', '### '],
    'C': [' ## ', '#  #', '#   ', '#   ', '#   ', '#  #', ' ## '],
    'D': ['### ', '#  #', '#  #', '#  #', '#  #', '#  #', '### '],
    'E': ['####', '#   ', '#   ', '### ', '#   ', '#   ', '####'],
    'F': ['####', '#   ', '#   ', '### ', '#   ', '#   ', '#   '],
    'G': [' ## ', '#  #', '#   ', '# ##', '#  #', '#  #', ' ###'],
    'H': ['#  #', '#  #', '#  #', '####', '#  #', '#  #', '#  #'],
    'I': [' ###', '  # ', '  # ', '  # ', '  # ', '  # ', ' ###'],
    'J': ['  ##', '   #', '   #', '   #', '   #', '#  #', ' ## '],
    'K': ['#  #', '# # ', '##  ', '#   ', '##  ', '# # ', '#  #'],
    'L': ['#   ', '#   ', '#   ', '#   ', '#   ', '#   ', '####'],
    'M': ['#  #', '####', '####', '#  #', '#  #', '#  #', '#  #'],
    'N': ['#  #', '## #', '## #', '# ##', '# ##', '#  #', '#  #'],
    'O': [' ## ', '#  #', '#  #', '#  #', '#  #', '#  #', ' ## '],
    'P': ['### ', '#  #', '#  #', '### ', '#   ', '#   ', '#   '],
    'Q': [' ## ', '#  #', '#  #', '#  #', '# ##', '#  #', ' ## #'[:4]],
    'R': ['### ', '#  #', '#  #', '### ', '# # ', '#  #', '#  #'],
    'S': [' ###', '#   ', '#   ', ' ## ', '   #', '   #', '### '],
    'T': ['####', ' #  ', ' #  ', ' #  ', ' #  ', ' #  ', ' #  '],
    'U': ['#  #', '#  #', '#  #', '#  #', '#  #', '#  #', ' ## '],
    'V': ['#  #', '#  #', '#  #', '#  #', '#  #', ' ## ', ' ## '],
    'W': ['#  #', '#  #', '#  #', '#  #', '####', '####', '#  #'],
    'X': ['#  #', '#  #', ' ## ', ' ## ', ' ## ', '#  #', '#  #'],
    'Y': ['#  #', '#  #', '#  #', ' ## ', ' #  ', ' #  ', ' #  '],
    'Z': ['####', '   #', '  # ', ' #  ', '#   ', '#   ', '####'],
    '0': [' ## ', '#  #', '# ##', '#  #', '## #', '#  #', ' ## '],
    '1': ['  # ', ' ## ', '  # ', '  # ', '  # ', '  # ', ' ###'],
    '2': [' ## ', '#  #', '   #', '  # ', ' #  ', '#   ', '####'],
    '3': ['### ', '   #', '   #', ' ## ', '   #', '   #', '### '],
    '4': ['#  #', '#  #', '#  #', '####', '   #', '   #', '   #'],
    '5': ['####', '#   ', '#   ', '### ', '   #', '   #', '### '],
    '6': [' ## ', '#   ', '#   ', '### ', '#  #', '#  #', ' ## '],
    '7': ['####', '   #', '   #', '  # ', '  # ', ' #  ', ' #  '],
    '8': [' ## ', '#  #', '#  #', ' ## ', '#  #', '#  #', ' ## '],
    '9': [' ## ', '#  #', '#  #', ' ###', '   #', '   #', ' ## '],
    ':': ['    ', ' #  ', ' #  ', '    ', ' #  ', ' #  ', '    '],
    '-': ['    ', '    ', '    ', '####', '    ', '    ', '    '],
    '+': ['    ', ' #  ', ' #  ', '### ', ' #  ', ' #  ', '    '],
    ' ': ['  ', '  ', '  ', '  ', '  ', '  ', '  '],
}
INK = (40, 57, 150)        # the blue of the Japanese title
X0, TOP = 3, 35            # left edge; the Japanese title occupies rows 35-42
LIMIT = 44                 # the Japanese title starts at x = 47


def stamp(base: Path, label: str, out: Path, bold: bool = False, left: int = X0) -> None:
    """bold: each stroke two pixels wide (glyphs one column wider)."""
    im = Image.open(base).convert('RGB')
    x = left
    for ch in label.upper():
        glyph = GLYPHS.get(ch)
        if glyph is None:
            raise SystemExit(f'no glyph for {ch!r}')
        width = len(glyph[0]) + (1 if bold else 0)
        if x + width - 1 > LIMIT:
            raise SystemExit(f'label {label!r} is too wide for the corner (max ~8 characters)')
        for row, bits in enumerate(glyph):
            for col, bit in enumerate(bits):
                if bit == '#':
                    im.putpixel((x + col, TOP + row), INK)
                    if bold:
                        im.putpixel((x + col + 1, TOP + row), INK)
        x += width + 1
    out.parent.mkdir(parents=True, exist_ok=True)
    im.save(out)


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('label')
    p.add_argument('out', type=Path)
    p.add_argument('--base', type=Path, default=Path(__file__).resolve().parent / 'icons' / 'gticlub2.png')
    p.add_argument('--bold', action='store_true', help='strokes two pixels wide')
    p.add_argument('--left', type=int, default=X0, help=f'left edge in pixels (default {X0})')
    a = p.parse_args()
    stamp(a.base, a.label, a.out, a.bold, a.left)
