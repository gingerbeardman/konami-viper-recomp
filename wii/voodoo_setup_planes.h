/* SPDX-License-Identifier: BSD-3-Clause
 * Derived from MAME Voodoo setup and triangle preparation (Aaron Giles).
 * Pure state model, not a GX vertex/interpolation equivalence claim.
 * Compile with -ffp-contract=off; do not enable reassociation/fast-math. */
#ifndef VIPER_WII_VOODOO_SETUP_PLANES_H
#define VIPER_WII_VOODOO_SETUP_PLANES_H
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <limits.h>
enum { WII_PLANE_R,WII_PLANE_G,WII_PLANE_B,WII_PLANE_A,WII_PLANE_Z,
 WII_PLANE_WB,WII_PLANE_S0,WII_PLANE_T0,WII_PLANE_W0,
 WII_PLANE_S1,WII_PLANE_T1,WII_PLANE_W1,WII_PLANE_COUNT };
typedef struct { int64_t start,dx,dy; } WiiSetupPlane;
typedef struct { WiiSetupPlane plane[WII_PLANE_COUNT];int16_t xy[3][2]; } WiiSetupPlanes;
typedef struct { float x,y,r,g,b,a,z,wb,s0,t0,w0,s1,t1,w1; } WiiSetupVertex;
static inline int32_t wii_setup_s24(int64_t x){uint32_t u=(uint32_t)x&0xffffffu;return u&0x800000u?(int32_t)u-0x1000000:(int32_t)u;}
static inline int64_t wii_setup_floor16(int64_t x){return x>=0?x/16:-1-(-(x+1))/16;}
static inline int wii_setup_i64(float x,int bits,int64_t *out){
 if(!isfinite(x))return 0;
 if(bits==32){if((double)x<-2147483648.0||(double)x>=2147483648.0)return 0;}
 else if((double)x<-9223372036854775808.0||(double)x>=9223372036854775808.0)return 0;
 *out=(int64_t)x;return 1;
}
static inline int wii_setup_make(WiiSetupPlane *out,float a,float b,float c,
 float dx1,float dx2,float dy1,float dy2,float scale,float div,int bits){
 /* Volatile float materialization makes the reference's f32 operation order
  * explicit even on hosts with excess precision. */
 volatile float d1=a-b,d2=a-c;
 volatile float x1=d1*dx1,x2=d2*dx2,y1=d2*dy1,y2=d1*dy2;
 volatile float xd=x1-x2,yd=y1-y2;
 volatile float start=a*scale,x=xd*div,y=yd*div;
 return wii_setup_i64(start,bits,&out->start)&&wii_setup_i64(x,bits,&out->dx)&&wii_setup_i64(y,bits,&out->dy);
}
/* Call only after reference culling acceptance, once per original triangle.
 * Failure leaves state unchanged. Invalid/degenerate/overflowing reference
 * conversions are rejected instead of reproducing undefined C++ casts. */
static inline int wii_setup_update(WiiSetupPlanes *state,const WiiSetupVertex v[3],unsigned attributes){
 if(attributes>255)return 0;
 WiiSetupPlanes next=*state;
 for(unsigned i=0;i<3;i++)for(unsigned j=0;j<2;j++){
  float x=j?v[i].y:v[i].x;volatile float q=x*16.0f;
  if(!isfinite(q)||q<-32768.0f||q>=32768.0f)return 0;
  next.xy[i][j]=(int16_t)q;
 }
 volatile float xa=v[0].x-v[1].x,ya=v[0].y-v[2].y;
 volatile float xb=v[0].x-v[2].x,yb=v[0].y-v[1].y;
 volatile float a=xa*ya,b=xb*yb,det=a-b;
 if(!isfinite(det)||det==0)return 0;
 volatile float reciprocal=1.0f/det;
 volatile float dx1=v[0].y-v[2].y,dx2=v[0].y-v[1].y;
 volatile float dy1=v[0].x-v[1].x,dy2=v[0].x-v[2].x;
 volatile float small_div=4096.0f*reciprocal,wide_div=4294967296.0f*reciprocal;
 #define WII_SETUP_MAKE(index,field,wide) do { \
 if(!wii_setup_make(&next.plane[index],v[0].field,v[1].field,v[2].field,dx1,dx2,dy1,dy2, \
 (wide)?4294967296.0f:4096.0f,(wide)?wide_div:small_div,(wide)?64:32))return 0; } while(0)
 if(attributes&1){WII_SETUP_MAKE(WII_PLANE_R,r,0);WII_SETUP_MAKE(WII_PLANE_G,g,0);WII_SETUP_MAKE(WII_PLANE_B,b,0);}
 if(attributes&2){WII_SETUP_MAKE(WII_PLANE_A,a,0);}
 if(attributes&4){WII_SETUP_MAKE(WII_PLANE_Z,z,0);}
 if(attributes&8){WII_SETUP_MAKE(WII_PLANE_WB,wb,1);next.plane[WII_PLANE_W0]=next.plane[WII_PLANE_W1]=next.plane[WII_PLANE_WB];}
 if(attributes&16){WII_SETUP_MAKE(WII_PLANE_W0,w0,1);next.plane[WII_PLANE_W1]=next.plane[WII_PLANE_W0];}
 if(attributes&32){WII_SETUP_MAKE(WII_PLANE_S0,s0,1);WII_SETUP_MAKE(WII_PLANE_T0,t0,1);next.plane[WII_PLANE_S1]=next.plane[WII_PLANE_S0];next.plane[WII_PLANE_T1]=next.plane[WII_PLANE_T0];}
 if(attributes&64){WII_SETUP_MAKE(WII_PLANE_W1,w1,1);}
 if(attributes&128){WII_SETUP_MAKE(WII_PLANE_S1,s1,1);WII_SETUP_MAKE(WII_PLANE_T1,t1,1);}
 #undef WII_SETUP_MAKE
 *state=next;return 1;
}
/* Fetch register semantics: RGB/A are signed24, Z signed32, W/ST signed64. */
static inline WiiSetupPlane wii_setup_read(const WiiSetupPlanes *s,unsigned i){
 WiiSetupPlane p=s->plane[i];if(i<4){p.start=wii_setup_s24(p.start);p.dx=wii_setup_s24(p.dx);p.dy=wii_setup_s24(p.dy);}return p;
}
/* Snapshot draw values and optionally mutate persistent starts, exactly once.
 * The current snapshot can exceed signed24 after adjustment; the NEXT read
 * sign-extends the stored low24, matching MAME's register getter boundary.
 * Conservative arithmetic-overflow rejection is transactional. */
static inline int wii_setup_prepare(WiiSetupPlanes *s,int subpixel,WiiSetupPlanes *draw){
 if(s==draw)return 0; /* Snapshot and persistent register state must be distinct. */
 WiiSetupPlanes next=*s,out=*s;
 int dx=8-((uint16_t)s->xy[0][0]&15),dy=8-((uint16_t)s->xy[0][1]&15);
 for(unsigned i=0;i<WII_PLANE_COUNT;i++){
  WiiSetupPlane p=wii_setup_read(s,i);
  if(subpixel){int64_t x,y,sum,adjust,value;
   if(__builtin_mul_overflow(p.dx,(int64_t)dx,&x)||__builtin_mul_overflow(p.dy,(int64_t)dy,&y)||__builtin_add_overflow(x,y,&sum))return 0;
   if(i<=WII_PLANE_Z&&(sum<INT32_MIN||sum>INT32_MAX))return 0;
   adjust=wii_setup_floor16(sum);
   if(__builtin_add_overflow(p.start,adjust,&value))return 0;
   if(i<=WII_PLANE_Z&&(value<INT32_MIN||value>INT32_MAX))return 0;
   p.start=value;next.plane[i].start=value;
  }
  out.plane[i]=p;
 }
 *s=next;*draw=out;return 1;
}
/* Full-word direct setup writes only. Partial bus writes must first be merged
 * by the device. Float aliases map a0..fc to20..7c. Integer S/T are14.18;
 * integer W is2.30. Chip bits: FBI1,TMU0=2,TMU1=4. Unsupported offsets return0.
 * Finite float conversion truncates toward zero; exceptional MAME float
 * conversion behavior is deliberately rejected, not approximated. */
static inline int wii_setup_write(WiiSetupPlanes *s,unsigned address,uint32_t raw,unsigned chips){
 int floating=address>=0xa0&&address<=0xfc;if(floating)address-=0x80;
 if(address<0x20||address>0x7c||(address&3))return 0;
 unsigned component=(address-0x20)/32,lane=((address-0x20)%32)/4;
 int64_t value;
 if(floating){float f;memcpy(&f,&raw,4);volatile float scaled=f*(lane==5||lane==6||lane==7?4294967296.0f:4096.0f);
  if(!wii_setup_i64(scaled,lane>=5?64:32,&value))return 0;
 }else {value=(int32_t)raw;if(lane==5||lane==6)value*=16384;else if(lane==7)value*=4;}
 unsigned indices[3],count=0;
 if(lane<5){static const unsigned map[5]={WII_PLANE_R,WII_PLANE_G,WII_PLANE_B,WII_PLANE_Z,WII_PLANE_A};if(chips&1)indices[count++]=map[lane];}
 else if(lane==7){if(chips&1)indices[count++]=WII_PLANE_WB;if(chips&2)indices[count++]=WII_PLANE_W0;if(chips&4)indices[count++]=WII_PLANE_W1;}
 else {if(chips&2)indices[count++]=lane==5?WII_PLANE_S0:WII_PLANE_T0;if(chips&4)indices[count++]=lane==5?WII_PLANE_S1:WII_PLANE_T1;}
 for(unsigned j=0;j<count;j++){WiiSetupPlane *p=&s->plane[indices[j]];if(component==0)p->start=value;else if(component==1)p->dx=value;else p->dy=value;}
 return 1;
}
#endif
