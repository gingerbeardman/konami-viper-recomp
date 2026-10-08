#ifndef VIPER_WII_GX_REJECTION_H
#define VIPER_WII_GX_REJECTION_H
#include <gccore.h>
/* Append black-key and source-alpha-parity predicates after an equation.
 * Sources refer to original inputs, not the equation's resulting PREV.
 * The caller must select a compatible alpha test/blend/depth policy: alpha
 * zero encoding alone does not reject a fragment under GX_ALWAYS.
 * Scratch alpha registers REG0 and REG1 are overwritten. Original sources
 * must remain available outside those registers; reserve any subsequent
 * fog/depth stages separately from this helper's stage limit. */
static inline unsigned wii_gx_rejection(unsigned first,u8 rgb_source,u8 alpha_source,
    int textured,int key,int binary,int mask){
    unsigned end=first+1+(mask?5:0);
    if(end>16)return 0;
    GX_SetNumTevStages(end);
    GX_SetTevOrder(first,textured?GX_TEXCOORD0:GX_TEXCOORDNULL,
        textured?GX_TEXMAP0:GX_TEXMAP_NULL,GX_COLOR0A0);
    GX_SetTevColorIn(first,key?rgb_source:GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
    GX_SetTevColorOp(first,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GX_SetTevAlphaIn(first,GX_CA_ZERO,GX_CA_ZERO,key?GX_CA_APREV:GX_CA_ZERO,key?GX_CA_ZERO:GX_CA_APREV);
    if(binary){
        GX_SetTevKColor(GX_KCOLOR0,(GXColor){0,0,0,255});
        GX_SetTevKAlphaSel(first,GX_TEV_KASEL_K0_A);
        GX_SetTevAlphaIn(first,GX_CA_ZERO,GX_CA_ZERO,key?GX_CA_KONST:GX_CA_ZERO,key?GX_CA_ZERO:GX_CA_KONST);
    }
    GX_SetTevAlphaOp(first,key?GX_TEV_COMP_BGR24_GT:GX_TEV_ADD,
        GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,mask?GX_TEVREG0:GX_TEVPREV);
    if(mask){
        for(unsigned stage=first+1;stage<=first+4;stage++){
            GX_SetTevOrder(stage,textured?GX_TEXCOORD0:GX_TEXCOORDNULL,
                textured?GX_TEXMAP0:GX_TEXMAP_NULL,GX_COLOR0A0);
            GX_SetTevColorIn(stage,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
            GX_SetTevColorOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
            GX_SetTevAlphaIn(stage,stage==first+1?alpha_source:GX_CA_A1,
                GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO);
            GX_SetTevAlphaOp(stage,GX_TEV_ADD,GX_TB_ZERO,
                stage==first+4?GX_CS_SCALE_2:GX_CS_SCALE_4,GX_FALSE,GX_TEVREG1);
        }
        unsigned last=first+5;
        GX_SetTevOrder(last,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
        GX_SetTevColorIn(last,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
        GX_SetTevColorOp(last,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GX_SetTevAlphaIn(last,GX_CA_A1,GX_CA_ZERO,GX_CA_A0,GX_CA_ZERO);
        GX_SetTevAlphaOp(last,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    return end;
}
#endif
