/* SPDX-License-Identifier: BSD-3-Clause
 * Derived from MAME Voodoo rasterizer_params::compute/compute_equations and
 * rasterizer_texture::fetch_texel/combine_texture (copyright Aaron Giles). */
#ifndef VIPER_WII_GX_TMU_PIPELINE_PLAN_H
#define VIPER_WII_GX_TMU_PIPELINE_PLAN_H
#include "gx_pipeline_plan.h"
#include "voodoo_tmu_eval.h"
typedef enum { WII_TMU_PIPE_OK,WII_TMU_PIPE_FBI,WII_TMU_PIPE_PACKET,
    WII_TMU_PIPE_CONFIG,WII_TMU_PIPE_FORMAT,WII_TMU_PIPE_LAYOUT,
    WII_TMU_PIPE_LOD_FACTOR,WII_TMU_PIPE_FILTER,WII_TMU_PIPE_BUDGET
} WiiTMUPipelineReason;
typedef struct {
    WiiVoodooTMUPlan equation;
    unsigned enabled,use,local_needed,other_needed,identity_local,bypass;
    unsigned format,level,perspective,linear,clamp_s,clamp_t;
    unsigned first,stages,detail_factor,lod_fraction;
    int32_t fixed_lod;
} WiiTMUUnitPlan;
typedef struct {
    WiiGXPipelinePlan fbi;
    WiiTMUUnitPlan unit[2];
    WiiTMUPipelineReason reason;
    unsigned texture_needed,texture_zero,final_unit;
    unsigned fbi_first,snapshot_first,snapshot_stages,rejection_first;
    unsigned fog_first,depth_first,total_stages;
} WiiTMUPipelinePlan;
static inline unsigned wii_tmu_equation_cost(WiiVoodooTMUPlan p){
    return (p.rgb_sub||p.alpha_sub?3u:1u)+!!(p.rgb_invert||p.alpha_invert);
}
/* Sufficient identities, not guesses based on captured register words.
 * Zero base makes multiplier irrelevant, including detail/fraction factors. */
static inline int wii_tmu_local_identity(WiiVoodooTMUPlan p){
    return p.rgb_zero&&!p.rgb_sub&&p.rgb_add==1&&!p.rgb_invert&&
        p.alpha_zero&&!p.alpha_sub&&p.alpha_add&&!p.alpha_invert;
}
static inline int wii_tmu_other_identity(WiiVoodooTMUPlan p){
    return !p.rgb_zero&&!p.rgb_sub&&!p.rgb_mul&&!p.rgb_reverse&&!p.rgb_add&&!p.rgb_invert&&
        !p.alpha_zero&&!p.alpha_sub&&!p.alpha_mul&&!p.alpha_reverse&&!p.alpha_add&&!p.alpha_invert;
}
static inline int wii_tmu_needs_local(WiiVoodooTMUPlan p){
    int rgb_base=!p.rgb_zero||p.rgb_sub,alpha_base=!p.alpha_zero||p.alpha_sub;
    return p.rgb_sub||p.alpha_sub||p.rgb_add==1||p.rgb_add==2||p.alpha_add||
        (rgb_base&&(p.rgb_mul==1||p.rgb_mul==3))||
        (alpha_base&&(p.alpha_mul==1||p.alpha_mul==3));
}
static inline int wii_tmu_needs_other(WiiVoodooTMUPlan p){
    return !p.rgb_zero||!p.alpha_zero||
        ((!p.rgb_zero||p.rgb_sub)&&p.rgb_mul==2)||
        ((!p.alpha_zero||p.alpha_sub)&&p.alpha_mul==2);
}
/* regs[u] is NULL for an absent chip; otherwise >=9 raw words beginning at
 * 0x300: mode0,lod1,detail2,base3..6,trexInit0=7,trexInit1=8 (0x320).
 * init3 is raw FBI init3, NOT a TMU register. Bit31 of raw textureMode is a
 * download flag, not the normalized software TMU_CONFIG_MASK.
 * Sampler support is intentionally bounded to existing RGBA8 conversions,
 * one fixed integer mip, nonnegative/valid per-vertex W checked by caller.
 * Current renderer filter OR approximation is NOT admitted for mismatched
 * min/mag filters here. No side effects and no GX calls. */
static inline WiiTMUPipelinePlan wii_gx_tmu_pipeline_plan(uint32_t cp,uint32_t fbz,
    uint32_t alpha,uint32_t fog,uint32_t key,uint32_t range,
    const uint32_t *t0,const uint32_t *t1,uint32_t init3,unsigned packet,int positive_alpha){
    WiiTMUPipelinePlan p={0};const uint32_t *r[2]={t0,t1};
    p.final_unit=2;p.reason=WII_TMU_PIPE_FBI;
    /* Reuse common FBI/rejection admission; placeholder sampler is discarded.
     * No old tuple classifier is used for actual TMU equations. */
    p.fbi=wii_gx_pipeline_plan(cp,fbz,alpha,fog,key,range,0x10241a07u,0x10241a07u,0x3b,positive_alpha);
    if(p.fbi.reason!=WII_PIPE_OK)return p;
    p.reason=WII_TMU_PIPE_PACKET;
    if(packet>255)return p; /* All eight FIFO attribute bits are defined. */
    WiiVoodooColorPlan f=p.fbi.equation;
    /* Conservative liveness includes OTHER even if equation zeros it, since
     * chroma/mask precede the FBI equation. Extra work is safe. */
    p.texture_needed=f.other_rgb==1||f.other_alpha==1||f.rgb_mul==4||f.rgb_mul==5||f.alpha_mul==4;
    for(unsigned u=0;u<2;u++)if(r[u]){
        p.unit[u].enabled=!(init3&64)&&((r[u][1]&63)<32);
        p.unit[u].equation=wii_voodoo_tmu_plan(r[u][0]);
    }
    p.texture_zero=1;
    if(p.texture_needed){
        if(p.unit[0].enabled){
            p.reason=WII_TMU_PIPE_CONFIG;
            if(t0[8]&(1u<<18))return p;
            WiiTMUUnitPlan *u=&p.unit[0];
            u->bypass=wii_tmu_other_identity(u->equation);
            u->identity_local=wii_tmu_local_identity(u->equation);
            u->use=!u->bypass;
            u->other_needed=!u->identity_local&&wii_tmu_needs_other(u->equation);
            if(u->use){p.final_unit=0;p.texture_zero=0;}
            if(u->bypass||u->other_needed)p.unit[1].use=p.unit[1].enabled;
        }else p.unit[1].use=p.unit[1].enabled;
        if(p.unit[1].use&&p.final_unit==2){p.final_unit=1;p.texture_zero=0;}
    }
    unsigned offset=0;
    for(int unit=1;unit>=0;unit--){
        WiiTMUUnitPlan *u=&p.unit[unit];if(!u->use)continue;
        const uint32_t *reg=r[unit];WiiVoodooTMUPlan q=u->equation;
        u->identity_local=wii_tmu_local_identity(q);
        u->local_needed=wii_tmu_needs_local(q);
        u->other_needed=unit==0&&wii_tmu_needs_other(q)&&p.unit[1].use;
        unsigned lo=reg[1]&63,hi=(reg[1]>>6)&63;
        int lod_factor=((!q.rgb_zero||q.rgb_sub)&&(q.rgb_mul==4||q.rgb_mul==5))||
            ((!q.alpha_zero||q.alpha_sub)&&(q.alpha_mul==4||q.alpha_mul==5));
        /* Values for dead multiplier inputs are harmless; no raw LOD proof
         * is needed when that product is identically zero. */
        p.reason=WII_TMU_PIPE_LOD_FACTOR;
        if(lod_factor&&lo!=hi)return p;
        u->fixed_lod=(int32_t)(lo<<6);
        u->detail_factor=wii_voodoo_tmu_detail(reg[2],u->fixed_lod);
        u->lod_fraction=(unsigned)u->fixed_lod&255;
        if(u->local_needed){
            p.reason=WII_TMU_PIPE_PACKET;
            /* setup_and_draw_triangle writes ST0 planes into BOTH TMUs,
             * then ST1 overrides TMU1 (voodoo_2.cpp). This is rasterizer
             * inheritance, not merely execute_type3 vertex initialization.
             * Do not invent coordinates when neither source exists. */
            if(!(packet&(unit==0?32u:(32u|128u))))return p;
            /* MAME leaves the prior W setup registers unchanged when W is
             * absent; the Wii callback defaults to one. Never treat that default as
             * a supplied perspective denominator. Affine sampling ignores W. */
            if((reg[0]&1)&&!(packet&(unit==0?(8u|16u):(8u|16u|64u))))return p;
            p.reason=WII_TMU_PIPE_FORMAT;u->format=(reg[0]>>8)&15;
            if(!wii_texture_format_supported(u->format))return p;
            p.reason=WII_TMU_PIPE_LAYOUT;
            if(lo>hi||lo/4!=hi/4||(reg[3]&1))return p;
            u->level=lo/4;
            if((reg[1]&(1u<<19))&&((u->level&1)!=!!(reg[1]&(1u<<18))))u->level++;
            if(u->level>8)return p;
            p.reason=WII_TMU_PIPE_FILTER;
            if(!!(reg[0]&2)!=!!(reg[0]&4))return p;
            u->linear=!!(reg[0]&4);u->perspective=!!(reg[0]&1);
            u->clamp_s=!!(reg[0]&64);u->clamp_t=!!(reg[0]&128);
        }
        u->first=offset;u->stages=u->identity_local?1:wii_tmu_equation_cost(q);offset+=u->stages;
    }
    p.fbi.textured=!p.texture_zero;p.fbi_first=offset;offset+=p.fbi.equation_stages;
    p.snapshot_first=offset;
    p.snapshot_stages=!p.texture_zero&&((p.fbi.key&&f.other_rgb==1)||(p.fbi.mask&&f.other_alpha==1));
    offset+=p.snapshot_stages;p.rejection_first=offset;offset+=p.fbi.rejection_stages;
    p.fog_first=offset;offset+=fog==0x41;p.depth_first=offset;offset+=!!(fbz&(16|1024));
    p.total_stages=offset;p.fbi.total_stages=offset;
    p.reason=offset>16?WII_TMU_PIPE_BUDGET:WII_TMU_PIPE_OK;
    return p;
}
#endif
