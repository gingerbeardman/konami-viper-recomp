#ifndef VIPER_WII_TEXTURE_CACHE_H
#define VIPER_WII_TEXTURE_CACHE_H
#include <stdint.h>
#include <string.h>
/* Metadata owns no GPU memory. A valid victim must be fenced before its image
 * is overwritten; page changes invalidate reuse, not queued GPU ownership. */
typedef struct {
    uint32_t mode,lod,base,width,height,epoch,key,range,unit,format,first,pages;
} WiiTextureKey;
typedef struct {
    int valid;
    WiiTextureKey key;
    uint32_t versions[33];
} WiiTextureCacheEntry;
/* A bound image is pinned until its draw is submitted. Draining the GPU does
 * not protect a texture whose vertices have not been submitted yet. Return
 * count when no eligible slot exists, including an insufficient pair budget. */
static inline unsigned wii_texture_cache_victim(const WiiTextureCacheEntry *e,
    unsigned count,unsigned first,const uint8_t *pinned,int valid_only){
    for(unsigned n=0;n<count;n++){
        unsigned slot=(first+n)%count;
        if((!pinned||!pinned[slot])&&(!valid_only||e[slot].valid))return slot;
    }
    return count;
}
static inline WiiTextureKey wii_texture_conversion_key(WiiTextureKey k){
    k.mode=k.lod=k.key=k.range=0;
    if(k.format!=5&&k.format!=14)k.epoch=0;
    return k;
}
static inline int wii_texture_cache_matches(const WiiTextureCacheEntry *e,
    const WiiTextureKey *k,const uint32_t versions[2048]){
    const WiiTextureKey *a=&e->key;
    if(!e->valid||k->pages>33||a->mode!=k->mode||a->lod!=k->lod||
       a->base!=k->base||a->width!=k->width||a->height!=k->height||
       a->epoch!=k->epoch||a->key!=k->key||a->range!=k->range||
       a->unit!=k->unit||a->format!=k->format||a->first!=k->first||a->pages!=k->pages)return 0;
    for(unsigned i=0;i<k->pages;i++)
        if(e->versions[i]!=versions[(k->first+i)&2047])return 0;
    return 1;
}
static inline void wii_texture_cache_commit(WiiTextureCacheEntry *e,
    const WiiTextureKey *k,const uint32_t versions[2048]){
    e->key=*k;
    for(unsigned i=0;i<k->pages;i++)e->versions[i]=versions[(k->first+i)&2047];
    e->valid=1;
}
#endif
