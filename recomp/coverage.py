#!/usr/bin/env python3
"""Coverage report: uncovered text ranges and unresolved bctr per module.

usage: coverage.py <game id> [module]
"""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from recomp import load_modules, apply_hints, game
from analyze import discover

def main(gid, only=None, show=20):
    g = game.load(gid)
    for m in load_modules(game.work_dir(g), g['modules']):
        if only and m.name != only:
            continue
        funcs, entries = discover(m, apply_hints(m, g.get('hints', {}).get(m.name, {})))
        cov = set()
        unres = []
        for f in funcs.values():
            cov |= set(f.insns)
            if f.entry in m.local_indirect:     # bctr/blr become a switch on the hinted targets
                continue
            for a, i in f.insns.items():
                if i is not None and i.name == 'bcctr' and not i.lk and i.unconditional and a not in f.jumptables:
                    unres.append(a)
        gaps = []
        a = m.text[0]
        while a < m.text[1]:
            if a not in cov:
                s = a
                while a < m.text[1] and a not in cov:
                    a += 4
                nz = any(m.word(x) for x in range(s, a, 4))
                if nz:
                    gaps.append((s, a))
            else:
                a += 4
        tot = (m.text[1] - m.text[0]) // 4
        unc = sum((e - s) // 4 for s, e in gaps)
        print(f"{m.name}: text words {tot}, covered {len(cov & set(range(m.text[0], m.text[1], 4)))}, "
              f"non-zero uncovered {unc} in {len(gaps)} gaps; unresolved bctr {len(set(unres))}")
        for s, e in sorted(gaps, key=lambda g: g[0] - g[1])[:show]:
            w = [m.word(x) for x in range(s, min(e, s + 20), 4)]
            print(f"  {s:08x}-{e:08x} ({(e - s) // 4:5d})", ' '.join(f'{x:08x}' for x in w))
        for a in sorted(set(unres))[:show]:
            print(f"  bctr {a:08x}")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)
