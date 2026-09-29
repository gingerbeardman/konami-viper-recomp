#!/usr/bin/env python3
"""Extract every file from the FAT12/16 partition of a raw CF image (Konami Viper)."""
import struct, sys, os

def main(img_path, out_dir):
    img = open(img_path, 'rb').read()
    ptype = img[0x1c2]
    lba = struct.unpack_from('<I', img, 0x1c6)[0]
    base = lba * 512
    bs = img[base:base+512]
    bps, spc, rsv, nfats, nroot, tot16, media, spf = struct.unpack_from('<HBHBHHBH', bs, 11)
    tot = tot16 or struct.unpack_from('<I', bs, 32)[0]
    print(f"partition type {ptype:#x} at LBA {lba}; bps={bps} spc={spc} rsv={rsv} fats={nfats} root={nroot} spf={spf} total={tot}")
    print("OEM:", bs[3:11], "label:", bs[43:54], "fs:", bs[54:62])
    fat_off = base + rsv*bps
    root_off = fat_off + nfats*spf*bps
    data_off = root_off + ((nroot*32 + bps-1)//bps)*bps
    clus = spc*bps
    nclusters = (tot - (data_off-base)//bps)//spc
    fat16 = nclusters >= 4085
    fat = img[fat_off:fat_off+spf*bps]

    def nxt(c):
        if fat16:
            return struct.unpack_from('<H', fat, c*2)[0]
        v = struct.unpack_from('<H', fat, c*3//2)[0]
        return (v >> 4) if c & 1 else (v & 0xfff)
    eoc = 0xfff8 if fat16 else 0xff8

    def chain(c):
        out = []
        while 2 <= c < eoc:
            out.append(c); c = nxt(c)
        return out

    def read_chain(c, size=None):
        d = b''.join(img[data_off+(x-2)*clus: data_off+(x-1)*clus] for x in chain(c))
        return d if size is None else d[:size]

    listing = []
    def walk(raw, path):
        lfn = ''
        for i in range(0, len(raw), 32):
            e = raw[i:i+32]
            if e[0] == 0: break
            if e[0] == 0xe5: lfn = ''; continue
            attr = e[11]
            if attr == 0x0f:
                part = (e[1:11] + e[14:26] + e[28:32]).decode('utf-16le', 'ignore').split('\x00')[0]
                lfn = part + lfn; continue
            name = e[0:8].decode('latin1').rstrip(); ext = e[8:11].decode('latin1').rstrip()
            short = name + ('.' + ext if ext else '')
            nm = lfn or short; lfn = ''
            if attr & 0x08 or nm in ('.', '..'): continue
            c = struct.unpack_from('<H', e, 26)[0]
            size = struct.unpack_from('<I', e, 28)[0]
            p = os.path.join(path, nm)
            if attr & 0x10:
                os.makedirs(os.path.join(out_dir, p), exist_ok=True)
                walk(read_chain(c), p)
            else:
                data = read_chain(c, size)
                with open(os.path.join(out_dir, p), 'wb') as f: f.write(data)
                listing.append((p, size, c))
    os.makedirs(out_dir, exist_ok=True)
    walk(img[root_off:data_off], '')
    for p, s, c in listing:
        print(f"{s:10d}  clus={c:5d}  {p}")
    print(f"{len(listing)} files, {sum(s for _,s,_ in listing)} bytes")

if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2])
