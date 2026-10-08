#ifndef WII_NATIVE_TEXTURE_SOURCE_H
#define WII_NATIVE_TEXTURE_SOURCE_H
#include "texture_layout.h"

/* Compatibility is decoded at preparation, not by the draw consumer. This
 * descriptor contains native resource dimensions, ranges and sampler choices.
 * Pixel/palette residency remains a separate versioned resource contract. */
enum {WII_TEXTURE_SOURCE_OK,WII_TEXTURE_SOURCE_VARIABLE_MIP,WII_TEXTURE_SOURCE_LAYOUT};
typedef struct {
    unsigned mode,lod,format,error;
    unsigned level,base,width,height,first_page,pages;
    unsigned clamp_s,clamp_t,linear;
} WiiNativeTextureSource;
typedef struct {
    WiiNativeTextureSource source[2];
    unsigned valid[2];
} WiiNativeTextureSources;
static inline void wii_native_texture_source_invalidate(WiiNativeTextureSources *s,unsigned unit){
    s->valid[unit]=0;
}
static inline WiiNativeTextureSource wii_native_texture_source_prepare(const uint32_t *r,unsigned format){
    WiiNativeTextureSource s={0};
    s.mode=r[0];s.lod=r[1];s.format=format;s.level=(s.lod&63)/4;
    if(((s.lod>>6)&63)/4!=s.level){s.error=WII_TEXTURE_SOURCE_VARIABLE_MIP;return s;}
    if((s.lod&(1u<<19))&&((s.level&1)!=!!(s.lod&(1u<<18))))s.level++;
    if(s.level>8||(r[3]&1)){s.error=WII_TEXTURE_SOURCE_LAYOUT;return s;}
    NativeMip mip[9];native_texture_layout(r,mip);
    s.base=mip[s.level].address;s.width=mip[s.level].width;s.height=mip[s.level].height;
    s.first_page=s.base/4096;
    s.pages=((s.base&4095)+s.width*s.height*(format>=8?2:1)+4095)/4096;
    s.clamp_s=!!(s.mode&64);s.clamp_t=!!(s.mode&128);s.linear=!!(s.mode&6);
    return s;
}
/* Same changed-word read set as native_texture_layout: mode, LOD and base0..3.
 * Device invalidation must precede the next acquire. Format belongs to the
 * compiled material and is checked separately so slot reuse cannot alias it. */
static inline const WiiNativeTextureSource *wii_native_texture_source_acquire(
    WiiNativeTextureSources *s,unsigned unit,const uint32_t *r,unsigned format){
    if(!s->valid[unit]||s->source[unit].format!=format){
        s->source[unit]=wii_native_texture_source_prepare(r,format);s->valid[unit]=1;
    }
    return &s->source[unit];
}
#endif
