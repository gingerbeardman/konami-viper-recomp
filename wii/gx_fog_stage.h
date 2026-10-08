#ifndef VIPER_WII_GX_FOG_STAGE_H
#define VIPER_WII_GX_FOG_STAGE_H
#include <gccore.h>
/* Initial TEV registers apply to every stage. Keep original color0/color1
 * in REG1/REG2 intact. REG0 RGB is unused by equations/rejection; parity
 * writes only REG0 alpha. K1 supplies the independent constant fog factor. */
static inline void wii_gx_fog_stage(unsigned stage,GXColor fog,int variable){
    GX_SetTevKColor(GX_KCOLOR1,fog);
    if(!variable)GX_SetTevColor(GX_TEVREG0,fog);
    GX_SetTevKColorSel(stage,variable?GX_TEV_KCSEL_K1:GX_TEV_KCSEL_K1_A);
    GX_SetNumTevStages(stage+1);
    GX_SetTevOrder(stage,variable?GX_TEXCOORD1:GX_TEXCOORDNULL,
        variable?GX_TEXMAP2:GX_TEXMAP_NULL,GX_COLORNULL);
    GX_SetTevColorIn(stage,GX_CC_CPREV,variable?GX_CC_KONST:GX_CC_C0,
        variable?GX_CC_TEXA:GX_CC_KONST,GX_CC_ZERO);
    GX_SetTevColorOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GX_SetTevAlphaIn(stage,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
    GX_SetTevAlphaOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
}
#endif
