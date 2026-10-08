#include "gx_tev_words.h"
#include <assert.h>
#include <stdio.h>
static uint32_t rng=0x61932abc;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static uint32_t field(uint32_t v,unsigned a,unsigned shift,unsigned bits){uint32_t mask=((1u<<bits)-1)<<shift;return (v&~mask)|((a<<shift)&mask);}
/* Independently follow installed setter instruction sequence. */
static uint32_t op_set(uint32_t v,unsigned op,unsigned bias,unsigned scale,unsigned clamp,unsigned out){
 v=field(v,op,18,1);v&=~0x300000u;
 if(op<=1){v|=(scale<<20)&0x300000u;v=field(v,bias,16,2);}
 else{v|=(op<<19)&0x300000u;v|=0x30000u;}
 v=field(v,clamp,19,1);return field(v,out,22,2);
}
int main(void){
 for(unsigned i=0;i<1000000;i++){
  uint32_t old=next();unsigned a=next()&255,b=next()&255,c=next()&255,d=next()&255;
  unsigned op=next()&255,bias=next()&255,scale=next()&255,clamp=next()&255,out=next()&255;
  uint32_t rgb=old;rgb=field(rgb,a,12,4);rgb=field(rgb,b,8,4);rgb=field(rgb,c,4,4);rgb=field(rgb,d,0,4);
  rgb=op_set(rgb,op,bias,scale,clamp,out);
  uint32_t alpha=old;alpha=field(alpha,a,13,3);alpha=field(alpha,b,10,3);alpha=field(alpha,c,7,3);alpha=field(alpha,d,4,3);
  alpha=op_set(alpha,op,bias,scale,clamp,out);
  assert(rgb==wii_tev_color_merge(old,wii_tev_color_payload(a,b,c,d,op,bias,scale,clamp,out)));
  assert(alpha==wii_tev_alpha_merge(old,wii_tev_alpha_payload(a,b,c,d,op,bias,scale,clamp,out)));
 }
 puts("TEV fused words1000000 bitfield differential cases PASS");
}
