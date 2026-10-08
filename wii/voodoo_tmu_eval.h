/* SPDX-License-Identifier: BSD-3-Clause
 * Arithmetic adapted from MAME's Voodoo renderer (copyright Aaron Giles). */
#ifndef VIPER_WII_VOODOO_TMU_EVAL_H
#define VIPER_WII_VOODOO_TMU_EVAL_H
#include "voodoo_tmu_plan.h"
#include "voodoo_color_eval.h"
/* Local and upstream OTHER are already fetched/clamped RGBA bytes. lod is
 * the post-fetch signed 8-fraction-bit LOD, not the raw register. Sampling,
 * NCC/palette conversion, filtering, LOD selection and FBI are outside scope.
 * detail is the raw texture-detail register. Wide unsigned arithmetic avoids
 * signed-shift UB; valid reference-domain results retain byte truncation
 * BEFORE detail_max clamping (including wrap at 256). */
static inline uint8_t wii_voodoo_tmu_detail(uint32_t detail,int32_t lod){
    int bias=(int)((detail>>8)&63);
    if(bias&32)bias-=64;
    bias*=256;
    if(bias<=lod)return 0;
    uint64_t delta=(uint64_t)((int64_t)bias-lod);
    unsigned factor=(unsigned)((delta<<((detail>>14)&7))>>8)&255;
    unsigned maximum=detail&255;
    return (uint8_t)(factor>maximum?maximum:factor);
}
static inline WiiVoodooRGBA wii_voodoo_tmu_eval(WiiVoodooTMUPlan p,
    WiiVoodooRGBA local,WiiVoodooRGBA other,int32_t lod,uint32_t detail){
    WiiVoodooRGBA result;
    uint8_t *out[4]={&result.r,&result.g,&result.b,&result.a};
    const int l[4]={local.r,local.g,local.b,local.a};
    const int o[4]={other.r,other.g,other.b,other.a};
    for(unsigned c=0;c<4;c++){
        int alpha=c==3;
        int value=(alpha?p.alpha_zero:p.rgb_zero)?0:o[c];
        if(alpha?p.alpha_sub:p.rgb_sub)value-=l[c];
        unsigned selector=alpha?p.alpha_mul:p.rgb_mul;
        int factor=selector==1?l[c]:selector==2?other.a:selector==3?local.a:
            selector==4?wii_voodoo_tmu_detail(detail,lod):selector==5?(int)((uint32_t)lod&255):0;
        if(!(alpha?p.alpha_reverse:p.rgb_reverse))factor^=255;
        int product=value*(factor+1);
        value=product>=0?product/256:-((-product+255)/256);
        value+=alpha?(p.alpha_add?local.a:0):p.rgb_add==1?l[c]:p.rgb_add==2?local.a:0;
        value=value<0?0:value>255?255:value;
        if(alpha?p.alpha_invert:p.rgb_invert)value^=255;
        *out[c]=(uint8_t)value;
    }
    return result;
}
#endif
