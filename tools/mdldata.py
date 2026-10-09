#!/usr/bin/env python3
"""GTI Club 2 model data (game/mdldata) and CF load tracing.

Model groups ("sections") are listed in game/mdldata/secname.zin; each is a pair of files
game/mdldata/<SEC>_mdl.zin and <SEC>_tex.zin, found in the extracted fs/_unk/ by name_hash.
A _mdl.zin container is:
  u32 nmodels, u32 ntex, ntex NUL-terminated texture names, pad to 4,
  u32 size[nmodels], then the model blobs back to back.
Every blob starts with u16 ?, u16 0x154a/0x144a/0x080a, f32 100.0, followed by int16 vertices.

  mdldata.py sections GAME              section list with model/texture counts
  mdldata.py models GAME SEC            texture names and model sizes of one section
  mdldata.py names GAME [PREFIX]        model names from mdlname.zin
  mdldata.py cflog GAME LOG [T0 T1]     map RT_CF_LOG=1 reads to files, merging runs per file
"""
import json, os, re, struct, sys
sys.path.insert(0, os.path.dirname(__file__))
from viper_fs import name_hash

ROOT = os.path.join(os.path.dirname(__file__), '..')

def fs_dir(game): return os.path.join(ROOT, 'work', game, 'fs')

def manifest(game):
    return json.load(open(os.path.join(fs_dir(game), 'manifest.json')))

def read_file(game, path):
    """Extracted contents of a CF file by its path (resolved through the name hash)."""
    h = name_hash(path)
    for e in manifest(game):
        if int(e['hash'], 16) == h:
            return open(os.path.join(fs_dir(game), e['name']), 'rb').read()
    raise FileNotFoundError(path)

def strings(data, n=3):
    return [m.group().decode() for m in re.finditer(rb'[\x20-\x7e]{%d,}' % n, data)]

def sections(game):
    return strings(read_file(game, 'game/mdldata/secname.zin'))

def parse(data):
    nm, nt = struct.unpack_from('>II', data, 0)
    p, tex = 8, []
    for _ in range(nt):
        e = data.index(b'\0', p)
        tex.append(data[p:e].decode())
        p = e + 1
    p = (p + 3) & ~3
    sizes = struct.unpack_from('>%dI' % nm, data, p)
    p += 4 * nm
    blobs = []
    for s in sizes:
        blobs.append(data[p:p + s])
        p += s
    if p != len(data):
        raise ValueError(f'container ends at {p:#x}, file is {len(data):#x}')
    return tex, blobs

def cmd_sections(game):
    for sec in sections(game):
        tex, blobs = parse(read_file(game, f'game/mdldata/{sec}_mdl.zin'))
        t = read_file(game, f'game/mdldata/{sec}_tex.zin')
        print(f'{sec:14s} models={len(blobs):3d} textures={len(tex):2d} tex_file={len(t):#9x}')

def cmd_models(game, sec):
    tex, blobs = parse(read_file(game, f'game/mdldata/{sec}_mdl.zin'))
    print('textures:', ' '.join(tex))
    for i, b in enumerate(blobs):
        print(f'{i:3d} size={len(b):#7x} head={b[:16].hex()}')

def cmd_names(game, prefix=''):
    for n in sorted(strings(read_file(game, 'game/mdldata/mdlname.zin'))):
        if n.startswith(prefix): print(n)

def gma_lba(game):
    """First CF sector of the game image (directory sectors are relative to it)."""
    cf = os.path.join(ROOT, 'work', game, 'cf')
    head = open(os.path.join(cf, os.listdir(cf)[0]), 'rb').read(512)
    with open(os.path.join(ROOT, 'work', game, 'cf.img'), 'rb') as img:
        for lba in range(0, 0x10000):
            img.seek(lba * 512)
            if img.read(512) == head: return lba
    raise ValueError('game image not found in cf.img')

def cmd_cflog(game, log, t0=0.0, t1=1e9):
    base = gma_lba(game)
    ents = sorted((int(e['sector'], 16) + base, int(e['size'], 16), e['name']) for e in manifest(game))
    def owner(lba):
        for s, size, name in ents:
            if s <= lba < s + (size + 511) // 512: return name
        return f'?lba {lba >> 7 << 7:#x}'
    runs = []
    for m in re.finditer(r'CF: read lba=0x([0-9a-f]+) count=(\d+) t=([\d.]+)', open(log).read()):
        t = float(m.group(3))
        if not t0 <= t <= t1: continue
        name = owner(int(m.group(1), 16))
        if runs and runs[-1][1] == name:
            runs[-1][2] += int(m.group(2)); runs[-1][3] = t
        else:
            runs.append([t, name, int(m.group(2)), t])
    for t, name, count, end in runs:
        print(f'{t:8.2f}-{end:8.2f} {name:30s} {count * 512:#x} bytes')

if __name__ == '__main__':
    a = sys.argv[1:]
    cmds = {'sections': cmd_sections, 'models': cmd_models, 'names': cmd_names, 'cflog': cmd_cflog}
    if not a or a[0] not in cmds:
        sys.exit(__doc__)
    args = a[1:]
    if a[0] == 'cflog': args = args[:2] + [float(x) for x in args[2:]]
    cmds[a[0]](*args)
