#ifndef VIPER_WII_RENDER_FAMILY_H
#define VIPER_WII_RENDER_FAMILY_H
#include <stdint.h>
#include "voodoo_color_plan.h"
#include "rejection_policy.h"
static inline int wii_iterated_mask_family(uint32_t cp,uint32_t fbz,uint32_t alpha,
    uint32_t fog,uint32_t key,uint32_t range){
    WiiVoodooColorPlan p=wii_voodoo_color_plan(cp);
    /* Antialias and reserved bits have no implementation in this path. */
    if((cp&0xe0000000u)||(alpha&0xe0u))return 0;
    /* The equation returns iterated RGBA; predicates use those same raw
     * sources. Zero/subtract choices make multiplier bits irrelevant. */
    if(p.other_rgb||p.other_alpha||p.local_rgb||p.local_alpha||p.local_override||
       !p.rgb_zero||p.rgb_sub||p.rgb_add!=1||p.rgb_invert||
       !p.alpha_zero||p.alpha_sub||!p.alpha_add||p.alpha_invert||!p.clamp)return 0;
    if(!(fbz&(1u<<13))||(fbz&1024)||
       (fbz&((1u<<2)|(1u<<16)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<21))))return 0;
    if(fog!=0x40&&fog!=0x41)return 0;
    /* MAME compares RGB only. Disabled range fields are inactive; enabled
     * black-only range requires zero upper RGB/exclusive/union fields. */
    if((fbz&2)&&((key&0xffffffu)||
       ((range&(1u<<28))&&(range&0x0fffffffu))))return 0;
    unsigned destination=(alpha>>12)&15;
    return (alpha&31)==31&&((alpha>>8)&15)==1&&
        (destination==4||destination==5);
}
typedef struct { unsigned unit,format,binary_chroma,reject_zero_alpha; } WiiRenderFamily;
/* Same iterated equation and blending, but chroma only. Depth writes are
 * allowed; the caller must prove alpha stays nonzero or chroma cannot hit. */
static inline int wii_iterated_chroma_family(uint32_t cp,uint32_t fbz,uint32_t alpha,
    uint32_t fog,uint32_t key,uint32_t range){
    if(!(fbz&2)||(fbz&(1u<<13)))return 0;
    return wii_iterated_mask_family(cp,(fbz&~1024u)|(1u<<13),alpha,fog,key,range);
}
/* Colour equations and TMU replacement/pass-through are independent of
 * framebuffer compare/write/blend choices. Retain the known ancillary bits;
 * unsupported alpha-plane, stipple and bias modes must not slip through. */
static inline int wii_render_family_with_alpha(uint32_t cp,uint32_t fbz,uint32_t alpha,
 uint32_t fog,uint32_t t0,uint32_t t1,unsigned packet,int positive_alpha,WiiRenderFamily *out){
 if(!out||(packet!=0x23&&packet!=0x3b)||(fog!=0x40&&fog!=0x41))return 0;
 const uint32_t variable=2u|16u|224u|1024u;
 if((fbz&~variable)!=(0x21329u&~variable))return 0;
 if(alpha&0xe0u)return 0;
 if(alpha&16){
  unsigned src=(alpha>>8)&15,dst=(alpha>>12)&15;
  if(src==3||src==7||src>6||dst==3||dst==7||dst>6)return 0;
 }
 unsigned mode=packet==0x23?6:7;
 uint32_t a=t0&~0xfc0u,b=t1&~0xfc0u;
 if(packet==0x23&&cp==0x1c482405&&a==0&&b==0x10241000)mode=0;
 if(b!=(0x10241000u|mode)||(a!=mode&&a!=b))return 0;
 unsigned unit=a==mode?1:0,format=((unit?t1:t0)>>8)&15;
 if(cp==0x1c482405){if(mode==0?format!=4:(format!=11&&format!=12))return 0;}
 else if(cp==0x1d022401){if(format!=5&&format!=10)return 0;}
 else if(cp==0x1c484104){if(format!=2)return 0;}
 else return 0;
 unsigned key=fbz&2;
 WiiRejectionPolicy policy=wii_rejection_policy(fbz,alpha,positive_alpha);
 if(key&&(policy==WII_REJECT_UNSUPPORTED||policy==WII_REJECT_SPLIT_DEPTH))return 0;
 *out=(WiiRenderFamily){unit,format,!!(key&&policy==WII_REJECT_BINARY),
     !!(key&&policy==WII_REJECT_ADD_NONZERO)};
 return 1;
}
static inline int wii_render_family(uint32_t cp,uint32_t fbz,uint32_t alpha,
 uint32_t fog,uint32_t t0,uint32_t t1,unsigned packet,WiiRenderFamily *out){
 return wii_render_family_with_alpha(cp,fbz,alpha,fog,t0,t1,packet,0,out);
}
#endif
