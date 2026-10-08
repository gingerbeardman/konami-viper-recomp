"""Differentially test actual runtime string helpers against their byte path."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'runtime/cpu.c').read_text()
start = source.find('#ifdef VIPER_INLINE_STRING_HELPERS\nvoid rt_lswi_slow(')
if start < 0:
    start = source.index('void rt_lswi(')
helpers = source[start:source.index('/* ============================================================ FPU */')]
harness = r'''
#include "ppc_rt.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
uint8_t *g_ram;
static unsigned calls;
static uint32_t addresses[64],values[64];
uint32_t rt_mmio_r8(uint32_t a){addresses[calls++]=a;return (a*17+3)&255;}
void rt_mmio_w8(uint32_t a,uint32_t v){addresses[calls]=a;values[calls++]=v;}
void reference_load(PPCContext*,uint32_t,int,int);
void reference_store(PPCContext*,uint32_t,int,int);
static void init(uint32_t ea){
 for(unsigned i=0;i<64;i++){uint32_t a=ea+i;if(a<RAM_LIMIT)g_ram[a&RAM_MASK]=(a*31+7)&255;}
 calls=0;
}
int main(void){
 g_ram=malloc(RAM_SIZE);assert(g_ram);
 uint32_t starts[]={0,1,2,3,127,0xffffe0,0xfffff1,0xffffff,0x1000000,0x1000001,0x1ffffe0,0x1fffff1,0x1ffffff,0x2000000,0x82000000,0xfffffff1};
 unsigned cases=0;
 for(unsigned a=0;a<sizeof(starts)/sizeof(*starts);a++)for(int nb=0;nb<=32;nb++)for(int reg=0;reg<32;reg++){
  uint32_t ea=starts[a],saved_a[64],saved_v[64];unsigned saved_calls;
  PPCContext original={0},expected,actual;
  for(int i=0;i<32;i++)original.r[i]=0x12345678u+i*0x01234567u;
  expected=actual=original;init(ea);reference_load(&expected,ea,reg,nb);
  saved_calls=calls;memcpy(saved_a,addresses,sizeof saved_a);
  init(ea);rt_lswi(&actual,ea,reg,nb);
  assert(!memcmp(&actual,&expected,sizeof actual));assert(calls==saved_calls);
  assert(!memcmp(addresses,saved_a,calls*sizeof(*addresses)));
  uint8_t saved_ram[64];init(ea);reference_store(&original,ea,reg,nb);
  for(unsigned i=0;i<64;i++){uint32_t p=ea+i;saved_ram[i]=p<RAM_LIMIT?g_ram[p&RAM_MASK]:0;}
  saved_calls=calls;memcpy(saved_a,addresses,sizeof saved_a);memcpy(saved_v,values,sizeof saved_v);
  init(ea);rt_stswi(&original,ea,reg,nb);
  for(unsigned i=0;i<64;i++){uint32_t p=ea+i;if(p<RAM_LIMIT)assert(g_ram[p&RAM_MASK]==saved_ram[i]);}
  assert(calls==saved_calls);assert(!memcmp(addresses,saved_a,calls*sizeof(*addresses)));
  assert(!memcmp(values,saved_v,calls*sizeof(*values)));cases++;
 }
 free(g_ram);printf("String helper differential PASS: %u cases, loads and stores\n",cases);
}
'''
with tempfile.TemporaryDirectory() as directory:
    temp = Path(directory)
    (temp / 'helpers.c').write_text('#include "ppc_rt.h"\n' + helpers)
    (temp / 'test.c').write_text(harness)
    flags = ['clang', '-O2', '-std=c11', '-fsanitize=address,undefined', '-I' + str(root / 'runtime')]
    subprocess.run(flags + ['-Drt_lswi=reference_load', '-Drt_stswi=reference_store', '-c', str(temp / 'helpers.c'), '-o', str(temp / 'reference.o')], check=True)
    subprocess.run(flags + ['-DVIPER_WII_STRING_WORDS', '-c', str(temp / 'helpers.c'), '-o', str(temp / 'candidate.o')], check=True)
    subprocess.run(flags + [str(temp / 'test.c'), str(temp / 'reference.o'), str(temp / 'candidate.o'), '-o', str(temp / 'test')], check=True)
    subprocess.run([str(temp / 'test')], check=True)
