#ifndef VIPER_WII_CR_UNPACK_H
#define VIPER_WII_CR_UNPACK_H
#include "ppc_rt.h"
/* Same ordered selected-byte writes as rt_cr_unpack. Unselected bytes, even
 * noncanonical ones, remain untouched. No memory callbacks or FP operations. */
static inline void wii_cr_unpack(PPCContext *c, uint32_t v, uint32_t crm) {
    if (crm & 0x80u) c->cr[0] = (v >> 28) & 15;
    if (crm & 0x40u) c->cr[1] = (v >> 24) & 15;
    if (crm & 0x20u) c->cr[2] = (v >> 20) & 15;
    if (crm & 0x10u) c->cr[3] = (v >> 16) & 15;
    if (crm & 0x08u) c->cr[4] = (v >> 12) & 15;
    if (crm & 0x04u) c->cr[5] = (v >> 8) & 15;
    if (crm & 0x02u) c->cr[6] = (v >> 4) & 15;
    if (crm & 0x01u) c->cr[7] = v & 15;
}
#endif
