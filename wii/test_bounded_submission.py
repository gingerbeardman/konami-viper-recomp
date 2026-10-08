#!/usr/bin/env python3
"""Differential execution of the actual 87-instruction vertex entry block."""
from pathlib import Path
import argparse
import subprocess
import tempfile
from specialize_bounded_submission import regions

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--emit-native', type=Path)
args = parser.parse_args()
_, _, original, replacement = regions((root/'generated/gticlub2/gl_000.c').read_text())
preamble = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include "runtime.h"
#include "bounded_submission.h"
uint8_t *g_ram;
static unsigned mmio_calls;
uint32_t rt_mmio_r32(uint32_t ea){(void)ea;mmio_calls++;return 0;}
uint32_t rt_mmio_r8(uint32_t ea){(void)ea;mmio_calls++;return 0;}
'''
functions = ('static void reference(PPCContext *c){\n'+original+'}\n'
             'static void candidate(PPCContext *c){\n'+replacement+'}\n')
common = r'''
static unsigned char backing[0x5404] __attribute__((aligned(32)));
static unsigned char saved[0x5404];
static uint32_t rng=0x16813ab1;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static unsigned proof(void){
 unsigned cases=0;
 const uint32_t special[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00001,0x7fa00001};
 for(unsigned rn=0;rn<4;rn++)for(unsigned sticky=0;sticky<STICKY_COUNT;sticky++)
 for(unsigned n=0;n<CASE_COUNT;n++){
  /* Both aligned fast and deliberately misaligned fallback backing. */
  unsigned offset=n%7==0?1:0;g_ram=backing+offset;
  for(unsigned i=0;i<0x5400;i+=4){uint32_t bits;
   if(n%3==0)bits=special[(i/4+n)%11];
   else if(n%3==1)bits=(next()&0x807fffff)|((next()%60+95)<<23);
   else bits=next();
   bits=guest_be32(bits);memcpy(g_ram+i,&bits,4);
  }
  g_ram[0x3451]=(uint8_t)(CASE_COUNT==65536?n>>8:n/256==0?0:n/256==1?255:n/256==2?n:255-n);
  g_ram[0x3452]=(uint8_t)n;
  memcpy(saved,backing,sizeof backing);
  PPCContext a,b;unsigned char *bytes=(unsigned char *)&a;
  for(unsigned i=0;i<sizeof a;i++)bytes[i]=(unsigned char)next();
  /* All doubles which the block reads are loaded in the block itself. */
  b=a;errno=0;set_status(incoming(rn,sticky));reference(&a);
  uint32_t expected_status=get_status();int expected_errno=errno;
  errno=0;set_status(incoming(rn,sticky));candidate(&b);
  uint32_t actual_status=get_status();
  if(memcmp(&a,&b,sizeof a)||expected_status!=actual_status||errno!=expected_errno||
     memcmp(saved,backing,sizeof backing)||mmio_calls){
   printf("Bounded entry mismatch n=%u RN=%u sticky=%u align=%u status=%08x/%08x\n",n,rn,sticky,offset,expected_status,actual_status);return 0;
  }
  cases++;
 }
 return cases;
}
'''
host = r'''
#include <fenv.h>
#define CASE_COUNT 65536
#define STICKY_COUNT 2
static uint32_t incoming(unsigned rn,unsigned sticky){return rn|(sticky?256u:0u);}
static void set_status(uint32_t s){const int rn[]={FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};fesetround(rn[s&3]);feclearexcept(FE_ALL_EXCEPT);if(s&256)feraiseexcept(FE_INEXACT);}
static uint32_t get_status(void){return fetestexcept(FE_ALL_EXCEPT);}
'''
native = r'''
#include <gccore.h>
#define CASE_COUNT 1024
#define STICKY_COUNT 4
static uint32_t incoming(unsigned rn,unsigned sticky){const uint32_t flags[]={0,0x82000000,0x9a000000,0x9a060000};return flags[sticky]|rn;}
static uint32_t get_status(void){double f;uint64_t bits;__asm__ volatile("mffs %0":"=f"(f)::"memory");memcpy(&bits,&f,8);return (uint32_t)bits;}
static void set_status(uint32_t s){uint64_t bits=s;double f;memcpy(&f,&bits,8);__asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");}
'''
if args.emit_native:
    main = r'''
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();if(mode->viTVMode&VI_NON_INTERLACE)VIDEO_WaitVSync();
 printf("VIPER WII BOUNDED ENTRY DIFFERENTIAL\n");unsigned n=proof();printf("VIPER WII BOUNDED ENTRY %s cases=%u FULL FPSCR\n",n==16384?"PASS":"FAIL",n);for(;;)VIDEO_WaitVSync();
}
'''
    args.emit_native.parent.mkdir(parents=True, exist_ok=True)
    args.emit_native.write_text(preamble+native+functions+common+main)
else:
    with tempfile.TemporaryDirectory(prefix='viper-bounded-entry-') as d:
        p=Path(d)
        main='int main(void){unsigned n=proof();assert(n==524288);printf("Bounded entry PASS: %u full context/RAM/FP cases\\n",n);}\n'
        (p/'test.c').write_text(preamble+host+functions+common+main)
        flags=['clang','-O2','-std=c11','-frounding-math','-ffp-contract=off',
               '-fsanitize=address,undefined','-DVIPER_WII_BOUNDED_SUBMISSION',
               '-I'+str(root/'wii'),'-I'+str(root/'runtime'),'-I'+str(root/'tests/fixtures')]
        subprocess.run(flags+[str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
