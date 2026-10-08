#ifndef VIPER_WII_GX_CONSTANT_FOG_H
#define VIPER_WII_GX_CONSTANT_FOG_H
#include <gccore.h>
#include "gx_fog_stage.h"
/* One owner translation unit: immutable after explicit initialization, so
 * queued draws never observe a rewritten factor tile. Call init before GX
 * submissions; bind does not mutate storage or REG0/REG1/REG2. */
static unsigned char wii_constant_fog_tiles[256][64] ATTRIBUTE_ALIGN(32);
static int wii_constant_fog_initialized;
static inline void wii_gx_constant_fog_init(void){
    if(wii_constant_fog_initialized)return;
    for(unsigned a=0;a<256;a++)for(unsigned i=0;i<16;i++){
        unsigned o=i*2;
        wii_constant_fog_tiles[a][o]=(unsigned char)a;
        wii_constant_fog_tiles[a][o+1]=0;
        wii_constant_fog_tiles[a][o+32]=0;
        wii_constant_fog_tiles[a][o+33]=0;
    }
    DCFlushRange(wii_constant_fog_tiles,sizeof wii_constant_fog_tiles);
    wii_constant_fog_initialized=1;
}
/* Returns zero without GX mutation for an invalid stage/generator count or
 * missing initialization. coord1 may subsequently receive the W-depth plane:
 * every texel in the selected clamp texture has the same alpha. */
static inline int wii_gx_constant_fog_bind(unsigned stage,GXColor fog,unsigned active_texgens){
    if(!wii_constant_fog_initialized||stage>=16||active_texgens>8)return 0;
    GXTexObj texture;
    GX_InitTexObj(&texture,wii_constant_fog_tiles[fog.a],4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    GX_LoadTexObj(&texture,GX_TEXMAP2);
    GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
    GX_SetNumTexGens(active_texgens<2?2:active_texgens);
    wii_gx_fog_stage(stage,fog,1);
    return 1;
}
#endif
