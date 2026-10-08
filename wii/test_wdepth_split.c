#include "wdepth_split.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
static void captured_triangle(void){
 const uint32_t bits[3][14]={
  {0x438bdc6e,0x4389616a,0x43872799,0x43912799,0x43a5a799,0x437f0000,0x3affc142,0x3ef0267c,0x3f752749,0,0x3c2019a8,0x3c2019a8,0x3ef0267c,0x3f752749},
  {0x438bd247,0x4389884a,0x43872799,0x43912799,0x43a5a799,0x437f0000,0x3afee606,0x3e01a584,0x3f74557a,0,0x3c1f90a3,0x3c1f90a3,0x3e01a584,0x3f74557a},
  {0x438be6a6,0x43893a48,0x43872799,0x43912799,0x43a5a799,0x437f0000,0x3b004efb,0x3f4dd19c,0x3f75fa82,0,0x3c20a399,0x3c20a399,0x3f4dd19c,0x3f75fa82}};
 WiiVoodooVertex v[3];memcpy(v,bits,sizeof v);
 WiiWDepthTriangle out[WII_WDEPTH_SPLIT_MAX];unsigned n;
 for(unsigned reverse=0;reverse<2;reverse++){
  assert(wii_wdepth_split(v,out,WII_WDEPTH_SPLIT_MAX,&n)&&n>0);
  for(unsigned i=0;i<n;i++){
   WiiVoodooVertex *p=out[i].v;
   double a=((double)p[1].x-p[0].x)*(p[2].y-p[0].y)-((double)p[1].y-p[0].y)*(p[2].x-p[0].x);
   assert(a!=0&&(a<0)==reverse);
   float lo,hi;assert(wii_wdepth_band(out[i].band,&lo,&hi));
   for(unsigned j=0;j<3;j++)assert(p[j].wb>=lo&&p[j].wb<=hi&&p[j].a==255);
  }
  WiiVoodooVertex t=v[1];v[1]=v[2];v[2]=t;
 }
}
static double area(WiiVoodooVertex a,WiiVoodooVertex b,WiiVoodooVertex c){return ((double)b.x-a.x)*(c.y-a.y)-((double)b.y-a.y)*(c.x-a.x);}
static void test(float wa,float wb,float wc,int reverse){
 WiiVoodooVertex v[3]={{0}};v[0].wb=wa;v[1].x=400;v[1].wb=wb;v[2].y=300;v[2].wb=wc;
 for(unsigned i=0;i<3;i++){v[i].r=2*v[i].x+v[i].y;v[i].s1=3*v[i].x-v[i].y;v[i].w0=.5f;v[i].a=255;}
 if(reverse){WiiVoodooVertex t=v[1];v[1]=v[2];v[2]=t;}
 WiiWDepthTriangle out[WII_WDEPTH_SPLIT_MAX];unsigned n;assert(wii_wdepth_split(v,out,WII_WDEPTH_SPLIT_MAX,&n));assert(n>0);
 double sum=0;
 for(unsigned i=0;i<n;i++){
  float lo,hi;assert(wii_wdepth_band(out[i].band,&lo,&hi));double ar=area(out[i].v[0],out[i].v[1],out[i].v[2]);assert((ar<0)==reverse);sum+=ar;
  for(unsigned j=0;j<3;j++){WiiVoodooVertex p=out[i].v[j];assert(p.wb>=lo&&p.wb<=hi);assert(fabs(p.r-(2*p.x+p.y))<.001);assert(fabs(p.s1-(3*p.x-p.y))<.001);assert(p.a==255&&p.w0==.5f);}
 }
 assert(fabs(sum-area(v[0],v[1],v[2]))<.1);
 if(wa==wb&&wb==wc)assert(n==1);
 assert(!wii_wdepth_split(v,out,0,&n)&&n==0);
}
int main(void){
 /* Independent arithmetic oracle for every bit-constructed boundary. */
 for(unsigned b=0;b<64;b++){
  float lo,hi;
  assert(wii_wdepth_band(b,&lo,&hi));
  assert(lo==ldexpf(.5f+(b%4)*.125f,-(int)(b/4)));
  assert(hi==ldexpf(.5f+(b%4+1)*.125f,-(int)(b/4)));
 }
 float lo,hi;
 assert(!wii_wdepth_band(66,&lo,&hi));
 assert(!wii_wdepth_band(0,NULL,&hi));
 assert(!wii_wdepth_band(0,&lo,NULL));
 captured_triangle();
 test(.51f,.62f,.55f,0);test(0x1p-18f,1.2f,.32f,0);test(0x1p-18f,1.2f,.32f,1);
 for(unsigned b=0;b<64;b++){float lo,hi;wii_wdepth_band(b,&lo,&hi);test(lo,lo,lo,0);test(lo,(lo+hi)/2,hi,0);}
 test(1,1,1,0);test(0x1p-17f,0x1p-17f,0x1p-17f,0);
 test(0,.62f,.32f,0);test(0,.62f,.32f,1);test(0,0,0,0);
 WiiVoodooVertex bad[3]={{0}};WiiWDepthTriangle out[198];unsigned n=42;
 bad[0].wb=-1;
 assert(!wii_wdepth_split(bad,out,198,&n)&&!n);
 for(unsigned i=0;i<3;i++)bad[i].wb=1;
 assert(wii_wdepth_split(bad,out,198,&n)&&!n);bad[0].s=NAN;assert(!wii_wdepth_split(bad,out,198,&n));
 puts("W-depth split tests PASS");
}
