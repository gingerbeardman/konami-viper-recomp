#include "projective_texture.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void check(WiiProjectiveVertex v[3]){
 float m[3][4];assert(wii_projective_texture_matrix(m,v,1.0f/256,1.0f/128));
 for(unsigned a=0;a<=10;a++)for(unsigned b=0;b<=10-a;b++){
  double l[3]={a/10.0,b/10.0,(10-a-b)/10.0},x=0,y=0,s=0,t=0,w=0;
  for(unsigned i=0;i<3;i++){x+=l[i]*v[i].x;y+=l[i]*v[i].y;s+=l[i]*v[i].s;t+=l[i]*v[i].t;w+=l[i]*v[i].w;}
  double q=m[2][0]*x+m[2][1]*y+m[2][3];assert(q>0);
  double u=(m[0][0]*x+m[0][1]*y+m[0][3])/q;
  double vv=(m[1][0]*x+m[1][1]*y+m[1][3])/q;
  assert(fabs(u-s/256/w)<2e-5*(1+fabs(u)));
  assert(fabs(vv-t/128/w)<2e-5*(1+fabs(vv)));
  for(unsigned row=0;row<3;row++)assert(m[row][2]==0);
 }
}
int main(void){
 WiiProjectiveVertex v[3]={{20,40,12,21,.25f},{300,55,70,8,.5f},{80,320,-3,90,1}};
 check(v);WiiProjectiveVertex temp=v[1];v[1]=v[2];v[2]=temp;check(v);
 for(unsigned i=0;i<3;i++)v[i].y=384-v[i].y;check(v);
 for(unsigned i=0;i<3;i++)v[i].w=1;check(v);
 float m[3][4],saved[3][4];memset(m,0x53,sizeof m);memcpy(saved,m,sizeof m);
 v[2]=v[1];assert(!wii_projective_texture_matrix(m,v,1,1));assert(!memcmp(m,saved,sizeof m));
 v[2]=(WiiProjectiveVertex){5,9,1,2,0};assert(!wii_projective_texture_matrix(m,v,1,1));
 v[2].w=NAN;assert(!wii_projective_texture_matrix(m,v,1,1));
 v[2].w=1;v[2].s=INFINITY;assert(!wii_projective_texture_matrix(m,v,1,1));
 assert(!wii_projective_texture_matrix(m,v,0,1));
 puts("Wii projective texture numerical tests PASS");return 0;
}
