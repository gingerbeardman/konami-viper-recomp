#ifndef VIPER_WII_GX_PIPELINE_PLAN_H
#define VIPER_WII_GX_PIPELINE_PLAN_H
#include "voodoo_color_plan.h"
#include "rejection_policy.h"
#include "texture.h"
typedef enum { WII_PIPE_OK,WII_PIPE_EQUATION,WII_PIPE_SAMPLING,
    WII_PIPE_FRAMEBUFFER,WII_PIPE_CHROMA,WII_PIPE_ALPHA_DEPTH,WII_PIPE_STAGES
} WiiPipelineReason;
static inline const char *wii_pipeline_reason_name(WiiPipelineReason reason){
    switch(reason){
    case WII_PIPE_OK:return "supported";
    case WII_PIPE_EQUATION:return "FBI source/clamp operation";
    case WII_PIPE_SAMPLING:return "TMU equation or sampling layout";
    case WII_PIPE_FRAMEBUFFER:return "framebuffer, blend or fog operation";
    case WII_PIPE_CHROMA:return "chroma key/range operation";
    case WII_PIPE_ALPHA_DEPTH:return "rejection with observable zero-alpha survivor";
    case WII_PIPE_STAGES:return "TEV stage capacity";
    }
    return "invalid pipeline reason";
}
typedef struct {
    WiiVoodooColorPlan equation;
    WiiRejectionPolicy rejection;
    WiiPipelineReason reason;
    unsigned textured,unit,format,equation_stages,rejection_stages,total_stages;
    unsigned key,mask;
} WiiGXPipelinePlan;
/* Preflight established single-TMU sampling with arbitrary FBI equations.
 * No GX mutation occurs here. True TMU arithmetic is a separate capability. */
static inline WiiGXPipelinePlan wii_gx_pipeline_plan(uint32_t cp,uint32_t fbz,
    uint32_t alpha,uint32_t fog,uint32_t key,uint32_t range,uint32_t t0,
    uint32_t t1,unsigned packet,int positive_alpha){
    WiiGXPipelinePlan p={0};p.equation=wii_voodoo_color_plan(cp);
    p.reason=WII_PIPE_EQUATION;
    if((cp&0xe0000000u)||!p.equation.clamp||p.equation.local_override||
       p.equation.local_alpha>=2)return p;
    p.reason=WII_PIPE_SAMPLING;
    if(packet==0x0b){
        /* No texture input is observable in this conservative subset. OTHER
         * still matters to rejection even when the equation zeros its base. */
        if(p.equation.other_rgb==1||p.equation.other_alpha==1||
           p.equation.rgb_mul==4||p.equation.rgb_mul==5||p.equation.alpha_mul==4)return p;
    }else{
        if(packet!=0x23&&packet!=0x3b)return p;
        unsigned mode=packet==0x23?6:7;
        uint32_t a=t0&~0xfc0u,b=t1&~0xfc0u;
        if(packet==0x23&&(b&7)==0)mode=0;
        if(b!=(0x10241000u|mode)||(a!=mode&&a!=b))return p;
        p.textured=1;p.unit=a==mode?1:0;p.format=((p.unit?t1:t0)>>8)&15;
        if(!wii_texture_format_supported(p.format))return p;
    }
    p.reason=WII_PIPE_FRAMEBUFFER;
    if((fbz&((1u<<2)|(1u<<16)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)))||
       (alpha&0xe0u)||(fog!=0x40&&fog!=0x41))return p;
    if(alpha&16){unsigned s=(alpha>>8)&15,d=(alpha>>12)&15;
        if(s==3||s>6||d==3||d>6)return p;}
    p.key=!!(fbz&2);p.mask=!!(fbz&(1u<<13));
    p.reason=WII_PIPE_CHROMA;
    if(p.key&&((key&0xffffffu)||((range&(1u<<28))&&(range&0x0fffffffu))))return p;
    p.rejection=wii_rejection_policy(fbz,alpha,positive_alpha);
    p.reason=WII_PIPE_ALPHA_DEPTH;
    if((p.key||p.mask)&&p.rejection==WII_REJECT_UNSUPPORTED)return p;
    p.equation_stages=(p.equation.rgb_sub||p.equation.alpha_sub?3:1)+
        !!(p.equation.rgb_invert||p.equation.alpha_invert);
    p.rejection_stages=p.key||p.mask?1+(p.mask?5:0):0;
    p.total_stages=p.equation_stages+p.rejection_stages+(fog==0x41)+!!(fbz&(16|1024));
    p.reason=p.total_stages>16?WII_PIPE_STAGES:WII_PIPE_OK;
    return p;
}
#endif
