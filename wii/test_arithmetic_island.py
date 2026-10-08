"""Offline selective-register spike; actual eight-instruction PPC source oracle.

No production lowering is installed by this probe. Arithmetic order and double
intermediates stay unchanged; only four context stores are delayed to the end.
"""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--emit-native', type=Path)
args = p.parse_args()
source = (root / 'generated/gticlub2/gl_000.c').read_text()
a = source.index('    /* 00029038:')
b = source.index('    /* 00029058:', a)
original = source[a:b]
assert hashlib.sha256(original.encode()).hexdigest() == 'adb227fa9edcbc18b38334788c5d739032171b4423df8ea71a16d3c2fdc6abcd'
assert set(re.findall(r'\b([A-Za-z_]\w*)\(', original)) == {'ROUND_S', 'fma'}
written = [31, 28, 27, 26]
candidate = re.sub(r'c->f\[(\d+)\]', lambda m: f'island_f{m[1]}' if int(m[1]) in written else m[0], original)
assert re.sub(r'island_f(\d+)', lambda m: f'c->f[{m[1]}]', candidate) == original
candidate = ''.join(f'double island_f{i}=c->f[{i}];\n' for i in written) + candidate + ''.join(f'c->f[{i}]=island_f{i};\n' for i in written)
body = '''
#include "ppc_rt.h"
#include <assert.h>
#include <errno.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
__attribute__((noinline)) void reference(PPCContext *c){
''' + original + '}\n__attribute__((noinline)) void candidate(PPCContext *c){\n' + candidate + r'''
}
static uint64_t mix(uint64_t x){x^=x>>30;x*=UINT64_C(0xbf58476d1ce4e5b9);x^=x>>27;x*=UINT64_C(0x94d049bb133111eb);return x^(x>>31);}
int main(void){
 static const uint64_t edge[]={0,UINT64_C(0x8000000000000000),1,UINT64_C(0x800fffffffffffff),UINT64_C(0x0010000000000000),UINT64_C(0x7fefffffffffffff),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff8000000000123),UINT64_C(0x7ff0000000000456),UINT64_C(0x3ff0000000000000),UINT64_C(0xbfe0000000000000)};
 int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};unsigned cases=0;
 for(unsigned rn=0;rn<4;rn++)for(unsigned n=0;n<4096;n++){
  PPCContext seed,expected,actual;memset(&seed,0xa5,sizeof seed);
  for(unsigned i=0;i<32;i++){uint64_t bits=n<256?edge[(n+i*7)%12]:mix(n*1777u+i*913u);memcpy(&seed.f[i],&bits,8);}
  memcpy(&expected,&seed,sizeof seed);memcpy(&actual,&seed,sizeof seed);
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_ARITHMETIC_NATIVE_PROBE
  uint32_t incoming=0x82000000u|(uint32_t[]){0,3,2,1}[rn];probe_set_fpscr(incoming);
#endif
  reference(&expected);
#ifdef VIPER_ARITHMETIC_NATIVE_PROBE
  uint32_t status=probe_fpscr();
#endif
  int flags=fetestexcept(FE_ALL_EXCEPT),err=errno;
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_ARITHMETIC_NATIVE_PROBE
  probe_set_fpscr(incoming);
#endif
  candidate(&actual);
#ifdef VIPER_ARITHMETIC_NATIVE_PROBE
  assert(status==probe_fpscr());
#endif
  assert(flags==fetestexcept(FE_ALL_EXCEPT));assert(err==errno);
  assert(!memcmp(&actual,&expected,sizeof actual));cases++;
 }
 printf("ARITHMETIC ISLAND original-source PASS cases=%u complete-context/FENV\n",cases);return 0;
}
'''
if args.emit_native:
    with tempfile.TemporaryDirectory() as t:
        support = Path(t) / 'support.c'
        subprocess.run([sys.executable, str(root/'wii/test_bulk_writer.py'), '--bulk-ram', '--bulk-publish', '--bulk-noalias', '--emit-native', str(support)], check=True)
        text = support.read_text()
    assert text.count('#define VIPER_WII_BULK_NOALIAS\n') == 1
    prefix = text.split('#define VIPER_WII_BULK_NOALIAS\n', 1)[0]
    native = body.replace('int main(void){', 'int test_main(void){')
    native = re.sub(r'\bassert\s*\(', 'probe_require(', native)
    main = 'int main(void){' + text.rsplit('int main(void){', 1)[1]
    main = main.replace('VIPER WII BULK WRITER', 'VIPER WII ARITHMETIC ISLAND')
    args.emit_native.parent.mkdir(parents=True, exist_ok=True)
    args.emit_native.write_text(prefix+'#define VIPER_ARITHMETIC_NATIVE_PROBE\n'+native+main)
else:
    with tempfile.TemporaryDirectory() as t:
        path = Path(t)
        (path/'probe.c').write_text(body)
        subprocess.run(['clang', '-O2', '-std=c11', '-fno-strict-aliasing', '-frounding-math', '-ffp-contract=off', '-fsanitize=address,undefined', '-Iruntime', '-Iwii', '-Itests/fixtures', str(path/'probe.c'), '-lm', '-o', str(path/'probe')], cwd=root, check=True)
        subprocess.run([str(path/'probe')], check=True)
