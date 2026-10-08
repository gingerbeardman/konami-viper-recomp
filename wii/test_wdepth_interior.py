"""Compare unchanged-triangle clipping shortcut with the original splitter."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
harness=r'''
#include "wdepth_split.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int reference_split(const WiiVoodooVertex*,WiiWDepthTriangle*,unsigned,unsigned*);
static uint32_t rng=7;
static float random_unit(void){rng=rng*1664525u+1013904223u;return (rng>>8)*0x1p-24f;}
int main(void){
 for(unsigned case_id=0;case_id<100000;case_id++){
  float lo,hi;assert(wii_wdepth_band(case_id%66,&lo,&hi));
  if(case_id%66==65)hi=2;
  WiiVoodooVertex v[3]={0};
  for(unsigned j=0;j<3;j++){
   float *fields=(float*)&v[j];
   for(unsigned k=0;k<sizeof(v[j])/sizeof(float);k++)fields[k]=(random_unit()-.5f)*512;
   switch(case_id%5){
    case 0:v[j].wb=lo+(hi-lo)*(.1f+.8f*random_unit());break;
    case 1:v[j].wb=j==0?lo:j==1?hi:(lo+hi)/2;break;
    case 2:v[j].wb=(lo+hi)/2;break;
    case 3:v[j].wb=ldexpf(random_unit(),-(int)(case_id%20));break;
    default:v[j].wb=j==0?nextafterf(lo,0):j==1?nextafterf(hi,INFINITY):(lo+hi)/2;break;
   }
  }
  if(case_id%17==0)v[2]=v[1];
  WiiWDepthTriangle expected[WII_WDEPTH_SPLIT_MAX],actual[WII_WDEPTH_SPLIT_MAX];
  unsigned en=999,an=999,cap=case_id%19==0?0:WII_WDEPTH_SPLIT_MAX;
  int e=reference_split(v,expected,cap,&en),a=wii_wdepth_split(v,actual,cap,&an);
  assert(a==e&&an==en);assert(!memcmp(expected,actual,en*sizeof(*expected)));
 }
 puts("W-depth interior differential PASS: 100000 triangles");
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(harness)
 flags=['clang','-O2','-std=c11','-ffp-contract=off','-fsanitize=address,undefined','-I'+str(root/'wii')]
 source=str(root/'wii/wdepth_split.c')
 subprocess.run(flags+['-Dwii_wdepth_split=reference_split','-Dwii_wdepth_band=reference_band','-c',source,'-o',str(p/'reference.o')],check=True)
 subprocess.run(flags+['-DVIPER_WII_WDEPTH_INTERIOR','-c',source,'-o',str(p/'candidate.o')],check=True)
 subprocess.run(flags+[str(p/'test.c'),str(p/'reference.o'),str(p/'candidate.o'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
