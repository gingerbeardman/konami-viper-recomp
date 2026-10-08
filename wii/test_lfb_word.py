"""Check the actual direct-LFB wrapper against generic bus word semantics."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
source=(root/'runtime/hw.c').read_text()
wrapper=source[source.index('void rt_mmio_w32('):source.index('void rt_mmio_w16(')]
harness=r'''
#include <stdint.h>
#include <assert.h>
#include <stdio.h>
static long g_mmio_log;
static unsigned calls, fallback;
static uint32_t offset, value, mask, address;
static uint32_t bswap32(uint32_t v){return __builtin_bswap32(v);}
static void voodoo_lfb_write(uint32_t o,uint32_t v,uint32_t m){calls++;offset=o;value=v;mask=m;}
static void hw_write(uint32_t ea,int size,uint32_t v){
 assert(size==4);fallback++;address=ea;
 if(ea>=0x84000000u&&ea<0x86000000u)voodoo_lfb_write((ea-0x84000000u)&~3u,bswap32(v),0xffffffffu);
}
'''
test=r'''
int main(void){
 uint32_t addresses[]={0,0x80000000,0x82000000,0x83ffffff,0x84000000,0x84000001,0x84000002,0x84000003,0x847fffff,0x84ffffff,0x85fffffc,0x85ffffff,0x86000000,0xfe800000,0xffffffff};
 uint32_t values[]={0,1,0x12345678,0xffffffff,0x80000000};
 long logs[]={-1,0,1,100}; unsigned cases=0;
 for(unsigned a=0;a<15;a++)for(unsigned v=0;v<5;v++)for(unsigned l=0;l<4;l++){
  uint32_t ea=addresses[a],val=values[v];g_mmio_log=logs[l];calls=fallback=0;
  rt_mmio_w32(ea,val);
  int lfb=ea>=0x84000000u&&ea<0x86000000u;
  assert(calls==(unsigned)lfb);
  assert(fallback==(unsigned)(!lfb||g_mmio_log!=0));
  if(lfb){assert(offset==((ea-0x84000000u)&~3u));assert(value==bswap32(val));assert(mask==0xffffffffu);}
  else assert(address==ea);
  cases++;
 }
 printf("Direct LFB word contract PASS: %u cases\n",cases);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness+wrapper+test)
 subprocess.run(['clang','-O2','-std=c11','-fsanitize=address,undefined','-DVIPER_WII_DIRECT_LFB_WORD',str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
