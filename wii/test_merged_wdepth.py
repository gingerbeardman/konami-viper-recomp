#!/usr/bin/env python3
"""Geometry invariants for the opt-in merged lookup spike; not pixel equivalence."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
source=r'''
#include "wdepth_split.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int reference_split(const WiiVoodooVertex*,WiiWDepthTriangle*,unsigned,unsigned*);
static uint32_t seed=0x387abc91;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float unit(void){return (next()>>8)*0x1p-24f;}
static double area(const WiiVoodooVertex *p){return ((double)p[1].x-p[0].x)*((double)p[2].y-p[0].y)-((double)p[1].y-p[0].y)*((double)p[2].x-p[0].x);}
int main(void){
 static WiiWDepthTriangle original[198],merged[198];
 unsigned long old_count=0,new_count=0;
 for(unsigned c=0;c<10000;c++){
  WiiVoodooVertex p[3];memset(p,0,sizeof p);
  p[0].x=0;p[0].y=0;p[1].x=512;p[1].y=0;p[2].x=0;p[2].y=512;
  for(unsigned j=0;j<3;j++){
   p[j].wb=ldexpf(.5f+.5f*unit(),-(int)(next()%18));
   p[j].r=100+10*p[j].x+20*p[j].y;p[j].s=2*p[j].x-3*p[j].y;
  }
  if(c%7==0){float lo,hi;assert(wii_wdepth_band(c%66,&lo,&hi));for(unsigned j=0;j<3;j++)p[j].wb=lo;}
  unsigned on=0,mn=0;assert(reference_split(p,original,198,&on));assert(wii_wdepth_split(p,merged,198,&mn));
  double sum=0;
  for(unsigned i=0;i<mn;i++){
   unsigned b=merged[i].band;assert(b>=64||!(b&3));float lo,hi;assert(wii_wdepth_render_band(b,&lo,&hi));
   double a=area(merged[i].v);assert(a>0);sum+=a;
   for(unsigned j=0;j<3;j++){
    const WiiVoodooVertex *v=&merged[i].v[j];assert(v->wb>=lo&&v->wb<=hi);
    assert(fabs(v->r-(100+10*(double)v->x+20*(double)v->y))<.002);
    assert(fabs(v->s-(2*(double)v->x-3*(double)v->y))<.0003);
   }
  }
  assert(fabs(sum-area(p))<.2);old_count+=on;new_count+=mn;
 }
 assert(new_count<old_count);
 WiiVoodooVertex invalid[3]={{0}};unsigned n=99;invalid[0].wb=NAN;assert(!wii_wdepth_split(invalid,merged,198,&n)&&n==0);
 printf("Merged W-depth geometry PASS cases=10000 pieces=%lu/%lu; GPU sampling and pixel equivalence unproven\n",new_count,old_count);
}
'''
with tempfile.TemporaryDirectory(prefix='wdepth-merged-') as d:
 d=Path(d);(d/'test.c').write_text(source)
 flags=['clang','-O2','-std=c11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-I'+str(root)]
 subprocess.run(flags+['-Dwii_wdepth_split=reference_split','-Dwii_wdepth_band=reference_band','-c',str(root/'wdepth_split.c'),'-o',str(d/'reference.o')],check=True)
 subprocess.run(flags+['-DVIPER_WII_WDEPTH_MERGED_LOOKUP','-DVIPER_WII_WDEPTH_BORROW_CLIP',str(d/'test.c'),str(root/'wdepth_split.c'),str(d/'reference.o'),'-lm','-o',str(d/'test')],check=True)
 subprocess.run([str(d/'test')],check=True)
