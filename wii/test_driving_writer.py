"""Execute original and batched second vertex writer against full device state."""
import argparse
from pathlib import Path
import re
import sys
import subprocess
import tempfile
from specialize_driving_writer import regions

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-c', type=Path)
parser.add_argument('--emit-native', type=Path)
parser.add_argument('--tail', action='store_true')
args = parser.parse_args()
if args.tail:
    from specialize_driving_tail import regions
_, _, original, candidate = regions((root/'generated/gticlub2/gl_000.c').read_text())
candidate = candidate.replace('uint32_t driving_words[10];', 'admissions++;uint32_t driving_words[10];')
fixture = (root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)', 'void device_contract_main(void)')
code = '#include "ppc_rt.h"\n'+fixture+'\nstatic unsigned admissions;\nvoid reference(PPCContext *c){\n'+original+'}\nvoid candidate(PPCContext *c){\n'+candidate+'}\n'+(root/'wii/driving_writer_probe_body.c').read_text()
if args.tail:
    code = code.replace('a.r[4]=0x00000080;', 'a.r[6]=0x11223344;a.r[4]=0x00000080;')
    code = code.replace('pattern=values;', 'a.r[30]=(uint32_t[]){0,1,2,0x7fffffff,0x80000000,0xffffffff,17,65536}[values];a.r[27]=0xfffffff0u+values;a.xer_so=values&1;pattern=values;')
    code = code.replace('initial.r[7]=0x12345678;', 'initial.r[30]=location==0?0u:location==1?1u:0xffffffffu;initial.r[27]=0xfffffff0u;initial.xer_so=rn&1;initial.r[7]=0x12345678;')
    code = code.replace('Second vertex-writer', 'Adjacent vertex-writer').replace('Second-writer context', 'Adjacent-writer context')
if args.emit_native:
    # Reuse the existing proof's owned MEM2 allocator and visible diagnostics,
    # not its tested CPU region or oracle. Require exact structural markers.
    with tempfile.TemporaryDirectory() as temp:
        template = Path(temp)/'native.c'
        subprocess.run([sys.executable,str(root/'wii/test_bulk_writer.py'),
                        '--bulk-ram','--bulk-publish','--bulk-noalias',
                        '--emit-native',str(template)],check=True)
        support = template.read_text()
    marker = '#define VIPER_WII_BULK_NOALIAS\n'
    if support.count(marker)!=1 or support.count('int main(void){')!=1:
        raise ValueError('Native proof support changed; re-audit allocator and console')
    prefix = support.split(marker,1)[0]
    native = code.replace('int main(void){','int test_main(void){')
    native = native.replace('reference(&a);int flags=',
        'probe_set_fpscr(0x82000000u|(uint32_t[]){0,3,2,1}[round]);reference(&a);uint32_t full=probe_fpscr();int flags=')
    native = native.replace('candidate(&b);assert(',
        'probe_set_fpscr(0x82000000u|(uint32_t[]){0,3,2,1}[round]);candidate(&b);uint32_t current=probe_fpscr();assert(full==current);assert(')
    native = re.sub(r'\bassert\s*\(', 'probe_require(',native)
    main = 'int main(void){'+support.rsplit('int main(void){',1)[1]
    main = main.replace('VIPER WII BULK WRITER','VIPER WII SECOND WRITER')
    args.emit_native.parent.mkdir(parents=True,exist_ok=True)
    args.emit_native.write_text(prefix+'#define VIPER_NATIVE_DRIVING_PROBE\n#define VIPER_WII_BULK_PUBLISH\n'+native+main)
elif args.emit_c:
    args.emit_c.write_text(code)
else:
    with tempfile.TemporaryDirectory() as temp:
        path = Path(temp);(path/'test.c').write_text(code)
        subprocess.run(['clang','-O2','-std=c11','-fno-strict-aliasing','-frounding-math','-ffp-contract=off','-fsanitize=address,undefined','-DVIPER_WII_BULK_WRITER','-DVIPER_WII_BULK_PUBLISH','-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures'),str(path/'test.c'),'-lm','-o',str(path/'test')],check=True)
        subprocess.run([str(path/'test')],check=True)
