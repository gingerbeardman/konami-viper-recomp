#ifndef VIPER_WII_FOG_H
#define VIPER_WII_FOG_H
#include <stdint.h>
/* Voodoo2 mode0x41: table fog, unsigned delta, 4x4 dither. The caller
 * chooses the dither value; using8 is an approximation to per-pixel dithering.
 * Returns the original 1..256 blend factor, before GX conversion. */
static inline unsigned wii_fog_factor(const uint32_t table[32],unsigned depth,unsigned dither){
 if(depth>65535)depth=65535;
 unsigned entry=(table[depth>>11]>>((depth&1024)?16:0))&65535;
 unsigned delta=((entry&252)*((depth>>2)&255))>>6;
 unsigned factor=(entry>>8)+((delta+dither)>>4)+1;
 return factor>256?256:factor;
}
/* GX's interpolation factor is byte+(byte>>7), so factor128 cannot be
 * represented exactly. Other factors map exactly; final RGB rounding differs. */
static inline unsigned wii_fog_tev_factor(unsigned factor){
 if(factor>256)factor=256;
 return factor>=129?factor-1:factor;
}
#endif
