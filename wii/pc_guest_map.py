"""Map a hardware PC profile (boot.log "VIPER WII PC bin=ADDR count=N", best
with WII_PERF_PC_BUCKET_BYTES=4) onto guest instructions, using a build with
debug line info (WII_PERF_DEBUG_LINES=1): host address -> generated C line ->
the nearest preceding guest-instruction comment (/* 000290d0: ... */) or,
in specialised (localized) code, guest block label (L_000290d0:).

Run: python3 wii/pc_guest_map.py ELF BOOT_LOG [--function f_gl_00028fc4]
     [--from 0x28fc4 --to 0x29afc] [--top 40]
Prints samples per guest instruction (with its disassembly when available)
and per guest address range of consecutive hot instructions."""
import argparse
import bisect
import re
import subprocess
from collections import Counter, defaultdict
from pathlib import Path

ADDR2LINE = '/opt/homebrew/opt/llvm/bin/llvm-addr2line'
NM = 'nm'


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument('elf', type=Path)
    p.add_argument('log', type=Path)
    p.add_argument('--function', default='f_gl_00028fc4')
    p.add_argument('--top', type=int, default=40)
    p.add_argument('--root', type=Path, default=Path(__file__).resolve().parent.parent)
    a = p.parse_args()

    bins = [(int(x, 16), int(n)) for x, n in re.findall(r'PC bin=([0-9a-f]+) count=(\d+)', a.log.read_text(errors='replace'))]
    total = sum(n for _, n in bins)
    syms = []
    for line in subprocess.run([NM, '-n', '-S', str(a.elf)], capture_output=True, text=True).stdout.splitlines():
        f = line.split()
        if len(f) == 4 and f[2] in 'tTwW':
            syms.append((int(f[0], 16), int(f[1], 16), f[3]))
    fn = next(s for s in syms if s[2] == a.function)
    lo, hi = fn[0], fn[0] + fn[1]
    inside = [(addr, n) for addr, n in bins if lo <= addr < hi]
    fn_total = sum(n for _, n in inside)
    print(f'{a.function}: {fn_total} of {total} samples ({100 * fn_total / max(total, 1):.1f}%)')

    # host address -> source file:line (one batch call)
    out = subprocess.run([ADDR2LINE, '-e', str(a.elf)] + [hex(x) for x, _ in inside],
                         capture_output=True, text=True).stdout.splitlines()
    sources: dict[str, list[str]] = {}

    def guest_for(loc: str):
        m = re.match(r'(.*):(\d+)', loc)
        if not m or m.group(1) == '??':
            return None
        path, ln = m.group(1), int(m.group(2))
        local = Path(path.replace('/src/', str(a.root) + '/', 1)) if path.startswith('/src/') else a.root / path
        if str(local) not in sources:
            try:
                sources[str(local)] = local.read_text(errors='replace').split('\n')
            except OSError:
                sources[str(local)] = []
        lines = sources[str(local)]
        for i in range(min(ln, len(lines)) - 1, max(ln - 3000, 0), -1):
            g = re.search(r'/\* ([0-9a-f]{8}): ', lines[i]) or re.search(r'\bL_([0-9a-f]{8}):', lines[i])
            if g:
                return int(g.group(1), 16)
        return None

    per_guest = Counter()
    unmapped = 0
    for (addr, n), loc in zip(inside, out):
        g = guest_for(loc)
        if g is None:
            unmapped += n
        else:
            per_guest[g] += n
    print(f'unmapped (no guest comment above the line): {unmapped}')

    dis = {}
    dpath = a.root / 'build/wii/28fc4.dis'
    if dpath.exists():
        for l in dpath.read_text().splitlines():
            f = l.split(None, 1)
            if len(f) == 2:
                dis[int(f[0], 16)] = f[1].strip()
    print('\nhottest guest instructions:')
    for g, n in per_guest.most_common(a.top):
        print(f'  {g:08x} {n:5d} {100 * n / max(fn_total, 1):5.1f}%  {dis.get(g, "")}')
    # ranges: group by 0x40-byte guest blocks
    blocks = defaultdict(int)
    for g, n in per_guest.items():
        blocks[g & ~0x3f] += n
    print('\nby 64-byte guest block:')
    for b in sorted(blocks):
        n = blocks[b]
        if n * 100 >= fn_total:
            print(f'  {b:08x}-{b + 0x3f:08x} {n:5d} {100 * n / max(fn_total, 1):5.1f}%')


if __name__ == '__main__':
    main()
