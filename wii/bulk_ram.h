#ifndef VIPER_WII_BULK_RAM_H
#define VIPER_WII_BULK_RAM_H
#include "ppc_rt.h"
/* Only inside the hashed bulk writer fast branch: r31 <= RAM_SIZE-32,
 * r14=0x33ac, r15=0x3398 and offsets4/8/12/16. No guest call or
 * checkpoint intervenes; aperture stores are deferred by the existing proof.
 * memcpy retains support for unaligned backing and source addresses. */
static inline uint32_t wii_bulk_ram32(uint32_t ea){
 uint32_t bits;memcpy(&bits,g_ram+ea,4);return guest_be32(bits);
}
static inline uint32_t wii_bulk_ram32le(uint32_t ea){return bswap32(wii_bulk_ram32(ea));}
static inline double wii_bulk_ramf32(uint32_t ea){
 uint32_t bits=wii_bulk_ram32(ea);float f;memcpy(&f,&bits,4);return (double)f;
}
static inline void wii_bulk_ramstf32(uint32_t ea,double d){
 float f=(float)d;uint32_t bits;memcpy(&bits,&f,4);bits=guest_be32(bits);
 memcpy(g_ram+ea,&bits,4);
}
#endif
