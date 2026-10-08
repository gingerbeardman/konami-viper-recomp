"""Differential proof of the original runtime CR loop versus ordered unrolling."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--emit-native', type=Path)
a = p.parse_args()
source = (root/'runtime/cpu.c').read_text()
match = re.search(r'void rt_cr_unpack\(PPCContext \*c, uint32_t v, uint32_t crm\) \{.*?#else\n(.*?)#endif\n\}', source, re.S)
if not match:
    raise SystemExit('original CR loop drifted')
original = match[1]
expected = '    for (int i = 0; i < 8; i++)\n        if (crm & (0x80u >> i)) c->cr[i] = (v >> (28 - 4 * i)) & 15;\n'
if original != expected:
    raise SystemExit('original CR loop no longer matches oracle')
code = r'''
#include "cr_unpack.h"
#include <assert.h>
#include <stdio.h>
static __attribute__((noinline)) void original(PPCContext *c,uint32_t v,uint32_t crm){
ORIGINAL_BODY
}
static __attribute__((noinline)) void candidate(PPCContext *c,uint32_t v,uint32_t crm){wii_cr_unpack(c,v,crm);}
int main(void){
 unsigned cases=0;
 for(unsigned rn=0;rn<4;rn++)for(unsigned mask=0;mask<256;mask++)for(unsigned pattern=0;pattern<128;pattern++){
  PPCContext ref,actual;memset(&ref,0xa5,sizeof ref);memcpy(&actual,&ref,sizeof ref);
  uint32_t value=(0x91827364u&~(15u<<(4*(pattern>>4))))|((pattern&15u)<<(4*(pattern>>4)));
  uint32_t crm=mask|((pattern&1)?0xffffff00u:0);
#ifdef VIPER_CR_NATIVE_PROBE
  uint32_t incoming=0x82000000u|rn;probe_set_fpscr(incoming);
#endif
  original(&ref,value,crm);
#ifdef VIPER_CR_NATIVE_PROBE
  uint32_t status=probe_fpscr();probe_set_fpscr(incoming);
#endif
  candidate(&actual,value,crm);
#ifdef VIPER_CR_NATIVE_PROBE
  assert(status==probe_fpscr());
#endif
  assert(!memcmp(&ref,&actual,sizeof ref));cases++;
 }
 printf("CR UNPACK differential PASS cases=%u complete-context\n",cases);return 0;
}
'''.replace('ORIGINAL_BODY', original)
if a.emit_native:
    with tempfile.TemporaryDirectory() as t:
        support_path = Path(t)/'support.c'
        subprocess.run([sys.executable, str(root/'wii/test_bulk_writer.py'), '--bulk-ram', '--bulk-publish', '--bulk-noalias', '--emit-native', str(support_path)], check=True)
        support = support_path.read_text()
    prefix = support.split('#define VIPER_WII_BULK_NOALIAS\n', 1)[0]
    native = re.sub(r'\bassert\s*\(', 'probe_require(', code.replace('int main(void){', 'int test_main(void){'))
    main = 'int main(void){'+support.rsplit('int main(void){', 1)[1]
    main = main.replace('VIPER WII BULK WRITER', 'VIPER WII CR UNPACK')
    a.emit_native.parent.mkdir(parents=True, exist_ok=True)
    a.emit_native.write_text(prefix+'#define VIPER_CR_NATIVE_PROBE\n'+native+main)
else:
    with tempfile.TemporaryDirectory() as t:
        path = Path(t); (path/'probe.c').write_text(code)
        subprocess.run(['clang','-O2','-std=c11','-fno-strict-aliasing','-fsanitize=address,undefined','-Iruntime','-Iwii',str(path/'probe.c'),'-o',str(path/'probe')],cwd=root,check=True)
        subprocess.run([str(path/'probe')],check=True)
