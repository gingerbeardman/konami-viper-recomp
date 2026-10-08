#ifndef VIPER_WII_BOUNDED_SUBMISSION_H
#define VIPER_WII_BOUNDED_SUBMISSION_H
#include "ppc_rt.h"
/* Only the hashed 2adac..2af04 entry block may use these accessors.
 * Its byte indices give r6 <= 6120 and r7 <= 12240. Every word EA is
 * four-aligned, below 0x5400; no store, call or checkpoint intervenes.
 * The enclosing guard proves backing alignment and contiguous RAM mode. */
static inline uint32_t wii_submission_ram8(uint32_t ea){return g_ram[ea];}
static inline uint32_t wii_submission_ram32(uint32_t ea){
    uint32_t bits;
    memcpy(&bits,__builtin_assume_aligned(g_ram+ea,4),4);
    return guest_be32(bits);
}
static inline uint32_t wii_submission_ram32le(uint32_t ea){
    return bswap32(wii_submission_ram32(ea));
}
static inline double wii_submission_ramf32(uint32_t ea){
    uint32_t bits=wii_submission_ram32(ea);float f;
    memcpy(&f,&bits,4);return (double)f;
}
#endif
