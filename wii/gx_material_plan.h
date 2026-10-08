/* Exact pure planner cache. Caller invalidates on relevant device mutations.
 * Returned slots are immutable until invalidation or packet change. Guest
 * writes cannot interleave synchronous recursive callbacks; alpha has two
 * independent slots because W-band interpolation can change its predicate. */
#ifndef VIPER_WII_GX_MATERIAL_PLAN_H
#define VIPER_WII_GX_MATERIAL_PLAN_H
#include "gx_tmu_pipeline_plan.h"
#include <string.h>
typedef struct {
    WiiTMUPipelinePlan plan[2];
    unsigned valid[2],packet,have_packet;
#ifdef VIPER_WII_PLAN_MEMO
    uint32_t ident[2];
#endif
} WiiMaterialPlans;
static inline void wii_material_plans_invalidate(WiiMaterialPlans *c){
    c->valid[0]=c->valid[1]=c->have_packet=0;
}
static inline const WiiTMUPipelinePlan *wii_material_plan_get(WiiMaterialPlans *c,
    uint32_t cp,uint32_t fbz,uint32_t alpha,uint32_t fog,uint32_t key,uint32_t range,
    const uint32_t *t0,const uint32_t *t1,uint32_t init3,unsigned packet,int positive_alpha){
    if(!c->have_packet||c->packet!=packet){
        c->valid[0]=c->valid[1]=0;c->packet=packet;c->have_packet=1;
    }
    unsigned slot=!!positive_alpha;
    if(!c->valid[slot]){
#ifdef VIPER_WII_PLAN_MEMO
        /* The planner is a pure function of exactly these inputs (TMU words
         * 0-2, bit 0 of word 3 and word 8 of each unit, plus the FBI words):
         * texture switches that keep the same mode/format reuse the plan. */
        static struct { uint32_t k[19]; uint32_t id; WiiTMUPipelinePlan plan; } memo[32];
        static uint32_t memo_ids;
        uint32_t k[19]={cp,fbz,alpha,fog,key,range,init3,packet,
            (uint32_t)positive_alpha|(t0?2u:0u)|(t1?4u:0u)};
        if(t0){k[9]=t0[0];k[10]=t0[1];k[11]=t0[2];k[12]=t0[3]&1u;k[13]=t0[8];}
        if(t1){k[14]=t1[0];k[15]=t1[1];k[16]=t1[2];k[17]=t1[3]&1u;k[18]=t1[8];}
        uint32_t h=0;for(unsigned i=0;i<19;i++)h=(h^k[i])*0x9e3779b1u;
        unsigned m=(h>>27)&31;
        if(memo[m].id&&!memcmp(memo[m].k,k,sizeof k))c->plan[slot]=memo[m].plan;
        else{
            c->plan[slot]=wii_gx_tmu_pipeline_plan(cp,fbz,alpha,fog,key,range,t0,t1,init3,packet,positive_alpha);
            memcpy(memo[m].k,k,sizeof k);memo[m].plan=c->plan[slot];
            if(!++memo_ids)memo_ids=1; /* ids are never reused before wrap */
            memo[m].id=memo_ids;
        }
        c->ident[slot]=memo[m].id;
#else
        c->plan[slot]=wii_gx_tmu_pipeline_plan(cp,fbz,alpha,fog,key,range,t0,t1,init3,packet,positive_alpha);
#endif
        c->valid[slot]=1;
    }
    return &c->plan[slot];
}
/* Memo entry id of a plan returned above (0 when not memoised). */
static inline uint32_t wii_material_plan_ident(const WiiMaterialPlans *c,const WiiTMUPipelinePlan *p){
#ifdef VIPER_WII_PLAN_MEMO
    return c->ident[p==&c->plan[1]];
#else
    (void)c;(void)p;return 0;
#endif
}
/* Conservative raw-word read set, including texBase bits not currently used.
 * Called only after masked writes actually change the stored register. */
static inline int wii_material_fbi_word(unsigned r){
    return r==0x104/4||r==0x110/4||r==0x10c/4||r==0x108/4||
        r==0x134/4||r==0x138/4||r==0x21c/4;
}
/* TMU words 4-6 (texBaseAddr_1.._3_8) select multibase mip addresses, so a
 * change there must also end bind-skip reuse of the previous texture. */
static inline int wii_material_tmu_word(unsigned r){
    return r<=6||r==8;
}
#endif
