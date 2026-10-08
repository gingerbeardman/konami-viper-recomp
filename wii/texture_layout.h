#pragma once
#include <stdint.h>
static inline uint16_t native_rgb565(uint32_t rgb) {
    return ((rgb>>8)&0xf800) | ((rgb>>5)&0x07e0) | ((rgb>>3)&31);
}
/* Banshee texture addresses include cumulative offsets even in multibase mode.
 * Derivation follows rasterizer_texture::recompute in runtime/voodoo. */
typedef struct { unsigned width, height, address; } NativeMip;
static inline void native_texture_layout(const uint32_t *r, NativeMip mip[9]) {
    unsigned lod=r[1], wm=255, hm=255, offset=0;
    if (lod & (1<<20)) hm >>= (lod>>21)&3; else wm >>= (lod>>21)&3;
    unsigned levels = (lod & (1<<19)) ? ((lod & (1<<18)) ? 0xaa : 0x155) : 0x1ff;
    unsigned bpp = ((r[0]>>8)&15) >= 8 ? 2 : 1;
    for (unsigned i=0; i<9; i++) {
        unsigned base=r[3];
        if ((lod & (1<<24)) && !(lod>>28)) base=r[i==0 ? 3 : i==1 ? 4 : i==2 ? 5 : 6];
        mip[i].width=(wm>>i)+1; mip[i].height=(hm>>i)+1;
        mip[i].address=((base & 0xfffff0)+offset)&0x7fffff;
        unsigned size=mip[i].width*mip[i].height;
        if (i>=4 && size<4) size=4;
        if (levels & (1<<i)) offset+=size*bpp;
    }
}
static inline uint16_t native_ai44(unsigned value) {
    return ((value & 0xf0)<<8) | ((value & 15)*0x111);
}

/* Alpha-only colour path leaves vertex RGB unchanged. PVR ARGB4444
 * quantizes the 8-bit mask to four bits; white RGB makes GL_MODULATE match. */
static inline uint16_t native_alpha8_mask(unsigned value) {
    return ((value >> 4) << 12) | 0x0fff;
}
