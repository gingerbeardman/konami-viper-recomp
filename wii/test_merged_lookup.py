#!/usr/bin/env python3
"""Check actual GX lookup helpers against original quarter tables on the host.
This validates texels/tiling and ideal sampling; native varying-plane rounding
and geometry/rasterization equivalence remain unproven.
"""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent
renderer=(root/'gx_renderer.c').read_text()
start=renderer.index('#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP\nenum { DEPTH_TABLE_HEIGHT=')
helpers=renderer[start:renderer.index('static uint8_t *depth_tables;',start)]
depth=renderer[renderer.index('static unsigned wdepth(float w) {'):renderer.index('static uint64_t wdepth_approximations;')]
harness=r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "wdepth_split.h"
#include "projective_texture.h"
#include "fog.h"
#define GX_REPEAT 1
#define GX_CLAMP 0
static float screen_y(const WiiVoodooView *view,float y){(void)view;return y;}
'''+depth+helpers+r'''
static uint32_t seed=0xf71ca891;
static uint32_t next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
static float reference_sample(unsigned band,unsigned x){
 float lo=ldexpf(.5f+(band%4)*.125f,-(int)(band/4));
 float hi=ldexpf(.5f+(band%4+1)*.125f,-(int)(band/4));
 return lo+(hi-lo)*((x+.5f)/1024);
}
static unsigned ideal_column(float s){float f=s-floorf(s);return (unsigned)(f*1024);}
int main(void){
 static uint8_t image[1024*8*4],seen[1024*8*4];
 unsigned texels=0,coordinates=0,fogcases=0;
 for(unsigned index=0;index<DEPTH_TABLE_COUNT;index++){
  memset(image,0,sizeof image);memset(seen,0,sizeof seen);
  unsigned first=lookup_band(index);assert(lookup_index(first)==index);
  for(unsigned y=0;y<DEPTH_TABLE_HEIGHT;y++)for(unsigned x=0;x<1024;x++){
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
   unsigned band=first+(y<4?y:3),sx=y<4?x:1023;
#else
   unsigned band=first,sx=x;
#endif
   float actual=lookup_sample(index,x,y),expected=reference_sample(band,sx);
   assert(!memcmp(&actual,&expected,sizeof actual));
   unsigned o=lookup_offset(x,y);assert(o+33<DEPTH_TABLE_BYTES);
   const unsigned offsets[]={o,o+1,o+32,o+33};
   for(unsigned c=0;c<4;c++){assert(!seen[offsets[c]]);seen[offsets[c]]=1;}
   unsigned d=wdepth(actual);image[o]=255;image[o+1]=d>>8;image[o+32]=d;image[o+33]=0;texels++;
  }
  for(unsigned i=0;i<DEPTH_TABLE_BYTES;i++)assert(seen[i]);
  float lo,hi;assert(wii_wdepth_render_band(first,&lo,&hi));
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
  const unsigned bins=4096;
#else
  const unsigned bins=1024;
#endif
  for(unsigned n=0;n<=bins;n++){
   unsigned bin=n<bins?n:bins-1;
   WiiVoodooVertex p={0};p.x=23;p.y=45;p.wb=n<bins?lo+(hi-lo)*((n+.5f)/bins):hi;
   WiiProjectiveVertex pv=lookup_vertex(NULL,&p,lo,hi);
   assert(pv.x==p.x&&pv.y==p.y&&pv.w==1);
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
   unsigned x=ideal_column(pv.s),y=(unsigned)(pv.t*8),band=first+bin/1024,sx=bin%1024;
#else
   unsigned x=(unsigned)(pv.s*1024),y=2,band=first,sx=bin;if(x>1023)x=1023;
#endif
   assert(y<DEPTH_TABLE_HEIGHT);unsigned o=lookup_offset(x,y);
   unsigned d=(image[o+1]<<8)|image[o+32];
   unsigned reference=wdepth(reference_sample(band,sx))&65535u;
   assert(d==reference);coordinates++;
  }
  for(unsigned f=0;f<32;f++){
   uint32_t table[32];for(unsigned k=0;k<32;k++)table[k]=next();
   for(unsigned n=0;n<bins;n++){
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
    unsigned y=n/1024,x=n%1024,band=first+y;
#else
    unsigned y=0,x=n,band=first;
#endif
    unsigned actual=wii_fog_tev_factor(wii_fog_factor(table,wdepth(lookup_sample(index,x,y)),8));
    unsigned expected=wii_fog_tev_factor(wii_fog_factor(table,wdepth(reference_sample(band,x)),8));
    assert(actual==expected);fogcases++;
   }
  }
 }
 printf("Actual GX lookup helpers PASS tables=%u texels=%u ideal_samples=%u randomized_fog_samples=%u\n",DEPTH_TABLE_COUNT,texels,coordinates,fogcases);
}
'''
with tempfile.TemporaryDirectory(prefix='merged-lookup-') as d:
 d=Path(d);(d/'proof.c').write_text(harness)
 for name,flags in [('original',[]),('merged',['-DVIPER_WII_WDEPTH_MERGED_LOOKUP'])]:
  exe=d/name
  subprocess.run(['clang','-O2','-std=c11','-fsanitize=address,undefined','-fno-omit-frame-pointer','-ffp-contract=off','-I'+str(root),*flags,str(d/'proof.c'),str(root/'wdepth_split.c'),'-lm','-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
