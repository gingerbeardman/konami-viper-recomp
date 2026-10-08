#ifndef WII_GPR_MULTIPLE_H
#define WII_GPR_MULTIPLE_H
#include "ppc_rt.h"
/* Integer-only lmw/stmw lowering. Admission excludes wrapped spans and any
 * context alias. Thus no callbacks or self-modifying register source can
 * occur inside a transfer. The slow path preserves per-word access order. */
static inline uint8_t *wii_gpr_multiple_ram(PPCContext *c,uint32_t ea,unsigned first){
#if defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)
    (void)c;(void)ea;(void)first;return NULL;
#else
    if(first>=32||ea>=RAM_LIMIT)return NULL;
    unsigned bytes=(32-first)*4,off=ea&RAM_MASK;
    if(off>RAM_SIZE-bytes)return NULL;
    uint8_t *p=g_ram+off;
    if((uintptr_t)p&3)return NULL;
    uintptr_t cp=(uintptr_t)c,rp=(uintptr_t)p;
    if(cp<=rp){if(sizeof(*c)>rp-cp)return NULL;}
    else if(bytes>cp-rp)return NULL;
    return p;
#endif
}
static inline void wii_gpr_stmw(PPCContext *c,uint32_t ea,unsigned first){
    uint8_t *p=wii_gpr_multiple_ram(c,ea,first);
    if(p){
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        memcpy(p,&c->r[first],(32-first)*4);
#else
        for(unsigned k=first;k<32;k++,p+=4){uint32_t word=guest_be32(c->r[k]);memcpy(p,&word,4);}
#endif
        return;
    }
    for(unsigned k=first;k<32;k++,ea+=4)ST32(ea,c->r[k]);
}
static inline void wii_gpr_lmw(PPCContext *c,uint32_t ea,unsigned first){
    uint8_t *p=wii_gpr_multiple_ram(c,ea,first);
    if(p){
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        memcpy(&c->r[first],p,(32-first)*4);
#else
        for(unsigned k=first;k<32;k++,p+=4){uint32_t word;memcpy(&word,p,4);c->r[k]=guest_be32(word);}
#endif
        return;
    }
    for(unsigned k=first;k<32;k++,ea+=4)c->r[k]=LD32(ea);
}
#endif
