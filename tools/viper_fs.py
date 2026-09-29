#!/usr/bin/env python3
"""Konami Viper game-image filesystem (GMAxxx.BIN on the CF FAT16 partition).

Layout (reverse engineered from BIOS 941B01 stage-2 and the RAM-0 loader):
  sector 0      : boot block. +0x000 signature, +0x100 descriptor (see BIOS fe12dc):
                  {?, ?, start_sector, comp_len, byte_sum, load_addr|0x80000000(compressed), entry}
  0x200..       : directory, N entries of 6 BE u32, sorted by hash, terminated by zeros:
                  {name_hash, sector(512B), stored_len, load_addr, byte_sum(stored), unpacked_len (0=raw)}
  name_hash     : CRC-32 poly 0x04C11DB7, feeding 6 bits (LSB first) of each char (loader @0xE9B8)
  compression   : Konami LZSS (see klzss.py)
"""
import struct, sys, os, json
sys.path.insert(0, os.path.dirname(__file__))
from klzss import decompress

def name_hash(name):
    r = 0
    for ch in name.encode():
        c = ch if ch < 128 else ch - 256
        for b in range(6):
            nb = (((r << 1) & 0xfffffffe) | ((c >> b) & 1)) & 0xffffffff
            r = nb ^ (0x04c11db7 if r & 0x80000000 else 0)
    return r

def read_dir(img):
    ents = []
    for off in range(0x200, 0x2000, 0x18):
        e = struct.unpack_from('>6I', img, off)
        if e == (0,) * 6:
            break
        ents.append(dict(hash=e[0], sector=e[1], size=e[2], load=e[3], sum=e[4], unpacked=e[5]))
    return ents

def main(img_path, out_dir, names_file=None):
    img = open(img_path, 'rb').read()
    names = {}
    for nf in ([names_file] if isinstance(names_file, str) else names_file or []):
        if os.path.exists(nf):
            for n in open(nf).read().split():
                names[name_hash(n)] = n
    os.makedirs(out_dir, exist_ok=True)
    manifest = []
    for e in read_dir(img):
        raw = img[e['sector'] * 512: e['sector'] * 512 + e['size']]
        ok = (sum(raw) & 0xffffffff) == e['sum']
        data = decompress(raw)[0] if e['unpacked'] else raw
        if e['unpacked'] and len(data) != e['unpacked']:
            ok = False
        name = names.get(e['hash'], f"_unk/{e['hash']:08x}.bin")
        p = os.path.join(out_dir, name)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data)
        manifest.append(dict(name=name, **{k: f"{v:#x}" for k, v in e.items()}, ok=ok, out_len=len(data)))
        print(f"{e['hash']:08x} sec={e['sector']:#07x} size={e['size']:#08x} load={e['load']:#08x} "
              f"unp={e['unpacked']:#08x} {'OK ' if ok else 'BAD'} {name}")
    json.dump(manifest, open(os.path.join(out_dir, 'manifest.json'), 'w'), indent=1)
    print(len(manifest), 'files,', sum(1 for m in manifest if m['ok']), 'verified')

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2], sys.argv[3] if len(sys.argv) > 3 else None)
