#include "wdepth_split.h"
#include <math.h>
#include <float.h>
#include <stdint.h>
#include <string.h>
#define FIELDS(F) F(x) F(y) F(r) F(g) F(b) F(a) F(wb) F(s) F(t) F(z) F(w0) F(w1) F(s1) F(t1)
#define DECLARE(n) double n;
typedef struct {FIELDS(DECLARE)} ClipVertex;
#undef DECLARE
int wii_wdepth_band(unsigned b,float *lo,float *hi){
 if(!lo||!hi||b>65)return 0;
 if(b==64){*lo=0;*hi=0x1p-16f;}
 else if(b==65){*lo=1;*hi=FLT_MAX;}
 else{
  /* All boundaries are exact normal binary32 powers-of-two fractions.
   * Construct their bits instead of calling ldexpf twice for every slab. */
  _Static_assert(sizeof(float)==sizeof(uint32_t),"W-depth requires binary32 floats");
  uint32_t lower=0x3f000000u+(b%4)*0x200000u-(b/4)*0x800000u;
  uint32_t upper=lower+0x200000u;
  memcpy(lo,&lower,sizeof lower);memcpy(hi,&upper,sizeof upper);
 }
 return 1;
}
#ifdef VIPER_WII_WDEPTH_BORROW_CLIP
/* Surviving vertices borrow immutable input/previous-plane storage. Only new
 * intersections occupy this plane's storage; all pointers die in this slab. */
static void intersect_into(const ClipVertex *a,const ClipVertex *b,double w,ClipVertex *r){
 if(a->wb>b->wb){const ClipVertex *t=a;a=b;b=t;}
 double f=(w-a->wb)/(b->wb-a->wb);
#define LERP(n) r->n=a->n+(b->n-a->n)*f;
 FIELDS(LERP)
#undef LERP
 r->wb=w;
}
static unsigned clip_borrowed(const ClipVertex *const *in,unsigned n,
 const ClipVertex **out,ClipVertex *storage,double w,int above){
 unsigned used=0;
 for(unsigned i=0;i<n;i++){
  const ClipVertex *a=in[i],*b=in[(i+1)%n];
  int ia=above?a->wb>=w:a->wb<=w,ib=above?b->wb>=w:b->wb<=w;
  if(ia)out[used++]=a;
  if(ia!=ib){intersect_into(a,b,w,&storage[used]);out[used]=&storage[used];used++;}
 }
 return used;
}
#else
static ClipVertex intersect(const ClipVertex *a,const ClipVertex *b,double w){
 /* Canonical lower-W endpoint makes an edge intersection independent of edge
  * traversal direction, preventing attribute cracks between adjacent pieces. */
 if(a->wb>b->wb){const ClipVertex *t=a;a=b;b=t;}
 double f=(w-a->wb)/(b->wb-a->wb);ClipVertex r;
#define LERP(n) r.n=a->n+(b->n-a->n)*f;
 FIELDS(LERP)
#undef LERP
 r.wb=w;return r;
}
static unsigned clip(const ClipVertex *in,unsigned n,ClipVertex *out,double w,int above){
 unsigned used=0;
 for(unsigned i=0;i<n;i++){
  const ClipVertex *a=&in[i],*b=&in[(i+1)%n];
  int ia=above?a->wb>=w:a->wb<=w,ib=above?b->wb>=w:b->wb<=w;
  if(ia)out[used++]=*a;
  if(ia!=ib)out[used++]=intersect(a,b,w);
 }
 return used;
}
#endif
static double area(const WiiVoodooVertex *a,const WiiVoodooVertex *b,const WiiVoodooVertex *c){
 return ((double)b->x-a->x)*((double)c->y-a->y)-((double)b->y-a->y)*((double)c->x-a->x);
}
int wii_wdepth_split(const WiiVoodooVertex in[3],WiiWDepthTriangle *out,unsigned cap,unsigned *count){
 if(!count)return 0;
 *count=0;if(!in||!out)return 0;
 float min_w=in[0].wb,max_w=in[0].wb;
 for(unsigned i=0;i<3;i++){
#define FINITE(n) if(!isfinite(in[i].n))return 0;
  FIELDS(FINITE)
#undef FINITE
  if(in[i].wb<0)return 0;
  if(in[i].wb<min_w)min_w=in[i].wb;
  if(in[i].wb>max_w)max_w=in[i].wb;
 }
 double original=area(in,in+1,in+2);if(original==0)return 1;
 /* A constant plane on a boundary belongs to one band, not both. */
 int constant=in[0].wb==in[1].wb&&in[1].wb==in[2].wb;
 ClipVertex input[3];
 for(unsigned i=0;i<3;i++){
#define COPY(n) input[i].n=in[i].n;
  FIELDS(COPY)
#undef COPY
 }
 unsigned used=0;
 for(unsigned band=0;band<66;band++){
  float lo,hi;if(!wii_wdepth_render_band(band,&lo,&hi))continue;
  /* Empty slabs cannot contribute geometry. Strict inequalities retain
   * boundary handling and ordering of every potentially overlapping slab. */
  if(max_w<lo||min_w>hi)continue;
  if(constant&&!(in[0].wb>=lo&&(in[0].wb<hi||band==65)))continue;
#ifdef VIPER_WII_WDEPTH_INTERIOR
  /* Strictly interior triangles survive both clipping planes unchanged.
   * Boundary cases retain the original clipping and sliver handling. */
  if(min_w>lo&&max_w<hi){
   if(used>=cap){*count=0;return 0;}
#ifdef VIPER_WII_WDEPTH_COPY_ONCE
   /* The public API does not forbid input/output overlap. memmove preserves
    * the compound literal's read-before-write behavior for this whole trio. */
   memmove(out[used].v,in,sizeof out[used].v);
   out[used].band=band;used++;
#else
   out[used++]=(WiiWDepthTriangle){{in[0],in[1],in[2]},band};
#endif
   continue;
  }
#endif
#ifdef VIPER_WII_WDEPTH_BORROW_CLIP
  const ClipVertex *refs[3]={input,input+1,input+2},*a[8],*b[8];
  ClipVertex storage_a[8],storage_b[8];
  unsigned n=clip_borrowed(refs,3,a,storage_a,lo,1);if(n<3)continue;
  n=clip_borrowed(a,n,b,storage_b,hi,0);if(n<3)continue;
#else
  ClipVertex a[8],b[8];unsigned n=clip(input,3,a,lo,1);if(n<3)continue;
  n=clip(a,n,b,hi,0);if(n<3)continue;
#endif
  for(unsigned i=1;i+1<n;i++){
   WiiVoodooVertex vertices[3];
#ifdef VIPER_WII_WDEPTH_BORROW_CLIP
   const ClipVertex *source[3]={b[0],b[i],b[i+1]};
#else
   const ClipVertex *source[3]={b,b+i,b+i+1};
#endif
   for(unsigned j=0;j<3;j++){
#define CONVERT(n) vertices[j].n=(float)source[j]->n;
    FIELDS(CONVERT)
#undef CONVERT
   }
   double ar=area(vertices,vertices+1,vertices+2);if(ar==0)continue;
   if(used>=cap){*count=0;return 0;}
   /* Final float quantization can reverse a subpixel sliver. Preserve the
    * original culling orientation without discarding its geometry/attributes. */
   if((ar<0)!=(original<0)){WiiVoodooVertex t=vertices[1];vertices[1]=vertices[2];vertices[2]=t;}
#ifdef VIPER_WII_WDEPTH_COPY_ONCE
   /* vertices is private stack storage. Copy its complete object representation
    * once instead of constructing a second three-vertex compound literal.
    * Keep quantization, winding correction and capacity handling unchanged. */
   memcpy(out[used].v,vertices,sizeof vertices);
   out[used].band=band;used++;
#else
   out[used++]=(WiiWDepthTriangle){{vertices[0],vertices[1],vertices[2]},band};
#endif
  }
 }
 *count=used;return 1;
}
