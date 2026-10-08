#pragma once
#include "ppc_rt.h"
/* Exact complete rounds of GTI Club 2's equal-status worker loop at45e1c.
 * Caller must be the verified original function, with no per-read tracing. */
static inline unsigned rt_idle_worker_batch(PPCContext *c){
    uint32_t base=c->r[29];
    if(c->budget<=94||c->r[24]!=0||c->r[26]!=base||c->r[23]!=base-3||
       base>RAM_LIMIT-142)return 0;
    uint32_t last=0;
    for(unsigned i=0;i<6;i++){
        uint32_t a=LD8(base+20+24*i),b=LD8(base+21+24*i);
        if(a!=b)return 0;
        last=b;
    }
    unsigned rounds=(unsigned)((c->budget-1)/94);
    c->budget-=(int64_t)rounds*94;
    c->r[0]=c->r[3]=(uint32_t)(int32_t)(int8_t)last;
    c->r[4]=15;c->r[31]=120;c->r[30]=0;
    c->cr[2]=c->cr[3];c->cr[1]=2|c->xer_so;
    c->xer_ca=(uint8_t)(((uint64_t)base+0xfffffffdu)>>32);
    return rounds;
}
