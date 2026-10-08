#!/usr/bin/env python3
"""Compare borrowed clipping with original copied polygons, bytes and FP status."""
from pathlib import Path
import argparse,subprocess,tempfile
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--emit-native',type=Path);parser.add_argument('--copy-once',action='store_true');args=parser.parse_args()
common=r"""
#include "wdepth_split.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int reference_split(const WiiVoodooVertex*,WiiWDepthTriangle*,unsigned,unsigned*);
static WiiWDepthTriangle expected[WII_WDEPTH_SPLIT_MAX],actual[WII_WDEPTH_SPLIT_MAX];
static uint32_t rng=0x721904ab;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static float random_unit(void){return (next()>>8)*0x1p-24f;}
static unsigned proof(void){
 unsigned cases=0;
 for(unsigned rn=0;rn<4;rn++)for(unsigned sticky=0;sticky<STICKY_COUNT;sticky++)
 for(unsigned case_id=0;case_id<CASE_COUNT;case_id++){
  set_status(incoming(rn,sticky));
  float lo,hi;assert(wii_wdepth_band(case_id%66,&lo,&hi));if(case_id%66==65)hi=2;
  WiiVoodooVertex v[3];
  for(unsigned j=0;j<3;j++){
   float fields[14];for(unsigned k=0;k<14;k++)fields[k]=(random_unit()-.5f)*512;
   memcpy(&v[j],fields,sizeof v[j]);
   switch(case_id%6){
    case 0:v[j].wb=lo+(hi-lo)*(.1f+.8f*random_unit());break;
    case 1:v[j].wb=j==0?lo:j==1?hi:(lo+hi)/2;break;
    case 2:v[j].wb=(lo+hi)/2;break;
    case 3:v[j].wb=ldexpf(random_unit(),-(int)(case_id%20));break;
    case 4:v[j].wb=j==0?nextafterf(lo,0):j==1?nextafterf(hi,INFINITY):(lo+hi)/2;break;
    default:v[j].wb=j==0?0:j==1?1:2;break;
   }
  }
  if(case_id%17==0)v[2]=v[1];
  if(case_id%29==0){const uint32_t bits[]={0x7fc00001,0x7fa00001,0x7f800000,0xff800000,0x80000000,1,0x7f7fffff};unsigned field=next()%14;memcpy((unsigned char *)&v[next()%3]+4*field,&bits[(case_id/29)%7],4);}
  static const unsigned caps[]={0,1,2,3,7,WII_WDEPTH_SPLIT_MAX};unsigned cap=caps[case_id%6];
  if(case_id%7)cap=WII_WDEPTH_SPLIT_MAX;
  memset(expected,0x55,sizeof expected);memset(actual,0x55,sizeof actual);
  const WiiVoodooVertex *ei=v,*ai=v;
#ifdef SPLIT_COPY_PROOF
  if(case_id%23==0){
   unsigned slot=(case_id/23)%3;
   memcpy(expected[slot].v,v,sizeof v);memcpy(actual[slot].v,v,sizeof v);
   ei=expected[slot].v;ai=actual[slot].v;
  }
#endif
  unsigned en=999,an=999;
  set_status(incoming(rn,sticky));int e=reference_split(ei,expected,cap,&en);uint32_t es=get_status();
  set_status(incoming(rn,sticky));int a=wii_wdepth_split(ai,actual,cap,&an);uint32_t as=get_status();
  if(a!=e||an!=en||es!=as||memcmp(expected,actual,sizeof expected)){
   printf("Borrow clip mismatch case=%u RN=%u sticky=%u status=%08x/%08x count=%u/%u\n",case_id,rn,sticky,es,as,en,an);return 0;
  }
  cases++;
 }
 return cases;
}
"""
host=r"""
#include <fenv.h>
#define CASE_COUNT 10000
#define STICKY_COUNT 2
static uint32_t incoming(unsigned rn,unsigned sticky){return rn|(sticky?256u:0u);}
static void set_status(uint32_t s){const int rn[]={FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};fesetround(rn[s&3]);feclearexcept(FE_ALL_EXCEPT);if(s&256)feraiseexcept(FE_INEXACT);}
static uint32_t get_status(void){return fetestexcept(FE_ALL_EXCEPT);}
"""
native=r"""
#include <gccore.h>
#define CASE_COUNT 2000
#define STICKY_COUNT 4
static uint32_t incoming(unsigned rn,unsigned sticky){const uint32_t flags[]={0,0x82000000,0x9a000000,0x9a060000};return flags[sticky]|rn;}
static uint32_t get_status(void){double f;uint64_t bits;__asm__ volatile("mffs %0":"=f"(f)::"memory");memcpy(&bits,&f,8);return (uint32_t)bits;}
static void set_status(uint32_t s){uint64_t bits=s;double f;memcpy(&f,&bits,8);__asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");}
"""
preamble='#include <stdint.h>\n#include <string.h>\n'
if args.copy_once:preamble+='#define SPLIT_COPY_PROOF\n'
if args.emit_native:
 main=r"""
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();if(mode->viTVMode&VI_NON_INTERLACE)VIDEO_WaitVSync();
 printf("VIPER WII BORROW CLIP DIFFERENTIAL\n");unsigned n=proof();printf("VIPER WII BORROW CLIP %s cases=%u FULL FPSCR\n",n==32000?"PASS":"FAIL",n);for(;;)VIDEO_WaitVSync();
}
"""
 if args.copy_once: main=main.replace('BORROW CLIP','SPLIT COPY ONCE')
 args.emit_native.parent.mkdir(parents=True,exist_ok=True);args.emit_native.write_text(preamble+native+common+main)
else:
 with tempfile.TemporaryDirectory(prefix='viper-borrow-clip-') as d:
  p=Path(d);(p/'test.c').write_text(preamble+host+common+'int main(void){unsigned n=proof();assert(n==80000);printf("Borrow clip80000 triangles, bytes/all four rounding modes/FP status PASS\\n");}\n')
  flags=['clang','-O2','-std=c11','-frounding-math','-ffp-contract=off','-fsanitize=address,undefined','-I'+str(root/'wii')]
  source=str(root/'wii/wdepth_split.c')
  reference_flags=['-DVIPER_WII_WDEPTH_BORROW_CLIP'] if args.copy_once else []
  candidate_flags=['-DVIPER_WII_WDEPTH_COPY_ONCE'] if args.copy_once else []
  subprocess.run(flags+reference_flags+['-DVIPER_WII_WDEPTH_INTERIOR','-Dwii_wdepth_split=reference_split','-Dwii_wdepth_band=reference_band','-c',source,'-o',str(p/'reference.o')],check=True)
  subprocess.run(flags+candidate_flags+['-DVIPER_WII_WDEPTH_INTERIOR','-DVIPER_WII_WDEPTH_BORROW_CLIP','-c',source,'-o',str(p/'candidate.o')],check=True)
  subprocess.run(flags+[str(p/'test.c'),str(p/'reference.o'),str(p/'candidate.o'),'-lm','-o',str(p/'test')],check=True)
  subprocess.run([str(p/'test')],check=True)
