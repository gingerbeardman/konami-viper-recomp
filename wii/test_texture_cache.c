#include "texture_cache.h"
#include <assert.h>
#include <stdio.h>
int main(void){
    uint32_t versions[2048]={0};
    WiiTextureCacheEntry entries[4]={0};
    WiiTextureKey key={0x10241c07,0,0x7fffe0,4,4,7,0,0x10000000,0,12,2047,2};
    for(unsigned i=0;i<4;i++){
        WiiTextureKey k=key;k.mode+=i;
        assert(!wii_texture_cache_matches(&entries[i],&k,versions));
        wii_texture_cache_commit(&entries[i],&k,versions);
    }
    /* Alternating textures retain independent snapshots. A wrapped write must
     * invalidate all affected entries, but an unrelated page must not. */
    for(unsigned i=0;i<40;i++){
        WiiTextureKey k=key;k.mode+=i%4;
        assert(wii_texture_cache_matches(&entries[i%4],&k,versions));
    }
    versions[1]++;assert(wii_texture_cache_matches(&entries[0],&key,versions));
    versions[0]++;assert(!wii_texture_cache_matches(&entries[0],&key,versions));
    wii_texture_cache_commit(&entries[0],&key,versions);
    versions[2047]++;assert(!wii_texture_cache_matches(&entries[0],&key,versions));
    wii_texture_cache_commit(&entries[0],&key,versions);
    /* Same image address is insufficient: distinguish palette, selected TMU,
     * format, filtering/wrap, LOD, dimensions and chroma register keys. */
    WiiTextureKey changed=key;
#define REJECT_CHANGE(field) do{changed=key;changed.field++;assert(!wii_texture_cache_matches(&entries[0],&changed,versions));}while(0)
    REJECT_CHANGE(epoch);REJECT_CHANGE(unit);REJECT_CHANGE(format);
    REJECT_CHANGE(mode);REJECT_CHANGE(lod);REJECT_CHANGE(base);
    REJECT_CHANGE(width);REJECT_CHANGE(height);REJECT_CHANGE(key);
    REJECT_CHANGE(range);REJECT_CHANGE(first);REJECT_CHANGE(pages);
#undef REJECT_CHANGE
    /* Replacement publishes a fresh key and loses the old image identity. */
    changed=key;changed.base=0;changed.first=0;
    wii_texture_cache_commit(&entries[0],&changed,versions);
    assert(wii_texture_cache_matches(&entries[0],&changed,versions));
    assert(!wii_texture_cache_matches(&entries[0],&key,versions));
    memset(entries,0,sizeof entries);
    assert(!wii_texture_cache_matches(&entries[0],&changed,versions));
    /* Sampler and chroma changes can share converted bytes. PAL8 and AP88
     * still require a fresh palette revision; direct-colour formats do not. */
    WiiTextureKey converted=wii_texture_conversion_key(key);
    wii_texture_cache_commit(&entries[0],&converted,versions);
    changed=key;changed.mode++;changed.lod++;changed.key++;changed.range++;changed.epoch++;
    changed=wii_texture_conversion_key(changed);
    assert(wii_texture_cache_matches(&entries[0],&changed,versions));
    for(unsigned format=5;format<=14;format+=9){
        changed=key;changed.format=format;
        converted=wii_texture_conversion_key(changed);
        wii_texture_cache_commit(&entries[0],&converted,versions);
        changed.epoch++;changed=wii_texture_conversion_key(changed);
        assert(!wii_texture_cache_matches(&entries[0],&changed,versions));
    }
    /* Exhaust capacities, pin sets and valid sets. A second acquisition must
     * never reuse a bound image, and an impossible budget must terminate. */
    for(unsigned count=0;count<=4;count++)for(unsigned pins=0;pins<16;pins++)
    for(unsigned valid=0;valid<16;valid++)for(unsigned first=0;first<4;first++)
    for(unsigned valid_only=0;valid_only<2;valid_only++){
        uint8_t pinned[4];unsigned available=0;
        for(unsigned i=0;i<4;i++){
            pinned[i]=(pins>>i)&1;entries[i].valid=(valid>>i)&1;
            if(i<count&&!pinned[i]&&(!valid_only||entries[i].valid))available++;
        }
        unsigned victim=wii_texture_cache_victim(entries,count,first,pinned,valid_only);
        assert((victim<count)==!!available);
        if(victim<count)assert(!pinned[victim]&&(!valid_only||entries[victim].valid));
    }
    puts("Wii texture cache invalidation and pinned acquisition tests PASS");
}
