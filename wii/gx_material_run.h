#ifndef VIPER_WII_GX_MATERIAL_RUN_H
#define VIPER_WII_GX_MATERIAL_RUN_H
#include "gx_tmu_pipeline_plan.h"
/* Resident identity for one combiner plan pointer. Resumes are counted, but
 * the caller currently emits every GX call: run.CczYTB, run.elTGje and
 * run.L8cffp all wrote the same wrong RGB8 image after skipping the suffix,
 * the identity texcoord gens, or both. Combiner constants, FBI order, stage
 * count, texture loads, matrices and every fog/depth stage still run.
 * Split-depth, clear, scanout, menu, texture-off and material register
 * changes drop residency. This record does not skip fog, depth, matrices or
 * texture bindings. */
typedef struct {
    const WiiTMUPipelinePlan *plan;
    unsigned color_stages;
    int valid;
    uint64_t resumes,emits;
#ifdef VIPER_WII_COMBINER_KEEP
    /* Plan-memo identity of the resident suffix. A material reset (plan may
     * change) keeps it; any GX stage owner change drops it. */
    uint32_t ident;
    uint64_t kept;
#endif
} WiiMaterialRun;
static inline void wii_material_run_invalidate(WiiMaterialRun *run){
    run->valid=0;
#ifdef VIPER_WII_COMBINER_KEEP
    run->ident=0;
#endif
}
/* The plan pointer may now name different contents; GX stages untouched. */
static inline void wii_material_run_plan_changed(WiiMaterialRun *run){run->valid=0;}
static inline unsigned wii_material_run_color_stages(const WiiTMUPipelinePlan *plan){
    unsigned end=plan->snapshot_first;
    if(plan->snapshot_stages>1)return 0;
    if(plan->snapshot_stages)end++;
    if(end!=plan->rejection_first)return 0;
    unsigned rejection=plan->fbi.key||plan->fbi.mask?1u+(plan->fbi.mask?5u:0u):0u;
    if(plan->fbi.rejection_stages!=rejection)return 0;
    return end+rejection;
}
static inline int wii_material_run_resume(const WiiMaterialRun *run,const WiiTMUPipelinePlan *plan){
    unsigned stages=wii_material_run_color_stages(plan);
    return run->valid&&run->plan==plan&&stages&&run->color_stages==stages;
}
static inline void wii_material_run_publish(WiiMaterialRun *run,const WiiTMUPipelinePlan *plan,unsigned color_stages){
    unsigned stages=wii_material_run_color_stages(plan);
    run->valid=color_stages&&color_stages==stages;
    run->plan=run->valid?plan:NULL;
    run->color_stages=run->valid?color_stages:0;
    if(run->valid)run->emits++;
}
#ifdef VIPER_WII_COMBINER_KEEP
/* Same memo entry as the resident suffix: equal planner inputs, equal stages.
 * The caller must not use the bind skip on this path (texture bases may have
 * changed with the reset that ended the run). */
static inline int wii_material_run_resume_ident(WiiMaterialRun *run,const WiiTMUPipelinePlan *plan,uint32_t ident){
    unsigned stages=wii_material_run_color_stages(plan);
    if(!ident||run->ident!=ident||!stages||run->color_stages!=stages)return 0;
    run->valid=1;run->plan=plan;run->kept++;
    return 1;
}
static inline void wii_material_run_publish_ident(WiiMaterialRun *run,uint32_t ident){
    run->ident=run->valid?ident:0;
}
#endif
#endif
