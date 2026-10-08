/* SPDX-License-Identifier: BSD-3-Clause
 * TMU arithmetic adapted from MAME's Voodoo renderer (copyright Aaron Giles).
 * Approximate GX lowering: TEV factor expansion and rounding differ. */
#ifndef VIPER_WII_GX_TMU_EQUATION_H
#define VIPER_WII_GX_TMU_EQUATION_H
#include <gccore.h>
#include "gx_tev_stage.h"
#include "voodoo_tmu_plan.h"
/* Returns exclusive end stage, or zero before mutation if the budget/output
 * is invalid. PREV is scratch; upstream OTHER is consumed only by stage zero
 * and may itself be PREV. other_alpha_rgb is its replicated-alpha COLOR
 * operand, distinct from the ALPHA operand. No initial registers are changed.
 * detail/fraction selectors refer to caller-initialized konst registers and
 * must describe this fragment's factor. Constant selectors cannot represent
 * spatially varying LOD; caller must supply a valid constant region or use a
 * different factor-producing pipeline. Local is fetched TEXC/TEXA on EVERY
 * stage needing it. The output register is written only at the final stage.
 * Negative intermediates are consumed solely via signed D, never A/B/C. */
static inline unsigned wii_gx_tmu_equation(WiiVoodooTMUPlan p,unsigned first,
    u8 coord,u8 map,u8 other_rgb,u8 other_alpha,u8 other_alpha_rgb,u8 output,
    u8 detail_rgb_sel,u8 detail_alpha_sel,u8 fraction_rgb_sel,u8 fraction_alpha_sel){
    unsigned subtract=p.rgb_sub||p.alpha_sub;
    unsigned stages=subtract?3:1,invert=p.rgb_invert||p.alpha_invert;
    unsigned end=first+stages+invert;
    if(first>=16||end>16||output>GX_TEVREG2)return 0;
    u8 factor=GX_CC_ZERO,factor_a=GX_CA_ZERO;
    switch(p.rgb_mul){
        case 1:factor=GX_CC_TEXC;break;
        case 2:factor=other_alpha_rgb;break;
        case 3:factor=GX_CC_TEXA;break;
        case 4:case 5:factor=GX_CC_KONST;break;
    }
    switch(p.alpha_mul){
        case 1:case 3:factor_a=GX_CA_TEXA;break;
        case 2:factor_a=other_alpha;break;
        case 4:case 5:factor_a=GX_CA_KONST;break;
    }
    GX_SetNumTevStages(end);
    for(unsigned s=first;s<end;s++)GX_SetTevOrder(s,coord,map,GX_COLORNULL);
    GX_SetTevKColorSel(first,p.rgb_mul==5?fraction_rgb_sel:detail_rgb_sel);
    GX_SetTevKAlphaSel(first,p.alpha_mul==5?fraction_alpha_sel:detail_alpha_sel);
    u8 other=p.rgb_zero?GX_CC_ZERO:other_rgb;
    u8 oa=p.alpha_zero?GX_CA_ZERO:other_alpha;
    u8 left=p.rgb_sub?GX_CC_TEXC:GX_CC_ZERO;
    u8 la=p.alpha_sub?GX_CA_TEXA:GX_CA_ZERO;
    u8 add=p.rgb_add==1?GX_CC_TEXC:p.rgb_add==2?GX_CC_TEXA:GX_CC_ZERO;
    u8 aa=p.alpha_add?GX_CA_TEXA:GX_CA_ZERO;
    u8 initial_output=stages+invert==1?output:GX_TEVPREV;
    wii_gx_tev_stage(first,p.rgb_reverse?left:other,p.rgb_reverse?other:left,factor,subtract?GX_CC_ZERO:add,
        p.alpha_reverse?la:oa,p.alpha_reverse?oa:la,factor_a,subtract?GX_CA_ZERO:aa,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,!subtract,initial_output);
    if(subtract){
        wii_gx_tev_stage(first+1,left,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV,
            la,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_TEV_SUB,GX_TB_ZERO,GX_CS_SCALE_1,GX_FALSE,GX_TEVPREV);
        wii_gx_tev_stage(first+2,add,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV,
            aa,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,invert?GX_TEVPREV:output);
    }
    if(invert){
        unsigned s=first+stages;
        GX_SetTevKAlphaSel(s,GX_TEV_KASEL_1);
        wii_gx_tev_stage(s,p.rgb_invert?GX_CC_CPREV:GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,p.rgb_invert?GX_CC_ONE:GX_CC_CPREV,
            p.alpha_invert?GX_CA_APREV:GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,p.alpha_invert?GX_CA_KONST:GX_CA_APREV,GX_TEV_SUB,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,output);
    }
    return end;
}
#endif
