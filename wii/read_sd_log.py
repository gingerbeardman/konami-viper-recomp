"""Read viper/boot.log from Dolphin's FAT32 SD image without modifying it."""
import argparse
import struct
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('image',type=Path)
p.add_argument('--file',default='boot.log',help='8.3 file in viper directory (boot.log, ram.bin, efb.bin, pgo.bin, capNNNNN.bin)')
p.add_argument('--output',type=Path,help='write exact bytes instead of printing decoded text')
p.add_argument('--tail',action='store_true',help='also read past the recorded size and FAT chain end, through contiguous non-empty clusters (a log a hang left unclosed)')
a=p.parse_args()
with a.image.open('rb') as f:
    boot=f.read(512)
    sector=struct.unpack_from('<H',boot,11)[0]
    cluster_sectors=boot[13]
    reserved=struct.unpack_from('<H',boot,14)[0]
    fats=boot[16]
    fat_sectors=struct.unpack_from('<I',boot,36)[0]
    root=struct.unpack_from('<I',boot,44)[0]
    if boot[82:90]!=b'FAT32   ' or sector!=512 or not cluster_sectors:
        p.error('expected unpartitioned FAT32 image with 512-byte sectors')
    size=cluster_sectors*sector
    data=(reserved+fats*fat_sectors)*sector
    def chain(c):
        seen=set()
        while 2<=c<0x0ffffff8:
            if c in seen:raise ValueError('cyclic FAT chain')
            seen.add(c)
            pos=data+(c-2)*size
            if pos+size>a.image.stat().st_size:raise ValueError('cluster outside image')
            f.seek(pos);yield f.read(size)
            f.seek(reserved*sector+c*4)
            raw=f.read(4)
            if len(raw)!=4:raise ValueError('truncated FAT')
            c=struct.unpack('<I',raw)[0]&0x0fffffff
    def find(c,name):
        for block in chain(c):
            for i in range(0,len(block),32):
                e=block[i:i+32]
                if e[0]==0:raise ValueError(f'missing {name!r}')
                if e[0]==0xe5 or e[11]==0x0f:continue
                if e[:11]==name:
                    cluster=(struct.unpack_from('<H',e,20)[0]<<16)|struct.unpack_from('<H',e,26)[0]
                    return cluster,struct.unpack_from('<I',e,28)[0]
        raise ValueError(f'missing {name!r}')
    directory,_=find(root,b'VIPER      ')
    log,length=find(directory,(lambda n,e:(n.upper().ljust(8)+e.upper().ljust(3)).encode())(*a.file.split('.')))
    content=b''.join(chain(log))[:length] if length else b''
    if a.tail:
        blocks=list(chain(log));last=log
        for _ in chain(log):pass
        c=log
        while True:
            f.seek(reserved*sector+c*4);n=struct.unpack('<I',f.read(4))[0]&0x0fffffff
            if not 2<=n<0x0ffffff8:break
            c=n
        extra=[]
        for k in range(1,4096):
            f.seek(data+(c+k-2)*size);b=f.read(size)
            if not b.strip(b'\0'):break
            extra.append(b)
        content=(b''.join(blocks)+b''.join(extra)).rstrip(b'\0')
    if a.output:
        a.output.write_bytes(content)
    else:
        if a.file!='boot.log':p.error('--output is required for binary snapshots')
        print(content.decode('utf-8',errors='replace'),end='')
