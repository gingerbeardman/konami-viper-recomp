#ifndef VIPER_WII_GX_COMBINER_PROGRAM_CACHE_H
#define VIPER_WII_GX_COMBINER_PROGRAM_CACHE_H
#include "gx_material_plan.h"
#include "gx_tmu_equation.h"
#include "gx_color_equation.h"
#include <string.h>
/* One immutable material slot. Resident stages are [0, end). Snapshot,
 * predicate, fog and depth start at end and must not write this range.
 * Invalidate before a slot rewrite and before clear, scanout, menu or any
 * other external stage owner. wii_gx_combiner_program_emit still writes
 * K2/K3, REG1/REG2 and the stage count on every hit. The optional emit takes
 * the caller's constants_needed flag and does not infer that those constants
 * stayed immutable. A hit may skip K2/K3 and GX_TEVREG1/REG2 only when that
 * flag is false. A hit always executes GX_SetNumTevStages and the same
 * repeated FBI GX_SetTevOrder. A miss always emits the original constants
 * and program for either flag. GX_SetNumTevStages dirties genMode only; one
 * repeated FBI GX_SetTevOrder re-establishes the order and texture-resource
 * dirty bit when the suffix itself calls no GX_SetTevOrder. Order and
 * konst-select words pack two stages, so that call is a public
 * read-modify-write of the pair. Publish only after the whole TMU and FBI
 * program has been emitted.
 * A TMU-only result is not resident. */
typedef struct {
 const WiiTMUPipelinePlan *plan;
 unsigned end;
 uint64_t hits,misses;
#ifdef VIPER_WII_COMBINER_KEEP
 /* Plan-memo identity of the program resident in GX stages [0,end). A
  * material reset only says the plan may change; the stages stay resident
  * until an external owner invalidates. One memo entry id names one set of
  * planner inputs, so equal ids emit equal stages. ident is the caller's
  * identity for the plan about to be emitted (0: unknown). */
 uint32_t last,ident;
 int resident;
 uint64_t kept;
#endif
} WiiCombinerProgramCache;
static inline void wii_combiner_program_invalidate(WiiCombinerProgramCache *c){
 c->plan=NULL;
#ifdef VIPER_WII_COMBINER_KEEP
 c->resident=0;
#endif
}
/* The plan pointer may now name different contents; GX stages untouched. */
static inline void wii_combiner_program_plan_changed(WiiCombinerProgramCache *c){c->plan=NULL;}
#ifdef VIPER_WII_COMBINER_KEEP
static inline int wii_combiner_program_kept(WiiCombinerProgramCache *c,const WiiTMUPipelinePlan *p){
 if(!c->resident||!c->ident||c->ident!=c->last)return 0;
 c->plan=p;c->kept++;
 return 1;
}
static inline void wii_combiner_program_resident(WiiCombinerProgramCache *c,const WiiTMUPipelinePlan *p){
 (void)p;c->last=c->ident;c->resident=!!c->ident;
}
#define COMBINER_KEPT(c,p) wii_combiner_program_kept(c,p)
#define COMBINER_RESIDENT(c,p) wii_combiner_program_resident(c,p)
#else
#define COMBINER_KEPT(c,p) 0
#define COMBINER_RESIDENT(c,p) ((void)0)
#endif
static inline void wii_combiner_program_reset(WiiCombinerProgramCache *c){memset(c,0,sizeof *c);}
static inline void wii_combiner_program_material_invalidate(WiiMaterialPlans *m,WiiCombinerProgramCache *c){
 wii_combiner_program_plan_changed(c);wii_material_plans_invalidate(m);
}
static inline const WiiTMUPipelinePlan *wii_combiner_program_material_get(
 WiiMaterialPlans *m,WiiCombinerProgramCache *c,uint32_t cp,uint32_t fbz,uint32_t alpha,
 uint32_t fog,uint32_t key,uint32_t range,const uint32_t *t0,const uint32_t *t1,
 uint32_t init3,unsigned packet,int positive_alpha){
 if(!m->have_packet||m->packet!=packet)wii_combiner_program_plan_changed(c);
 return wii_material_plan_get(m,cp,fbz,alpha,fog,key,range,t0,t1,init3,packet,positive_alpha);
}
static inline void wii_combiner_program_dynamic(const WiiTMUPipelinePlan *p,GXColor c0,GXColor c1,unsigned end){
 for(int unit=1;unit>=0;unit--)if(p->unit[unit].use){
  const WiiTMUUnitPlan *u=&p->unit[unit];
  GX_SetTevKColor(unit?GX_KCOLOR2:GX_KCOLOR3,
   (GXColor){u->detail_factor,u->detail_factor,u->detail_factor,u->lod_fraction});
 }
 GX_SetTevColor(GX_TEVREG1,c0);GX_SetTevColor(GX_TEVREG2,c1);
 GX_SetNumTevStages(end);
 /* Same FBI order the cold path writes. Public setter; no SDK shadow store. */
 GX_SetTevOrder(p->fbi_first,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
}
/* fault: 1 TMU budget, 2 TMU/plan mismatch, 3 FBI budget, 4 FBI/plan mismatch. Zero on success.
 * constants_needed is the caller's boolean. False skips invariant K2/K3 and
 * REG1/REG2 on a hit only. It never skips stage-count or FBI order restoration. */
static inline unsigned wii_gx_combiner_program_emit_optional(WiiCombinerProgramCache *c,
 const WiiTMUPipelinePlan *p,GXColor c0,GXColor c1,int constants_needed,int *fault){
 *fault=0;
 if(c->plan==p||COMBINER_KEPT(c,p)){
  c->hits++;
  if(constants_needed)wii_combiner_program_dynamic(p,c0,c1,c->end);
  else{
   GX_SetNumTevStages(c->end);
   GX_SetTevOrder(p->fbi_first,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
  }
  return c->end;
 }
 c->misses++;
 wii_combiner_program_invalidate(c);
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
  if(!end){*fault=1;return 0;}
 }
 if(end!=p->fbi_first){*fault=2;return end;}
 unsigned fbi=wii_gx_color_equation_at(p->fbi.equation,c0,c1,end,
  GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
 if(!fbi){*fault=3;return 0;}
 if(fbi!=p->snapshot_first){*fault=4;return fbi;}
 c->plan=p;c->end=fbi;COMBINER_RESIDENT(c,p);
 return fbi;
}
static inline unsigned wii_gx_combiner_program_emit(WiiCombinerProgramCache *c,
 const WiiTMUPipelinePlan *p,GXColor c0,GXColor c1,int *fault){
 *fault=0;
 if(c->plan==p||COMBINER_KEPT(c,p)){
  c->hits++;
  wii_combiner_program_dynamic(p,c0,c1,c->end);
  return c->end;
 }
 c->misses++;
 wii_combiner_program_invalidate(c);
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
  if(!end){*fault=1;return 0;}
 }
 if(end!=p->fbi_first){*fault=2;return end;}
 unsigned fbi=wii_gx_color_equation_at(p->fbi.equation,c0,c1,end,
  GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
 if(!fbi){*fault=3;return 0;}
 if(fbi!=p->snapshot_first){*fault=4;return fbi;}
 c->plan=p;c->end=fbi;COMBINER_RESIDENT(c,p);
 return fbi;
}
#endif
