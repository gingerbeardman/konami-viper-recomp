#ifndef VIPER_WII_TEXTURE_H
#define VIPER_WII_TEXTURE_H
#include <stddef.h>
#include <stdint.h>
/* GX RGBA8: 4x4 tiles, first 32 bytes A/R pairs, next 32 G/B pairs.
 * Unused tile pixels are zero; guest source is a wrapping LE byte ring.
 * palette is 256 host-order ARGB8888 entries; formats5/14 ignore its alpha.
 * NCC formats1/9 and special RGBA palette6 require separate lookup tables. */
size_t wii_texture_rgba8_size(unsigned width,unsigned height);
/* Formats with source-derived conversion already implemented below. */
static inline int wii_texture_format_supported(unsigned format){
    switch(format){
    case 0:case 2:case 3:case 4:case 5:case 8:
    case 10:case 11:case 12:case 13:case 14:return 1;
    default:return 0;
    }
}
int wii_texture_rgba8(uint8_t *dst,size_t dst_bytes,const uint8_t *source,
                     size_t source_bytes,size_t base,unsigned width,unsigned height,
                     unsigned format,const uint32_t *palette);
#endif
