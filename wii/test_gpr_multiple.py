"""Full state/RAM/MMIO oracle for integer native multi-register transfers."""
import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile
root=Path(__file__).resolve().parent.parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--emit-native',type=Path);args=p.parse_args()
code=r'''
#include "gpr_multiple.h"
#include <assert.h>
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
static PPCContext *active;
static uint32_t events[512];static unsigned used;
uint32_t rt_mmio_r32(uint32_t ea){events[used++]=ea;active->r[17]^=0x10203040u;return ea^0x91827364u;}
void rt_mmio_w32(uint32_t ea,uint32_t v){events[used++]=ea;events[used++]=v;active->r[17]^=0x10203040u;}
uint32_t rt_mmio_r8(uint32_t ea){events[used++]=ea;active->r[17]^=0x10203040u;return ea&255;}
void rt_mmio_w8(uint32_t ea,uint32_t v){events[used++]=ea;events[used++]=v;active->r[17]^=0x10203040u;}
uint32_t rt_mmio_r16(uint32_t ea){return rt_mmio_r32(ea)&65535;}
void rt_mmio_w16(uint32_t ea,uint32_t v){rt_mmio_w32(ea,v);}
static void original(PPCContext *c,uint32_t ea,unsigned first,unsigned load){
 if(load){for(int k=(int)first;k<32;k++,ea+=4)c->r[k]=LD32(ea);}
 else {for(int k=(int)first;k<32;k++,ea+=4)ST32(ea,c->r[k]);}
}
int main(void){
 g_ram=malloc(RAM_SIZE);uint8_t *saved=malloc(RAM_SIZE);assert(g_ram&&saved);
 unsigned cases=0,admitted=0;int rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 for(unsigned rn=0;rn<4;rn++)for(unsigned first=0;first<32;first++)for(unsigned mode=0;mode<8;mode++)for(unsigned load=0;load<2;load++){
  uint32_t ea=(uint32_t[]){0x2000,0x2001,RAM_SIZE+0x3000,RAM_SIZE-8,RAM_LIMIT-4,0x8000fff0,0xfffffff0,0x9004}[mode];
  PPCContext seed,reference,actual;memset(&seed,0xa5,sizeof seed);
  for(unsigned k=0;k<32;k++)seed.r[k]=0x91827364u+k*0x01020305u+first;
  memset(g_ram,0x63,RAM_SIZE);used=0;
  PPCContext *c=mode==7?(PPCContext *)(void *)(g_ram+0x9000):&reference;
  memcpy(c,&seed,sizeof seed);active=c;
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);
#ifdef VIPER_GPR_MULTIPLE_NATIVE_PROBE
  uint32_t incoming=0x82000000u|(uint32_t[]){0,3,2,1}[rn];probe_set_fpscr(incoming);
#endif
  original(c,ea,first,load);
#ifdef VIPER_GPR_MULTIPLE_NATIVE_PROBE
  uint32_t status=probe_fpscr();
#endif
  int flags=fetestexcept(FE_ALL_EXCEPT);memcpy(&reference,c,sizeof reference);memcpy(saved,g_ram,RAM_SIZE);
  unsigned count=used;uint32_t trace[512];memcpy(trace,events,sizeof trace);
  memset(g_ram,0x63,RAM_SIZE);used=0;memset(events,0,sizeof events);
  c=mode==7?(PPCContext *)(void *)(g_ram+0x9000):&actual;memcpy(c,&seed,sizeof seed);active=c;
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);
#ifdef VIPER_GPR_MULTIPLE_NATIVE_PROBE
  probe_set_fpscr(incoming);
#endif
  admitted+=wii_gpr_multiple_ram(c,ea,first)!=NULL;
  if(load)wii_gpr_lmw(c,ea,first);else wii_gpr_stmw(c,ea,first);
#ifdef VIPER_GPR_MULTIPLE_NATIVE_PROBE
  assert(status==probe_fpscr());
#endif
  assert(flags==fetestexcept(FE_ALL_EXCEPT));
  assert(!memcmp(c,&reference,sizeof reference));assert(!memcmp(saved,g_ram,RAM_SIZE));
  assert(count==used&&!memcmp(trace,events,count*sizeof(uint32_t)));cases++;
 }
 printf("GPR MULTIPLE differential PASS cases=%u admissions=%u complete-context/RAM/ordered-MMIO/FENV\n",cases,admitted);return 0;
}
'''
if args.emit_native:
 with tempfile.TemporaryDirectory() as t:
  path=Path(t)/'support.c';subprocess.run([sys.executable,str(root/'wii/test_bulk_writer.py'),'--bulk-ram','--bulk-publish','--bulk-noalias','--emit-native',str(path)],check=True);support=path.read_text()
 prefix=support.split('#define VIPER_WII_BULK_NOALIAS\n',1)[0]
 native=re.sub(r'\bassert\s*\(','probe_require(',code.replace('int main(void){','int test_main(void){'))
 main='int main(void){'+support.rsplit('int main(void){',1)[1];main=main.replace('VIPER WII BULK WRITER','VIPER WII GPR MULTIPLE')
 args.emit_native.parent.mkdir(parents=True,exist_ok=True);args.emit_native.write_text(prefix+'#define VIPER_GPR_MULTIPLE_NATIVE_PROBE\n'+native+main)
else:
 with tempfile.TemporaryDirectory() as t:
  path=Path(t);(path/'probe.c').write_text(code)
  subprocess.run(['clang','-O2','-std=c11','-fno-strict-aliasing','-frounding-math','-ffp-contract=off','-fsanitize=address,undefined','-Iruntime','-Iwii','-Itests/fixtures',str(path/'probe.c'),'-lm','-o',str(path/'probe')],cwd=root,check=True)
  subprocess.run([str(path/'probe')],check=True)
