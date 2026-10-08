/* SPDX-License-Identifier: BSD-3-Clause
 * Arithmetic adapted from MAME's Voodoo renderer (copyright Aaron Giles). */
#ifndef VIPER_WII_VOODOO_COLOR_EVAL_H
#define VIPER_WII_VOODOO_COLOR_EVAL_H
#include "voodoo_color_plan.h"
typedef struct { uint8_t r,g,b,a; } WiiVoodooRGBA;
typedef struct {
    WiiVoodooRGBA iterated,texture,color0,color1;
    uint8_t z_alpha,w_alpha; /* Already clamped by the reference depth rules. */
} WiiVoodooColorInputs;
/* Integer reference for the FBI equation only. Sampling, source clamping,
 * chroma/mask rejection, fog and framebuffer blending are separate stages.
 * Arithmetic order follows voodoo_renderer::combine_color. */
static inline WiiVoodooRGBA wii_voodoo_color_eval(WiiVoodooColorPlan p,WiiVoodooColorInputs in){
    const WiiVoodooRGBA zero={0,0,0,0};
    WiiVoodooRGBA other=p.other_rgb==0?in.iterated:p.other_rgb==1?in.texture:p.other_rgb==2?in.color1:zero;
    other.a=p.other_alpha==0?in.iterated.a:p.other_alpha==1?in.texture.a:p.other_alpha==2?in.color1.a:0;
    WiiVoodooRGBA local=(p.local_override?(in.texture.a&128)!=0:p.local_rgb)?in.color0:in.iterated;
    local.a=p.local_alpha==0?in.iterated.a:p.local_alpha==1?in.color0.a:p.local_alpha==2?in.z_alpha:in.w_alpha;
    WiiVoodooRGBA out;
    for(unsigned channel=0;channel<4;channel++){
        int alpha=channel==3;
        int o=channel==0?other.r:channel==1?other.g:channel==2?other.b:other.a;
        int l=channel==0?local.r:channel==1?local.g:channel==2?local.b:local.a;
        int t=channel==0?in.texture.r:channel==1?in.texture.g:channel==2?in.texture.b:in.texture.a;
        int value=(alpha?p.alpha_zero:p.rgb_zero)?0:o;
        if(alpha?p.alpha_sub:p.rgb_sub)value-=l;
        unsigned selector=alpha?p.alpha_mul:p.rgb_mul;
        int factor=0;
        if(selector==1)factor=l;
        else if(selector==2)factor=other.a;
        else if(selector==3)factor=local.a;
        else if(selector==4)factor=in.texture.a;
        else if(!alpha&&selector==5)factor=t;
        if(!(alpha?p.alpha_reverse:p.rgb_reverse))factor^=255;
        int add=alpha?(p.alpha_add?local.a:0):p.rgb_add==1?l:p.rgb_add==2?local.a:0;
        int product=value*(factor+1);
        /* Explicit floor division also works on hosts with logical signed shifts. */
        value=(product>=0?product/256:-((-product+255)/256))+add;
        value=value<0?0:value>255?255:value;
        if(alpha?p.alpha_invert:p.rgb_invert)value^=255;
        if(channel==0)out.r=(uint8_t)value;else if(channel==1)out.g=(uint8_t)value;
        else if(channel==2)out.b=(uint8_t)value;else out.a=(uint8_t)value;
    }
    return out;
}
#endif
