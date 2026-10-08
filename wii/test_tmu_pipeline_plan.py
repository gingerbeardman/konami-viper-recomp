"""Compare planner-elided TMU chains with the checked integer oracle."""
from pathlib import Path
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
code = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "wii/gx_tmu_pipeline_plan.h"
static uint32_t rng=123;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static WiiVoodooRGBA rgba(void){uint32_t x=next();return (WiiVoodooRGBA){x,x>>8,x>>16,x>>24};}
static int equal(WiiVoodooRGBA a,WiiVoodooRGBA b){return a.r==b.r&&a.g==b.g&&a.b==b.b&&a.a==b.a;}
int main(void){
 unsigned compared=0,elided=0,rejected=0;
 for(unsigned n=0;n<1000000;n++){
  uint32_t regs[2][9]={{0}};WiiVoodooRGBA local[2]={rgba(),rgba()};
  for(unsigned u=0;u<2;u++){
   regs[u][0]=(next()&0x3ffff000u)|0xa07u;
   unsigned lo=next()%32;regs[u][1]=lo|(lo<<6);regs[u][2]=next();
  }
  // Exercise provable identities heavily, independently evaluated below.
  if(n%3==0)regs[0][0]=0x10241a07u;
  if(n%5==0)regs[0][0]=0xa07u;
  uint32_t init=(n%7==0)?64:0;
  if(n%11==0)regs[0][1]=32|(32<<6);
  if(n%13==0)regs[1][1]=32|(32<<6);
  const uint32_t *r0=n%17?regs[0]:NULL,*r1=n%19?regs[1]:NULL;
  WiiTMUPipelinePlan p=wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,r0,r1,init,0x3b,0);
  if(p.reason!=WII_TMU_PIPE_OK){rejected++;continue;}
  WiiVoodooRGBA zero={0},reference=zero,actual=zero;
  if(!(init&64)){
   if(r1&&(r1[1]&63)<32)reference=wii_voodoo_tmu_eval(wii_voodoo_tmu_plan(r1[0]),local[1],reference,(r1[1]&63)<<6,r1[2]);
   if(r0&&(r0[1]&63)<32)reference=wii_voodoo_tmu_eval(wii_voodoo_tmu_plan(r0[0]),local[0],reference,(r0[1]&63)<<6,r0[2]);
  }
  for(int u=1;u>=0;u--)if(p.unit[u].use){
   WiiVoodooRGBA input=p.unit[u].other_needed?actual:zero;
   WiiVoodooRGBA fetched=p.unit[u].local_needed?local[u]:zero;
   actual=wii_voodoo_tmu_eval(p.unit[u].equation,fetched,input,p.unit[u].fixed_lod,regs[u][2]);
  }
  if(p.texture_zero)actual=zero;
  assert(equal(reference,actual));assert(p.total_stages<=16);
  if(!p.unit[0].use||!p.unit[1].use||!p.unit[0].local_needed||!p.unit[1].local_needed)elided++;
  compared++;
 }
 uint32_t a[9]={0x10241a07,0,0,0},b[9]={0x10241a07,0,0,0};
 // Configuration mode is meaningful only when the enabled TMU is observed.
 a[8]=1u<<18;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x3b,0).reason==WII_TMU_PIPE_CONFIG);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,64,0x3b,0).reason==WII_TMU_PIPE_OK);
 a[8]=0;a[0]=0xa07|(4u<<14);a[1]=1u<<6;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x3b,0).reason==WII_TMU_PIPE_LOD_FACTOR);
 a[0]=b[0]=0xa07|(1u<<13)|(1u<<22)|(1u<<20)|(1u<<29);a[1]=b[1]=0;
 uint32_t complex_cp=0x10000001u|(1u<<9)|(1u<<18)|(1u<<16)|(1u<<25);
 assert(wii_gx_tmu_pipeline_plan(complex_cp,0x2373b,0,0x41,0,0,a,b,0,0x3b,0).reason==WII_TMU_PIPE_BUDGET);
 a[0]=b[0]=0x10241a07;a[1]=b[1]=0;
 // TMU0 local replacement requires ST0 even when an independent ST1 exists.
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0xfb,0).reason==WII_TMU_PIPE_OK);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0xff,0).reason==WII_TMU_PIPE_OK);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x80,0).reason==WII_TMU_PIPE_PACKET);
 // TMU0 passthrough leaves TMU1: either independent ST1 or inherited ST0.
 a[0]=0xa07;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0xc0,0).reason==WII_TMU_PIPE_OK);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x30,0).reason==WII_TMU_PIPE_OK);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x1f,0).reason==WII_TMU_PIPE_PACKET);
 // Perspective ST alone is insufficient; affine samples ignore missing W.
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x80,0).reason==WII_TMU_PIPE_PACKET);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x20,0).reason==WII_TMU_PIPE_PACKET);
 b[0]&=~1u;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x80,0).reason==WII_TMU_PIPE_OK);
 a[0]=0x10241a07;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x20,0).reason==WII_TMU_PIPE_PACKET);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x28,0).reason==WII_TMU_PIPE_OK);
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x30,0).reason==WII_TMU_PIPE_OK);
 a[0]&=~1u;
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,0,0x20,0).reason==WII_TMU_PIPE_OK);
 // Disabled chain supplies zero and performs no local fetch: ST is irrelevant.
 assert(wii_gx_tmu_pipeline_plan(0x1d022401,0x21329,0x4511f,0x40,0,0,a,b,64,0,0).reason==WII_TMU_PIPE_OK);
 printf("TMU plan PASS compared=%u elided=%u explicit_rejections=%u\n",compared,elided,rejected);
}
'''
with tempfile.TemporaryDirectory(prefix='viper-tmu-plan-') as directory:
    src=Path(directory)/'test.c';binary=Path(directory)/'test';src.write_text(code)
    subprocess.run(['clang','-std=c11','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root),str(src),str(root/'wii/texture.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
