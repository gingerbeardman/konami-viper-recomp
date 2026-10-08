"""Memoized lookup matrices must match the production solver bit for bit."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
code=r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include "wii/lookup_plane_cache.h"
static unsigned seed=101;
static unsigned next(void){seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;return seed;}
int main(void){
 WiiLookupPlaneCache cache={0};unsigned hits=0;
 for(unsigned n=0;n<100000;n++){
  WiiProjectiveVertex v[3]={{(float)(next()%600),0,0,.5f,1},{640,0,0,.5f,1},{0,480,0,.5f,1}};
  for(unsigned i=0;i<3;i++)v[i].s=(float)(next()%10000)/8192;
  cache.valid=0;float reference[3][4],result[3][4];
  assert(wii_projective_texture_matrix(reference,v,1,1));
  assert(!wii_lookup_plane_get(&cache,v,result));
  wii_lookup_plane_store(&cache,v,reference);
  for(unsigned draw=0;draw<3;draw++){
   assert(wii_lookup_plane_get(&cache,v,result));
   assert(!memcmp(reference,result,sizeof result));hits++;
  }
  v[1].s=nextafterf(v[1].s,INFINITY);
  assert(!wii_lookup_plane_get(&cache,v,result));
  cache.valid=0;assert(!wii_lookup_plane_get(&cache,cache.vertices,result));
 }
 WiiProjectiveVertex zero[3]={{0,0,0,.5f,1},{1,0,1,.5f,1},{0,1,0,.5f,1}};
 float matrix[3][4];assert(wii_projective_texture_matrix(matrix,zero,1,1));wii_lookup_plane_store(&cache,zero,matrix);
 zero[0].s=-0.f;assert(!wii_lookup_plane_get(&cache,zero,matrix));
 zero[1]=zero[0];cache.valid=0;
 assert(!wii_projective_texture_matrix(matrix,zero,1,1));assert(!cache.valid);
 printf("lookup plane cache PASS triangles=100000 exact_reuses=%u signed_zero/mutation/reset/failure\n",hits);
}
'''
with tempfile.TemporaryDirectory(prefix='viper-lookup-') as d:
    src=Path(d)/'test.c';binary=Path(d)/'test';src.write_text(code)
    subprocess.run(['clang','-O2','-std=c11','-Wall','-Wextra','-Werror','-ffp-contract=off','-fsanitize=address,undefined','-I',str(root),str(src),str(root/'wii/projective_texture.c'),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
