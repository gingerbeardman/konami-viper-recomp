#ifndef VIPER_WII_GX_TMU_PREFIX_CACHE_H
#define VIPER_WII_GX_TMU_PREFIX_CACHE_H
#include "gx_material_plan.h"
#include "gx_tmu_equation.h"
/* The material slot is immutable until device invalidation or packet change.
 * Caller owns GX stages[0,fbi_first): suffix setters must not touch that range.
 * Invalidate before any fallback/clear/scanout/menu/external GX owner does so.
 * Dynamic konst colours and stage count are emitted even on a hit. There must
 * be no draw or state consumer between this prefix and the FBI suffix. */
typedef struct {
 const WiiTMUPipelinePlan *plan;
 uint64_t hits,misses;
} WiiTMUPrefixCache;
static inline void wii_tmu_prefix_invalidate(WiiTMUPrefixCache *c){c->plan=NULL;}
static inline void wii_tmu_prefix_reset(WiiTMUPrefixCache *c){memset(c,0,sizeof *c);}
static inline void wii_tmu_prefix_material_invalidate(WiiMaterialPlans *m,WiiTMUPrefixCache *c){
 wii_tmu_prefix_invalidate(c);wii_material_plans_invalidate(m);
}
static inline const WiiTMUPipelinePlan *wii_tmu_prefix_material_get(
 WiiMaterialPlans *m,WiiTMUPrefixCache *c,uint32_t cp,uint32_t fbz,uint32_t alpha,
 uint32_t fog,uint32_t key,uint32_t range,const uint32_t *t0,const uint32_t *t1,
 uint32_t init3,unsigned packet,int positive_alpha){
 if(!m->have_packet||m->packet!=packet)wii_tmu_prefix_invalidate(c);
 return wii_material_plan_get(m,cp,fbz,alpha,fog,key,range,t0,t1,init3,packet,positive_alpha);
}
static inline unsigned wii_gx_tmu_prefix_original(const WiiTMUPipelinePlan *p){
 unsigned end=0;
 for(int unit=1;unit>=0;unit--)if(p->unit[unit].use){
  const WiiTMUUnitPlan *u=&p->unit[unit];
  GX_SetTevKColor(unit?GX_KCOLOR2:GX_KCOLOR3,
   (GXColor){u->detail_factor,u->detail_factor,u->detail_factor,u->lod_fraction});
  int other=unit==0&&p->unit[1].use;
  end=wii_gx_tmu_equation(u->equation,end,
   u->local_needed?(unit?GX_TEXCOORD2:GX_TEXCOORD0):GX_TEXCOORDNULL,
   u->local_needed?(unit?GX_TEXMAP3:GX_TEXMAP0):GX_TEXMAP_NULL,
   other?GX_CC_C0:GX_CC_ZERO,other?GX_CA_A0:GX_CA_ZERO,
   other?GX_CC_A0:GX_CC_ZERO,GX_TEVREG0,
   unit?GX_TEV_KCSEL_K2:GX_TEV_KCSEL_K3,
   unit?GX_TEV_KASEL_K2_R:GX_TEV_KASEL_K3_R,
   unit?GX_TEV_KCSEL_K2_A:GX_TEV_KCSEL_K3_A,
   unit?GX_TEV_KASEL_K2_A:GX_TEV_KASEL_K3_A);
  if(!end)return 0;
 }
 return end;
}
static inline unsigned wii_gx_tmu_prefix_emit(WiiTMUPrefixCache *c,const WiiTMUPipelinePlan *p){
 if(c->plan==p){
  c->hits++;
  for(int unit=1;unit>=0;unit--)if(p->unit[unit].use){
   const WiiTMUUnitPlan *u=&p->unit[unit];
   GX_SetTevKColor(unit?GX_KCOLOR2:GX_KCOLOR3,
    (GXColor){u->detail_factor,u->detail_factor,u->detail_factor,u->lod_fraction});
  }
  GX_SetNumTevStages(p->fbi_first);return p->fbi_first;
 }
 c->misses++;
 unsigned end=wii_gx_tmu_prefix_original(p);
 c->plan=end&&end==p->fbi_first?p:NULL;
 return end;
}
#endif
