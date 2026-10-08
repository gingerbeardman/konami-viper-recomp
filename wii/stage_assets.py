"""Stage existing original assets in the isolated Dolphin SD folder."""
import argparse
import hashlib
import json
import shutil
from pathlib import Path

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('source',type=Path,help='directory containing original boot assets')
parser.add_argument('--menu-font',type=Path,help='optional original 256x512 alpha font atlas')
args=parser.parse_args()
root=Path(__file__).resolve().parent.parent
target=root/'build/wii/dolphin-user/Load/WiiSDSync/viper'
names=('kernel.bin','bios.bin','nvram.bin','ds2430.bin','cf.img')
for name in names:
    if not (args.source/name).is_file():parser.error(f'missing {name}')
font=args.menu_font or args.source/'menu_font.a8'
if args.menu_font and not font.is_file():parser.error('missing menu font')
if font.is_file() and font.stat().st_size!=256*512:parser.error('menu font must be 128 KiB')
target.mkdir(parents=True,exist_ok=True)
manifest={}
for name in names:
    source=args.source/name
    shutil.copyfile(source,target/name)
    manifest[name]={'bytes':source.stat().st_size,'sha256':hashlib.sha256(source.read_bytes()).hexdigest()}
if font.is_file():
    shutil.copyfile(font,target/'menu_font.a8')
    manifest['menu_font.a8']={'bytes':font.stat().st_size,'sha256':hashlib.sha256(font.read_bytes()).hexdigest()}
(root/'build/wii/sd-assets.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(target)
