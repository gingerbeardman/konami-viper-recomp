#ifndef VIPER_WII_TEXTURE_LAYOUT_CACHE_H
#define VIPER_WII_TEXTURE_LAYOUT_CACHE_H
#include "texture_layout.h"
/* Pure descriptor storage, independent of palette/VRAM/image/GX state.
 * Device mutation notifications invalidate the exact layout read set. */
typedef struct { NativeMip mip[2][9];unsigned valid[2]; } WiiTextureLayouts;
static inline int wii_texture_layout_word(unsigned word){
    return word==0||word==1||(word>=3&&word<=6);
}
static inline void wii_texture_layout_invalidate(WiiTextureLayouts *cache,unsigned unit){
    cache->valid[unit]=0;
}
static inline const NativeMip *wii_texture_layout_get(WiiTextureLayouts *cache,
    unsigned unit,const uint32_t *regs){
    if(!cache->valid[unit]){
        native_texture_layout(regs,cache->mip[unit]);cache->valid[unit]=1;
    }
    return cache->mip[unit];
}
#endif
