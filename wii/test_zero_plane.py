"""Bitwise differential check of actual plane solver under four rounding modes."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
harness=r'''
#include "projective_texture.h"
#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma STDC FENV_ACCESS ON
int reference_matrix(float[3][4],const WiiProjectiveVertex[3],float,float);
static uint32_t seed=11;
static float random_value(void){seed=seed*1664525u+1013904223u;return (float)(int32_t)seed*0x1p-22f;}
int main(void){
 int modes[]={FE_TONEAREST,FE_UPWARD,FE_DOWNWARD,FE_TOWARDZERO};unsigned cases=0;
 for(unsigned mode=0;mode<4;mode++){
  assert(fesetround(modes[mode])==0);
  for(unsigned n=0;n<50000;n++){
   WiiProjectiveVertex v[3];
   for(unsigned i=0;i<3;i++)v[i]=(WiiProjectiveVertex){random_value(),random_value(),random_value(),random_value(),fabsf(random_value())+.125f};
   if(n%3==0)for(unsigned i=0;i<3;i++){v[i].t=.5f;v[i].w=1;}
   if(n%7==0)for(unsigned i=0;i<3;i++)v[i].s=(n&1)?-0.0f:0.0f;
   if(n%11==0)for(unsigned i=0;i<3;i++)v[i].t=i&1?-0.0f:0.0f;
   if(n%17==0){WiiProjectiveVertex t=v[1];v[1]=v[2];v[2]=t;}
   if(n%29==0)v[2]=v[1];
   if(n%31==0)v[0].w=0;
   if(n%37==0)v[0].s=NAN;
   float expected[3][4],actual[3][4];memset(expected,0x53,sizeof expected);memcpy(actual,expected,sizeof actual);
   float ss=n%2?1:0x1p-8f,ts=n%2?1:0x1p-7f;
   int e=reference_matrix(expected,v,ss,ts),a=wii_projective_texture_matrix(actual,v,ss,ts);
   assert(a==e);assert(!memcmp(expected,actual,sizeof actual));cases++;
  }
 }
 assert(fesetround(FE_TONEAREST)==0);printf("Zero plane differential PASS: %u cases, four rounding modes\n",cases);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness)
 flags=['clang','-O2','-std=c11','-ffp-contract=off','-frounding-math','-fsanitize=address,undefined','-I'+str(root/'wii')]
 source=str(root/'wii/projective_texture.c')
 subprocess.run(flags+['-Dwii_projective_texture_matrix=reference_matrix','-c',source,'-o',str(p/'reference.o')],check=True)
 subprocess.run(flags+['-DVIPER_WII_ZERO_PLANE_QUOTIENT','-c',source,'-o',str(p/'candidate.o')],check=True)
 subprocess.run(flags+[str(p/'test.c'),str(p/'reference.o'),str(p/'candidate.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
