#ifndef VIPER_WII_GX_TEV_STAGE_H
#define VIPER_WII_GX_TEV_STAGE_H
#include <gccore.h>
/* No GX draw, callback or state consumer may occur between these setters.
 * Other setters (order, constants, selectors, stage count) stay in callers. */
static inline void wii_gx_tev_stage(u8 stage,u8 ra,u8 rb,u8 rc,u8 rd,
    u8 aa,u8 ab,u8 ac,u8 ad,u8 op,u8 bias,u8 scale,u8 clamp,u8 out){
    GX_SetTevColorIn(stage,ra,rb,rc,rd);GX_SetTevAlphaIn(stage,aa,ab,ac,ad);
    GX_SetTevColorOp(stage,op,bias,scale,clamp,out);GX_SetTevAlphaOp(stage,op,bias,scale,clamp,out);
}
#endif
