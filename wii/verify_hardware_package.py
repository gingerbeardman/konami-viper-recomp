"""Audit Wii benchmark ZIP assets against every compiled SD path and checksums."""
import argparse
import hashlib
from pathlib import Path
import re
import struct
import zipfile
from xml.etree import ElementTree as ET

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('zip',type=Path)
a=p.parse_args()
outputs={'viper/boot.log','viper/ram.bin','viper/efb.bin'}
sizes={'viper/bios.bin':262144,'viper/nvram.bin':8192,
       'viper/ds2430.bin':40,'viper/menu_font.a8':131072}
with zipfile.ZipFile(a.zip) as z:
    assert z.testzip() is None,'ZIP CRC error'
    names=z.namelist();assert len(names)==len(set(names)),'duplicate archive entries'
    assert all(not n.startswith('/') and '..' not in Path(n).parts for n in names),'unsafe archive path'
    dols=[n for n in names if n.startswith('apps/') and n.endswith('/boot.dol')]
    assert len(dols)==3,'expected three apps'
    required=set()
    for name in dols:
        data=z.read(name)
        offsets=struct.unpack_from('>18I',data,0);lengths=struct.unpack_from('>18I',data,0x90)
        assert all(not size or (offset>=256 and offset+size<=len(data)) for offset,size in zip(offsets,lengths)),name+' invalid DOL section'
        paths={s.decode()[4:] for s in re.findall(rb'sd:/[a-zA-Z0-9_./-]+',data)}
        required|={s for s in paths if not s.endswith('/') and s not in outputs}
        meta=ET.fromstring(z.read(str(Path(name).with_name('meta.xml'))))
        assert meta.tag=='app' and meta.findtext('name') and meta.findtext('long_description'),name+' metadata missing'
    for name in sorted(required):
        assert name in names,'missing runtime input: '+name
        data=z.read(name);assert data,'empty input: '+name
        if name in sizes:assert len(data)==sizes[name],'wrong input size: '+name
        if name=='viper/kernel.bin':assert len(data)<=16777216,'kernel exceeds guest RAM'
        if name=='viper/cf.img':assert len(data)%512==0,'CF image is not sector aligned'
    manifest={}
    for line in z.read('SHA256SUMS.txt').decode().splitlines():
        digest,name=line.split('  ',1);assert name not in manifest,'duplicate manifest entry'
        assert hashlib.sha256(z.read(name)).hexdigest()==digest,'checksum mismatch: '+name
        manifest[name]=digest
    assert set(manifest)==set(names)-{'SHA256SUMS.txt'},'manifest coverage incomplete'
    print('PASS: three DOLs, metadata, all compiled runtime inputs, sizes, checksums, ZIP CRC')
    print('Runtime inputs: '+', '.join(sorted(required)))
    print('Generated outputs (not inputs): '+', '.join(sorted(outputs)))
