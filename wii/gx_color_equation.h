#ifndef VIPER_WII_GX_COLOR_EQUATION_H
#define VIPER_WII_GX_COLOR_EQUATION_H
#include <gccore.h>
#include "gx_tev_stage.h"
#include "voodoo_color_plan.h"
/* Approximate FBI equation compiler. TEV factor expansion/rounding differ
 * from Voodoo. This does not implement rejection, TMU combining or fog.
 * REG1/REG2 hold color0/color1 until these stages finish; subsequent stages
 * may reuse them. Negative intermediates are read ONLY through signed D. */
static inline unsigned wii_gx_color_equation_at(WiiVoodooColorPlan p,GXColor c0,GXColor c1,
    unsigned first,u8 texture_rgb,u8 texture_alpha,u8 texture_alpha_rgb,u8 coord,u8 map){
    if(p.local_override||p.local_alpha>=2)return 0;
    const u8 rgb_sources[4]={GX_CC_RASC,texture_rgb,GX_CC_C2,GX_CC_ZERO};
    const u8 alpha_sources[4]={GX_CA_RASA,texture_alpha,GX_CA_A2,GX_CA_ZERO};
    const u8 alpha_rgb_sources[4]={GX_CC_RASA,texture_alpha_rgb,GX_CC_A2,GX_CC_ZERO};
    u8 other=p.rgb_zero?GX_CC_ZERO:rgb_sources[p.other_rgb];
    u8 other_a=p.alpha_zero?GX_CA_ZERO:alpha_sources[p.other_alpha];
    u8 local=p.local_rgb?GX_CC_C1:GX_CC_RASC;
    u8 local_a=p.local_alpha?GX_CA_A1:GX_CA_RASA;
    u8 local_ar=p.local_alpha?GX_CC_A1:GX_CC_RASA;
    u8 factor=GX_CC_ZERO,factor_a=GX_CA_ZERO;
    switch(p.rgb_mul){
        case 1:factor=local;break;
        case 2:factor=alpha_rgb_sources[p.other_alpha];break;
        case 3:factor=local_ar;break;
        case 4:factor=texture_alpha_rgb;break;
        case 5:factor=texture_rgb;break;
    }
    switch(p.alpha_mul){
        case 1:case 3:factor_a=local_a;break;
        case 2:factor_a=alpha_sources[p.other_alpha];break;
        case 4:factor_a=texture_alpha;break;
    }
    u8 add=p.rgb_add==1?local:p.rgb_add==2?local_ar:GX_CC_ZERO;
    u8 add_a=p.alpha_add?local_a:GX_CA_ZERO;
    unsigned subtract=p.rgb_sub||p.alpha_sub;
    unsigned stages=subtract?3:1;
    unsigned invert=p.rgb_invert||p.alpha_invert;
    if(first>=16||first+stages+invert>16)return 0;
    GX_SetTevColor(GX_TEVREG1,c0);GX_SetTevColor(GX_TEVREG2,c1);
    GX_SetNumTevStages(first+stages+invert);
    for(unsigned stage=first;stage<first+stages+invert;stage++){
        GX_SetTevOrder(stage,coord,map,GX_COLOR0A0);
        GX_SetTevKAlphaSel(stage,GX_TEV_KASEL_1);
    }
    u8 left=p.rgb_sub?local:GX_CC_ZERO;
    u8 left_a=p.alpha_sub?local_a:GX_CA_ZERO;
    wii_gx_tev_stage(first+0,p.rgb_reverse?left:other,p.rgb_reverse?other:left,factor,subtract?GX_CC_ZERO:add,
        p.alpha_reverse?left_a:other_a,p.alpha_reverse?other_a:left_a,factor_a,subtract?GX_CA_ZERO:add_a,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,!subtract,GX_TEVPREV);
    if(subtract){
        wii_gx_tev_stage(first+1,p.rgb_sub?local:GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV,
            p.alpha_sub?local_a:GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_TEV_SUB,GX_TB_ZERO,GX_CS_SCALE_1,GX_FALSE,GX_TEVPREV);
        wii_gx_tev_stage(first+2,add,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV,
            add_a,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    if(invert){
        wii_gx_tev_stage(first+stages,p.rgb_invert?GX_CC_CPREV:GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,p.rgb_invert?GX_CC_ONE:GX_CC_CPREV,
            p.alpha_invert?GX_CA_APREV:GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,p.alpha_invert?GX_CA_KONST:GX_CA_APREV,GX_TEV_SUB,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    return first+stages+invert;
}
/* Compatibility entry point: identical stage-zero fetched-texture behavior. */
static inline unsigned wii_gx_color_equation(WiiVoodooColorPlan p,GXColor c0,GXColor c1){
    return wii_gx_color_equation_at(p,c0,c1,0,GX_CC_TEXC,GX_CA_TEXA,GX_CC_TEXA,GX_TEXCOORD0,GX_TEXMAP0);
}
#endif
