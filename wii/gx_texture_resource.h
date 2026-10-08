#ifndef WII_GX_TEXTURE_RESOURCE_H
#define WII_GX_TEXTURE_RESOURCE_H
#include <gccore.h>
#include <stdint.h>
#include <string.h>

/* Prepared native RGBA8 resource. The owner validates image residency/content
 * before acquisition. Pixel uploads, palette changes, GPU fencing and texture
 * invalidation remain the owner's responsibility. Keep a pristine descriptor:
 * GX_LoadTexObj rewrites register IDs in its argument, so consumers take copies.
 * Sampler fields are native GX values, not encoded Voodoo registers. */
typedef struct {
    const void *image;
    unsigned width,height,wrap_s,wrap_t,filter;
    GXTexObj object;
    unsigned valid;
    uint64_t hits,misses;
} WiiGXTextureResource;

static inline void wii_gx_texture_resource_invalidate(WiiGXTextureResource *r){
    r->valid=0;
}
static inline GXTexObj wii_gx_texture_resource_acquire(WiiGXTextureResource *r,
    void *image,unsigned width,unsigned height,unsigned wrap_s,unsigned wrap_t,unsigned filter){
    if(r->valid&&r->image==image&&r->width==width&&r->height==height&&
       r->wrap_s==wrap_s&&r->wrap_t==wrap_t&&r->filter==filter){
        r->hits++;
    }else{
        GX_InitTexObj(&r->object,image,width,height,GX_TF_RGBA8,wrap_s,wrap_t,GX_FALSE);
        GX_InitTexObjLOD(&r->object,filter,filter,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
        r->image=image;r->width=width;r->height=height;
        r->wrap_s=wrap_s;r->wrap_t=wrap_t;r->filter=filter;r->valid=1;r->misses++;
    }
    return r->object;
}
#endif
