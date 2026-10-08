"""Check writer attribution preserves actual RAM and ordered MMIO stores."""
from pathlib import Path
import subprocess
import tempfile
from instrument_submission import instrument
from specialize_submission import specialize
import re

root = Path(__file__).resolve().parent.parent
lowered = specialize((root / 'generated/gticlub2/gl_000.c').read_text())
instrumented, actual_hooks = instrument(lowered)
recovered = re.sub(r'WII_SUBMISSION_(ST32LE|ST32)\(0x[0-9a-f]{8}u,',
                   r'\1(', instrumented.removeprefix('#include "submission_profile.h"\n'))
assert recovered == lowered
assert actual_hooks > 500
print(f'Post-batching source instrumentation round-trip PASS: {actual_hooks} static hooks')
fixture = r'''
void reference(PPCContext *c){
 /* 0002af88: 91990000 stw */
 ST32(c->r[25],c->r[12]);
 /* 0002b078: 7ec0bd2c stwbrx */
 ST32LE((0+c->r[23]),c->r[22]);
 /* 00001000: bfe10000 stmw */
 {uint32_t ea=c->r[1];for(int k=28;k<32;k++,ea+=4)ST32(ea,c->r[k]);}
}
'''
transformed, count = instrument(fixture.replace('reference(', 'candidate('))
assert count == 3
assert 'WII_SUBMISSION_ST32(0x0002af88u,' in transformed
assert 'WII_SUBMISSION_ST32LE(0x0002b078u,' in transformed
assert 'WII_SUBMISSION_ST32(0x00001000u,' in transformed
try:
    instrument('void bad(void){ST32(0,1);}')
except ValueError:
    pass
else:
    raise AssertionError('Unattributed store accepted')
harness = r'''
#include "submission_profile.c"
#include <assert.h>
#include <stdarg.h>
#include <stdlib.h>
uint8_t *g_ram;
typedef struct {uint32_t ea,value,width;} Event;
static Event events[16],saved[16];static unsigned used,rows;
void rt_mmio_w32(uint32_t ea,uint32_t v){assert(used<16);events[used++]=(Event){ea,v,4};}
void rt_mmio_w8(uint32_t ea,uint32_t v){assert(used<16);events[used++]=(Event){ea,v,1};}
void rt_log(const char *fmt,...){(void)fmt;rows++;}
int main(void){
 g_ram=calloc(1,RAM_SIZE);uint8_t *before=calloc(1,RAM_SIZE);assert(g_ram&&before);
 PPCContext c={0};c.r[12]=0x12345678;c.r[22]=0x87654321;
 for(unsigned k=28;k<32;k++)c.r[k]=0x31415926u+k;
 uint32_t addresses[]={0x1000,RAM_MASK-1,RAM_LIMIT-1,0x84000000,0x85ffffff,0x86000000};
 unsigned comparisons=0;
 for(unsigned a=0;a<6;a++)for(unsigned b=0;b<6;b++){
  c.r[25]=addresses[a];c.r[23]=addresses[b];c.r[1]=0x84001000;
  memset(g_ram,0,RAM_SIZE);used=0;reference(&c);unsigned n=used;
  memcpy(saved,events,sizeof events);memcpy(before,g_ram,RAM_SIZE);
  memset(g_ram,0,RAM_SIZE);used=0;wii_submission_profile_start();candidate(&c);
  assert(n==used&&!memcmp(saved,events,n*sizeof(Event)));
  assert(!memcmp(before,g_ram,RAM_SIZE));
  unsigned expected=4+(addresses[a]>=0x84000000&&addresses[a]<0x86000000)+
                      (addresses[b]>=0x84000000&&addresses[b]<0x86000000);
  assert(total==expected&&overflow==0);wii_submission_profile_stop();
  uint64_t t=total;used=0;candidate(&c);assert(total==t);comparisons++;
 }
 wii_submission_profile_start();
 for(unsigned i=0;i<SLOTS;i++)wii_submission_observe(i*4,0x84000000+i*4,i&1);
 assert(total==SLOTS&&overflow==0);
 wii_submission_observe(SLOTS*4,0x84000000,0);assert(overflow==1);
 wii_submission_observe(0,0x84001000,1);
 assert(writers[0].count==2&&writers[0].little==1&&writers[0].high==0x84001000);
 wii_submission_profile_stop();uint64_t t=total;
 wii_submission_observe(0,0x84000000,0);assert(total==t&&rows>0);
 free(g_ram);free(before);
 printf("Submission profile differential PASS: %u RAM/MMIO cases, collisions/overflow/inactive checks\n",comparisons);
}
'''
with tempfile.TemporaryDirectory() as temp:
    directory = Path(temp)
    source = directory / 'test.c'
    source.write_text('#include "submission_profile.h"\n#include <stdio.h>\n'
                      + fixture + transformed + harness)
    binary = directory / 'test'
    subprocess.run(['clang', '-O2', '-std=c11', '-Wall', '-Wextra', '-Werror',
                    '-fsanitize=address,undefined', '-I' + str(root / 'runtime'),
                    '-I' + str(root / 'wii'), str(source), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
