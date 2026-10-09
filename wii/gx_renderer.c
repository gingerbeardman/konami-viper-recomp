/* GX backend. Device/FIFO/shadow semantics stay in voodoo_headless.
 * Register fields follow runtime/voodoo/voodoo_regs.h. This is not yet a general
 * Voodoo renderer: varying fixed-point colour/depth, TMUs and fog stop explicitly. */
#include "hot_layout.h"
#include "gx_renderer.h"
#ifdef VIPER_WII_GX_BATCH
#if !defined(VIPER_WII_GX_STATE_SHADOW) || !defined(VIPER_WII_VERTEX_STQ) || defined(VIPER_WII_GX_CONFIG_TRACE)
#error GX batching needs the state shadow and per-vertex STQ, and wraps the setters tracing wraps
#endif
/* Must precede every header that issues GX calls. */
#include "gx_batch.h"
#if defined(VIPER_WII_TEXLOAD_SKIP) && (!defined(VIPER_WII_GX_BATCH) || !defined(VIPER_WII_NATIVE_TEXTURE_RESOURCE))
#error Texture load skip wraps the batched GX_LoadTexObj and compares native texture resources
#endif
#ifdef VIPER_WII_TEXLOAD_SKIP
/* The last GX_LoadTexObj issued, when it came from a texture bind. Repeating
 * that exact load (same object, same map, no load of any kind in between)
 * changes no GX register, TMEM region or batch, so it is skipped. Every other
 * load and every external GX owner (clear, scanout, present/menu,
 * texture-off) forgets it. */
static struct { const void *image; unsigned width,height,wrap_s,wrap_t,filter,map; int valid; uint64_t skipped; } texload;
#undef GX_LoadTexObj
#define GX_LoadTexObj(...) (texload.valid=0, GX_BATCH_FLUSHING(GX_LoadTexObj, __VA_ARGS__))
#define TEXLOAD_FORGET() (texload.valid=0)
#else
#define TEXLOAD_FORGET() ((void)0)
#endif
#ifdef VIPER_WII_MEMO_MULTI
#include "gx_memo_log.h"
static unsigned memo_tex_gen;   /* bumped by every texture upload and discard */
#endif
/* libogc's GX_SetCurrentGXThread names the thread the CP FIFO overflow
 * interrupt suspends; it disables interrupts around the update. Every
 * caller in the port goes through here (GX_Init names the initial thread
 * before any of them), so the call is skipped only when it would store the
 * thread already named. */
static lwp_t gx_owner=LWP_THREAD_NULL;
#ifdef VIPER_WII_SUPERSAMPLE
static void ss_abort_line(int line);
#define ss_abort() ss_abort_line(__LINE__)
#endif
void wii_gx_own_thread(void){
    lwp_t self=LWP_GetSelf();
    if(self!=gx_owner){
        GX_SetCurrentGXThread();gx_owner=self;
    }
}
#endif
#include "render_family.h"
#include "voodoo_color_plan.h"
#include "gx_rejection.h"
#include "gx_fog_stage.h"
#ifdef VIPER_WII_GX_COLOR_EQUATION
#include "gx_color_equation.h"
#include "gx_pipeline_plan.h"
#ifdef VIPER_WII_GX_TMU_PIPELINE
#include "gx_tmu_pipeline_plan.h"
#include "gx_tmu_equation.h"
#include "gx_constant_fog.h"
#endif
#endif
#include "voodoo_headless.h"
#include "runtime.h"
#include "menu.h"
#ifdef VIPER_WII_FRAME_CAPTURE
#include "net_report.h"
static void frame_capture(void);
#endif
#include "texture.h"
#include "texture_cache.h"
#include "projective_texture.h"
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
#include "lookup_plane_cache.h"
#endif
#ifdef VIPER_WII_GX_FOG_APPROX
#ifndef VIPER_WII_GX_WDEPTH_APPROX
#error Fog lookup requires the W-depth band splitter
#endif
#include "fog.h"
#endif
#ifdef VIPER_WII_GX_WDEPTH_APPROX
#include "wdepth_split.h"
#endif
#include "texture_layout.h"
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
#include "texture_layout_cache.h"
static WiiTextureLayouts texture_layouts;
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
#include "native_texture_source.h"
static WiiNativeTextureSources native_texture_sources;
#endif
static void texture_layout_reset(unsigned unit){
    wii_texture_layout_invalidate(&texture_layouts,unit);
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
    wii_native_texture_source_invalidate(&native_texture_sources,unit);
#endif
}
#endif
#if defined(VIPER_WII_NATIVE_TEXTURE_SOURCE) && (!defined(VIPER_WII_TEXTURE_LAYOUT_CACHE) || !defined(VIPER_WII_NATIVE_TEXTURE_RESOURCE))
#error Native texture sources require device layout invalidation and native GX resources
#endif
#include <math.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <ogc/lwp_watchdog.h>
#if defined(VIPER_WII_GX_STATE_SHADOW) && !defined(VIPER_WII_GX_BATCH)
/* Every renderer call to these fixed-function setters goes through a shadow
 * that drops a call repeating the last value sent. Only menu.c sets them
 * outside this file, so present() resets the shadow after the menu draws.
 * Sentinels never match a real argument, so the first call always emits. */
static struct {
    u16 sx,sy,sw,sh;unsigned proj_w,proj_h;
    u8 dither,color,alpha,zloc,zen,zfn,zup,a0,r0,aop,a1,r1,bm,src,dst,lo,zt_op,zt_fmt;u32 zt_bias;
} gx_shadow;
static void gx_shadow_reset(void){memset(&gx_shadow,0xff,sizeof gx_shadow);}
static void shadow_dither(u8 v){if(gx_shadow.dither!=v){gx_shadow.dither=v;GX_SetDither(v);}}
static void shadow_color(u8 v){if(gx_shadow.color!=v){gx_shadow.color=v;GX_SetColorUpdate(v);}}
static void shadow_alpha(u8 v){if(gx_shadow.alpha!=v){gx_shadow.alpha=v;GX_SetAlphaUpdate(v);}}
static void shadow_zloc(u8 v){if(gx_shadow.zloc!=v){gx_shadow.zloc=v;GX_SetZCompLoc(v);}}
static void shadow_zmode(u8 en,u8 fn,u8 up){
    if(gx_shadow.zen!=en||gx_shadow.zfn!=fn||gx_shadow.zup!=up){
        gx_shadow.zen=en;gx_shadow.zfn=fn;gx_shadow.zup=up;GX_SetZMode(en,fn,up);
    }
}
static void shadow_acmp(u8 c0,u8 r0,u8 op,u8 c1,u8 r1){
    if(gx_shadow.a0!=c0||gx_shadow.r0!=r0||gx_shadow.aop!=op||gx_shadow.a1!=c1||gx_shadow.r1!=r1){
        gx_shadow.a0=c0;gx_shadow.r0=r0;gx_shadow.aop=op;gx_shadow.a1=c1;gx_shadow.r1=r1;
        GX_SetAlphaCompare(c0,r0,op,c1,r1);
    }
}
static void shadow_blend(u8 bm,u8 src,u8 dst,u8 lo){
    if(gx_shadow.bm!=bm||gx_shadow.src!=src||gx_shadow.dst!=dst||gx_shadow.lo!=lo){
        gx_shadow.bm=bm;gx_shadow.src=src;gx_shadow.dst=dst;gx_shadow.lo=lo;GX_SetBlendMode(bm,src,dst,lo);
    }
}
static void shadow_ztex(u8 op,u8 fmt,u32 bias){
    if(gx_shadow.zt_op!=op||gx_shadow.zt_fmt!=fmt||gx_shadow.zt_bias!=bias){
        gx_shadow.zt_op=op;gx_shadow.zt_fmt=fmt;gx_shadow.zt_bias=bias;GX_SetZTexture(op,fmt,bias);
    }
}
#ifdef VIPER_WII_SUPERSAMPLE
static int ss_scissor_segment(u32 x,u32 y,u32 w,u32 h);
#endif
static void shadow_scissor(u32 x,u32 y,u32 w,u32 h){
    if(gx_shadow.sx!=x||gx_shadow.sy!=y||gx_shadow.sw!=w||gx_shadow.sh!=h){
        gx_shadow.sx=x;gx_shadow.sy=y;gx_shadow.sw=w;gx_shadow.sh=h;
#ifdef VIPER_WII_SUPERSAMPLE
        if(ss_scissor_segment(x,y,w,h))return;
#endif
        GX_SetScissor(x,y,w,h);
    }
}
/* Any direct projection load ends reuse of the renderer's w x h ortho. */
static void shadow_projection(Mtx44 m,u8 type){gx_shadow.proj_w=gx_shadow.proj_h=~0u;GX_LoadProjectionMtx(m,type);}
#define GX_SetDither shadow_dither
#define GX_SetColorUpdate shadow_color
#define GX_SetAlphaUpdate shadow_alpha
#define GX_SetZCompLoc shadow_zloc
#define GX_SetZMode shadow_zmode
#define GX_SetAlphaCompare shadow_acmp
#define GX_SetBlendMode shadow_blend
#define GX_SetZTexture shadow_ztex
#define GX_SetScissor shadow_scissor
#define GX_LoadProjectionMtx shadow_projection
#elif !defined(VIPER_WII_GX_BATCH)
static void gx_shadow_reset(void){}
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
#if !defined(VIPER_WII_GX_TMU_PIPELINE) || defined(VIPER_WII_DRAW_PLAN_REUSE)
#error Material plan cache requires full TMU and excludes parent plan reuse
#endif
#include "gx_material_plan.h"
static WiiMaterialPlans material_plans;
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
#if !defined(VIPER_WII_GX_NATIVE_TEXTURE_BIND) || defined(VIPER_WII_GX_CONFIG_TRACE) || defined(VIPER_WII_TEV_FUSION) || defined(VIPER_WII_TMU_PREFIX_CACHE)
#error Combiner program cache requires native texture binding, excludes setter tracing/fusion, and stays separate from the TMU prefix cache
#endif
#include "gx_combiner_program_cache.h"
static WiiCombinerProgramCache combiner_cache;
#ifdef VIPER_WII_MATERIAL_RUN
#if !defined(VIPER_WII_COMBINER_PROGRAM_CACHE) || !defined(VIPER_WII_GX_TMU_PIPELINE) || !defined(VIPER_WII_GX_COLOR_EQUATION) || !defined(VIPER_WII_GX_NATIVE_TEXTURE_BIND)
#error Material-run suffix resume requires native texture binding, the combined program cache and the full TMU pipeline
#endif
#include "gx_material_run.h"
static WiiMaterialRun material_run;
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
/* Same plan pointer: texture scale and the TEX0/texcoord gens stay put.
 * The projective matrix is still solved and loaded for every triangle.
 * run.U39oOB kept the TEV skip only. This skip is a separate speed cut. */
static float material_bind_scale_s[2],material_bind_scale_t[2];
static unsigned material_bind_slot[2],material_bind_valid;
static WiiTextureKey material_bind_key[2];
static uint32_t material_bind_palette[2],material_bind_epoch[2];
#endif
#endif
static void material_plans_reset(void){
    wii_combiner_program_material_invalidate(&material_plans,&combiner_cache);
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_plan_changed(&material_run);
#endif
}
#if defined(VIPER_WII_PLAN_KEEP) && defined(VIPER_WII_MATERIAL_RUN)
/* A texture base change keeps plans and combiner programs (they never read
 * base addresses) but ends the material run, whose bind skip reuses the
 * previously bound texture without re-reading the TMU registers. */
static void material_texture_reset(void){wii_material_run_invalidate(&material_run);}
#endif
#else
static void material_plans_reset(void){wii_material_plans_invalidate(&material_plans);}
#endif
#endif
#if defined(VIPER_WII_COMBINER_PROGRAM_CACHE) && (!defined(VIPER_WII_MATERIAL_PLAN_CACHE) || !defined(VIPER_WII_GX_TMU_PIPELINE))
#error Combiner program cache requires full TMU and the material plan cache
#endif
#if defined(VIPER_WII_NATIVE_TEXTURE_ATTRS)
#if !defined(VIPER_WII_GX_NATIVE_TEXTURE_BIND) || !defined(VIPER_WII_GX_TMU_PIPELINE)
#error Native texture attributes require native texture binding and the full TMU pipeline
#endif
#endif
static WiiGXProfile profile;
static uint64_t plane_ticks[3];
static uint64_t setup_ticks[5];
#ifdef VIPER_WII_GX_BIND_PROFILE
static uint64_t bind_ticks[2];
#endif
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
static void texture_resource_profile(WiiGXProfile *result);
#endif
WiiGXProfile wii_gx_profile(void){
    WiiGXProfile result=profile;
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
    texture_resource_profile(&result);
#endif
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    result.combiner_hits=combiner_cache.hits;result.combiner_misses=combiner_cache.misses;
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    result.material_run_resumes=material_run.resumes;result.material_run_emits=material_run.emits;
#endif
    for(unsigned i=0;i<3;i++)result.plane_us[i]=ticks_to_microsecs(plane_ticks[i]);
    for(unsigned i=0;i<5;i++)result.setup_us[i]=ticks_to_microsecs(setup_ticks[i]);
#ifdef VIPER_WII_GX_BIND_PROFILE
    for(unsigned i=0;i<2;i++)result.bind_us[i]=ticks_to_microsecs(bind_ticks[i]);
#endif
    return result;
}
static inline uint64_t setup_start(void){
#ifdef VIPER_WII_GX_PLANE_PROFILE
    return gettime();
#else
    return 0;
#endif
}
static inline void setup_end(unsigned kind,uint64_t start){
#ifdef VIPER_WII_GX_PLANE_PROFILE
    setup_ticks[kind]+=gettime()-start;profile.setup_calls[kind]++;
#else
    (void)kind;(void)start;
#endif
}
static inline int solve_plane(float matrix[3][4],const WiiProjectiveVertex vertices[3],float s,float t,unsigned kind){
#ifdef VIPER_WII_GX_PLANE_PROFILE
    uint64_t start=gettime();
    int ok=wii_projective_texture_matrix(matrix,vertices,s,t);
    /* Accumulate raw ticks: converting each short solve to whole microseconds
     * first discards most of its duration. */
    profile.plane_calls[kind]++;plane_ticks[kind]+=gettime()-start;
    return ok;
#else
    (void)kind;return wii_projective_texture_matrix(matrix,vertices,s,t);
#endif
}
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
static WiiLookupPlaneCache lookup_plane_cache;
#endif
static inline int solve_lookup_plane(float matrix[3][4],const WiiProjectiveVertex vertices[3],unsigned kind){
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    if(wii_lookup_plane_get(&lookup_plane_cache,vertices,matrix)){
        profile.lookup_plane_reuses++;return 1;
    }
#endif
    int ok=solve_plane(matrix,vertices,1,1,kind);
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    /* Depth-only draws have no duplicate fog solve to eliminate. Avoid
     * copying their matrices; fog results can serve both later depth passes. */
    if(ok&&kind==2)wii_lookup_plane_store(&lookup_plane_cache,vertices,matrix);
#endif
    return ok;
}
#ifndef VIPER_WII_GX_FIFO_KB
#define VIPER_WII_GX_FIFO_KB 256
#endif
/* A larger FIFO lets the CPU run further ahead of a busy GPU before the
 * write-gather pipe stalls (hardware only; Dolphin's GPU never falls behind). */
static uint8_t fifo[VIPER_WII_GX_FIFO_KB*1024] ATTRIBUTE_ALIGN(32);
static GXRModeObj *video;
static void *framebuffer;
static int geometry;
/* Read from a paused RAM dump; no I/O inside a GPU fence. Zero means returned.
 * Sites: 1 depth upload, 2 color upload, 3 scanout, 4 present before copy,
 * 5 present after copy, 6 initialization. */
volatile uint32_t wii_gx_wait_site;
#ifdef VIPER_WII_HANG_TRACE
static void hang_note(char w,int x,int y);
#define HANG_NOTE(w,x,y) hang_note((w),(int)(x),(int)(y))
#else
#define HANG_NOTE(w,x,y) ((void)0)
#endif
static void gx_wait(unsigned site){
#ifdef VIPER_WII_SUPERSAMPLE
#ifdef VIPER_WII_SUPERSAMPLE_TRACE
    {static unsigned n;if(n<3000){n++;rt_log("VIPER WII SS wait site=%u\n",site);}}
#endif
    ss_abort();   /* a wait inside a recorded frame would never finish */
#endif
    uint64_t start=gettime();
    HANG_NOTE('W',site,0);
    wii_gx_wait_site=site;GX_DrawDone();wii_gx_wait_site=0;
    if(site<8){profile.fence_calls[site]++;profile.fence_us[site]+=ticks_to_microsecs(gettime()-start);}
}
static uint64_t dither_approximations;
static int dither_reported;
uint64_t wii_gx_dither_approximations(void){return dither_approximations;}
static const u8 compare[8]={GX_NEVER,GX_LESS,GX_EQUAL,GX_LEQUAL,GX_GREATER,GX_NEQUAL,GX_GEQUAL,GX_ALWAYS};

static void unsupported(const WiiVoodooView *v,uint32_t cmd,const char *why) {
#ifdef VIPER_WII_GX_COLOR_EQUATION
    /* This diagnostic deliberately omits the triangle's positive-alpha proof.
     * Report both outcomes so a predicate guard cannot masquerade as a missing
     * equation, and keep the original fatal reason authoritative. */
    for(unsigned positive=0;positive<2;positive++){
        WiiGXPipelinePlan capability=wii_gx_pipeline_plan(v->regs[0x104/4],
            v->regs[0x110/4],v->regs[0x10c/4],v->regs[0x108/4],
            v->regs[0x134/4],v->regs[0x138/4],v->tmu[0][0],v->tmu[1][0],
            (cmd>>10)&255,positive);
        rt_log("VIPER WII GX CAPABILITY positive_alpha=%u reason=%s stages=%u\n",
            positive,wii_pipeline_reason_name(capability.reason),capability.total_stages);
    }
#endif
    WiiVoodooColorPlan plan=wii_voodoo_color_plan(v->regs[0x104/4]);
    rt_log("VIPER WII GX COLOR PLAN other=%u/%u local=%u/%u override=%u RGB zero/sub/mul/reverse/add/invert=%u/%u/%u/%u/%u/%u A=%u/%u/%u/%u/%u/%u\n",
        plan.other_rgb,plan.other_alpha,plan.local_rgb,plan.local_alpha,plan.local_override,
        plan.rgb_zero,plan.rgb_sub,plan.rgb_mul,plan.rgb_reverse,plan.rgb_add,plan.rgb_invert,
        plan.alpha_zero,plan.alpha_sub,plan.alpha_mul,plan.alpha_reverse,plan.alpha_add,plan.alpha_invert);
    rt_log("VIPER WII GX UNSUPPORTED %s cmd=%08lx cp=%08lx fbz=%08lx alpha=%08lx fog=%08lx tmu=%08lx/%08lx\n",why,
      (unsigned long)cmd,(unsigned long)v->regs[0x104/4],(unsigned long)v->regs[0x110/4],
      (unsigned long)v->regs[0x10c/4],(unsigned long)v->regs[0x108/4],
      (unsigned long)v->tmu[0][0],(unsigned long)v->tmu[1][0]);
    char message[384];
    snprintf(message,sizeof message,"%s cmd=%08lx cp=%08lx fbz=%08lx alpha=%08lx fog=%08lx tmu=%08lx/%08lx key=%08lx range=%08lx",why,
      (unsigned long)cmd,(unsigned long)v->regs[0x104/4],(unsigned long)v->regs[0x110/4],
      (unsigned long)v->regs[0x10c/4],(unsigned long)v->regs[0x108/4],
      (unsigned long)v->tmu[0][0],(unsigned long)v->tmu[1][0],
      (unsigned long)v->regs[0x134/4],(unsigned long)v->regs[0x138/4]);
    rt_fatal(message);
}
static unsigned width(const WiiVoodooView *v){
    unsigned w=v->io[0x98/4]&4095,dx=v->io[0xa4/4];
    return dx?(unsigned)(((uint64_t)w*dx)>>20):w;
}
static unsigned height(const WiiVoodooView *v){
    unsigned h=(v->io[0x98/4]>>12)&4095,dy=v->io[0xac/4];
    return dy?(unsigned)(((uint64_t)h*dy)>>20):h;
}
/* Output area in the 640x480 EFB. Default: the whole EFB (the game's
 * 512x384 scaled 1.25x). VIPER_WII_LETTERBOX (SHARP): 384 lines unscaled, centred, with
 * black borders (half the cabinet's 1024x768; even font/pixel scaling). */
/* Chosen at init: on a Wii set to 16:9 the TV stretches the 640-wide frame
 * to widescreen, so the picture is squeezed to 3/4 width and centred with
 * black side bars (it then shows at its true 4:3 shape; 1:1 keeps square
 * pixels). WII_FORCE_ASPECT 43/169 overrides the console setting. */
static int OUT_X=0,OUT_Y=0,OUT_W=640,OUT_H=480;
/* Render box: where the game's picture is drawn in the EFB. Normally the
 * output box itself. VIPER_WII_NATIVE_RES: the arcade's own 512x384 (WIDE:
 * 640x384, its 682 columns squeezed to the EFB width), so every triangle is
 * rasterised and textured 1:1 as on the cabinet (no seams from bilinear
 * sampling past atlas regions, glyphs on whole pixels); present scales the
 * finished picture to the output box in one filtered pass. */
static int RB_X=0,RB_Y=0,RB_W=640,RB_H=480;
#ifdef VIPER_WII_DISPLAY_MULTI
/* MULTI build: the display mode is chosen at run time (Minus cycles it). */
/* SHARP: 384 lines, exactly half the arcade's 768 (width stretched for the
 * aspect); PIXEL: nearest-neighbour textures; FULL: all 480 lines; WIDE: 16:9,
 * more view at the sides; SUPER: 2x2 supersampled. */
enum { DISPLAY_SHARP, DISPLAY_SHARP_PIXEL, DISPLAY_FULL, DISPLAY_FULL_SUPER, DISPLAY_WIDE_PLAIN, DISPLAY_WIDE_SUPER, DISPLAY_MODES };
static const char *const display_names[DISPLAY_MODES]={"SHARP","SHARP PIXEL","FULL","FULL SUPER",
    "WIDE","WIDE SUPER"};
/* Without a build-time start mode: supersampled, fitted to the TV's aspect.
 * Scripted benchmarks keep FULL unless told otherwise. */
#if !defined(VIPER_WII_DISPLAY_START) && defined(VIPER_WII_SCRIPTED_RACE)
#define VIPER_WII_DISPLAY_START DISPLAY_FULL
#endif
#ifdef VIPER_WII_DISPLAY_START
static volatile int display_mode=VIPER_WII_DISPLAY_START;
#else
static volatile int display_mode=DISPLAY_FULL_SUPER;
#endif
static int display_applied=-1;
void wii_gx_display_cycle(void){display_mode=(display_mode+1)%DISPLAY_MODES;}
/* The chosen mode survives a restart: one number in a text file. */
#define DISPLAY_MODE_FILE "sd:/viper/display_mode.txt"
/* Scripted benchmarks start in their configured mode and leave no file. */
static void display_mode_load(void){
#if !defined(VIPER_WII_DISPLAY_START) && !defined(VIPER_WII_FORCE_ASPECT)
    if(CONF_GetAspectRatio()==CONF_ASPECT_16_9)display_mode=DISPLAY_WIDE_SUPER;
#elif !defined(VIPER_WII_DISPLAY_START) && VIPER_WII_FORCE_ASPECT==169
    display_mode=DISPLAY_WIDE_SUPER;
#endif
#if defined(VIPER_WII_SCRIPTED_RACE) || defined(VIPER_WII_DISPLAY_START)
    return;   /* a build-time start mode wins over the saved one */
#endif
    FILE *f=fopen(DISPLAY_MODE_FILE,"r");int m;
    if(!f)return;
    if(fscanf(f,"%d",&m)==1&&m>=0&&m<DISPLAY_MODES)display_mode=m;
    fclose(f);
}
static void display_mode_save(void){
#ifdef VIPER_WII_SCRIPTED_RACE
    return;
#endif
    FILE *f=fopen(DISPLAY_MODE_FILE,"w");
    if(f){fprintf(f,"%d\n",display_mode);fclose(f);}
}
#define DISPLAY_LETTERBOX (display_mode==DISPLAY_SHARP||display_mode==DISPLAY_SHARP_PIXEL)
#define DISPLAY_NEAREST (display_mode==DISPLAY_SHARP_PIXEL)
#define DISPLAY_SUPERSAMPLE (display_mode==DISPLAY_FULL_SUPER||display_mode==DISPLAY_WIDE_SUPER)
#define DISPLAY_WIDE (display_mode==DISPLAY_WIDE_PLAIN||display_mode==DISPLAY_WIDE_SUPER)
#else
void wii_gx_display_cycle(void){}
#ifdef VIPER_WII_LETTERBOX
#define DISPLAY_LETTERBOX 1
#else
#define DISPLAY_LETTERBOX 0
#endif
#ifdef VIPER_WII_NEAREST_TEXTURES
#define DISPLAY_NEAREST 1
#else
#define DISPLAY_NEAREST 0
#endif
#define DISPLAY_SUPERSAMPLE 1
#define DISPLAY_WIDE 0
#endif
#include "widescreen.h"
/* Widescreen: the game draws x from -M to w + M (wii/widescreen.c). */
#define WIDE_M (wii_wide_margin)
static void output_box_init(void){
    int w=DISPLAY_LETTERBOX?512:640,h=DISPLAY_LETTERBOX?384:480;
#if defined(VIPER_WII_FORCE_ASPECT) && VIPER_WII_FORCE_ASPECT==169
    int wide=1;
#elif defined(VIPER_WII_FORCE_ASPECT) && VIPER_WII_FORCE_ASPECT==43
    int wide=0;
#else
    int wide=CONF_GetAspectRatio()==CONF_ASPECT_16_9;
#endif
    if(wide&&!DISPLAY_WIDE)w=w*3/4;
    OUT_W=w;OUT_H=h;OUT_X=(640-w)/2;OUT_Y=(480-h)/2;
#ifdef VIPER_WII_NATIVE_RES
    {int rw=512+2*WIDE_M;if(rw>640)rw=640;   /* the game's 512x384 (logged as its only display size) */
     if(rw==OUT_W&&OUT_H==384){RB_X=OUT_X;RB_Y=OUT_Y;}   /* SHARP on 4:3: already 1:1, no scaling pass */
     else{RB_X=(640-rw)/2;RB_Y=(480-384)/2;}
     RB_W=rw;RB_H=384;}
#else
    RB_X=OUT_X;RB_Y=OUT_Y;RB_W=OUT_W;RB_H=OUT_H;
#endif
}
void wii_gx_output_box(int *x,int *y,int *w,int *h){*x=OUT_X;*y=OUT_Y;*w=OUT_W;*h=OUT_H;}
#ifdef VIPER_WII_SUPERSAMPLE
/* While a frame is recorded the game's projection is not put in the list:
 * each replay loads its own (the tile's 2x scale and offset in clip space,
 * so the viewport stays the normal one). One projection per frame. */
static int ss_recording,ss_proj_set;
/* Per TMU: this triangle maps texels to pixels 1:1, axis-aligned (a 2D
 * sprite: logo, HUD, hi-score car). The arcade samples those at texel
 * centres; a 2x tile sampled bilinearly bleeds the neighbouring atlas texel
 * into the edge pixels (hair-lines above the title-screen Konami logo).
 * Point-sampled, each 2x2 group lands in the right texel and the box filter
 * returns the arcade's pixel exactly. */
static unsigned sprite_near;
static unsigned long long sprite_near_tris;
/* The flagged sprite's texel range per TMU (level-0 texels). Inside its
 * texture it needs no repeat: it is clamped, so an edge pixel cannot wrap to
 * the opposite edge. Hardware snaps vertices to 1/12 pixel; in WIDE the
 * arcade's x=0 is fractional, and its first pixel sampled s just below 0,
 * drawing the sprite's last column down the left edge (title logo,
 * copyright line). Dolphin does not snap, so only the Wii showed it. */
static float sprite_s_min[2],sprite_s_max[2],sprite_t_min[2],sprite_t_max[2];
#ifdef VIPER_WII_FRAME_CAPTURE
static char edge_sprite_text[2048];static unsigned edge_sprite_len;   /* this frame's flagged sprites at the left edge */
#endif
#ifdef VIPER_WII_FRAME_CAPTURE
/* This frame's triangles per (fbzMode, alphaMode, fbzColorPath) state. */
static struct {uint32_t fbz,alpha,cp;unsigned n;float wmin,wmax;} state_tally[48];static unsigned state_tally_n;
static void tally_state(const WiiVoodooView *v,const WiiVoodooVertex p[3]){
    uint32_t fbz=v->regs[0x110/4],alpha=v->regs[0x10c/4],cp=v->regs[0x104/4];
    unsigned i=0;for(;i<state_tally_n;i++)if(state_tally[i].fbz==fbz&&state_tally[i].alpha==alpha&&state_tally[i].cp==cp)break;
    if(i==state_tally_n){if(i==48)return;state_tally_n++;state_tally[i].fbz=fbz;state_tally[i].alpha=alpha;state_tally[i].cp=cp;state_tally[i].n=0;state_tally[i].wmin=1e30f;state_tally[i].wmax=0;}
    state_tally[i].n++;for(unsigned k=0;k<3;k++){if(p[k].wb<state_tally[i].wmin)state_tally[i].wmin=p[k].wb;if(p[k].wb>state_tally[i].wmax)state_tally[i].wmax=p[k].wb;}
}
#endif
/* A clip rectangle smaller than the box was set this frame: the next frame
 * is drawn plain from its start, until one goes by without (letterboxed
 * scenes like car select and loading: an abort part-way through a recorded
 * frame left the loading screen grey). */
static int ss_partial_clip;
static unsigned ss_proj_w,ss_proj_h;
static int ss_capture_projection(unsigned w,unsigned h){
    if(!ss_recording)return 0;
    if(ss_proj_set&&(ss_proj_w!=w||ss_proj_h!=h)){ss_abort();return 0;}   /* a second one: this frame at 1x */
    ss_proj_w=w;ss_proj_h=h;ss_proj_set=1;
    return 1;
}
#endif
static void projection(const WiiVoodooView *v) {
    unsigned w=width(v),h=height(v);
    if(!w||!h||w>1024||h>1024)unsupported(v,0,"GX display dimensions");
    static unsigned last_w,last_h;
    if(w!=last_w||h!=last_h){
        rt_log("VIPER WII GX display=%ux%u raw=%08lx scale=%08lx/%08lx\n",w,h,
          (unsigned long)v->io[0x98/4],(unsigned long)v->io[0xa4/4],(unsigned long)v->io[0xac/4]);
        last_w=w;last_h=h;
    }
    if((v->regs[0x110/4]&(1u<<17))&&(((v->io[0x10/4]>>18)&4095)+1!=h))unsupported(v,0,"GX nonstandard Y origin");
#ifdef VIPER_WII_GX_STATE_SHADOW
    if(gx_shadow.proj_w==w&&gx_shadow.proj_h==h)return;
#ifdef VIPER_WII_SUPERSAMPLE
    if(ss_capture_projection(w,h)){gx_shadow.proj_w=w;gx_shadow.proj_h=h;return;}
#endif
    Mtx44 p;guOrtho(p,0,h,-(f32)WIDE_M,w+WIDE_M,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
    gx_shadow.proj_w=w;gx_shadow.proj_h=h;
#else
    Mtx44 p;guOrtho(p,0,h,0,w,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
#endif
}
static float screen_y(const WiiVoodooView *v,float y) {
    return (v->regs[0x110/4]&(1u<<17))?((v->io[0x10/4]>>18)&4095)+1.0f-y:y;
}
static int clip_key_stale;   /* the output box changed: recompute */
static void clip(const WiiVoodooView *v,int enabled) {
    /* The rectangle depends only on these inputs; GX_SetScissor below is
     * still called so a scissor set elsewhere is always restored. */
    static uint32_t key[6]={~0u,~0u,~0u,~0u,~0u,~0u};static u32 cx0,cy0,cw,ch;
    if(clip_key_stale){memset(key,0xff,sizeof key);clip_key_stale=0;}
    uint32_t now[6]={v->io[0x98/4],v->io[0xa4/4],v->io[0xac/4],enabled?v->regs[0x118/4]:~0u,
        enabled?v->regs[0x11c/4]:~0u,v->regs[0x110/4]&(1u<<17)};
    if(!memcmp(key,now,sizeof key)){
        GX_SetScissor(cx0,cy0,cw,ch);return;}
    memcpy(key,now,sizeof key);
    unsigned w=width(v),h=height(v),l=0,r=w,t=0,b=h;
    if(enabled){l=(v->regs[0x118/4]>>16)&1023;r=v->regs[0x118/4]&1023;
        t=(v->regs[0x11c/4]>>16)&1023;b=v->regs[0x11c/4]&1023;}
    if(l>w)l=w;
    if(r>w)r=w;
    if(t>h)t=h;
    if(b>h)b=h;
    if(r<l)r=l;
    if(b<t)b=t;
    if(v->regs[0x110/4]&(1u<<17)){unsigned old=t;t=h-b;b=h-old;}
    unsigned x0,x1,y0=RB_Y+t*RB_H/h,y1=RB_Y+b*RB_H/h;
    if(WIDE_M){   /* a rectangle reaching an edge reaches the margin's edge */
        unsigned ww=w+2*WIDE_M,lw=l?l+WIDE_M:0,rw=r>=w?ww:r+WIDE_M;
        x0=RB_X+lw*RB_W/ww;x1=RB_X+rw*RB_W/ww;
    }else{x0=RB_X+l*RB_W/w;x1=RB_X+r*RB_W/w;}
    cx0=x0;cy0=y0;cw=x1-x0;ch=y1-y0;
    /* A recorded supersample frame keeps each clip rectangle with its list
     * segment and scales it per tile (ss_scissor_segment). */
    GX_SetScissor(cx0,cy0,cw,ch);
}
static void vertex(float x,float y,float depth,GXColor c) {
    GX_Position3f32(x,y,-depth);GX_Color4u8(c.r,c.g,c.b,c.a);
}
#ifdef VIPER_WII_VERTEX_STQ
/* Full-pipeline texture coordinates travel per vertex as the normal (one
 * unit) or normal plus binormal (two units). Texgen MTX3x4 through an
 * identity reads them unchanged; GX interpolates S, T, Q in screen space and
 * divides per pixel, as the old per-triangle plane matrix did. */
enum { STQ_IDENTITY=GX_TEXMTX9 };
static int stq_desc=-1;
/* libogc derives the XF normal count from the last NRM/NBT descriptor call
 * even when its type is GX_NONE, and only GX_ClearVtxDesc resets it. So
 * normals are removed by rebuilding the descriptor; TEX0 is tracked. */
#ifdef VIPER_WII_GX_BATCH
#define stq_tex0_desc gx_desc_of(GX_VA_TEX0)
#else
static u8 stq_tex0_desc=GX_NONE;
static void stq_vtx_desc(u8 attr,u8 type){if(attr==GX_VA_TEX0)stq_tex0_desc=type;GX_SetVtxDesc(attr,type);}
#endif
static void stq_set_mode(int mode){
    if(mode==stq_desc)return;
    if(mode==0){
        u8 tex0=stq_tex0_desc;
        GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
        GX_SetVtxDesc(GX_VA_TEX0,tex0);
    }else if(mode==1)GX_SetVtxDesc(GX_VA_NRM,GX_DIRECT);
    else GX_SetVtxDesc(GX_VA_NBT,GX_DIRECT);
    stq_desc=mode;
}
#ifndef VIPER_WII_GX_BATCH
#define GX_SetVtxDesc(attr,type) stq_vtx_desc(attr,type)
#endif
static void stq_vertex(float x,float y,float depth,GXColor c,int mode,const float *n,const float *b){
    GX_Position3f32(x,y,-depth);
    if(mode>=1)GX_Normal3f32(n[0],n[1],n[2]);
    if(mode==2){GX_Normal3f32(b[0],b[1],b[2]);GX_Normal3f32(0,0,0);}
    GX_Color4u8(c.r,c.g,c.b,c.a);
}
#else
static void stq_set_mode(int mode){(void)mode;}
#endif
static unsigned wdepth(float w) {
    uint64_t value=(uint64_t)((double)w*4294967296.0)<<16;
    if(!value)return 65535;
    int e=__builtin_clzll(value)-16;
    if(e<0)return 0;
    if(e>=16)return 65535;
    return ((e<<12)|((value>>(35-e))^0x1fff))+1;
}
#ifdef VIPER_WII_WDEPTH_LINEAR
#if defined(VIPER_WII_WDEPTH_CONSTANT) || !defined(VIPER_WII_GX_DISABLE_FOG)
#error Linear W depth replaces constant W depth and needs fog off (fog reads per-pixel W bands)
#endif
/* Inverse of wdepth(): the smallest wb that encodes to d, as linear Z 1-wb.
 * 0 is wb>=1 (Z 0); 65535 also covers every wb below 2^-16 (Z 1). */
static float linear_depth_from_wdepth(unsigned d){
    if(!d)return 0;
    if(d>=65535)return 1;
    unsigned m=d-1,e=m>>12,frac=~m&0xfff;
    return 1.f-ldexpf((float)(4096+frac),-(int)(13+e));
}
#endif
#if defined(VIPER_WII_DEPTH_TRACE) || defined(VIPER_WII_FRAME_CAPTURE)
/* Per-frame range of linear-depth W (wb=1/W) and a log2 histogram. */
static float depth_trace_min=1e30f,depth_trace_max;static unsigned depth_trace_hist[32],depth_trace_flat;
#endif
static uint64_t wdepth_approximations;
uint64_t wii_gx_wdepth_approximations(void){return wdepth_approximations;}
static void depth_texture_off(void){GX_SetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);GX_SetNumTevStages(1);}
#ifdef VIPER_WII_GX_WDEPTH_APPROX
static WiiWDepthTriangle depth_pieces[WII_WDEPTH_SPLIT_MAX];
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
enum { DEPTH_TABLE_HEIGHT=8, DEPTH_TABLE_COUNT=16 };
#define DEPTH_WRAP_S GX_REPEAT
#else
enum { DEPTH_TABLE_HEIGHT=4, DEPTH_TABLE_COUNT=64 };
#define DEPTH_WRAP_S GX_CLAMP
#endif
enum { DEPTH_TABLE_BYTES=1024*DEPTH_TABLE_HEIGHT*4 };
static unsigned lookup_index(unsigned band){
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 return band/4;
#else
 return band;
#endif
}
static unsigned lookup_band(unsigned index){
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 return index*4;
#else
 return index;
#endif
}
static unsigned lookup_offset(unsigned x,unsigned y){
 return (y/4)*256*64+(x/4)*64+((y&3)*4+(x&3))*2;
}
static float lookup_sample(unsigned index,unsigned x,unsigned y){
 unsigned band=lookup_band(index);
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 /* Keep original quarter sample arithmetic. Extra rows hold the clamped
  * high endpoint so S wrapping at norm=1 does not select quarter-three start. */
 band+=y<4?y:3;
 if(y>=4)x=1023;
#else
 (void)y;
#endif
 float lo,hi;wii_wdepth_band(band,&lo,&hi);
 return lo+(hi-lo)*((x+.5f)/1024);
}
static WiiProjectiveVertex lookup_vertex(const WiiVoodooView *v,const WiiVoodooVertex *p,float lo,float hi){
 float norm=(p->wb-lo)/(hi-lo);
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 return (WiiProjectiveVertex){p->x,screen_y(v,p->y),4*norm,.5f*norm,1};
#else
 return (WiiProjectiveVertex){p->x,screen_y(v,p->y),norm,.5f,1};
#endif
}
static uint8_t *depth_tables;
#ifdef VIPER_WII_GX_LOOKUP_OBJECTS
static GXTexObj depth_objects[DEPTH_TABLE_COUNT];
static void init_lookup_objects(GXTexObj objects[DEPTH_TABLE_COUNT],uint8_t *images){
    for(unsigned band=0;band<DEPTH_TABLE_COUNT;band++){
        GX_InitTexObj(&objects[band],images+band*DEPTH_TABLE_BYTES,1024,DEPTH_TABLE_HEIGHT,GX_TF_RGBA8,DEPTH_WRAP_S,GX_CLAMP,GX_FALSE);
        GX_InitTexObjLOD(&objects[band],GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    }
}
#endif
static int active_depth_band=-1;
#ifdef VIPER_WII_PARENT_PROJECTION
static int parent_projection_ready;
#endif
#ifdef VIPER_WII_GX_FOG_APPROX
static uint8_t *fog_tables;
#ifdef VIPER_WII_GX_LOOKUP_OBJECTS
static GXTexObj fog_objects[DEPTH_TABLE_COUNT];
#endif
static uint32_t fog_key[32];
static int fog_valid;
static void bind_fog_lookup(const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,int textured,unsigned stage){
#ifdef VIPER_WII_GX_BIND_PROFILE
    uint64_t bind_start=gettime();
#endif
    if(!fog_tables)unsupported(v,cmd,"GX fog lookup tables not allocated");
    const uint32_t *table=&v->regs[0x160/4];
    int variable=active_depth_band>=0&&active_depth_band<64;
    int check_table=variable;
    if(check_table&&(!fog_valid||memcmp(table,fog_key,sizeof fog_key))){
        /* A queued primitive may still reference the previous table version. */
        gx_wait(7);
        for(unsigned band=0;band<DEPTH_TABLE_COUNT;band++){
            uint8_t *image=fog_tables+band*DEPTH_TABLE_BYTES;
            for(unsigned y=0;y<DEPTH_TABLE_HEIGHT;y++)for(unsigned x=0;x<1024;x++){
                unsigned factor=wii_fog_factor(table,wdepth(lookup_sample(band,x,y)),8);
                unsigned o=lookup_offset(x,y);
                image[o]=wii_fog_tev_factor(factor);image[o+1]=image[o+32]=image[o+33]=0;
            }
        }
        DCFlushRange(fog_tables,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);GX_InvalidateTexAll();
        memcpy(fog_key,table,sizeof fog_key);fog_valid=1;
    }
    unsigned factor=0;
    if(variable){
        unsigned band=(unsigned)active_depth_band;float lo,hi;wii_wdepth_render_band(band,&lo,&hi);
        #ifdef VIPER_WII_GX_LOOKUP_OBJECTS
        /* Copy keeps any load-time descriptor mutations local to this draw. */
        GXTexObj tex=fog_objects[lookup_index(band)];
#else
        GXTexObj tex;GX_InitTexObj(&tex,fog_tables+lookup_index(band)*DEPTH_TABLE_BYTES,1024,DEPTH_TABLE_HEIGHT,GX_TF_RGBA8,DEPTH_WRAP_S,GX_CLAMP,GX_FALSE);
        GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
#endif
        GX_LoadTexObj(&tex,GX_TEXMAP2);
        WiiProjectiveVertex pv[3];Mtx matrix;
        for(unsigned i=0;i<3;i++)pv[i]=lookup_vertex(v,&p[i],lo,hi);
        if(!solve_lookup_plane(matrix,pv,2))unsupported(v,cmd,"GX fog W plane failed");
        GX_LoadTexMtxImm(matrix,GX_TEXMTX1,GX_MTX3x4);
        if(!textured)GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
        GX_SetNumTexGens(textured>2?textured:2);GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX1);
    }else{
        unsigned d=p[0].wb>=1?0:wdepth(p[0].wb);
        factor=wii_fog_tev_factor(wii_fog_factor(table,d,8));
    }
    uint32_t rgb=v->regs[0x12c/4];
#ifdef VIPER_WII_GX_TMU_PIPELINE
    if(!variable&&textured>2){
        if(!wii_gx_constant_fog_bind(stage,(GXColor){rgb>>16,rgb>>8,rgb,(u8)factor},textured))
            unsupported(v,cmd,"GX constant fog binding failed");
    }else
#endif
    wii_gx_fog_stage(stage,(GXColor){rgb>>16,rgb>>8,rgb,(u8)factor},variable);
#ifdef VIPER_WII_GX_BIND_PROFILE
    bind_ticks[0]+=gettime()-bind_start;profile.bind_calls[0]++;
#endif
}
#endif
static void bind_depth_lookup(const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,int textured,unsigned stage){
    if(!depth_tables)unsupported(v,cmd,"GX depth lookup tables not allocated");
#ifdef VIPER_WII_GX_BIND_PROFILE
    uint64_t bind_start=gettime();
#endif
    unsigned band=(unsigned)active_depth_band;float lo,hi;wii_wdepth_render_band(band,&lo,&hi);
    /* Immutable band tables remain valid for every queued draw. */
    #ifdef VIPER_WII_GX_LOOKUP_OBJECTS
    /* Copy keeps any load-time descriptor mutations local to this draw. */
    GXTexObj tex=depth_objects[lookup_index(band)];
#else
    GXTexObj tex;GX_InitTexObj(&tex,depth_tables+lookup_index(band)*DEPTH_TABLE_BYTES,1024,DEPTH_TABLE_HEIGHT,GX_TF_RGBA8,DEPTH_WRAP_S,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
#endif
    GX_LoadTexObj(&tex,GX_TEXMAP1);
    WiiProjectiveVertex pv[3];Mtx matrix;
    for(unsigned i=0;i<3;i++)pv[i]=lookup_vertex(v,&p[i],lo,hi);
    if(!solve_lookup_plane(matrix,pv,1))unsupported(v,cmd,"GX W-depth plane failed");
    GX_LoadTexMtxImm(matrix,GX_TEXMTX1,GX_MTX3x4);
    if(!textured)GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
    GX_SetNumTexGens(textured>2?textured:2);GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX1);
    GX_SetNumTevStages(stage+1);GX_SetTevOrder(stage,GX_TEXCOORD1,GX_TEXMAP1,GX_COLORNULL);
    GX_SetTevColorIn(stage,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
    GX_SetTevColorOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GX_SetTevAlphaIn(stage,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
    GX_SetTevAlphaOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    GX_SetZTexture(GX_ZT_REPLACE,GX_TF_Z24X8,0);
#ifdef VIPER_WII_GX_BIND_PROFILE
    bind_ticks[1]+=gettime()-bind_start;profile.bind_calls[1]++;
#endif
}
#endif
static u8 blend_factor(const WiiVoodooView *v,unsigned f,int source) {
    switch(f){case 0:return GX_BL_ZERO;case 1:return GX_BL_SRCALPHA;
    case 2:return source?GX_BL_DSTCLR:GX_BL_SRCCLR;case 4:return GX_BL_ONE;
    case 5:return GX_BL_INVSRCALPHA;case 6:return source?GX_BL_INVDSTCLR:GX_BL_INVSRCCLR;
    default:unsupported(v,0,"GX destination alpha/reserved blend factor");return GX_BL_ZERO;}
}
/* Fixed owned slots: hits retain their bytes; fence every valid victim before
 * overwrite. Round-robin replacement has no age counter to overflow. */
#ifndef VIPER_WII_GX_COLOR_CACHE_SLOTS
#ifdef VIPER_WII_GX_RESIDENT_CACHE
#define VIPER_WII_GX_COLOR_CACHE_SLOTS 256
#else
#define VIPER_WII_GX_COLOR_CACHE_SLOTS 4
#endif
#endif
#if VIPER_WII_GX_COLOR_CACHE_SLOTS < 1
#error At least one colour texture cache slot is required
#endif
enum { COLOR_IMAGE_BYTES=262144, COLOR_CACHE_SLOTS=VIPER_WII_GX_COLOR_CACHE_SLOTS };
static WiiTextureCacheEntry texture_cache[COLOR_CACHE_SLOTS];
static uint8_t texture_pinned[COLOR_CACHE_SLOTS];
/* Slots pinned by the current triangle; unpinning clears only these. */
static uint16_t texture_pin_list[8];static unsigned texture_pin_count;
static inline void texture_pin(unsigned slot){
    if(texture_pinned[slot])return;
    texture_pinned[slot]=1;
    if(texture_pin_count<8)texture_pin_list[texture_pin_count++]=(uint16_t)slot;
    else rt_fatal("GX texture pin list overflow");
}
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
#include "gx_texture_resource.h"
/* Image residency is validated before these native handles are acquired. */
static WiiGXTextureResource texture_resources[COLOR_CACHE_SLOTS];
static void texture_resource_profile(WiiGXProfile *result){
    for(unsigned i=0;i<COLOR_CACHE_SLOTS;i++){
        result->texture_resource_hits+=texture_resources[i].hits;
        result->texture_resource_misses+=texture_resources[i].misses;
    }
}
#endif
#ifdef VIPER_WII_GX_RESIDENT_CACHE
#ifndef VIPER_WII_GX_RESIDENT_BUDGET
#define VIPER_WII_GX_RESIDENT_BUDGET (12*1024*1024)
#endif
#if VIPER_WII_GX_RESIDENT_BUDGET < 262144
#error Resident budget must hold the largest accepted texture
#endif
enum { COLOR_RESIDENT_BUDGET=VIPER_WII_GX_RESIDENT_BUDGET };
static uint8_t *texture_slot_images[COLOR_CACHE_SLOTS];
static unsigned texture_slot_bytes[COLOR_CACHE_SLOTS],texture_resident_bytes;
static void discard_texture_slot(unsigned slot){
#ifdef VIPER_WII_MEMO_MULTI
    memo_tex_gen++;
#endif
    /* Caller has drained all queued GX users before freeing any valid image. */
    texture_resident_bytes-=texture_slot_bytes[slot];
    free(texture_slot_images[slot]);texture_slot_images[slot]=NULL;
    texture_slot_bytes[slot]=0;texture_cache[slot].valid=0;
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
    wii_gx_texture_resource_invalidate(&texture_resources[slot]);
#endif
}
#else
static uint8_t *texture_images;
#endif
static unsigned texture_victim,texture_last_slot=COLOR_CACHE_SLOTS;
#ifdef VIPER_WII_TEXTURE_HINT
/* Slot last bound per request-key hash (a hint only: every use is re-matched). */
static uint16_t texture_hint[256];
#ifdef VIPER_WII_TEXTURE_SLOT_HINT
/* Key hash of each slot's committed key: equal keys hash equally, so a scan
 * needs the full match only where the hash agrees (same slot found). */
static uint8_t texture_slot_hint[COLOR_CACHE_SLOTS];
#define TEXTURE_SCAN_MATCH(slot) (texture_slot_hint[slot]==hint&&wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions))
#else
#define TEXTURE_SCAN_MATCH(slot) wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions)
#endif
static inline unsigned texture_key_hint(const WiiTextureKey *k){
    uint32_t h=k->base*0x9e3779b1u^k->lod*0x85ebca6bu^k->mode*0xc2b2ae35u^(k->unit<<8)^k->format^k->epoch*0x27d4eb2fu;
    return (h^(h>>16)^(h>>24))&255u;
}
#endif
#ifdef VIPER_WII_GX_TEXTURE_TRACE
/* Diagnostic-only catalogue of source layouts seen on cache misses. It is not
 * a benchmark: linear searches add overhead. Reused VRAM addresses are counted
 * as source updates rather than pretending all assets are immutable. */
enum { TEXTURE_TRACE_SOURCES=1024 };
static WiiTextureCacheEntry texture_sources[TEXTURE_TRACE_SOURCES];
static void trace_texture_source(const WiiTextureKey *request,const uint32_t versions[2048]){
    WiiTextureKey source=*request;
    source.mode=source.key=source.range=source.lod=source.epoch=0;
    unsigned i=0;
    for(;i<profile.texture_sources;i++){
        const WiiTextureKey *k=&texture_sources[i].key;
        if(k->base==source.base&&k->width==source.width&&k->height==source.height&&
           k->unit==source.unit&&k->format==source.format)break;
    }
    if(i==profile.texture_sources){
        if(i==TEXTURE_TRACE_SOURCES){profile.texture_trace_overflow++;return;}
        profile.texture_sources++;
        profile.texture_source_bytes+=wii_texture_rgba8_size(source.width,source.height);
    }else{
        WiiTextureKey previous=source;
        previous.epoch=texture_sources[i].key.epoch;
        if(!wii_texture_cache_matches(&texture_sources[i],&previous,versions))profile.texture_source_updates++;
        if(source.format==5&&texture_sources[i].key.epoch!=request->epoch)profile.texture_palette_updates++;
    }
    source.epoch=request->epoch;
    wii_texture_cache_commit(&texture_sources[i],&source,versions);
}
#endif
static void texture_off(void) {
    TEXLOAD_FORGET();
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_invalidate(&material_run);
#endif
    GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
}
#ifdef VIPER_WII_SUPERSAMPLE
static inline int sprite_clamp_s(unsigned unit,unsigned w,unsigned level){
    float n=(float)(w<<level);return (sprite_near>>unit&4)&&sprite_s_min[unit]>=0&&sprite_s_max[unit]<=n;
}
static inline int sprite_clamp_t(unsigned unit,unsigned h,unsigned level){
    float n=(float)(h<<level);return (sprite_near>>unit&4)&&sprite_t_min[unit]>=0&&sprite_t_max[unit]<=n;
}
#else
#define sprite_clamp_s(unit,w,level) 0
#define sprite_clamp_t(unit,h,level) 0
#endif
WII_HOT_bind_flat_texture static void bind_flat_texture(const WiiVoodooView *v,uint32_t cmd,unsigned unit,unsigned format,u8 coord,u8 map,float *us,float *vs,int native_program) {
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
    const uint32_t *r=v->tmu[unit];
    const WiiNativeTextureSource *source=wii_native_texture_source_acquire(&native_texture_sources,unit,r,format);
    if(source->error==WII_TEXTURE_SOURCE_VARIABLE_MIP)unsupported(v,cmd,"GX variable mip selection pending");
    if(source->error==WII_TEXTURE_SOURCE_LAYOUT)unsupported(v,cmd,"GX texture layout pending");
    unsigned lod=source->lod,level=source->level,base=source->base,w=source->width,h=source->height;
    unsigned first=source->first_page,pages=source->pages,mode=source->mode;
#else
    const uint32_t *r=v->tmu[unit];unsigned lod=r[1],level=(lod&63)/4;
    if(((lod>>6)&63)/4!=level)unsupported(v,cmd,"GX variable mip selection pending");
    if((lod&(1u<<19))&&((level&1)!=!!(lod&(1u<<18))))level++;
    if(level>8||(r[3]&1))unsupported(v,cmd,"GX texture layout pending");
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    const NativeMip *mip=wii_texture_layout_get(&texture_layouts,unit,r);
#else
    NativeMip mip[9];native_texture_layout(r,mip);
#endif
    unsigned base=mip[level].address,w=mip[level].width,h=mip[level].height;
    unsigned first=base/4096,pages=((base&4095)+w*h*(format>=8?2:1)+4095)/4096;
#endif
    uint32_t key=v->regs[0x134/4],range=v->regs[0x138/4];
    if((v->regs[0x110/4]&2)&&((key&0xffffffu)||
       ((range&(1u<<28))&&(range&0x0fffffffu))))unsupported(v,cmd,"GX AI44 chroma range pending");
    if(pages>33||wii_texture_rgba8_size(w,h)>COLOR_IMAGE_BYTES)unsupported(v,cmd,"GX AI44 upload capacity");
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
    WiiTextureKey request={mode,lod,base,w,h,v->palette_epoch[unit],key,range,unit,format,first,pages};
#else
    WiiTextureKey request={r[0],lod,base,w,h,v->palette_epoch[unit],key,range,unit,format,first,pages};
#endif
#ifdef VIPER_WII_GX_RESIDENT_CACHE
    /* Conversion depends on source pixels and palette, not sampler state or
     * post-filter chroma. GX sampler/TEV state is still set on every bind. */
    request=wii_texture_conversion_key(request);
#endif
    unsigned slot=texture_last_slot;
#ifdef VIPER_WII_TEXTURE_HINT
    /* At most one valid entry matches a request (entries are committed only
     * on a miss and page versions never repeat), so any lookup order finds
     * the same slot: try the slot last bound for this key hash before the scan. */
    unsigned hint=texture_key_hint(&request);
    if(slot>=COLOR_CACHE_SLOTS||!wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions)){
        slot=texture_hint[hint];
        if(slot>=COLOR_CACHE_SLOTS||!wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions))
            for(slot=0;slot<COLOR_CACHE_SLOTS;slot++)if(TEXTURE_SCAN_MATCH(slot))break;
    }
#else
    if(slot>=COLOR_CACHE_SLOTS||!wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions)){
        for(slot=0;slot<COLOR_CACHE_SLOTS;slot++)if(wii_texture_cache_matches(&texture_cache[slot],&request,v->vram_versions))break;
    }
#endif
    int hit=slot<COLOR_CACHE_SLOTS;
    if(!hit){
#ifdef VIPER_WII_GX_BATCH
        /* Batched triangles are not pinned and not yet in the FIFO: draw them
         * before a victim is chosen and its image rewritten by the CPU. */
        gx_batch_flush();
#endif
        for(slot=0;slot<COLOR_CACHE_SLOTS&&texture_cache[slot].valid;slot++);
        if(slot==COLOR_CACHE_SLOTS){
            slot=wii_texture_cache_victim(texture_cache,COLOR_CACHE_SLOTS,texture_victim,texture_pinned,0);
            if(slot==COLOR_CACHE_SLOTS)unsupported(v,cmd,"GX bound texture pair exceeds cache slots");
            texture_victim=(slot+1)%COLOR_CACHE_SLOTS;
        }
    }
    WiiTextureCacheEntry *entry=&texture_cache[slot];
#ifdef VIPER_WII_GX_RESIDENT_CACHE
    unsigned image_bytes=wii_texture_rgba8_size(w,h);
    if(!hit){
        /* A miss can require several evictions to satisfy the byte budget.
         * One global fence protects every image freed during this operation. */
        int fenced=0;
        if(entry->valid){gx_wait(2);fenced=1;discard_texture_slot(slot);profile.color_cache_evictions++;}
        while(image_bytes>COLOR_RESIDENT_BUDGET-texture_resident_bytes){
            unsigned victim=wii_texture_cache_victim(texture_cache,COLOR_CACHE_SLOTS,texture_victim,texture_pinned,1);
            if(victim==COLOR_CACHE_SLOTS)unsupported(v,cmd,"GX bound texture pair exceeds resident budget");
            texture_victim=(victim+1)%COLOR_CACHE_SLOTS;
            if(!fenced){gx_wait(2);fenced=1;}
            discard_texture_slot(victim);profile.color_cache_evictions++;
        }
        texture_slot_images[slot]=memalign(32,image_bytes);
#ifdef VIPER_WII_TEST_TEXTURE_OOM
        {static unsigned n;if(!(++n%20)){free(texture_slot_images[slot]);texture_slot_images[slot]=NULL;}}   /* exercise the retry */
#endif
        /* The heap can run short (or too fragmented for this size) before the
         * byte budget does, after a long session of varied textures: free
         * more cached images and retry before giving up. */
        while(!texture_slot_images[slot]){
            unsigned victim=wii_texture_cache_victim(texture_cache,COLOR_CACHE_SLOTS,texture_victim,texture_pinned,1);
            if(victim==COLOR_CACHE_SLOTS)rt_fatal("GX resident texture allocation");
            texture_victim=(victim+1)%COLOR_CACHE_SLOTS;
            if(!fenced){gx_wait(2);fenced=1;}
            discard_texture_slot(victim);profile.color_cache_evictions++;profile.color_cache_heap_evictions++;
            texture_slot_images[slot]=memalign(32,image_bytes);
        }
        texture_slot_bytes[slot]=image_bytes;texture_resident_bytes+=image_bytes;
        profile.color_cache_bytes=texture_resident_bytes;
        if(profile.color_cache_bytes>profile.color_cache_peak_bytes)profile.color_cache_peak_bytes=profile.color_cache_bytes;
    }
    uint8_t *texture_image=texture_slot_images[slot];
#else
    uint8_t *texture_image=texture_images+slot*COLOR_IMAGE_BYTES;
#endif
    if(!hit){
        profile.color_cache_misses++;
#ifdef VIPER_WII_GX_TEXTURE_TRACE
        trace_texture_source(&request,v->vram_versions);
#endif
#ifndef VIPER_WII_GX_RESIDENT_CACHE
        if(entry->valid)gx_wait(2);
#endif
        uint64_t start=gettime();
        if(!wii_texture_rgba8(texture_image,wii_texture_rgba8_size(w,h),v->vram,0x800000,base,w,h,format,v->palette[unit]))unsupported(v,cmd,"GX AI44 conversion failed");
        DCFlushRange(texture_image,wii_texture_rgba8_size(w,h));GX_InvalidateTexAll();
        wii_texture_cache_commit(entry,&request,v->vram_versions);
#ifdef VIPER_WII_MEMO_MULTI
        memo_tex_gen++;
#endif
#ifdef VIPER_WII_TEXTURE_SLOT_HINT
        texture_slot_hint[slot]=(uint8_t)hint;
#endif
        profile.color_upload_us+=ticks_to_microsecs(gettime()-start);
    }else profile.color_cache_hits++;
    texture_last_slot=slot;
#ifdef VIPER_WII_TEXTURE_HINT
    texture_hint[hint]=(uint16_t)slot;
#endif
    texture_pin(slot);
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
    unsigned filter=source->linear?GX_LINEAR:GX_NEAR;
    if(DISPLAY_NEAREST)filter=GX_NEAR;   /* Option: every game texture point-sampled. */
#ifdef VIPER_WII_SUPERSAMPLE
    if(sprite_near&(1u<<unit))filter=GX_NEAR;
#endif
    GXTexObj tex=wii_gx_texture_resource_acquire(&texture_resources[slot],texture_image,w,h,
        (source->clamp_s||sprite_clamp_s(unit,w,level))?GX_CLAMP:GX_REPEAT,(source->clamp_t||sprite_clamp_t(unit,h,level))?GX_CLAMP:GX_REPEAT,filter);
#elif defined(VIPER_WII_NATIVE_TEXTURE_RESOURCE)
    unsigned filter=(r[0]&6)?GX_LINEAR:GX_NEAR;
    if(DISPLAY_NEAREST)filter=GX_NEAR;   /* Option: every game texture point-sampled. */
#ifdef VIPER_WII_SUPERSAMPLE
    if(sprite_near&(1u<<unit))filter=GX_NEAR;
#endif
    GXTexObj tex=wii_gx_texture_resource_acquire(&texture_resources[slot],texture_image,w,h,
        ((r[0]&64)||sprite_clamp_s(unit,w,level))?GX_CLAMP:GX_REPEAT,((r[0]&128)||sprite_clamp_t(unit,h,level))?GX_CLAMP:GX_REPEAT,filter);
#else
    GXTexObj tex;GX_InitTexObj(&tex,texture_image,w,h,GX_TF_RGBA8,(r[0]&64)?GX_CLAMP:GX_REPEAT,(r[0]&128)?GX_CLAMP:GX_REPEAT,GX_FALSE);
    unsigned filter=(r[0]&6)?GX_LINEAR:GX_NEAR;
    if(DISPLAY_NEAREST)filter=GX_NEAR;   /* Option: every game texture point-sampled. */
#ifdef VIPER_WII_SUPERSAMPLE
    if(sprite_near&(1u<<unit))filter=GX_NEAR;
#endif
    GX_InitTexObjLOD(&tex,filter,filter,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
#endif
#if defined(VIPER_WII_TEXLOAD_SKIP) && defined(VIPER_WII_NATIVE_TEXTURE_RESOURCE)
    if(texload.valid&&texload.image==texture_image&&texload.width==w&&texload.height==h&&texload.map==map&&
       texload.wrap_s==texture_resources[slot].wrap_s&&texload.wrap_t==texture_resources[slot].wrap_t&&
       texload.filter==texture_resources[slot].filter)texload.skipped++;
    else{
        GX_LoadTexObj(&tex,map);
        texload.image=texture_image;texload.width=w;texload.height=h;texload.map=map;
        texload.wrap_s=texture_resources[slot].wrap_s;texload.wrap_t=texture_resources[slot].wrap_t;
        texload.filter=texture_resources[slot].filter;texload.valid=1;
    }
#else
    GX_LoadTexObj(&tex,map);
#endif
    /* Full TMU overwrites TEX0, the texgen count, and this coord from position
     * before GX_Begin. Other binds keep the direct ST vertex format. */
#if defined(VIPER_WII_NATIVE_TEXTURE_ATTRS)
    if(!native_program)
#endif
    {
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GX_SetNumTexGens(coord+1);GX_SetTexCoordGen(coord,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    }
#ifdef VIPER_WII_GX_NATIVE_TEXTURE_BIND
    /* Full TMU emission overwrites stage zero's order and all four arithmetic
     * fields before any GX_Begin. Texture sampling/matrices remain dynamic. */
    if(!native_program)
#else
    (void)native_program;
#endif
    {GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_MODULATE);}
    *us=1.0f/(w*(1u<<level));*vs=1.0f/(h*(1u<<level));
}
#ifndef VIPER_WII_GX_FRAME_DIVISOR
#define VIPER_WII_GX_FRAME_DIVISOR 1
#endif
#if VIPER_WII_GX_FRAME_DIVISOR < 1
#error Invalid GX frame divisor
#endif
static unsigned render_frame;
#ifndef VIPER_WII_GX_FRAME_NUMERATOR
#define VIPER_WII_GX_FRAME_NUMERATOR 1
#endif
#ifndef VIPER_WII_GX_FRAME_DENOMINATOR
#define VIPER_WII_GX_FRAME_DENOMINATOR VIPER_WII_GX_FRAME_DIVISOR
#endif
#if VIPER_WII_GX_FRAME_NUMERATOR < 1 || VIPER_WII_GX_FRAME_DENOMINATOR < VIPER_WII_GX_FRAME_NUMERATOR
#error Invalid GX frame fraction
#endif
/* Select whole frames consistently across draw, clear and present callbacks.
 * Frame zero is retained; no guest CPU or device operation is skipped. */
#ifdef VIPER_WII_AUTO_FRAMESKIP
/* Option: when the game is more than a frame behind real time, the next
 * frame is not drawn (at most every other frame): the guest still runs every
 * frame unchanged; the display shows the previous picture once more. */
extern volatile int32_t wii_pace_behind_us;
static int skip_frame;
#endif
static int render_this_frame(void){
#ifdef VIPER_WII_AUTO_FRAMESKIP
    if(skip_frame)return 0;
#endif
    return ((uint64_t)(render_frame%VIPER_WII_GX_FRAME_DENOMINATOR)*VIPER_WII_GX_FRAME_NUMERATOR)%VIPER_WII_GX_FRAME_DENOMINATOR<VIPER_WII_GX_FRAME_NUMERATOR;
}
#ifdef VIPER_WII_TRIANGLE_MEMO
#if !defined(VIPER_WII_GX_BATCH) || !defined(VIPER_WII_VERTEX_STQ) || !defined(VIPER_WII_GX_EARLY_CULL) || \
    !defined(VIPER_WII_WDEPTH_LINEAR) || !defined(VIPER_WII_MATERIAL_RUN) || !defined(VIPER_WII_COMBINER_PROGRAM_CACHE) || \
    !defined(VIPER_WII_GX_DITHER_APPROX) || !defined(VIPER_WII_MATERIAL_BIND_SKIP)
#error The triangle memo replays the batched STQ, linear-depth, material-run draw path
#endif
/* The last fully set-up triangle, when its draw depended on nothing but
 * device state, the packet format and per-vertex values. A triangle with the
 * same device state epoch, texture epoch, packet format and positive-alpha
 * proof, and no GX state call since, would make the full path issue only
 * state calls that change nothing (the lazy and shadowed setters all compare
 * equal), so only its per-vertex work and counters are repeated here. Any
 * value the full path would reject or route elsewhere (non-finite input,
 * W beyond the linear range, invalid STQ) falls back to the full path before
 * any side effect, so results are exact by construction. */
static struct {
    int valid,alpha_plan_positive,positive_alpha,depth_linear,stq_mode,tmu_draw,dual;
    uint32_t state_epoch,texture_epoch,fbz;
    unsigned gx_gen,packet,stq_units,perspective;
    float scale_s[2],scale_t[2];
    /* screen_y() inputs at record time: unchanged while the epoch is (any I/O
     * write and any register change bump it), so screen_y is y_base - y or y. */
    int y_flip;float y_base;
    const WiiTMUPipelinePlan *plan;
    uint64_t hits,misses;
} tri_memo;
/* Broadway quantized store: GQR7 stores u8 (saturate to 0..255, truncate
 * toward zero) and loads f32, both unscaled. For the finite colours below
 * that is exactly c<=0?0:c>=255?255:(u8)c, written straight into the batch
 * with no float-to-integer round trip through memory. GQRs are per-thread
 * context and unused elsewhere; set on every call. */
static inline void memo_color_gqr(void){__asm__ volatile("mtspr 919,%0"::"r"(4u));}
static inline void memo_color_store(u32 *out,const float *rgba){
    double rg,ba;
    __asm__ volatile("psq_l %0,0(%2),0,7\n\tpsq_l %1,8(%2),0,7\n\t"
                     "psq_st %0,0(%3),0,7\n\tpsq_st %1,2(%3),0,7"
                     :"=&f"(rg),"=&f"(ba):"b"(rgba),"b"(out):"memory");
}
/* Memo misses: state, texture, gx state, packet, alpha/area, range, depth, stq, invalid, depth band. */
static unsigned long long memo_miss[10];
#ifdef VIPER_WII_MEMO_INT_CHECKS
/* Float range tests as integer tests on the IEEE bits, OR-ed into one flag
 * (one branch instead of a compare-and-branch per value, each a stall on the
 * 750). Each form below accepts exactly the values the float test accepts. */
static inline uint32_t fbits(const float *f){uint32_t u;memcpy(&u,f,4);return u;}
#define FB_FINITE_BAD(u) (((u)&0x7fffffffu)>=0x7f800000u)          /* !(fabsf(x)<INFINITY) */
#define FB_ABS2048_BAD(u) (((u)&0x7fffffffu)>=0x45000000u)         /* !(fabsf(x)<2048.f) */
#define FB_UNIT_BAD(u) ((u)>0x3f800000u&&(u)!=0x80000000u)          /* !(x>=0&&x<=1) */
#define FB_POSITIVE_BAD(u) ((u)-1u>=0x7f7fffffu)                    /* !(isfinite(q)&&q>0) */
#define FB_ALPHA_OK(u) ((u)-0x3f800000u<0x40000000u)                /* isfinite(a)&&a>=1 */
#endif
#ifdef VIPER_WII_RENDER_O3
#define RENDER_O3 __attribute__((optimize("O3")))
#else
#define RENDER_O3
#endif
#ifdef VIPER_WII_MEMO_VERTEX_PREP
#if !defined(VIPER_WII_DIRECT_PACKET59)
#error Per-vertex memo preparation needs packet vertex identities (VIPER_WII_DIRECT_PACKET59)
#endif
/* Per-vertex half of the memo hit: everything the memo path computes or
 * tests for one vertex depends only on the vertex and the recorded memo
 * (y flip/base, depth mode, STQ units, perspective, scales, mode), so a
 * strip vertex shared by three triangles is prepared once. Cached by its
 * slot in the packet's own vertex array, the packet serial and the memo
 * record generation; anything else is prepared afresh. The triangle path
 * below makes the same decisions in the same order on these values. */
static uint32_t tri_memo_gen=1;
typedef struct {
    uint32_t serial,gen;
    uint8_t alpha_ok,range_bad,unit_bad,wdepth_bad,stq_bad;
    float sy;
    u32 w[13];
} MemoVertex;
static MemoVertex memo_vertices[16];
static void memo_vertex_prepare(MemoVertex *m,const WiiVoodooVertex *p){
    m->alpha_ok=isfinite(p->a)&&p->a>=1;
    m->sy=tri_memo.y_flip?tri_memo.y_base-p->y:p->y;
    m->range_bad=!(fabsf(p->x)<INFINITY)||!(fabsf(p->y)<INFINITY)||
        !(fabsf(p->r)<2048.f)||!(fabsf(p->g)<2048.f)||!(fabsf(p->b)<2048.f)||!(fabsf(p->a)<2048.f);
    float depth=0;
    m->unit_bad=m->wdepth_bad=0;
    if(tri_memo.depth_linear){
        m->unit_bad=!(p->wb>=0&&p->wb<=1);
        m->wdepth_bad=p->wb<0x1p-15f&&wdepth(p->wb)>65535;
        depth=1.f-p->wb;
    }
    float stq[2][3]={{0,0,0},{0,0,0}};
    m->stq_bad=0;
    for(unsigned unit=0;unit<2;unit++)if(tri_memo.stq_units&(1u<<unit)){
        float s=unit?p->s1:p->s,t=unit?p->t1:p->t;
        float q=(tri_memo.perspective&(1u<<unit))?(unit?p->w1:p->w0):1;
        if(!isfinite(s)||!isfinite(t)||!isfinite(q)||q<=0)m->stq_bad=1;
        stq[unit][0]=s*tri_memo.scale_s[unit];stq[unit][1]=t*tri_memo.scale_t[unit];stq[unit][2]=q;
    }
    int mode=tri_memo.stq_mode;
    u32 *o=m->w;
    float f[3]={p->x,m->sy,-depth};
    memcpy(o,f,12);o+=3;
    if(mode){memcpy(o,stq[tri_memo.stq_units==2?1:0],12);o+=3;}
    if(mode==2){memcpy(o,stq[1],12);o+=3;memset(o,0,12);o+=3;}
    memo_color_gqr();
    memo_color_store(o,&p->r);
}
static inline const MemoVertex *memo_vertex(const WiiVoodooView *v,const WiiVoodooVertex *p,MemoVertex *scratch){
    const WiiVoodooVertex *lo=(const WiiVoodooVertex *)v->packet_lo;
    if(p>=lo&&(const void *)p<v->packet_hi){
        MemoVertex *m=&memo_vertices[(unsigned)(p-lo)&15];
        if(m->serial!=v->packet_serial||m->gen!=tri_memo_gen){
            memo_vertex_prepare(m,p);m->serial=v->packet_serial;m->gen=tri_memo_gen;
        }
        return m;
    }
    memo_vertex_prepare(scratch,p);
    return scratch;
}
RENDER_O3 static int triangle_memo(const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,float area){
    if(tri_memo.state_epoch!=wii_voodoo_state_epoch||tri_memo.texture_epoch!=wii_voodoo_texture_epoch||
       tri_memo.gx_gen!=gx_state_gen||tri_memo.packet!=((cmd>>10)&255)){
        memo_miss[tri_memo.state_epoch!=wii_voodoo_state_epoch?0:tri_memo.texture_epoch!=wii_voodoo_texture_epoch?1:
                  tri_memo.gx_gen!=gx_state_gen?2:3]++;
        return 0;
    }
    MemoVertex scratch[3];
    const MemoVertex *m0=memo_vertex(v,&p[0],&scratch[0]),*m1=memo_vertex(v,&p[1],&scratch[1]),
                     *m2=memo_vertex(v,&p[2],&scratch[2]);
    int positive_alpha=tri_memo.alpha_plan_positive&m0->alpha_ok&m1->alpha_ok&m2->alpha_ok;
    if(positive_alpha!=tri_memo.positive_alpha||!isfinite(area)){memo_miss[4]++;return 0;}
    double y0=m0->sy,y1=m1->sy,y2=m2->sy;
    double submitted_area=((double)p[1].x-p[0].x)*(y2-y0)-(y1-y0)*((double)p[2].x-p[0].x);
    if(submitted_area==0){
        if(!tri_memo.tmu_draw){
            wii_combiner_program_invalidate(&combiner_cache);
            wii_material_run_invalidate(&material_run);
        }
        return 1;
    }
    if(m0->range_bad|m1->range_bad|m2->range_bad){memo_miss[5]++;return 0;}
    if(tri_memo.depth_linear&&(m0->unit_bad|m1->unit_bad|m2->unit_bad|m0->wdepth_bad)){memo_miss[6]++;return 0;}
    if(m0->stq_bad|m1->stq_bad|m2->stq_bad){memo_miss[7]++;return 0;}
    tri_memo.hits++;
    if(tri_memo.tmu_draw){
        if(wii_material_run_resume(&material_run,tri_memo.plan))material_run.resumes++;
        profile.tmu_pipeline_draws++;
        if(tri_memo.dual)profile.dual_texture_draws++;
    }else{
        wii_combiner_program_invalidate(&combiner_cache);
        wii_material_run_invalidate(&material_run);
    }
    int mode=tri_memo.stq_mode;
    stq_set_mode(mode);
    unsigned words=4+(mode==2?9:mode?3:0);
    u32 *o=gx_batch_reserve(mode==2?GX_VTXFMT1:GX_VTXFMT0,words);
    memcpy(o,m0->w,words*4);memcpy(o+words,m1->w,words*4);memcpy(o+2*words,m2->w,words*4);
    if(tri_memo.fbz&256){
        unsigned dither=0;
        for(unsigned i=0;i<3;i++){
            u32 c=o[i*words+words-1];u8 r=c>>24,g=c>>16,b=c>>8;
            dither+=(r!=0&&r!=255)||(g!=0&&g!=255)||(b!=0&&b!=255);
        }
        if(dither){
            dither_approximations+=dither;
            if(!dither_reported){dither_reported=1;rt_log("VIPER WII GX APPROX Voodoo ordered RGB565 dithering omitted; pixel equivalence unproven\n");}
        }
    }
    geometry=1;
    return 1;
}
#else
RENDER_O3 static int triangle_memo(const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,float area){
    if(tri_memo.state_epoch!=wii_voodoo_state_epoch||tri_memo.texture_epoch!=wii_voodoo_texture_epoch||
       tri_memo.gx_gen!=gx_state_gen||tri_memo.packet!=((cmd>>10)&255)){
        memo_miss[tri_memo.state_epoch!=wii_voodoo_state_epoch?0:tri_memo.texture_epoch!=wii_voodoo_texture_epoch?1:
                  tri_memo.gx_gen!=gx_state_gen?2:3]++;
        return 0;
    }
    int positive_alpha=tri_memo.alpha_plan_positive;
#ifdef VIPER_WII_MEMO_INT_CHECKS
    positive_alpha&=FB_ALPHA_OK(fbits(&p[0].a))&FB_ALPHA_OK(fbits(&p[1].a))&FB_ALPHA_OK(fbits(&p[2].a));
#else
    for(unsigned i=0;i<3;i++)positive_alpha&=isfinite(p[i].a)&&p[i].a>=1;
#endif
    if(positive_alpha!=tri_memo.positive_alpha||!isfinite(area)){memo_miss[4]++;return 0;}
    float sy[3];
    if(tri_memo.y_flip){sy[0]=tri_memo.y_base-p[0].y;sy[1]=tri_memo.y_base-p[1].y;sy[2]=tri_memo.y_base-p[2].y;}
    else{sy[0]=p[0].y;sy[1]=p[1].y;sy[2]=p[2].y;}
    (void)v;
    double y0=sy[0],y1=sy[1],y2=sy[2];
    double submitted_area=((double)p[1].x-p[0].x)*(y2-y0)-(y1-y0)*((double)p[2].x-p[0].x);
    if(submitted_area==0){
        /* The full path invalidates these before its own zero-area return. */
        if(!tri_memo.tmu_draw){
            wii_combiner_program_invalidate(&combiner_cache);
            wii_material_run_invalidate(&material_run);
        }
        return 1;
    }
    /* Everything the full path would reject or route elsewhere, before any
     * side effect. Comparisons are false for NaN, so each test also covers
     * the full path's isfinite() checks. */
#ifdef VIPER_WII_MEMO_INT_CHECKS
    {
        uint32_t bad=0;
        for(unsigned i=0;i<3;i++){
            bad|=FB_FINITE_BAD(fbits(&p[i].x))|FB_FINITE_BAD(fbits(&p[i].y));
            bad|=FB_ABS2048_BAD(fbits(&p[i].r))|FB_ABS2048_BAD(fbits(&p[i].g))|
                 FB_ABS2048_BAD(fbits(&p[i].b))|FB_ABS2048_BAD(fbits(&p[i].a));
        }
        if(UNLIKELY(bad)){memo_miss[5]++;return 0;}
    }
#else
    for(unsigned i=0;i<3;i++){
        if(!(fabsf(p[i].x)<INFINITY)||!(fabsf(p[i].y)<INFINITY)){memo_miss[5]++;return 0;}
        if(!(fabsf(p[i].r)<2048.f)||!(fabsf(p[i].g)<2048.f)||
           !(fabsf(p[i].b)<2048.f)||!(fabsf(p[i].a)<2048.f)){memo_miss[5]++;return 0;}
    }
#endif
    float depths[3]={0,0,0};
    if(tri_memo.depth_linear){
#ifdef VIPER_WII_MEMO_INT_CHECKS
        {uint32_t u0=fbits(&p[0].wb),u1=fbits(&p[1].wb),u2=fbits(&p[2].wb);
         if(UNLIKELY(FB_UNIT_BAD(u0)|FB_UNIT_BAD(u1)|FB_UNIT_BAD(u2))){memo_miss[6]++;return 0;}}
#else
        for(unsigned i=0;i<3;i++)if(!(p[i].wb>=0&&p[i].wb<=1)){memo_miss[6]++;return 0;}
#endif
        /* wdepth() can exceed 65535 only for wb in [2^-16, 2^-15). */
        if(p[0].wb<0x1p-15f&&wdepth(p[0].wb)>65535){memo_miss[6]++;return 0;}
        for(unsigned i=0;i<3;i++)depths[i]=1.f-p[i].wb;
    }
    float stq[2][3][3];
#ifdef VIPER_WII_MEMO_INT_CHECKS
    {
        uint32_t bad=0;
        for(unsigned unit=0;unit<2;unit++)if(tri_memo.stq_units&(1u<<unit)){
            int persp=!!(tri_memo.perspective&(1u<<unit));
            for(unsigned i=0;i<3;i++){
                const float *sp=unit?&p[i].s1:&p[i].s,*tp=unit?&p[i].t1:&p[i].t;
                const float *qp=unit?&p[i].w1:&p[i].w0;
                bad|=FB_FINITE_BAD(fbits(sp))|FB_FINITE_BAD(fbits(tp));
                if(persp)bad|=FB_POSITIVE_BAD(fbits(qp));
                stq[unit][i][0]=*sp*tri_memo.scale_s[unit];stq[unit][i][1]=*tp*tri_memo.scale_t[unit];
                stq[unit][i][2]=persp?*qp:1;
            }
        }
        if(UNLIKELY(bad)){memo_miss[7]++;return 0;}
    }
#else
    for(unsigned unit=0;unit<2;unit++)if(tri_memo.stq_units&(1u<<unit)){
        for(unsigned i=0;i<3;i++){
            float s=unit?p[i].s1:p[i].s,t=unit?p[i].t1:p[i].t;
            float q=(tri_memo.perspective&(1u<<unit))?(unit?p[i].w1:p[i].w0):1;
            if(!isfinite(s)||!isfinite(t)||!isfinite(q)||q<=0){memo_miss[7]++;return 0;}
            stq[unit][i][0]=s*tri_memo.scale_s[unit];stq[unit][i][1]=t*tri_memo.scale_t[unit];stq[unit][i][2]=q;
        }
    }
#endif
    /* Committed: the counters and cache flags the full path would touch. */
    tri_memo.hits++;
    if(tri_memo.tmu_draw){
        if(wii_material_run_resume(&material_run,tri_memo.plan))material_run.resumes++;
        profile.tmu_pipeline_draws++;
        if(tri_memo.dual)profile.dual_texture_draws++;
    }else{
        wii_combiner_program_invalidate(&combiner_cache);
        wii_material_run_invalidate(&material_run);
    }
    int mode=tri_memo.stq_mode;
    stq_set_mode(mode);
    const float *stq_n=stq[tri_memo.stq_units==2?1:0][0],*stq_b=stq[1][0];
    unsigned words=4+(mode==2?9:mode?3:0);
    u32 *o=gx_batch_reserve(mode==2?GX_VTXFMT1:GX_VTXFMT0,words),*colors=o+words-1;
    memo_color_gqr();
    for(unsigned i=0;i<3;i++){
        float f[3]={p[i].x,sy[i],-depths[i]};
        memcpy(o,f,12);o+=3;
        if(mode){memcpy(o,stq_n+i*3,12);o+=3;}
        if(mode==2){memcpy(o,stq_b+i*3,12);o+=3;memset(o,0,12);o+=3;}
        memo_color_store(o++,&p[i].r);
    }
#ifndef VIPER_WII_NO_DITHER_STATS
    if(tri_memo.fbz&256){
        /* One count per vertex with any partial RGB channel, as the full path. */
        unsigned dither=0;
        for(unsigned i=0;i<3;i++){
            u32 c=colors[i*words];u8 r=c>>24,g=c>>16,b=c>>8;
            dither+=(r!=0&&r!=255)||(g!=0&&g!=255)||(b!=0&&b!=255);
        }
        if(dither){
            dither_approximations+=dither;
            if(!dither_reported){dither_reported=1;rt_log("VIPER WII GX APPROX Voodoo ordered RGB565 dithering omitted; pixel equivalence unproven\n");}
        }
    }
#else
    (void)colors;   /* diagnostic tally only: the picture does not depend on it */
#endif
    geometry=1;
    return 1;
}
#endif /* VIPER_WII_MEMO_VERTEX_PREP */
#endif
static void triangle_full(void *user,const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,float early_area) __attribute__((noinline));
#ifdef VIPER_WII_MEMO_MULTI
/* Multi-entry triangle memo: each entry is a full-path triangle's memo with
 * the device state it was made in (every FBI, I/O and TMU register, the
 * texture epoch and packet format) and the GX setter calls that path made
 * (gx_memo_log.h). When the device returns to a recorded state, the calls
 * are replayed (through the same wrappers) and the entry becomes the current
 * memo. Recording starts from cold renderer caches so the log is complete;
 * a replay leaves them cold too. Entries die with any texture upload or
 * discard (their texture objects name slot memory). */
#define MEMO_KEY_WORDS (256+64+128+2)
#define MEMO_ENTRIES 8
typedef struct {
    int used;uint32_t hash;unsigned tex_gen;uint64_t stamp;
    uint32_t key[MEMO_KEY_WORDS];
    __typeof__(tri_memo) memo;
    WiiTMUPipelinePlan plan;
    unsigned nlog;MemoCall *log;
} MemoEntry;
static MemoEntry *memo_entries;
static uint64_t memo_stamp,memo_multi_hits,memo_multi_records;
static uint32_t memo_key_hash(const WiiVoodooView *v,uint32_t cmd,uint32_t *key){
    memcpy(key,v->regs,256*4);memcpy(key+256,v->io,64*4);memcpy(key+320,v->tmu,128*4);
    key[448]=wii_voodoo_texture_epoch;key[449]=(cmd>>10)&255;
    uint32_t h=2166136261u;
    for(unsigned i=0;i<MEMO_KEY_WORDS;i++)h=(h^key[i])*16777619u;
    return h;
}
static void memo_caches_cold(void){
    wii_combiner_program_invalidate(&combiner_cache);
    wii_material_run_invalidate(&material_run);
    TEXLOAD_FORGET();
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
    material_bind_valid=0;
#endif
#ifdef VIPER_WII_VERTEX_STQ
    stq_desc=-1;
#endif
    gx_shadow.proj_w=gx_shadow.proj_h=0;
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    lookup_plane_cache.valid=0;
#endif
}
static uint32_t memo_key_now[MEMO_KEY_WORDS];
/* A device-state memo miss: put a recorded entry back, or 0. */
static int memo_multi_restore(const WiiVoodooView *v,uint32_t cmd){
    if(!memo_entries)return 0;
    uint32_t h=memo_key_hash(v,cmd,memo_key_now);
    for(unsigned i=0;i<MEMO_ENTRIES;i++){
        MemoEntry *e=&memo_entries[i];
        if(!e->used||e->hash!=h||e->tex_gen!=memo_tex_gen||memcmp(e->key,memo_key_now,sizeof e->key))continue;
        memo_caches_cold();
        memo_replay(e->log,e->nlog);
        memo_caches_cold();
        tri_memo=e->memo;tri_memo.plan=&e->plan;
        tri_memo.state_epoch=wii_voodoo_state_epoch;tri_memo.gx_gen=gx_state_gen;tri_memo.valid=1;
        e->stamp=++memo_stamp;memo_multi_hits++;
        return 1;
    }
    return 0;
}
static void memo_multi_record_begin(void){
    memo_caches_cold();
    memo_log_n=0;memo_log_bad=0;memo_logging=1;
}
static void memo_multi_record_end(const WiiVoodooView *v,uint32_t cmd){
    memo_logging=0;
    if(memo_log_bad||!tri_memo.valid||tri_memo.state_epoch!=wii_voodoo_state_epoch||tri_memo.gx_gen!=gx_state_gen)return;
    if(!memo_entries){
        memo_entries=calloc(MEMO_ENTRIES,sizeof *memo_entries);
        if(!memo_entries)return;
        for(unsigned i=0;i<MEMO_ENTRIES;i++)memo_entries[i].log=malloc(MEMO_LOG_CAP*sizeof(MemoCall));
    }
    MemoEntry *e=&memo_entries[0];
    for(unsigned i=1;i<MEMO_ENTRIES;i++)if(!memo_entries[i].used||memo_entries[i].stamp<e->stamp)e=&memo_entries[i];
    if(!e->log)return;
    e->hash=memo_key_hash(v,cmd,e->key);e->tex_gen=memo_tex_gen;
    e->memo=tri_memo;e->plan=*tri_memo.plan;e->memo.plan=&e->plan;
    memcpy(e->log,memo_log,memo_log_n*sizeof(MemoCall));e->nlog=memo_log_n;
    e->used=1;e->stamp=++memo_stamp;memo_multi_records++;
}
#endif
#ifdef VIPER_WII_SUPERSAMPLE
static unsigned sprite_unit_1to1(const WiiVoodooVertex p[3],unsigned unit){
    float q0=unit?p[0].w1:p[0].w0,q1=unit?p[1].w1:p[1].w0,q2=unit?p[2].w1:p[2].w0;
    if(q0!=q1||q0!=q2||!(q0>0))return 0;   /* perspective-mapped: 3D */
    float dx1=p[1].x-p[0].x,dy1=p[1].y-p[0].y,dx2=p[2].x-p[0].x,dy2=p[2].y-p[0].y;
    float det=dx1*dy2-dx2*dy1;
    if(!(fabsf(det)>=1.f))return 0;
    float r=1.f/(det*q0);
    float s0=unit?p[0].s1:p[0].s,s1=unit?p[1].s1:p[1].s,s2=unit?p[2].s1:p[2].s;
    float t0=unit?p[0].t1:p[0].t,t1=unit?p[1].t1:p[1].t,t2=unit?p[2].t1:p[2].t;
    float sx=((s1-s0)*dy2-(s2-s0)*dy1)*r,sy=((s2-s0)*dx1-(s1-s0)*dx2)*r;
    float tx=((t1-t0)*dy2-(t2-t0)*dy1)*r,ty=((t2-t0)*dx1-(t1-t0)*dx2)*r;
    if(!(fabsf(fabsf(sx)-1.f)<0.02f&&fabsf(fabsf(ty)-1.f)<0.02f&&fabsf(sy)<0.02f&&fabsf(tx)<0.02f))return 0;
    sprite_s_min[unit]=fminf(s0,fminf(s1,s2))/q0;sprite_s_max[unit]=fmaxf(s0,fmaxf(s1,s2))/q0;
    sprite_t_min[unit]=fminf(t0,fminf(t1,t2))/q0;sprite_t_max[unit]=fmaxf(t0,fmaxf(t1,t2))/q0;
    return 1;
}
static void sprite_near_update(const WiiVoodooVertex p[3]){
    unsigned n=ss_recording?sprite_unit_1to1(p,0)|sprite_unit_1to1(p,1)<<1:0;
    /* Bits 2-3: that unit's texels fit a 256 texture (a clamp candidate; the
     * bind checks the real size). Part of the state, so cached bindings
     * are dropped when it changes. */
    for(unsigned u=0;u<2;u++)if(n>>u&1&&sprite_s_min[u]>=0&&sprite_t_min[u]>=0&&sprite_s_max[u]<=256&&sprite_t_max[u]<=256)n|=4u<<u;
#ifdef VIPER_WII_SPRITE_TRACE
    {static unsigned logged;float xmin=fminf(p[0].x,fminf(p[1].x,p[2].x));
     int textured=p[0].s!=0||p[1].s!=0||p[2].s!=0||p[0].t!=0||p[1].t!=0||p[2].t!=0;
     if(logged<60&&render_frame>=1300&&render_frame<=1500&&textured&&xmin>-4&&xmin<4){logged++;
      rt_log("VIPER WII SPRITE f=%u near=%u v0=%.4f,%.4f st=%.4f,%.4f q=%.4f v1=%.4f,%.4f st=%.4f,%.4f v2=%.4f,%.4f st=%.4f,%.4f tex=%08lx\n",render_frame,n,
        (double)p[0].x,(double)p[0].y,(double)p[0].s,(double)p[0].t,(double)p[0].w0,(double)p[1].x,(double)p[1].y,(double)p[1].s,(double)p[1].t,
        (double)p[2].x,(double)p[2].y,(double)p[2].s,(double)p[2].t,(unsigned long)0);}}
#endif
    sprite_near_tris+=n!=0;
#ifdef VIPER_WII_FRAME_CAPTURE
    {float xmin=fminf(p[0].x,fminf(p[1].x,p[2].x));
     if((p[0].s!=p[1].s||p[0].t!=p[1].t||p[0].s!=p[2].s)&&xmin<4&&edge_sprite_len<sizeof edge_sprite_text-200)
        edge_sprite_len+=snprintf(edge_sprite_text+edge_sprite_len,sizeof edge_sprite_text-edge_sprite_len,
          "n=%u (%.4f,%.4f s%.4f t%.4f q%.3f) (%.4f,%.4f s%.4f t%.4f) (%.4f,%.4f s%.4f t%.4f)\n",n,
          (double)p[0].x,(double)p[0].y,(double)p[0].s,(double)p[0].t,(double)p[0].w0,(double)p[1].x,(double)p[1].y,(double)p[1].s,(double)p[1].t,
          (double)p[2].x,(double)p[2].y,(double)p[2].s,(double)p[2].t);}
#endif
    if(n==sprite_near)return;
    sprite_near=n;
    /* Bound texture objects carry the filter: no cached binding survives. */
    TEXLOAD_FORGET();
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
    material_bind_valid=0;
#endif
#ifdef VIPER_WII_TRIANGLE_MEMO
    tri_memo.valid=0;
#endif
}
#endif

#ifndef RENDER_O3
#define RENDER_O3   /* defined with the triangle memo; plain otherwise */
#endif
WII_HOT_triangle RENDER_O3 static void triangle(void *user,const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd) {
#ifdef VIPER_WII_FONT_TRACE
    /* Diagnostic: triangles textured from the game font pages (VRAM 0x5e400
     * digits, 0x6e400 letters), to recover how the game lays out glyphs. */
    {unsigned base=v->tmu[0][3]&0xfffff0;static unsigned n;
     if((base==0x6e400||base==0x5e400)&&n<20000){n++;
        rt_log("VIPER WII FONT f=%u base=%x mode=%08lx lod=%08lx v=%.2f,%.2f,%.4f,%.4f,%.4f %.2f,%.2f,%.4f,%.4f,%.4f %.2f,%.2f,%.4f,%.4f,%.4f\n",
          render_frame,base,(unsigned long)v->tmu[0][0],(unsigned long)v->tmu[0][1],
          (double)p[0].x,(double)p[0].y,(double)p[0].s,(double)p[0].t,(double)p[0].wb,
          (double)p[1].x,(double)p[1].y,(double)p[1].s,(double)p[1].t,(double)p[1].wb,
          (double)p[2].x,(double)p[2].y,(double)p[2].s,(double)p[2].t,(double)p[2].wb);}}
#endif
#if defined(VIPER_WII_PARENT_PROJECTION) && defined(VIPER_WII_GX_WDEPTH_APPROX)
    /* Recursive children are synchronous: neither guest register writes nor
     * clear/present callbacks can interleave. All other draws reset this. */
    if(active_depth_band<0)parent_projection_ready=0;
#endif
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    lookup_plane_cache.valid=0;
#endif
    if(!render_this_frame())return;
#ifdef VIPER_WII_GX_EARLY_CULL
    /* Same finite-area/culling predicate as the admission path below. A
     * discarded primitive emits no GX commands and cannot use its combiner.
     * Retained setup-plane state, when integrated, must precede this return. */
    float area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
    if(isfinite(area)){
        if(area==0)return;
        if(cmd&(1u<<23)){
            unsigned sign=(cmd>>24)&1;
            if(!(cmd&(1u<<22))&&!(cmd&(1u<<25))){
                if(v->strip_count<3)unsupported(v,cmd,"GX invalid strip setup count");
                sign^=(v->strip_count-3)&1;
            }
            if((area<0)==sign)return;
        }
    }
#endif
    wii_gx_own_thread();
#ifdef VIPER_WII_FRAME_CAPTURE
    tally_state(v,p);
#endif
    /* WIDE draws 682 columns; an untextured flat triangle whose every
     * vertex sits on the game's left or right edge is a full-width overlay
     * (the white fade as a car lands in the river): it reaches the margin's
     * edge too, as a clear rectangle does, instead of leaving the margins
     * unfaded. */
    WiiVoodooVertex wide_overlay[3];
    if(WIDE_M&&!(v->regs[0x104/4]&(1u<<27))&&p[1].wb==p[0].wb&&p[2].wb==p[0].wb){
        float right=(float)width(v);unsigned left_n=0,right_n=0;
        for(unsigned i=0;i<3;i++){if(p[i].x<=0.5f)left_n++;else if(p[i].x>=right-0.5f)right_n++;}
        if(left_n&&right_n&&left_n+right_n==3){
            for(unsigned i=0;i<3;i++){wide_overlay[i]=p[i];wide_overlay[i].x=p[i].x<=0.5f?-(float)WIDE_M:right+(float)WIDE_M;}
            p=wide_overlay;
        }
    }
#ifdef VIPER_WII_SUPERSAMPLE
    sprite_near_update(p);
#endif
#ifdef VIPER_WII_TRIANGLE_MEMO
    if(tri_memo.valid&&active_depth_band<0&&triangle_memo(v,p,cmd,area))return;
#ifdef VIPER_WII_MEMO_MULTI
    if(active_depth_band<0&&(!tri_memo.valid||tri_memo.state_epoch!=wii_voodoo_state_epoch)&&
       memo_multi_restore(v,cmd)&&triangle_memo(v,p,cmd,area))return;
#endif
    if(tri_memo.valid)tri_memo.misses++;
    else memo_miss[8]++;
    if(active_depth_band>=0)memo_miss[9]++;
    tri_memo.valid=0;
#endif
#ifdef VIPER_WII_GX_EARLY_CULL
#if defined(VIPER_WII_MEMO_MULTI)
    memo_multi_record_begin();
    triangle_full(user,v,p,cmd,area);
    memo_multi_record_end(v,cmd);
#elif defined(VIPER_WII_MEMO_SPURIOUS_STATS)
    {   /* Diagnostic: a memo miss after which the full path changed no GX
         * state could have kept the memo (a keyed multi-entry memo's case). */
        static unsigned long long misses,quiet;unsigned gen=gx_state_gen;
        triangle_full(user,v,p,cmd,area);
        misses++;quiet+=gen==gx_state_gen;
        if(!(misses&0x3ffff))rt_log("VIPER WII MEMO SPURIOUS misses=%llu quiet=%llu\n",misses,quiet);
    }
#else
    triangle_full(user,v,p,cmd,area);
#endif
#else
    triangle_full(user,v,p,cmd,0.f);
#endif
}
/* Everything after the early cull and the memo: kept out of line so the
 * common memo hit does not pay this function's frame and register saves. */
WII_HOT_triangle_full static void triangle_full(void *user,const WiiVoodooView *v,const WiiVoodooVertex p[3],uint32_t cmd,float early_area) {
#ifdef VIPER_WII_GX_EARLY_CULL
    float area=early_area;
#else
    (void)early_area;
#endif
    depth_texture_off();
    (void)user;uint32_t cp=v->regs[0x104/4],fbz=v->regs[0x110/4],alpha=v->regs[0x10c/4];
    int known_fog=v->regs[0x108/4]==0x40,fog_enabled=0;
#ifdef VIPER_WII_GX_FOG_APPROX
    if(v->regs[0x108/4]==0x41){known_fog=1;fog_enabled=1;}
#endif
    int textured=cp==0x1c482405&&((cmd>>10)&255)==0x23&&fbz==0x2132b&&alpha==0x0c045119&&
      known_fog&&v->tmu[0][0]==0x000004c0&&v->tmu[1][0]==0x102414c0;
    unsigned texture_unit=1,texture_format=4;
    unsigned family_binary_chroma=0,family_selected=0,family_reject_zero_alpha=0;
    int generic_pipeline=0;
    int full_pipeline=0;
    WiiVoodooColorPlan alpha_plan=wii_voodoo_color_plan(cp);
    int positive_alpha=alpha_plan.clamp&&!alpha_plan.local_override&&
        !alpha_plan.local_alpha&&alpha_plan.alpha_zero&&!alpha_plan.alpha_sub&&
        alpha_plan.alpha_add&&!alpha_plan.alpha_invert;
    for(unsigned i=0;i<3;i++)positive_alpha&=isfinite(p[i].a)&&p[i].a>=1;
#ifdef VIPER_WII_GX_COLOR_EQUATION
    /* Generic FBI equations and original-source rejection share established
     * single-TMU sampling. Preflight all stages before emitting GX state.
     * True TMU combinations, override and depth-derived alpha stay guarded. */
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
#ifdef VIPER_WII_MATERIAL_RUN
    /* A new packet recomputes the plan in the same slot, so the pointer a
     * resume compares would match a plan built for the old packet. */
    if(!material_plans.have_packet||material_plans.packet!=((cmd>>10)&255))
#ifdef VIPER_WII_COMBINER_KEEP
        wii_material_run_plan_changed(&material_run); /* the memo id covers the packet */
#else
        wii_material_run_invalidate(&material_run);
#endif
#endif
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    const WiiTMUPipelinePlan *material_plan=wii_combiner_program_material_get(&material_plans,&combiner_cache,
#else
    const WiiTMUPipelinePlan *material_plan=wii_material_plan_get(&material_plans,
#endif
        cp,fbz,alpha,v->regs[0x108/4],v->regs[0x134/4],v->regs[0x138/4],
        v->tmu[0],v->tmu[1],v->regs[0x21c/4],(cmd>>10)&255,positive_alpha);
#define tmu_pipeline (*material_plan)
#define pipeline (material_plan->fbi)
    full_pipeline=tmu_pipeline.reason==WII_TMU_PIPE_OK;
    generic_pipeline=full_pipeline;
    if(!full_pipeline){
        rt_log("VIPER WII GX TMU PREFLIGHT reason=%u FBI=%s stages=%u\n",
            tmu_pipeline.reason,wii_pipeline_reason_name(pipeline.reason),tmu_pipeline.total_stages);
        unsupported(v,cmd,"GX full TMU pipeline preflight");
    }
#else
    WiiGXPipelinePlan pipeline=wii_gx_pipeline_plan(cp,fbz,alpha,v->regs[0x108/4],
        v->regs[0x134/4],v->regs[0x138/4],v->tmu[0][0],v->tmu[1][0],
        (cmd>>10)&255,positive_alpha);
    generic_pipeline=pipeline.reason==WII_PIPE_OK;
#ifdef VIPER_WII_GX_TMU_PIPELINE
    WiiTMUPipelinePlan tmu_pipeline=wii_gx_tmu_pipeline_plan(cp,fbz,alpha,v->regs[0x108/4],
        v->regs[0x134/4],v->regs[0x138/4],v->tmu[0],v->tmu[1],v->regs[0x21c/4],
        (cmd>>10)&255,positive_alpha);
    full_pipeline=tmu_pipeline.reason==WII_TMU_PIPE_OK;
    if(full_pipeline){pipeline=tmu_pipeline.fbi;generic_pipeline=1;}
    else{
        rt_log("VIPER WII GX TMU PREFLIGHT reason=%u FBI=%s stages=%u\n",
            tmu_pipeline.reason,wii_pipeline_reason_name(tmu_pipeline.fbi.reason),tmu_pipeline.total_stages);
        unsupported(v,cmd,"GX full TMU pipeline preflight");
    }
#endif
#endif /* material cache / original preparation */
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    if(!(full_pipeline&&!tmu_pipeline.texture_zero))wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    if(!(full_pipeline&&!tmu_pipeline.texture_zero))wii_material_run_invalidate(&material_run);
#endif
    if(generic_pipeline){
        textured=pipeline.textured;texture_unit=pipeline.unit;texture_format=pipeline.format;family_selected=1;
    }
#endif
#ifdef VIPER_WII_GX_TMU_PIPELINE
    /* The full pipeline overwrites textured/family_selected below, and its
     * draw branch never reads texture_unit/format or the family flags, so the
     * captured-family classifier only matters off that path. */
    int skip_families=full_pipeline&&!tmu_pipeline.texture_zero;
#else
    int skip_families=0;
#endif
    if(!skip_families){
#ifdef VIPER_WII_GX_CHROMA_APPROX
    WiiRenderFamily family;
    if(known_fog&&wii_render_family_with_alpha(cp,fbz,alpha,v->regs[0x108/4],
       v->tmu[0][0],v->tmu[1][0],(cmd>>10)&255,positive_alpha,&family)){
        textured=1;texture_unit=family.unit;texture_format=family.format;
        family_binary_chroma=family.binary_chroma;
        family_reject_zero_alpha=family.reject_zero_alpha;
        family_selected=1;
    }
    unsigned local_unit=(v->tmu[0][0]&~0xfc0u)==7?1:0;
    unsigned local_format=(v->tmu[local_unit][0]>>8)&15;
    unsigned affine_unit=(v->tmu[0][0]&~0xfc0u)==6?1:0;
    unsigned affine_format=(v->tmu[affine_unit][0]>>8)&15;
    if(cp==0x1d022401&&((cmd>>10)&255)==0x23&&fbz==0x2132b&&alpha==0x0c045119&&
       known_fog&&
       ((v->tmu[0][0]&~0xfc0u)==0x10241006||(v->tmu[0][0]&~0xfc0u)==6)&&
       (v->tmu[1][0]&~0xfc0u)==0x10241006&&affine_format==10){
        textured=1;texture_unit=affine_unit;texture_format=10;
    }
    /* Captured affine HUD families from dc/voodoo_native.c. Perspective and
     * affine packets share colour equations; mode bit0 changes ST evaluation. */
    if(cp==0x1c482405&&((cmd>>10)&255)==0x23&&
       ((fbz==0x2136b&&alpha==0x0004511f)||(fbz==0x2132b&&alpha==0x0c045119)||
        (fbz==0x2132b&&alpha==0x4511f&&v->regs[0x108/4]==0x40&&
         v->tmu[0][0]==0x10241c06&&v->tmu[1][0]==0x10241c06&&
         v->regs[0x134/4]==0&&v->regs[0x138/4]==0x10000000))&&
       known_fog&&
       ((v->tmu[0][0]&~0xfc0u)==0x10241006||(v->tmu[0][0]&~0xfc0u)==6)&&
       (v->tmu[1][0]&~0xfc0u)==0x10241006&&(affine_format==11||affine_format==12)){
        textured=1;texture_unit=affine_unit;texture_format=affine_format;
    }
    if(cp==0x1c482405&&((cmd>>10)&255)==0x23&&(fbz==0x2136b||fbz==0x21329)&&
       alpha==0x0004511f&&known_fog&&
       v->tmu[0][0]==0x000004c0&&v->tmu[1][0]==0x102414c0){
        textured=1;texture_unit=1;texture_format=4;
    }
    /* The captured affine A8 overlay: same alpha-only equation,
     * no depth writes, standard source-alpha blend and ALWAYS alpha test. */
    if(cp==0x1c484104&&((cmd>>10)&255)==0x23&&
       (((fbz==0x2136b||fbz==0x2132b)&&alpha==0x0004511f)||(fbz==0x2132b&&alpha==0x0c045119))&&
       known_fog&&
       ((v->tmu[0][0]&~0xc0u)==0x10241206||(v->tmu[0][0]&~0xc0u)==0x206)&&
       (v->tmu[1][0]&~0xc0u)==0x10241206){
        textured=1;texture_unit=(v->tmu[0][0]&~0xc0u)==0x206?1:0;texture_format=2;
    }
    /* These depth/alpha combinations are also captured in dc/voodoo_native.c.
     * GX already decodes depth comparison/write and blend independently. */
    if(cp==0x1c484104&&((cmd>>10)&255)==0x3b&&
       (((fbz==0x2173b||fbz==0x2132b)&&alpha==0x0c045119)||
        (fbz==0x2137b&&alpha==0x0004511f))&&
       known_fog&&
       ((v->tmu[0][0]&~0xc0u)==0x10241207||(v->tmu[0][0]&~0xc0u)==0x207)&&
       (v->tmu[1][0]&~0xc0u)==0x10241207){
        textured=1;texture_unit=local_unit;texture_format=2;
    }
    if(cp==0x1c482405&&((cmd>>10)&255)==0x3b&&
       (((fbz==0x2173b||fbz==0x2133b)&&alpha==0x0c045119)||
        ((fbz==0x2137b||fbz==0x21379||fbz==0x21779||
          (fbz==0x217b&&local_format==11))&&alpha==0x0004511f)||
        ((fbz==0x21359||fbz==0x21379||fbz==0x21779)&&alpha==0x0004411f)||
        (fbz==0x21379&&alpha==0x0004421f))&&
       known_fog&&
       ((v->tmu[0][0]&~0xfc0u)==0x10241007||(v->tmu[0][0]&~0xfc0u)==7)&&
       (v->tmu[1][0]&~0xfc0u)==0x10241007&&(local_format==11||local_format==12)){
        textured=1;texture_unit=local_unit;texture_format=local_format;
        static int reported;
        if(!reported){reported=1;rt_log("VIPER WII GX APPROX ARGB1555 modulation rounding can change partial-alpha rejection; fixed-point equivalence unproven\n");}
    }
    if(cp==0x1d022401&&((cmd>>10)&255)==0x3b&&
       ((fbz==0x2172b&&alpha==0x0c045109)||
        ((fbz==0x2173b||fbz==0x2133b)&&alpha==0x0c045119)||
        ((fbz==0x2137b||fbz==0x21359||fbz==0x21379||fbz==0x21779)&&alpha==0x0004511f)||
        ((fbz==0x21379||fbz==0x21779)&&alpha==0x0004411f)||
        ((fbz==0x2176b||fbz==0x21729)&&alpha==0x0004510f)||
        (fbz==0x21359&&(alpha==0x0004221f||alpha==0x0004421f))||
        (fbz==0x2175b&&alpha==0x0004221f&&v->regs[0x108/4]==0x40&&
         v->tmu[0][0]==0x10241507&&v->tmu[1][0]==0x10241507&&
         v->regs[0x134/4]==0&&v->regs[0x138/4]==0x10000000))&&
       known_fog&&
       ((v->tmu[0][0]&~0xfc0u)==0x10241007||(v->tmu[0][0]&~0xfc0u)==7)&&
       (v->tmu[1][0]&~0xfc0u)==0x10241007&&
       (local_format==10||local_format==5)){
        textured=1;texture_unit=local_unit;texture_format=local_format;
        static int reported;
        if(!reported){reported=1;rt_log("VIPER WII GX APPROX RGB565 perspective colour modulation; post-filter black-key, GX modulation rounding unverified\n");}
    }
    if(cp==0x1c482405&&((cmd>>10)&255)==0x23&&fbz==0x2132b&&alpha==0x0c045119&&
       known_fog&&v->tmu[0][0]==0x10241c06&&v->tmu[1][0]==0x10241c06){
        textured=1;texture_unit=0;texture_format=12;
        static int reported;
        if(!reported){reported=1;rt_log("VIPER WII GX APPROX ARGB4444 post-filter black chroma; GX/Voodoo filtering precision unverified\n");}
    }
#endif
    /* Exact captured ARGB4444 ALWAYS-depth/additive fog family. */
    if(cp==0x1c482405&&((cmd>>10)&255)==0x3b&&fbz==0x213fb&&
       alpha==0x4411f&&fog_enabled&&
       (v->tmu[0][0]==0xcc7||v->tmu[0][0]==0x10241cc7)&&
       v->tmu[1][0]==0x10241cc7&&v->regs[0x134/4]==0&&
       v->regs[0x138/4]==0x10000000){
        textured=1;texture_unit=v->tmu[0][0]==0xcc7?1:0;texture_format=12;
    }
    /* The second ALWAYS-depth additive variant uses DST_COLOR/ONE.
     * Alpha masking cannot remove its RGB contribution. With black fog,
     * filtered keyed black remains zero RGB and is an exact blend no-op;
     * coloured fog requires the binary post-filter predicate below. */
    if(cp==0x1c482405&&((cmd>>10)&255)==0x3b&&fbz==0x213fb&&
       alpha==0x4421f&&fog_enabled&&v->tmu[0][0]==0x10241cc7&&
       v->tmu[1][0]==0x10241cc7&&v->regs[0x134/4]==0&&
       v->regs[0x138/4]==0x10000000){
        textured=1;texture_unit=0;texture_format=12;
    }
    }
#ifdef VIPER_WII_GX_COLOR_EQUATION
    if(full_pipeline){textured=pipeline.textured;family_selected=1;}
#endif
    int iterated_mask=0,iterated_chroma=0,chroma_encode=0;
#ifdef VIPER_WII_GX_CHROMA_APPROX
    iterated_mask=!textured&&((cmd>>10)&255)==0x0b&&
        wii_iterated_mask_family(cp,fbz,alpha,v->regs[0x108/4],
            v->regs[0x134/4],v->regs[0x138/4]);
    iterated_chroma=!textured&&((cmd>>10)&255)==0x0b&&
        wii_iterated_chroma_family(cp,fbz,alpha,v->regs[0x108/4],
            v->regs[0x134/4],v->regs[0x138/4]);
    if(cp==0x15024100&&fbz==0x217b&&((cmd>>10)&255)==0x0b){
        static int reported;
        if(!reported){reported=1;rt_log("VIPER WII ITERATED ADMISSION textured=%d accepted=%d alpha=%08lx key=%08lx range=%08lx\n",
            textured,iterated_mask,(unsigned long)alpha,
            (unsigned long)v->regs[0x134/4],(unsigned long)v->regs[0x138/4]);}
    }
#endif
    if(!generic_pipeline&&!textured&&!iterated_mask&&!iterated_chroma&&((cp&~(1u<<26))!=0x11024100||((cmd>>10)&255)!=0x0b)){
        for(unsigned i=0;i<3;i++)
            rt_log("VIPER WII GX UNSUPPORTED V%u xy=%.9g,%.9g rgba=%.9g,%.9g,%.9g,%.9g wb=%.9g\n",
                i,(double)p[i].x,(double)p[i].y,(double)p[i].r,(double)p[i].g,
                (double)p[i].b,(double)p[i].a,(double)p[i].wb);
        unsupported(v,cmd,"GX textured/colour combiner pending");
    }
    if((v->regs[0x108/4]&1)&&!fog_enabled)unsupported(v,cmd,"GX fog pending");
#ifdef VIPER_WII_GX_DISABLE_FOG
    /* Fog colour is off on Wii. Guest registers, admission and rejection stay.
     * Depth lookup and splitting still run wherever the framebuffer needs them. */
    fog_enabled=0;
#endif
    if(fbz&((1u<<2)|(1u<<16)|(1u<<18)|(1u<<19)|(1u<<20)|(1u<<21)))unsupported(v,cmd,"GX stipple/depth-bias/alpha-plane state");
#ifndef VIPER_WII_GX_EARLY_CULL
    float area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
#endif
    if(!isfinite(area))unsupported(v,cmd,"GX nonfinite geometry");
    if(area==0)return;
    /* Y-origin subtraction can collapse a clipped sliver to identical GX
     * positions even when its source coordinates still have nonzero area.
     * Test the actual submitted float positions in double before solving any
     * texture/fog/depth planes. Such a primitive has no rasterizable area. */
    double y0=screen_y(v,p[0].y),y1=screen_y(v,p[1].y),y2=screen_y(v,p[2].y);
    double submitted_area=((double)p[1].x-p[0].x)*(y2-y0)-
        (y1-y0)*((double)p[2].x-p[0].x);
    if(submitted_area==0)return;
    if(cmd&(1u<<23)) {
        unsigned sign=(cmd>>24)&1;
        if(!(cmd&(1u<<22))&&!(cmd&(1u<<25))){
            if(v->strip_count<3)unsupported(v,cmd,"GX invalid strip setup count");
            sign^=(v->strip_count-3)&1;
        }
        if((area<0)==sign)return;
    }
    GXColor colors[3];
    int varying_color=textured&&(family_selected||cp==0x1d022401||cp==0x1c484104||texture_format==11||
        ((cmd>>10)&255)==0x3b||(cp==0x1c482405&&fbz==0x2136b));
    if(iterated_mask||iterated_chroma||generic_pipeline)varying_color=1;
    /* Race geometry also uses interpolated untextured vertex RGBA. This
     * captured state has neither chroma rejection nor an alpha-plane mask,
     * so GX_PASSCLR can interpolate it without the uniform-colour shortcuts
     * below. Byte interpolation remains the same documented approximation. */
    if(!generic_pipeline&&!textured&&cp==0x15024100&&((cmd>>10)&255)==0x0b&&
       fbz==0x21379&&alpha==0x0004411f&&known_fog)
        varying_color=1;
    /* The captured no-blend transition proof: the entire primitive
     * passes alpha>12 and cannot hit the black key, or it is wholly rejected.
     * Mixed coverage retains the guard rather than using a uniform shortcut. */
    if(!generic_pipeline&&!textured&&cp==0x15024100&&((cmd>>10)&255)==0x0b&&
       ((cmd>>3)&7)<=2&&fbz==0x2132b&&alpha==0x0c045109&&
       v->regs[0x108/4]==0x40&&v->regs[0x134/4]==0&&
       v->regs[0x138/4]==0x10000000){
        int all_fail=1,all_pass=1,red=1,green=1,blue=1,black=1;
        for(unsigned i=0;i<3;i++){
            if(!isfinite(p[i].a)||!isfinite(p[i].r)||!isfinite(p[i].g)||!isfinite(p[i].b))
                unsupported(v,cmd,"GX transition nonfinite colour");
            all_fail&=p[i].a<=12;all_pass&=p[i].a>=13;
            red&=p[i].r>=1;green&=p[i].g>=1;blue&=p[i].b>=1;
            black&=p[i].r<=0&&p[i].g<=0&&p[i].b<=0;
        }
        if(all_fail||black)return;
        if(!all_pass||!(red||green||blue))unsupported(v,cmd,"GX transition mixed alpha/chroma coverage");
        varying_color=1;
    }
    for(unsigned i=0;i<3;i++){
        const float components[4]={p[i].r,p[i].g,p[i].b,p[i].a};
        if(!isfinite(p[i].x)||!isfinite(p[i].y))unsupported(v,cmd,"GX nonfinite geometry");
        if(!varying_color&&(p[i].r!=p[0].r||p[i].g!=p[0].g||p[i].b!=p[0].b||p[i].a!=p[0].a)){
            for(unsigned j=0;j<3;j++)rt_log("VIPER WII GX VARYING V%u rgba=%.9g,%.9g,%.9g,%.9g\n",j,
                (double)p[j].r,(double)p[j].g,(double)p[j].b,(double)p[j].a);
            unsupported(v,cmd,"GX varying colour setup pending");
        }
        u8 channels[4];
        for(unsigned j=0;j<4;j++){
            if(!isfinite(components[j])||components[j]<=-2048||components[j]>=2048)unsupported(v,cmd,"GX overflowing colour setup pending");
            channels[j]=components[j]<=0?0:components[j]>=255?255:(u8)components[j];
        }
        colors[i]=(GXColor){channels[0],channels[1],channels[2],channels[3]};
        if((fbz&256)&&((channels[0]!=0&&channels[0]!=255)||(channels[1]!=0&&channels[1]!=255)||(channels[2]!=0&&channels[2]!=255))){
#ifdef VIPER_WII_GX_DITHER_APPROX
            dither_approximations++;
            if(!dither_reported){dither_reported=1;rt_log("VIPER WII GX APPROX Voodoo ordered RGB565 dithering omitted; pixel equivalence unproven\n");}
#else
            unsupported(v,cmd,"GX dithered triangle pending");
#endif
        }
    }
    /* GX interpolates byte vertex colours; Voodoo x.12 setup precision differs. */
    GXColor color=colors[0];
    if(iterated_chroma&&!generic_pipeline){
        int red=1,green=1,blue=1,black=1,positive_alpha=1;
        for(unsigned i=0;i<3;i++){
            red&=colors[i].r>=1;green&=colors[i].g>=1;blue&=colors[i].b>=1;
            black&=!(colors[i].r|colors[i].g|colors[i].b);
            positive_alpha&=colors[i].a>=1;
        }
        if(black)return;
        if(!(red||green||blue)){
            if(!positive_alpha)unsupported(v,cmd,"GX mixed chroma with zero-alpha depth survivor");
            chroma_encode=1;
        }
    }
    GX_SetDither(GX_FALSE);
    if(!generic_pipeline&&!textured&&(fbz&2)&&!iterated_mask&&!iterated_chroma) {
        if(v->regs[0x134/4]!=0||v->regs[0x138/4]!=0x10000000)unsupported(v,cmd,"GX chroma range pending");
        if(!(color.r|color.g|color.b))return; /* exact uniform black rejection */
    }
    if((fbz&(1u<<13))&&!generic_pipeline){
        if(textured&&cp!=0x1d022401){
            if(!(cp==0x1c482405&&fbz==0x217b&&alpha==0x0004511f&&texture_format==11))
                unsupported(v,cmd,"GX filtered texture alpha-mask pending");
        }else if(!iterated_mask&&!(color.a&1))return;
    }
    float depth=0,depths[3];
    int linear_depth=0;
    if((fbz&(16|1024))||fog_enabled) {
        if((fbz&(16|1024))&&!(fbz&8))unsupported(v,cmd,"GX Z-buffer source pending");
        int varying=p[1].wb!=p[0].wb||p[2].wb!=p[0].wb;
        int constant_w=0;
        for(unsigned i=0;i<3;i++)if(!isfinite(p[i].wb)||p[i].wb<0)unsupported(v,cmd,"GX invalid W-depth");
#ifdef VIPER_WII_WDEPTH_LINEAR
        /* Voodoo interpolates wb (1/W) affinely in screen space and stores a
         * monotonic float encoding of it, so a per-vertex Z of 1-wb gives GX
         * the same per-pixel depth order; only quantisation differs. Vertices
         * nearer than W=1 saturate on Voodoo: those triangles still go through
         * the band split, and only the saturated band keeps a constant 0. */
        if(active_depth_band!=65&&p[0].wb<=1&&p[1].wb<=1&&p[2].wb<=1){
            /* No tie bias: stored Z must stay exactly 1-wb so EQUAL passes
             * (the game draws them over LESS/LEQUAL bases) match it.
             * run.HyRUFz showed a bias moves no colour pixels anyway. */
            for(unsigned i=0;i<3;i++)depths[i]=1.f-p[i].wb;
            linear_depth=1;
            varying=0;
#if defined(VIPER_WII_DEPTH_TRACE) || defined(VIPER_WII_FRAME_CAPTURE)
            for(unsigned i=0;i<3;i++){float w=p[i].wb;if(w<depth_trace_min)depth_trace_min=w;if(w>depth_trace_max&&w<0.99f)depth_trace_max=w;
             int e=w>0?-(int)floorf(log2f(w)):31;if(e>31)e=31;if(e<0)e=0;depth_trace_hist[e]++;}
            {float a=fabsf(p[1].wb-p[0].wb)+fabsf(p[2].wb-p[0].wb);if(a<1e-12f)depth_trace_flat++;}
#endif
        }
#endif
#ifdef VIPER_WII_WDEPTH_CONSTANT
        /* One Z for the whole triangle, from the average W. No band split
         * and no depth-lookup texture. Occlusion is coarser. */
        if(varying&&active_depth_band<0){
            float w=(p[0].wb+p[1].wb+p[2].wb)*(1.f/3.f);
            unsigned d=w>1.f?0u:wdepth(w);
            if(d>65535u)unsupported(v,cmd,"GX W-depth overflow");
            depth=d/65535.0f;
            constant_w=1;
            varying=0;
        }
#endif
#ifdef VIPER_WII_GX_WDEPTH_APPROX
        if(varying&&active_depth_band<0){
            unsigned count;
            uint64_t start=gettime();
            if(!wii_wdepth_split(p,depth_pieces,WII_WDEPTH_SPLIT_MAX,&count)){
                for(unsigned i=0;i<3;i++)rt_log("VIPER WII SPLIT V%u x=%a y=%a rgba=%a,%a,%a,%a wb=%a st=%a,%a z=%a w01=%a,%a st1=%a,%a\n",i,
                    p[i].x,p[i].y,p[i].r,p[i].g,p[i].b,p[i].a,p[i].wb,p[i].s,p[i].t,p[i].z,p[i].w0,p[i].w1,p[i].s1,p[i].t1);
                /* Raw float bits remain recoverable even if the SD log fails. */
                char detail[1024];unsigned offset=snprintf(detail,sizeof detail,"GX W-depth split failed cmd=%08lx\nfields x y r g b a wb s t z w0 w1 s1 t1\n",(unsigned long)cmd);
                for(unsigned i=0;i<3;i++){
                    const float fields[]={p[i].x,p[i].y,p[i].r,p[i].g,p[i].b,p[i].a,p[i].wb,p[i].s,p[i].t,p[i].z,p[i].w0,p[i].w1,p[i].s1,p[i].t1};
                    offset+=snprintf(detail+offset,sizeof detail-offset,"V%u:",i);
                    for(unsigned j=0;j<14;j++){uint32_t bits;memcpy(&bits,&fields[j],sizeof bits);offset+=snprintf(detail+offset,sizeof detail-offset," %08lx",(unsigned long)bits);}
                    offset+=snprintf(detail+offset,sizeof detail-offset,"\n");
                }
                rt_fatal(detail);
            }
            profile.split_us+=ticks_to_microsecs(gettime()-start);profile.split_children+=count;
            if(!wdepth_approximations)rt_log("VIPER WII GX APPROX varying W-depth interval lookup; GX coordinate and boundary rounding unverified\n");
            wdepth_approximations++;
            for(unsigned i=0;i<count;i++){
                active_depth_band=(int)depth_pieces[i].band;
                triangle(user,v,depth_pieces[i].v,cmd);
            }
            active_depth_band=-1;return;
        }
        if(active_depth_band==64)depth=1;
        else if(active_depth_band==65)depth=0;
        else
#else
        if(varying||p[0].wb>1)unsupported(v,cmd,"GX varying W-depth pending");
#endif
        if(!constant_w){unsigned d=p[0].wb>1?0:wdepth(p[0].wb);if(d>65535)unsupported(v,cmd,"GX W-depth overflow");depth=d/65535.0f;}
    }
    if(!linear_depth)depths[0]=depths[1]=depths[2]=depth;
    uint64_t setup_clock=setup_start();
#if defined(VIPER_WII_PARENT_PROJECTION) && defined(VIPER_WII_GX_WDEPTH_APPROX)
    if(!parent_projection_ready){
        projection(v);clip(v,fbz&1);parent_projection_ready=1;
    }
#else
    projection(v);clip(v,fbz&1);
#endif
    setup_end(0,setup_clock);
    setup_clock=setup_start();
    GX_SetColorUpdate(!!(fbz&512));GX_SetAlphaUpdate(GX_FALSE);
    GX_SetZCompLoc(GX_FALSE);GX_SetZMode(!!(fbz&(16|1024)),fbz&16?compare[(fbz>>5)&7]:GX_ALWAYS,!!(fbz&1024));
    GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,GX_AOP_AND,GX_ALWAYS,0);
    if(alpha&16)GX_SetBlendMode(GX_BM_BLEND,blend_factor(v,(alpha>>8)&15,1),blend_factor(v,(alpha>>12)&15,0),GX_LO_COPY);
    else GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    setup_end(1,setup_clock);setup_clock=setup_start();
#ifdef VIPER_WII_GX_DISABLE_TEXTURES
    /* Game maps are off. Vertex colour still draws, and depth lookup stays.
     * Texture chroma keys and alpha masks are not sampled. */
    textured=0;
    full_pipeline=0;
    generic_pipeline=0;
#endif
    float us=0,vs=0;int projective=0;unsigned color_stages=1;
#ifdef VIPER_WII_VERTEX_STQ
    float stq[2][3][3];unsigned stq_units=0;
#endif
    /* Each recursive W-band child submits its own complete draw(s). */
    while(texture_pin_count)texture_pinned[texture_pin_list[--texture_pin_count]]=0;
#ifdef VIPER_WII_GX_COLOR_EQUATION
    unsigned generic_rejection_first=0;
    u8 generic_rgb_source[]={GX_CC_RASC,GX_CC_TEXC,GX_CC_C2,GX_CC_ZERO};
    u8 generic_alpha_source[]={GX_CA_RASA,GX_CA_TEXA,GX_CA_A2,GX_CA_ZERO};
#endif
#ifdef VIPER_WII_GX_TMU_PIPELINE
    if(full_pipeline&&!tmu_pipeline.texture_zero){
        /* Both images stay pinned until these vertices and any depth pass
         * are submitted. TMU1 uses map3/coord2; map1 stays reserved for depth. */
#ifndef VIPER_WII_GX_NATIVE_TEXTURE_BIND
        texture_off();
#endif
        projective=1;
#ifdef VIPER_WII_MATERIAL_RUN
        int material_resume=wii_material_run_resume(&material_run,material_plan);
        int ident_resume=0;
#ifdef VIPER_WII_COMBINER_KEEP
        uint32_t material_ident=wii_material_plan_ident(&material_plans,material_plan);
        if(!material_resume)material_resume=ident_resume=
            wii_material_run_resume_ident(&material_run,material_plan,material_ident);
#endif
        if(material_resume)material_run.resumes++;
        /* Speed track allows the skip. Earlier exact runs elTGje and L8cffp
         * wrote one wrong RGB8 image; that picture change is noted, not hidden.
         * run.U39oOB on the constant-depth stack changed 192 colour pixels. */
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
        int skip_texture_gx=material_resume&&!ident_resume&&material_bind_valid;
        if(!material_resume||ident_resume)material_bind_valid=0;
#else
        int skip_texture_gx=0;(void)ident_resume;
#endif
#else
        int skip_texture_gx=0;
#endif
        if(!skip_texture_gx){
            GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
            GX_SetTexCoordGen(GX_TEXCOORD2,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
        }
        for(int unit=1;unit>=0;unit--)if(tmu_pipeline.unit[unit].use){
            const WiiTMUUnitPlan *u=&tmu_pipeline.unit[unit];
            u8 coord=unit?GX_TEXCOORD2:GX_TEXCOORD0,map=unit?GX_TEXMAP3:GX_TEXMAP0;
            if(u->local_needed){
                float scale_s,scale_t;
                int unit_skip=0;
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
                /* Same plan is not same texels: palette and texture memory
                 * writes do not touch the plan, so re-check the bound slot. */
                /* Unchanged device texture epoch means no texture-readable
                 * write since the bind: skip the full re-check. */
                unit_skip=skip_texture_gx&&(material_bind_valid&(1u<<unit))&&
                    (material_bind_epoch[unit]==wii_voodoo_texture_epoch||
                     (material_bind_palette[unit]==v->palette_epoch[unit]&&
                      material_bind_slot[unit]<COLOR_CACHE_SLOTS&&
                      wii_texture_cache_matches(&texture_cache[material_bind_slot[unit]],
                          &material_bind_key[unit],v->vram_versions)));
                if(unit_skip)material_bind_epoch[unit]=wii_voodoo_texture_epoch;
                if(unit_skip){
                    scale_s=material_bind_scale_s[unit];
                    scale_t=material_bind_scale_t[unit];
                    texture_pin(material_bind_slot[unit]);
                }else
#endif
                {
                bind_flat_texture(v,cmd,unit,u->format,coord,map,&scale_s,&scale_t,1);
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
                material_bind_scale_s[unit]=scale_s;
                material_bind_scale_t[unit]=scale_t;
                material_bind_slot[unit]=texture_last_slot;
                if(texture_last_slot<COLOR_CACHE_SLOTS){
                    material_bind_key[unit]=texture_cache[texture_last_slot].key;
                    material_bind_palette[unit]=v->palette_epoch[unit];
                    material_bind_epoch[unit]=wii_voodoo_texture_epoch;
                    material_bind_valid|=1u<<unit;
                }else material_bind_valid&=~(1u<<unit);
#endif
                }
#ifdef VIPER_WII_VERTEX_STQ
                (void)unit_skip;
                for(unsigned i=0;i<3;i++){
                    float s=unit?p[i].s1:p[i].s,t=unit?p[i].t1:p[i].t;
                    float q=u->perspective?(unit?p[i].w1:p[i].w0):1;
                    if(!isfinite(s)||!isfinite(t)||!isfinite(q)||q<=0)
                        unsupported(v,cmd,"GX TMU texture STQ invalid");
                    stq[unit][i][0]=s*scale_s;stq[unit][i][1]=t*scale_t;stq[unit][i][2]=q;
                }
                stq_units|=1u<<unit;
#else
                WiiProjectiveVertex vertices[3];Mtx matrix;
                for(unsigned i=0;i<3;i++){
                    float s=unit?p[i].s1:p[i].s,t=unit?p[i].t1:p[i].t;
                    float q=u->perspective?(unit?p[i].w1:p[i].w0):1;
                    if(!isfinite(s)||!isfinite(t)||!isfinite(q)||q<=0)
                        unsupported(v,cmd,"GX TMU texture STQ invalid");
                    vertices[i]=(WiiProjectiveVertex){p[i].x,screen_y(v,p[i].y),s,t,q};
                }
                if(!solve_plane(matrix,vertices,scale_s,scale_t,0))
                    unsupported(v,cmd,"GX TMU texture plane failed");
                GX_LoadTexMtxImm(matrix,unit?GX_TEXMTX2:GX_TEXMTX0,GX_MTX3x4);
                if(!unit_skip)
                    GX_SetTexCoordGen(coord,GX_TG_MTX3x4,GX_TG_POS,unit?GX_TEXMTX2:GX_TEXMTX0);
#endif
            }
        }
#ifdef VIPER_WII_VERTEX_STQ
        /* Always set both coordinates: another draw path may have left either
         * on a different source, and a source must exist in this vertex. */
        GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,(stq_units&1)?GX_TG_NRM:GX_TG_POS,(stq_units&1)?STQ_IDENTITY:GX_IDENTITY);
        GX_SetTexCoordGen(GX_TEXCOORD2,GX_TG_MTX3x4,(stq_units&2)?(stq_units==3?GX_TG_BINRM:GX_TG_NRM):GX_TG_POS,(stq_units&2)?STQ_IDENTITY:GX_IDENTITY);
#endif
        if(!skip_texture_gx){
            GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(3);
        }
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
        uint32_t c0=v->regs[0x144/4],c1=v->regs[0x148/4];
        int combiner_fault=0;
#ifdef VIPER_WII_COMBINER_KEEP
        combiner_cache.ident=wii_material_plan_ident(&material_plans,material_plan);
#endif
        unsigned end=wii_gx_combiner_program_emit(&combiner_cache,material_plan,
            (GXColor){c0>>16,c0>>8,c0,c0>>24},(GXColor){c1>>16,c1>>8,c1,c1>>24},&combiner_fault);
        if(combiner_fault==1)unsupported(v,cmd,"GX planned TMU stage budget");
        if(combiner_fault==2)unsupported(v,cmd,"GX TMU plan/emission mismatch");
        if(combiner_fault==4)unsupported(v,cmd,"GX FBI program plan/emission mismatch");
        if(!end)unsupported(v,cmd,"GX planned TMU/FBI stage budget");
#else
        unsigned end=0;
        for(int unit=1;unit>=0;unit--)if(tmu_pipeline.unit[unit].use){
            const WiiTMUUnitPlan *u=&tmu_pipeline.unit[unit];
            GX_SetTevKColor(unit?GX_KCOLOR2:GX_KCOLOR3,
                (GXColor){u->detail_factor,u->detail_factor,u->detail_factor,u->lod_fraction});
            int other=unit==0&&tmu_pipeline.unit[1].use;
            end=wii_gx_tmu_equation(u->equation,end,
                u->local_needed?(unit?GX_TEXCOORD2:GX_TEXCOORD0):GX_TEXCOORDNULL,
                u->local_needed?(unit?GX_TEXMAP3:GX_TEXMAP0):GX_TEXMAP_NULL,
                other?GX_CC_C0:GX_CC_ZERO,other?GX_CA_A0:GX_CA_ZERO,
                other?GX_CC_A0:GX_CC_ZERO,GX_TEVREG0,
                unit?GX_TEV_KCSEL_K2:GX_TEV_KCSEL_K3,
                unit?GX_TEV_KASEL_K2_R:GX_TEV_KASEL_K3_R,
                unit?GX_TEV_KCSEL_K2_A:GX_TEV_KCSEL_K3_A,
                unit?GX_TEV_KASEL_K2_A:GX_TEV_KASEL_K3_A);
            if(!end)unsupported(v,cmd,"GX planned TMU stage budget");
        }
        if(end!=tmu_pipeline.fbi_first)unsupported(v,cmd,"GX TMU plan/emission mismatch");
        uint32_t c0=v->regs[0x144/4],c1=v->regs[0x148/4];
        end=wii_gx_color_equation_at(pipeline.equation,
            (GXColor){c0>>16,c0>>8,c0,c0>>24},(GXColor){c1>>16,c1>>8,c1,c1>>24},
            end,GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
        if(!end)unsupported(v,cmd,"GX planned TMU/FBI stage budget");
#endif
        {
#ifdef VIPER_WII_MATERIAL_RUN
        if(material_resume){
            color_stages=material_run.color_stages;
            generic_rgb_source[1]=GX_CC_C2;generic_alpha_source[1]=GX_CA_A2;
            generic_rejection_first=tmu_pipeline.rejection_first;
            if(pipeline.key||pipeline.mask){
                /* The skipped suffix set this count via wii_gx_rejection; the
                 * combiner hit only restores the FBI end. Without it keyed
                 * texels draw whenever no later depth/fog stage resets it. */
                GX_SetNumTevStages(color_stages);
                if(pipeline.rejection==WII_REJECT_BINARY)
                    GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
                else if(pipeline.rejection==WII_REJECT_ADD_NONZERO||pipeline.rejection==WII_REJECT_SPLIT_DEPTH)
                    GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,GX_AOP_AND,GX_GREATER,0);
            }
        }else{
#endif
        if(tmu_pipeline.snapshot_stages){
            /* Only replace components whose original OTHER is texture.
             * Keep color1 components for mixed predicate source selection. */
            GX_SetTevOrder(end,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
            GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,
                pipeline.equation.other_rgb==1?GX_CC_C0:GX_CC_C2);
            GX_SetTevAlphaIn(end,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,
                pipeline.equation.other_alpha==1?GX_CA_A0:GX_CA_A2);
            GX_SetTevColorOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG2);
            GX_SetTevAlphaOp(end++,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG2);
        }
        generic_rgb_source[1]=GX_CC_C2;generic_alpha_source[1]=GX_CA_A2;
        generic_rejection_first=end;
        if(end!=tmu_pipeline.rejection_first)unsupported(v,cmd,"GX predicate plan/emission mismatch");
        if(pipeline.key||pipeline.mask){
            end=wii_gx_rejection(end,generic_rgb_source[pipeline.equation.other_rgb],
                generic_alpha_source[pipeline.equation.other_alpha],0,pipeline.key,
                pipeline.rejection==WII_REJECT_BINARY,pipeline.mask);
            if(!end)unsupported(v,cmd,"GX planned TMU predicate budget");
            if(pipeline.rejection==WII_REJECT_BINARY)
                GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
            else if(pipeline.rejection==WII_REJECT_ADD_NONZERO||pipeline.rejection==WII_REJECT_SPLIT_DEPTH)
                GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,GX_AOP_AND,GX_GREATER,0);
        }
        color_stages=end;
#ifdef VIPER_WII_MATERIAL_RUN
        wii_material_run_publish(&material_run,material_plan,color_stages);
#ifdef VIPER_WII_COMBINER_KEEP
        wii_material_run_publish_ident(&material_run,material_ident);
#endif
        }
#endif
        }
        profile.tmu_pipeline_draws++;
        if(tmu_pipeline.unit[0].local_needed&&tmu_pipeline.unit[1].local_needed&&
           tmu_pipeline.unit[0].use&&tmu_pipeline.unit[1].use)profile.dual_texture_draws++;
    }else
#endif
    if(textured){
        WiiProjectiveVertex pv[3];
        for(unsigned i=0;i<3;i++){
            float s=texture_unit?p[i].s1:p[i].s,t=texture_unit?p[i].t1:p[i].t;
            float q=texture_unit?p[i].w1:p[i].w0;
            if(!isfinite(s)||!isfinite(t)||!isfinite(q)||q<=0)unsupported(v,cmd,"GX invalid texture STQ");
            pv[i]=(WiiProjectiveVertex){p[i].x,screen_y(v,p[i].y),s,t,q};
            if(q!=1)projective=1;
        }
        bind_flat_texture(v,cmd,texture_unit,texture_format,GX_TEXCOORD0,GX_TEXMAP0,&us,&vs,0);
        unsigned chroma_stage=1;
#ifdef VIPER_WII_GX_COLOR_EQUATION
        uint32_t c0=v->regs[0x144/4],c1=v->regs[0x148/4];
        chroma_stage=wii_gx_color_equation(wii_voodoo_color_plan(cp),
            (GXColor){c0>>16,c0>>8,c0,c0>>24},(GXColor){c1>>16,c1>>8,c1,c1>>24});
        if(!chroma_stage)unsupported(v,cmd,"GX equation source unsupported");
        /* Reserve chroma, five mask stages, fog and depth before emitting. */
        if(chroma_stage+8>16)unsupported(v,cmd,"GX equation stage budget");
#else
        if(cp==0x1c484104){
            GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_RASC);
            GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        }
        if(cp==0x1d022401){
            /* This colourpath selects iterated alpha independently of TMU alpha. */
            GX_SetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_RASA);
            GX_SetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        }
#endif
#ifdef VIPER_WII_GX_COLOR_EQUATION
        if(generic_pipeline){
            color_stages=chroma_stage;
            generic_rejection_first=chroma_stage;
            if(pipeline.key||pipeline.mask){
                const u8 rgb_source[]={GX_CC_RASC,GX_CC_TEXC,GX_CC_C2,GX_CC_ZERO};
                const u8 alpha_source[]={GX_CA_RASA,GX_CA_TEXA,GX_CA_A2,GX_CA_ZERO};
                color_stages=wii_gx_rejection(chroma_stage,rgb_source[pipeline.equation.other_rgb],
                    alpha_source[pipeline.equation.other_alpha],1,pipeline.key,
                    pipeline.rejection==WII_REJECT_BINARY,pipeline.mask);
                if(!color_stages)unsupported(v,cmd,"GX planned rejection stage budget");
                if(pipeline.rejection==WII_REJECT_BINARY)
                    GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
                else if(pipeline.rejection==WII_REJECT_ADD_NONZERO||pipeline.rejection==WII_REJECT_SPLIT_DEPTH)
                    GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,
                        GX_AOP_AND,GX_GREATER,0);
            }
        }else
#endif
        {
        /* Compare filtered texture RGB against black; retain the original alpha
         * for surviving fragments. Validated by chroma_probe's alpha/depth holes. */
        GX_SetNumTevStages(chroma_stage+1);
        GX_SetTevOrder(chroma_stage,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
        GX_SetTevColorIn(chroma_stage,(fbz&2)?(cp==0x1c484104?GX_CC_RASC:GX_CC_TEXC):GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
        GX_SetTevColorOp(chroma_stage,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GX_SetTevAlphaIn(chroma_stage,GX_CA_ZERO,GX_CA_ZERO,(fbz&2)?GX_CA_APREV:GX_CA_ZERO,(fbz&2)?GX_CA_ZERO:GX_CA_APREV);
        if(family_binary_chroma||(cp==0x1d022401&&fbz==0x2176b&&alpha==0x0004510f)||
           (cp==0x1d022401&&fbz==0x2175b&&alpha==0x4221f)||
           (cp==0x1c482405&&fbz==0x213fb&&alpha==0x4421f)){
            /* These ALWAYS-alpha states ignore source alpha in blending and
             * disable alpha-plane writes. Binary rejection also prevents
             * keyed fragments from writing depth in the RGB565 EQUAL case.
             * A binary predicate rejects keyed fragments without rejecting
             * valid nonblack fragments whose original alpha happens to be0. */
            GX_SetTevKColor(GX_KCOLOR0,(GXColor){0,0,0,255});
            GX_SetTevKAlphaSel(chroma_stage,GX_TEV_KASEL_K0_A);
            GX_SetTevAlphaIn(chroma_stage,GX_CA_ZERO,GX_CA_ZERO,GX_CA_KONST,GX_CA_ZERO);
            GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
        }
        int texture_mask=(fbz&(1u<<13))&&cp!=0x1d022401;
        GX_SetTevAlphaOp(chroma_stage,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,texture_mask?GX_TEVREG0:GX_TEVPREV);
        if(family_reject_zero_alpha)
            GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,
                GX_AOP_AND,GX_GREATER,0);
        color_stages=chroma_stage+1;
        if(texture_mask){
            /* Re-read unclamped alpha through an 8-bit TEV input at each step.
             * Multiplication by 128 leaves low8=128 for odd inputs, zero for
             * even inputs. REG0 retains modulated/chroma-gated original alpha. */
            for(unsigned stage=chroma_stage+1;stage<=chroma_stage+4;stage++){
                GX_SetTevOrder(stage,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
                GX_SetTevColorIn(stage,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
                GX_SetTevColorOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
                GX_SetTevAlphaIn(stage,stage==chroma_stage+1?GX_CA_TEXA:GX_CA_A1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO);
                GX_SetTevAlphaOp(stage,GX_TEV_ADD,GX_TB_ZERO,stage==chroma_stage+4?GX_CS_SCALE_2:GX_CS_SCALE_4,GX_FALSE,GX_TEVREG1);
            }
            GX_SetNumTevStages(chroma_stage+6);
            GX_SetTevOrder(chroma_stage+5,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
            GX_SetTevColorIn(chroma_stage+5,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
            GX_SetTevColorOp(chroma_stage+5,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
            GX_SetTevAlphaIn(chroma_stage+5,GX_CA_A1,GX_CA_ZERO,GX_CA_A0,GX_CA_ZERO);
            GX_SetTevAlphaOp(chroma_stage+5,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
            /* This guarded state writes no depth and blends source alpha:
             * rejecting zero-output survivors is observationally neutral. */
            GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
            color_stages=chroma_stage+6;
        }
        /* Voodoo truncates modulation; GX rounds. For uniform iterated alpha,
         * compare raw filtered alpha with the algebraically equivalent cutoff.
         * This fixes rejection/depth holes, while blended alpha still rounds. */
        if((cp==0x1c482405||cp==0x1c484104)&&(alpha&15)==9&&
           p[0].a==p[1].a&&p[0].a==p[2].a&&color.a!=255){
            unsigned cutoff=(((alpha>>24)+1)*256+color.a)/(color.a+1);
            if(cutoff>255)return;
            GX_SetNumTevStages(chroma_stage+2);
            GX_SetTevOrder(chroma_stage+1,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
            GX_SetTevColorIn(chroma_stage+1,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
            GX_SetTevColorOp(chroma_stage+1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
            GX_SetTevKColor(GX_KCOLOR0,(GXColor){0,0,0,(u8)(cutoff-1)});
            GX_SetTevKAlphaSel(chroma_stage+1,GX_TEV_KASEL_K0_A);
            GX_SetTevAlphaIn(chroma_stage+1,GX_CA_TEXA,GX_CA_KONST,GX_CA_APREV,GX_CA_ZERO);
            GX_SetTevAlphaOp(chroma_stage+1,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
            GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
            color_stages=chroma_stage+2;
        }
        }
        if(projective){
            Mtx matrix;
            if(!solve_plane(matrix,pv,us,vs,0)){
                rt_log("VIPER WII GX PROJECTIVE FAILURE band=%d scale=%.9g/%.9g\n",
#ifdef VIPER_WII_GX_WDEPTH_APPROX
                    active_depth_band,
#else
                    -1,
#endif
                    (double)us,(double)vs);
                for(unsigned i=0;i<3;i++)rt_log("VIPER WII GX PROJECTIVE V%u xy=%.9g,%.9g stq=%.9g,%.9g,%.9g\n",
                    i,(double)pv[i].x,(double)pv[i].y,(double)pv[i].s,(double)pv[i].t,(double)pv[i].w);
                char failure[512];
                snprintf(failure,sizeof failure,"GX projective plane failed cmd=%08lx scale=%.9g/%.9g\n"
                    "V0 xy=%.9g,%.9g stq=%.9g,%.9g,%.9g\n"
                    "V1 xy=%.9g,%.9g stq=%.9g,%.9g,%.9g\n"
                    "V2 xy=%.9g,%.9g stq=%.9g,%.9g,%.9g",
                    (unsigned long)cmd,(double)us,(double)vs,
                    (double)pv[0].x,(double)pv[0].y,(double)pv[0].s,(double)pv[0].t,(double)pv[0].w,
                    (double)pv[1].x,(double)pv[1].y,(double)pv[1].s,(double)pv[1].t,(double)pv[1].w,
                    (double)pv[2].x,(double)pv[2].y,(double)pv[2].s,(double)pv[2].t,(double)pv[2].w);
                rt_fatal(failure);
            }
            GX_LoadTexMtxImm(matrix,GX_TEXMTX0,GX_MTX3x4);
            GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);
            GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX0);
        }
    }else{
        texture_off();
#ifdef VIPER_WII_GX_COLOR_EQUATION
        if(generic_pipeline){
            uint32_t c0=v->regs[0x144/4],c1=v->regs[0x148/4];
            color_stages=wii_gx_color_equation_at(pipeline.equation,
                (GXColor){c0>>16,c0>>8,c0,c0>>24},(GXColor){c1>>16,c1>>8,c1,c1>>24},
                0,GX_CC_ZERO,GX_CA_ZERO,GX_CC_ZERO,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
            if(!color_stages)unsupported(v,cmd,"GX untextured equation stage budget");
            generic_rejection_first=color_stages;
            if(pipeline.key||pipeline.mask){
                const u8 rgb_source[]={GX_CC_RASC,GX_CC_ZERO,GX_CC_C2,GX_CC_ZERO};
                const u8 alpha_source[]={GX_CA_RASA,GX_CA_ZERO,GX_CA_A2,GX_CA_ZERO};
                color_stages=wii_gx_rejection(color_stages,rgb_source[pipeline.equation.other_rgb],
                    alpha_source[pipeline.equation.other_alpha],0,pipeline.key,
                    pipeline.rejection==WII_REJECT_BINARY,pipeline.mask);
                if(!color_stages)unsupported(v,cmd,"GX untextured predicate stage budget");
                if(pipeline.rejection==WII_REJECT_BINARY)
                    GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
                else if(pipeline.rejection==WII_REJECT_ADD_NONZERO||pipeline.rejection==WII_REJECT_SPLIT_DEPTH)
                    GX_SetAlphaCompare(alpha&1?compare[(alpha>>1)&7]:GX_ALWAYS,alpha>>24,
                        GX_AOP_AND,GX_GREATER,0);
            }
        }else
#endif
        {
        if(chroma_encode){
            color_stages=wii_gx_rejection(1,GX_CC_RASC,GX_CA_RASA,0,1,0,0);
            if(!color_stages)unsupported(v,cmd,"GX iterated chroma stage budget");
            GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
        }
        if(iterated_mask){
            color_stages=wii_gx_rejection(1,GX_CC_RASC,GX_CA_RASA,0,!!(fbz&2),0,1);
            if(!color_stages)unsupported(v,cmd,"GX iterated predicate stage budget");
            GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
            static int reported;
            if(!reported){reported=1;rt_log("VIPER WII GX APPROX iterated-alpha parity/chroma at GX byte interpolation; Voodoo fixed-point equivalence unproven\n");}
        }
        }
    }
    setup_end(2,setup_clock);setup_clock=setup_start();
    (void)color_stages;
#ifdef VIPER_WII_GX_FOG_APPROX
    if(fog_enabled){
        static int reported;
        if(!reported){reported=1;rt_log("VIPER WII GX APPROX fog table lerp, midpoint dither; signed software fog/rounding equivalence unproven\n");}
        bind_fog_lookup(v,p,cmd,full_pipeline&&textured?3:textured,color_stages++);
    }
#endif
#ifdef VIPER_WII_GX_WDEPTH_APPROX
    if((fbz&(16|1024))&&!linear_depth&&active_depth_band>=0&&active_depth_band<64)bind_depth_lookup(v,p,cmd,full_pipeline&&textured?3:textured,color_stages);
#endif
    setup_end(3,setup_clock);setup_clock=setup_start();
#ifdef VIPER_WII_GX_COLOR_EQUATION
    int split_depth=generic_pipeline&&(pipeline.key||pipeline.mask)&&
        pipeline.rejection==WII_REJECT_SPLIT_DEPTH;
    if(split_depth)GX_SetZMode(!!(fbz&(16|1024)),fbz&16?compare[(fbz>>5)&7]:GX_ALWAYS,GX_FALSE);
#endif
#ifdef VIPER_WII_VERTEX_STQ
    int stq_mode=stq_units==3?2:stq_units?1:0;
    const float *stq_n=stq[stq_units==2?1:0][0],*stq_b=stq[1][0];
#ifdef VIPER_WII_GX_BATCH
#define EMIT_TRIANGLE() do{ stq_set_mode(stq_mode); \
    if(textured&&!projective){ \
        GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3); \
        for(unsigned i=0;i<3;i++){ \
            stq_vertex(p[i].x,screen_y(v,p[i].y),depths[i],colors[i],stq_mode,stq_n+i*3,stq_b+i*3); \
            GX_TexCoord2f32((texture_unit?p[i].s1:p[i].s)*us,(texture_unit?p[i].t1:p[i].t)*vs); \
        } \
        GX_End(); \
    }else{ \
        unsigned words=4+(stq_mode==2?9:stq_mode?3:0); \
        u32 *o=gx_batch_reserve(stq_mode==2?GX_VTXFMT1:GX_VTXFMT0,words); \
        for(unsigned i=0;i<3;i++){ \
            float f[3]={p[i].x,screen_y(v,p[i].y),-depths[i]}; \
            memcpy(o,f,12);o+=3; \
            if(stq_mode){memcpy(o,stq_n+i*3,12);o+=3;} \
            if(stq_mode==2){memcpy(o,stq_b+i*3,12);o+=3;memset(o,0,12);o+=3;} \
            *o++=((u32)colors[i].r<<24)|((u32)colors[i].g<<16)|((u32)colors[i].b<<8)|colors[i].a; \
        } \
    } }while(0)
#else
#define EMIT_TRIANGLE() do{ stq_set_mode(stq_mode); \
    GX_Begin(GX_TRIANGLES,stq_mode==2?GX_VTXFMT1:GX_VTXFMT0,3); \
    for(unsigned i=0;i<3;i++){ \
        stq_vertex(p[i].x,screen_y(v,p[i].y),depths[i],colors[i],stq_mode,stq_n+i*3,stq_b+i*3); \
        if(textured&&!projective)GX_TexCoord2f32((texture_unit?p[i].s1:p[i].s)*us,(texture_unit?p[i].t1:p[i].t)*vs); \
    } \
    GX_End(); }while(0)
#endif
#else
#define EMIT_TRIANGLE() do{ GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3); \
    for(unsigned i=0;i<3;i++){ \
        vertex(p[i].x,screen_y(v,p[i].y),depths[i],colors[i]); \
        if(textured&&!projective)GX_TexCoord2f32((texture_unit?p[i].s1:p[i].s)*us,(texture_unit?p[i].t1:p[i].t)*vs); \
    } \
    GX_End(); }while(0)
#endif
    EMIT_TRIANGLE();
#ifdef VIPER_WII_GX_COLOR_EQUATION
    if(split_depth){
        /* Finish each triangle before the next one: a depth prepass would
         * incorrectly make an original strict LESS test pass on equality. */
        if(!textured){generic_rgb_source[1]=GX_CC_ZERO;generic_alpha_source[1]=GX_CA_ZERO;}
        unsigned end=wii_gx_rejection(generic_rejection_first,generic_rgb_source[pipeline.equation.other_rgb],
            generic_alpha_source[pipeline.equation.other_alpha],full_pipeline?0:textured,pipeline.key,1,pipeline.mask);
        if(!end)unsupported(v,cmd,"GX split-depth predicate stage budget");
        GX_SetColorUpdate(GX_FALSE);GX_SetAlphaUpdate(GX_FALSE);
        GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
        GX_SetZMode(!!(fbz&(16|1024)),fbz&16?compare[(fbz>>5)&7]:GX_ALWAYS,GX_TRUE);
#ifdef VIPER_WII_GX_WDEPTH_APPROX
        if(!linear_depth&&active_depth_band>=0&&active_depth_band<64)bind_depth_lookup(v,p,cmd,full_pipeline&&textured?3:textured,end);
#endif
        EMIT_TRIANGLE();
        profile.split_depth_draws++;
#ifdef VIPER_WII_MATERIAL_RUN
        wii_material_run_invalidate(&material_run);
#endif
        GX_SetColorUpdate(!!(fbz&512));
    }
#endif
#ifdef VIPER_WII_TRIANGLE_MEMO
    if(active_depth_band<0&&generic_pipeline&&full_pipeline&&!split_depth&&!fog_enabled&&
       (linear_depth||!(fbz&(16|1024)))&&!(textured&&!projective)&&
       (!tmu_pipeline.texture_zero||!textured)){
        tri_memo.state_epoch=wii_voodoo_state_epoch;tri_memo.texture_epoch=wii_voodoo_texture_epoch;
        tri_memo.gx_gen=gx_state_gen;tri_memo.packet=(cmd>>10)&255;
        tri_memo.y_flip=!!(v->regs[0x110/4]&(1u<<17));
        tri_memo.y_base=((v->io[0x10/4]>>18)&4095)+1.0f;
        tri_memo.alpha_plan_positive=alpha_plan.clamp&&!alpha_plan.local_override&&
            !alpha_plan.local_alpha&&alpha_plan.alpha_zero&&!alpha_plan.alpha_sub&&
            alpha_plan.alpha_add&&!alpha_plan.alpha_invert;
        tri_memo.positive_alpha=positive_alpha;
        tri_memo.depth_linear=linear_depth;tri_memo.fbz=fbz;
        tri_memo.stq_units=stq_units;tri_memo.stq_mode=stq_mode;
        tri_memo.tmu_draw=!tmu_pipeline.texture_zero;
        tri_memo.dual=tmu_pipeline.unit[0].local_needed&&tmu_pipeline.unit[1].local_needed&&
            tmu_pipeline.unit[0].use&&tmu_pipeline.unit[1].use;
        tri_memo.plan=material_plan;tri_memo.perspective=0;
        for(unsigned unit=0;unit<2;unit++){
            tri_memo.perspective|=(unsigned)(tmu_pipeline.unit[unit].perspective!=0)<<unit;
            tri_memo.scale_s[unit]=stq_units&(1u<<unit)?material_bind_scale_s[unit]:0;
            tri_memo.scale_t[unit]=stq_units&(1u<<unit)?material_bind_scale_t[unit]:0;
        }
        tri_memo.valid=1;
#ifdef VIPER_WII_MEMO_VERTEX_PREP
        tri_memo_gen++;
#endif
#ifdef VIPER_WII_MEMO_REVISIT_STATS
        {
            /* Diagnostic: would a content-keyed multi-entry memo help? Hash
             * the material inputs at every refill; count refills whose hash
             * is one of the last 4 or 8 (an LRU of states). */
            static const unsigned fbi[]={0x104/4,0x108/4,0x10c/4,0x110/4,0x134/4,0x138/4,0x21c/4};
            uint32_t h=2166136261u;
            for(unsigned i=0;i<sizeof fbi/sizeof fbi[0];i++)h=(h^v->regs[fbi[i]])*16777619u;
            for(unsigned t=0;t<2;t++)for(unsigned i=0;i<9;i++)h=(h^v->tmu[t][i])*16777619u;
            h=(h^wii_voodoo_texture_epoch)*16777619u;h=(h^((cmd>>10)&255))*16777619u;
            static uint32_t lru[8];static unsigned long long refills,hit4,hit8;
            unsigned found=8;for(unsigned i=0;i<8;i++)if(lru[i]==h){found=i;break;}
            refills++;if(found<4)hit4++;if(found<8)hit8++;
            if(found==8)found=7;
            for(unsigned i=found;i>0;i--)lru[i]=lru[i-1];
            lru[0]=h;
            if((refills&0x3ffff)==0)rt_log("VIPER WII MEMO REVISIT refills=%llu lru4=%llu lru8=%llu\n",refills,hit4,hit8);
        }
#endif
    }
#endif
    geometry=1;setup_end(4,setup_clock);
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
#undef pipeline
#undef tmu_pipeline
#endif
}
static void clear(void *user,const WiiVoodooView *v) {
    TEXLOAD_FORGET();
#ifdef VIPER_WII_TRIANGLE_MEMO
    tri_memo.valid=0;
#endif
    stq_set_mode(0);
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_invalidate(&material_run);
#endif
    if(!render_this_frame())return;
    wii_gx_own_thread();
    depth_texture_off();texture_off();
    (void)user;uint32_t fbz=v->regs[0x110/4],rgb=v->regs[0x148/4];
    if(!(fbz&(512|1024)))return;
    if(fbz&(1u<<18))unsupported(v,0,"GX alpha-plane clear pending");
    /* Only endpoint colours have an invariant RGB565 dither result. */
    unsigned r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
    if((fbz&256)&&((r!=0&&r!=255)||(g!=0&&g!=255)||(b!=0&&b!=255))){
#ifdef VIPER_WII_GX_DITHER_APPROX
        dither_approximations++;
#else
        unsupported(v,0,"GX dithered clear pending");
#endif
    }
    GX_SetDither(GX_FALSE);
    GXColor c={((r>>3)<<3)|(r>>5),((g>>2)<<2)|(g>>6),((b>>3)<<3)|(b>>5),255};
#ifdef VIPER_WII_CLEAR_DIAG
    c=(GXColor){255,0,255,255};   /* Diagnostic: every clear magenta (hardware band test). */
#endif
    projection(v);GX_SetScissor(0,0,640,480);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetColorUpdate(!!(fbz&512));GX_SetZMode(GX_TRUE,GX_ALWAYS,!!(fbz&1024));
    float l=(v->regs[0x118/4]>>16)&1023,rgt=v->regs[0x118/4]&1023;
    float t=(v->regs[0x11c/4]>>16)&1023,bot=v->regs[0x11c/4]&1023;
#ifdef VIPER_WII_CLEAR_TRACE
    {static uint32_t seen[64][4];static unsigned n;uint32_t k[4]={v->regs[0x118/4],v->regs[0x11c/4],rgb,fbz};
     unsigned i;for(i=0;i<n&&memcmp(seen[i],k,sizeof k);i++);
     if(i==n&&n<64){memcpy(seen[n++],k,sizeof k);
       rt_log("VIPER WII CLEAR frame=%u lr=%08lx tb=%08lx rgb=%06lx fbz=%08lx disp=%ux%u\n",render_frame,
         (unsigned long)k[0],(unsigned long)k[1],(unsigned long)rgb,(unsigned long)fbz,width(v),height(v));}}
#endif
    if(rgt<=l||bot<=t)return;
    if(WIDE_M){if(l<=0)l=-(float)WIDE_M;if(rgt>=width(v))rgt=width(v)+(float)WIDE_M;}
    t=screen_y(v,t);bot=screen_y(v,bot);
#ifdef VIPER_WII_WDEPTH_LINEAR
    float d=linear_depth_from_wdepth(v->regs[0x130/4]&65535);
#else
    float d=(v->regs[0x130/4]&65535)/65535.0f;
#endif
#ifdef VIPER_WII_CLEAR_TRACE
    {static unsigned logged;if(logged<400){logged++;union{float f;uint32_t u;}dd={d};
     rt_log("VIPER WII CLEAR DRAW frame=%u rect=%.1f,%.1f-%.1f,%.1f d=%.9g(%08lx) za=%08lx rgb=%06lx fbz=%08lx\n",render_frame,
       (double)l,(double)t,(double)rgt,(double)bot,(double)d,(unsigned long)dd.u,(unsigned long)v->regs[0x130/4],(unsigned long)rgb,(unsigned long)fbz);}}
#endif
#if defined(VIPER_WII_CLEAR_VARIANT) && VIPER_WII_CLEAR_VARIANT==4
    c=(GXColor){255,0,255,255};
#endif
#if defined(VIPER_WII_CLEAR_VARIANT) && (VIPER_WII_CLEAR_VARIANT==2 || VIPER_WII_CLEAR_VARIANT==4)
    if(d>=1.f)d=1.f-0x1p-24f;   /* Band test: one Z24 step inside the far plane. */
#endif
#if defined(VIPER_WII_CLEAR_VARIANT) && (VIPER_WII_CLEAR_VARIANT==3 || VIPER_WII_CLEAR_VARIANT==4)
    if(t>bot){float y=t;t=bot;bot=y;}   /* Band test: conventional winding. */
#endif
#if defined(VIPER_WII_CLEAR_NOCLIP) || (defined(VIPER_WII_CLEAR_VARIANT) && (VIPER_WII_CLEAR_VARIANT==1 || VIPER_WII_CLEAR_VARIANT==4))
    /* The clear quad lies exactly on the far plane (d=1). Real hardware
     * clips it away (boot fade stayed blue, bands missing; Dolphin draws it).
     * XF clip-disable register 0x1005 with clipping detection, trivial
     * rejection and the clipping accelerator all off (libogc's
     * GX_SetClipMode only writes bit 0), restored after the quad: the same
     * pixels and depth, drawn. Hardware test S (2026-10-08): fade to black. */
    gx_batch_flush();gx_state_gen++;
    wgPipe->U8=0x10;wgPipe->U32=0x1005;wgPipe->U32=7;
#endif
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    vertex(l,t,d,c);vertex(rgt,t,d,c);vertex(rgt,bot,d,c);
    vertex(l,t,d,c);vertex(rgt,bot,d,c);vertex(l,bot,d,c);GX_End();
#if defined(VIPER_WII_CLEAR_NOCLIP) || (defined(VIPER_WII_CLEAR_VARIANT) && (VIPER_WII_CLEAR_VARIANT==1 || VIPER_WII_CLEAR_VARIANT==4))
    wgPipe->U8=0x10;wgPipe->U32=0x1005;wgPipe->U32=0;
#endif
}
static void scan_vertex(float x,float y,float u,float t) {
    GX_Position3f32(x,y,0);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(u,t);
}
static void scanout(const WiiVoodooView *v,unsigned base) {
    TEXLOAD_FORGET();
    stq_set_mode(0);
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_invalidate(&material_run);
#endif
    depth_texture_off();
    unsigned w=width(v),h=height(v);
    unsigned stride=(v->io[0xe8/4]>>16)&0x7fff;
    if(v->io[0x5c/4]&(1u<<25))stride*=128;
    if(!w||!h||w>1024||h>1024||stride<w*2||base>0x800000||
       (uint64_t)base+(h-1)*stride+w*2>0x800000)unsupported(v,0,"GX scanout bounds");
    uint8_t *image=memalign(32,640*480*4);
    if(!image)rt_fatal("GX scanout allocation");
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++) {
        unsigned addr=base+(y*h/480)*stride+(x*w/640)*2;
        unsigned byte_xor=0;
        unsigned pixel=v->vram[addr^byte_xor]|((unsigned)v->vram[(addr+1)^byte_xor]<<8);
        unsigned r=pixel>>11,g=(pixel>>5)&63,b=pixel&31;
        r=(r<<3)|(r>>2);g=(g<<2)|(g>>4);b=(b<<3)|(b>>2);
        if(!(v->io[0x5c/4]&(1u<<11))) {
            unsigned bank=(v->io[0x5c/4]&(1u<<13))?256:0;
            r=(v->clut[bank+r]>>16)&255;g=(v->clut[bank+g]>>8)&255;b=v->clut[bank+b]&255;
            r=(r&248)|(r>>5);g=(g&252)|(g>>6);b=(b&248)|(b>>5);
        }
        unsigned off=((y/4)*160+x/4)*64+((y&3)*4+(x&3))*2;
        image[off]=255;image[off+1]=r;image[off+32]=g;image[off+33]=b;
    }
    DCFlushRange(image,640*480*4);GX_InvalidateTexAll();
    GXTexObj tex;GX_InitTexObj(&tex,image,640,480,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
    Mtx44 p;guOrtho(p,0,480,0,640,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
    GX_SetScissor(0,0,640,480);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GX_SetNumTexGens(1);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    scan_vertex(0,0,0,0);scan_vertex(640,0,1,0);scan_vertex(640,480,1,1);
    scan_vertex(0,0,0,0);scan_vertex(640,480,1,1);scan_vertex(0,480,0,1);GX_End();
    gx_wait(3);free(image);
    GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
}
/* Paint everything outside the output box black once a frame (nothing else
 * draws there except the menu overlay, which follows). Full-screen viewport
 * and projection for these quads; the caller restores the box. */
static void letterbox_borders(void){
    GX_SetViewport(0,0,640,480,0,1);
    Mtx44 p;guOrtho(p,0,480,0,640,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
    gx_shadow.proj_w=gx_shadow.proj_h=0;
    texture_off();depth_texture_off();
    GX_SetScissor(0,0,640,480);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetColorUpdate(GX_TRUE);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    const GXColor k={0,0,0,255};
    const float r[4][4]={{0,0,640,OUT_Y},{0,OUT_Y+OUT_H,640,480},{0,OUT_Y,OUT_X,OUT_Y+OUT_H},{OUT_X+OUT_W,OUT_Y,640,OUT_Y+OUT_H}};
    GX_Begin(GX_QUADS,GX_VTXFMT0,16);
    for(unsigned i=0;i<4;i++){
        /* Mid-depth: the Wii clips geometry lying exactly on a clip plane. */
        vertex(r[i][0],r[i][1],0.5f,k);vertex(r[i][2],r[i][1],0.5f,k);
        vertex(r[i][2],r[i][3],0.5f,k);vertex(r[i][0],r[i][3],0.5f,k);
    }
    GX_End();
}
#ifdef VIPER_WII_NATIVE_RES
/* The finished native picture (render box) is copied to native_tex and drawn
 * bilinear into the output box; after the display copy it is drawn back 1:1
 * (point sampled) into the render box, so a frame the game leaves undrawn
 * (loading) still shows the same picture, as the EFB keeps it at 1x. */
static u8 *native_tex;
static int native_scaled;   /* this frame's picture is in native_tex */
static void native_quad(u8 filter,float x0,float y0,float x1,float y1){
    GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
    {Mtx44 p;guOrtho(p,0,480,0,640,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);}
    GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetCullMode(GX_CULL_NONE);GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GX_SetNumChans(1);GX_SetNumTexGens(1);GX_SetNumTevStages(1);GX_SetNumIndStages(0);
    GX_SetTevDirect(GX_TEVSTAGE0);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevSwapModeTable(GX_TEV_SWAP0,GX_CH_RED,GX_CH_GREEN,GX_CH_BLUE,GX_CH_ALPHA);
    GX_SetTevSwapMode(GX_TEVSTAGE0,GX_TEV_SWAP0,GX_TEV_SWAP0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GXTexObj o;GX_InitTexObj(&o,native_tex,RB_W,RB_H,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&o,filter,filter,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    GX_LoadTexObj(&o,GX_TEXMAP0);
    GX_Begin(GX_QUADS,GX_VTXFMT7,4);
    GX_Position3f32(x0,y0,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(0,0);
    GX_Position3f32(x1,y0,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(1,0);
    GX_Position3f32(x1,y1,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(1,1);
    GX_Position3f32(x0,y1,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(0,1);
    GX_End();
    GX_SetAlphaUpdate(GX_TRUE);
#ifdef VIPER_WII_VERTEX_STQ
    stq_desc=-1;stq_set_mode(0);   /* back to the renderer's mode 0, re-sent */
#endif
    TEXLOAD_FORGET();gx_shadow.proj_w=gx_shadow.proj_h=0;
}
static void native_upscale(void){
    if(RB_X==OUT_X&&RB_Y==OUT_Y&&RB_W==OUT_W&&RB_H==OUT_H)return;   /* already 1:1 */
    if(!native_tex&&!(native_tex=memalign(32,640*384*4)))rt_fatal("GX native picture allocation");
    GX_SetTexCopySrc(RB_X,RB_Y,RB_W,RB_H);
    GX_SetTexCopyDst(RB_W,RB_H,GX_TF_RGBA8,GX_FALSE);
    GX_CopyTex(native_tex,GX_FALSE);
    GX_PixModeSync();GX_InvalidateTexAll();
    native_quad(GX_LINEAR,OUT_X,OUT_Y,OUT_X+OUT_W,OUT_Y+OUT_H);
    native_scaled=1;
}
static void native_restore(void){
    if(!native_scaled)return;
    native_scaled=0;
    native_quad(GX_NEAR,RB_X,RB_Y,RB_X+RB_W,RB_Y+RB_H);
    GX_SetViewport(RB_X,RB_Y,RB_W,RB_H,0,1);
}
#endif
#ifdef VIPER_WII_SUPERSAMPLE
/* 2x2 supersampling (playable option; it changes the picture). Each frame's
 * GX commands are recorded into a display list between presents and
 * replayed four times with a 2x viewport offset to one quarter of a
 * 2x image; after each pass the copy unit's 2x2 box filter writes a
 * half-size RGBA8 tile of the output box, and the four tiles are drawn back
 * before the usual display copy. Tiles are taken in clip space (the
 * projection), so the viewport and the recorded scissor rectangles stay in
 * output-box coordinates; a game clip rectangle smaller than the screen is
 * not rescaled. Anything that has to wait
 * for the GPU mid-frame (texture eviction, table updates, CPU scanout), or a
 * full list, ends the recording: what was recorded is replayed once at 1x
 * and the rest of the frame is drawn directly, without supersampling. */
#define SS_DL_BYTES (3u<<20)
#ifdef VIPER_WII_SUPERSAMPLE_TRACE
static unsigned ss_trace_n;
#define SS_TRACE(...) do { if(ss_trace_n<6000){ss_trace_n++;rt_log(__VA_ARGS__);} } while (0)
static unsigned ss_n_begin,ss_n_abort42;
#else
#define SS_TRACE(...) ((void)0)
#endif
#ifdef VIPER_WII_NATIVE_RES
/* Tiles at full size: the four quarters of the 2x native picture (1024x768
 * from 512x384, the cabinet's own output size) are composited straight into
 * the output box, bilinear: more detail than 384 lines, rasterised at an
 * exact 2x. */
#define SS_TILE_W RB_W
#define SS_TILE_H RB_H
#define SS_TILE_BOX GX_FALSE
#define SS_TILE_FILTER GX_LINEAR
#define SS_OX OUT_X
#define SS_OY OUT_Y
#define SS_OW OUT_W
#define SS_OH OUT_H
#define SS_TILE_BYTES (640*384*4)
#else
/* Tiles box-filtered to half size by the copy, composited 1:1. */
#define SS_TILE_W (RB_W/2)
#define SS_TILE_H (RB_H/2)
#define SS_TILE_BOX GX_TRUE
#define SS_TILE_FILTER GX_NEAR
#define SS_OX RB_X
#define SS_OY RB_Y
#define SS_OW RB_W
#define SS_OH RB_H
#define SS_TILE_BYTES (320*240*4)
#endif
/* What the last resolve did; present scales only a picture left in the
 * render box (VIPER_WII_NATIVE_RES). */
enum {SS_NONE,SS_TILED,SS_EMPTY};
static int ss_result,ss_last_tiled;
static u8 *ss_dl,*ss_tile[4];
#ifdef VIPER_WII_SS_SHARPEN
static u8 *ss_sharp;   /* the composited picture, re-read by the sharpening pass */
#endif
#ifdef VIPER_WII_SS_PIPELINE
/* Two lists: the GPU replays frame N from one while the CPU records frame
 * N + 1 into the other. ss_resolve waits for frame N - 1's GPU work (the
 * other list's last use) before queueing frame N, instead of present waiting
 * after it, so the 4 replays overlap the next frame's emulation. */
static u8 *ss_dl_buf[2];static unsigned ss_dl_index;
static int ss_waited;   /* this frame's present wait was done by ss_resolve */
/* GPU-bound scenes (the attract tunnel explosion: 4x the fill) slowed the
 * game below real time and the sound ran dry. When the previous frame's GPU
 * work is still running by more than SS_BEHIND_US at a resolve, the next
 * frames are drawn plain; each repeat doubles the pause (to SS_BACKOFF_MAX),
 * and a supersampled frame that keeps up resets it. */
#define SS_BEHIND_US 2000
#define SS_BACKOFF_MIN 30
#define SS_BACKOFF_MAX 240
static unsigned ss_backoff,ss_backoff_len=SS_BACKOFF_MIN;
static int ss_prev_tiled;   /* the frame the resolve waits for was supersampled */
#endif
/* A recorded frame is a run of list segments, one per clip rectangle: the
 * scissor is not recorded (it would be wrong for every tile but one); each
 * replay sets the segment's rectangle scaled and offset for its tile. The
 * interior and bumper cameras clip their rear-view mirror this way, and
 * before segments those frames were drawn at 1x. */
#define SS_SEGMENTS 64
static struct {u32 off,n;s16 x,y,w,h;} ss_seg[SS_SEGMENTS];
static unsigned ss_nseg;
static void ss_seg_start(u32 off,u32 x,u32 y,u32 w,u32 h){
    ss_seg[ss_nseg].off=off;ss_seg[ss_nseg].n=0;
    ss_seg[ss_nseg].x=(s16)x;ss_seg[ss_nseg].y=(s16)y;ss_seg[ss_nseg].w=(s16)w;ss_seg[ss_nseg].h=(s16)h;
    GX_BeginDispList(ss_dl+off,SS_DL_BYTES-off);
    wgPipe->U8=0;   /* GX NOP: an empty segment pads to 32 bytes, so 0 means overflow */
}
static u32 ss_seg_end(void){u32 n=GX_EndDispList();ss_seg[ss_nseg++].n=n;return n;}
/* tile < 0: each segment's own rectangle; else scaled into that tile. A
 * rectangle that misses the tile gets an empty one outside the render box
 * (the segment's state still applies, nothing is drawn). */
static void ss_seg_scissor(unsigned i,int tile){
    int x=ss_seg[i].x,y=ss_seg[i].y,w=ss_seg[i].w,h=ss_seg[i].h;
    if(tile<0){GX_SetScissor(x,y,w,h);return;}
    int ox=(tile&1)*RB_W/2,oy=(tile>>1)*RB_H/2;
    int x0=x-RB_X,x1=x+w-RB_X,y0=y-RB_Y,y1=y+h-RB_Y;
    x0=(x0-ox)*2;x1=(x1-ox)*2;y0=(y0-oy)*2;y1=(y1-oy)*2;
    if(x0<0)x0=0;if(y0<0)y0=0;if(x1>RB_W)x1=RB_W;if(y1>RB_H)y1=RB_H;
    if(x1<=x0||y1<=y0){if(RB_Y>0)GX_SetScissor(0,0,640,RB_Y);else GX_SetScissor(0,RB_Y+RB_H,640,1);return;}
    GX_SetScissor(RB_X+x0,RB_Y+y0,x1-x0,y1-y0);
}
static void ss_replay_segments(int tile){
    for(unsigned i=0;i<ss_nseg;i++){
        if(!ss_seg[i].n)continue;   /* overflowed: lost */
        ss_seg_scissor(i,tile);GX_CallDispList(ss_dl+ss_seg[i].off,ss_seg[i].n);
    }
}
static int ss_scissor_segment(u32 x,u32 y,u32 w,u32 h){
    if(!ss_recording)return 0;
    if(ss_nseg+1>=SS_SEGMENTS){ss_abort();return 0;}   /* too many: this frame at 1x */
    u32 n=ss_seg_end();
    if(!n){ss_abort();return 0;}
    u32 off=ss_seg[ss_nseg-1].off+((n+31)&~31u);
    if(off+64>=SS_DL_BYTES){ss_abort();return 0;}
    ss_seg_start(off,x,y,w,h);
    return 1;
}
/* tile < 0: the plain projection; else quarter tile (x = tile&1, y = tile>>1). */
static void ss_load_projection(int tile){
    if(!ss_proj_set)return;
    Mtx44 p;guOrtho(p,0,ss_proj_h,-(f32)WIDE_M,ss_proj_w+WIDE_M,0,1);
    if(tile>=0){
        float ox=1.f-2.f*(tile&1),oy=2.f*(tile>>1)-1.f;
        for(unsigned c=0;c<4;c++){p[0][c]*=2;p[1][c]*=2;}
        p[0][3]+=ox;p[1][3]+=oy;
    }
    GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
}
#ifdef VIPER_WII_HANG_TRACE
int usleep(unsigned int);   /* newlib hides it under -std=c11 */
/* Diagnostic: the last supersample events in RAM; a watchdog thread writes
 * them out when frames stop (a GPU hang leaves the guest in a fence). */
static struct {unsigned frame;char what;int a,b;} hang_ring[64];static volatile unsigned hang_pos;
static void hang_note(char w,int x,int y){unsigned i=hang_pos++&63;hang_ring[i].frame=render_frame;hang_ring[i].what=w;hang_ring[i].a=x;hang_ring[i].b=y;}
static void *hang_watch(void *arg){
    (void)arg;unsigned last=~0u,still=0;
    for(;;){
        usleep(500000);
        if(render_frame!=last){last=render_frame;still=0;continue;}
        if(++still==6){
            rt_log("VIPER WII HANG frame=%u events (B begin, A abort line/bytes, R resolve bytes/proj, C partial clip, W wait site):\n",render_frame);
            for(unsigned k=0;k<64;k++){unsigned i=(hang_pos+k)&63;if(!hang_ring[i].what)continue;
                rt_log("  f=%u %c %d %d\n",hang_ring[i].frame,hang_ring[i].what,hang_ring[i].a,hang_ring[i].b);}
            rt_log("VIPER WII HANG recording=%d wait_site=%u\n",ss_recording,(unsigned)wii_gx_wait_site);
            extern void wii_log_sync(void);wii_log_sync();
        }
    }
    return NULL;
}
#endif
static unsigned long long ss_frames,ss_plain;
unsigned ss_dl_peak,ss_dl_overflows;   /* largest recorded list and overflows (diagnostics read them) */
static void ss_begin(void){
    if(!ss_dl){
#ifdef VIPER_WII_SS_PIPELINE
        ss_dl_buf[0]=memalign(32,SS_DL_BYTES);ss_dl_buf[1]=memalign(32,SS_DL_BYTES);
        if(!ss_dl_buf[1])rt_fatal("GX supersample allocation");
        DCInvalidateRange(ss_dl_buf[1],SS_DL_BYTES);
        ss_dl=ss_dl_buf[0];
#else
        ss_dl=memalign(32,SS_DL_BYTES);
#endif
        for(unsigned t=0;t<4;t++)ss_tile[t]=memalign(32,SS_TILE_BYTES);
        if(!ss_dl||!ss_tile[3])rt_fatal("GX supersample allocation");
#ifdef VIPER_WII_SS_SHARPEN
        if(!(ss_sharp=memalign(32,640*480*4)))rt_fatal("GX supersample allocation");
#endif
        /* The write-gather pipe fills the list behind the cache: no stale
         * lines may sit over it (libogc's display-list note). */
        DCInvalidateRange(ss_dl,SS_DL_BYTES);
        /* By default libogc snapshots its register model at BeginDispList and
         * restores it at EndDispList (a list "does not change state"). Ours
         * is the frame itself: its state changes are real, the replays apply
         * them, and the model must keep them, or later writes are built from
         * stale values (a wrong vertex size: "Unknown opcode"). */
        GX_SetMisc(GX_MT_DL_SAVE_CTX,0);
    }
    if(!DISPLAY_SUPERSAMPLE||((RB_W|RB_H)&7))return;   /* tiles must be whole 4x4 texture blocks */
#ifdef VIPER_WII_SS_PIPELINE
    ss_prev_tiled=0;   /* set again by this frame's resolve if it is tiled */
#endif
    if(ss_partial_clip){ss_partial_clip=0;ss_plain++;return;}
#ifdef VIPER_WII_SS_PIPELINE
    if(ss_backoff){ss_backoff--;ss_plain++;return;}
#endif
#ifdef VIPER_WII_SUPERSAMPLE_TRACE
    if(!(++ss_n_begin%25))SS_TRACE("VIPER WII SS heartbeat frame=%u begins=%u thread_aborts=%u resolved=%llu plain=%llu\n",
        render_frame,ss_n_begin,ss_n_abort42,ss_frames,ss_plain);
#endif
    /* The four replays run the same list, but only the first starts from the
     * GPU state current while it was recorded; the others start from what
     * the list itself left. So the list must carry every state it relies on:
     * no CPU-side cache may skip a command because the GPU "already has" it
     * from before the frame (rare skips drew some quarters with the previous
     * frame's TEV/texture state: torn WIDE SUPER frames with the Cobra). */
    gx_shadow_reset();clip_key_stale=1;TEXLOAD_FORGET();
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_invalidate(&material_run);
#endif
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
    material_bind_valid=0;
#endif
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    lookup_plane_cache.valid=0;
#endif
#ifdef VIPER_WII_TRIANGLE_MEMO
    tri_memo.valid=0;
#endif
    GX_Flush();
#ifdef VIPER_WII_SS_PIPELINE
    ss_dl=ss_dl_buf[ss_dl_index^=1];
#endif
    HANG_NOTE('B',ss_dl_index,0);
    ss_nseg=0;ss_seg_start(0,0,0,640,480);
    /* libogc sends the vertex descriptor only when it changes, so a list
     * could start drawing with the one in force before it, which replays 2-4
     * do not have (they start from the list's last one): the GPU then reads
     * vertices at the wrong size ("Unknown opcode", a hang; interior view in
     * WIDE SUPER). Mark the current descriptor dirty: the list's first draw
     * sends it. */
    {GXVtxDesc vd[GX_MAX_VTXDESC_LISTSIZE];GX_GetVtxDescv(vd);GX_SetVtxDescv(vd);}
    ss_recording=1;ss_proj_set=0;
#ifdef VIPER_WII_VERTEX_STQ
    /* libogc sends vertex descriptors lazily (at the next GX_Begin): make
     * every list carry its own, or a replay after the composite (TEX0 on)
     * decodes the frame's vertices with the wrong size. */
    stq_desc=-1;stq_set_mode(0);
#endif
    gx_shadow.proj_w=gx_shadow.proj_h=~0u;   /* the frame's first projection() is captured */
}
static void ss_abort_line(int line){
    if(!ss_recording)return;
#ifdef VIPER_WII_GX_BATCH
    /* Batched triangles belong to the recorded frame: put them in the list
     * before it ends. Otherwise the projection load below flushes them
     * straight to the GPU ahead of the list's replay, with the previous
     * frame's vertex layout: "Unknown opcode" and a hang (an abort at the
     * interior view's partial clip in WIDE SUPER; the same order fix as
     * wii_gx_batch_flush at the end of a run). */
    gx_batch_flush();
#endif
    ss_recording=0;
    u32 n=ss_seg_end();
    HANG_NOTE('A',line,n);
#ifdef VIPER_WII_SUPERSAMPLE_TRACE
    if(line==42)ss_n_abort42++;
    else SS_TRACE("VIPER WII SS abort frame=%u bytes=%lu line=%d thread=%p\n",render_frame,(unsigned long)n,line,(void*)LWP_GetSelf());
#endif
    (void)line;(void)n;
    ss_load_projection(-1);
    ss_replay_segments(-1);
    ss_plain++;
}
#ifdef VIPER_WII_SS_SHARPEN
/* Unsharp mask over the composited picture, which the 2x2 box downscale
 * leaves soft: out = c + k (c - b), b the mean of the 4 neighbours, k the
 * TEV constant VIPER_WII_SS_SHARPEN (GX_TEV_KCSEL_1_4 .. _1). Only TEV input
 * d keeps a sign, so stage 4 forms k (c - b) as d - lerp(c, b, k) unclamped
 * and stage 5 adds c. Every GX call here is shadowed and present resets the
 * shadows after the resolve; vertex descriptors are put back as the
 * composite left them. Called with the composite's state current. */
static void ss_sharpen(void){
    GX_SetTexCopySrc(SS_OX,SS_OY,SS_OW,SS_OH);
    GX_SetTexCopyDst(SS_OW,SS_OH,GX_TF_RGBA8,GX_FALSE);
    GX_CopyTex(ss_sharp,GX_FALSE);
    GX_PixModeSync();GX_InvalidateTexAll();
    GXTexObj o;GX_InitTexObj(&o,ss_sharp,SS_OW,SS_OH,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&o,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    GX_LoadTexObj(&o,GX_TEXMAP0);
    GX_SetNumChans(0);GX_SetNumTexGens(5);GX_SetNumTevStages(6);
    for(unsigned i=0;i<5;i++)GX_SetTexCoordGen(GX_TEXCOORD0+i,GX_TG_MTX2x4,GX_TG_TEX0+i,GX_IDENTITY);
    /* Stages 0-3: b = n/4 + e/4 + s/4 + w/4 (texcoords 1-4). */
    for(unsigned st=0;st<4;st++){
        GX_SetTevOrder(st,GX_TEXCOORD1+st,GX_TEXMAP0,GX_COLORNULL);
        GX_SetTevKColorSel(st,GX_TEV_KCSEL_1_4);
        GX_SetTevColorIn(st,GX_CC_ZERO,GX_CC_TEXC,GX_CC_KONST,st?GX_CC_CPREV:GX_CC_ZERO);
        GX_SetTevColorOp(st,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
        GX_SetTevAlphaIn(st,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO);
        GX_SetTevAlphaOp(st,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    /* Stage 4: c - lerp(c, b, k) = k (c - b), signed. Stage 5: + c. */
    GX_SetTevOrder(GX_TEVSTAGE4,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
    GX_SetTevKColorSel(GX_TEVSTAGE4,VIPER_WII_SS_SHARPEN);
    GX_SetTevColorIn(GX_TEVSTAGE4,GX_CC_TEXC,GX_CC_CPREV,GX_CC_KONST,GX_CC_TEXC);
    GX_SetTevColorOp(GX_TEVSTAGE4,GX_TEV_SUB,GX_TB_ZERO,GX_CS_SCALE_1,GX_FALSE,GX_TEVPREV);
    GX_SetTevOrder(GX_TEVSTAGE5,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
    GX_SetTevColorIn(GX_TEVSTAGE5,GX_CC_ZERO,GX_CC_TEXC,GX_CC_ONE,GX_CC_CPREV);
    GX_SetTevColorOp(GX_TEVSTAGE5,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    for(unsigned st=4;st<6;st++){
        GX_SetTevAlphaIn(st,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO);
        GX_SetTevAlphaOp(st,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
    }
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);
    for(unsigned i=0;i<5;i++){
        GX_SetVtxDesc(GX_VA_TEX0+i,GX_DIRECT);
        GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_TEX0+i,GX_TEX_ST,GX_F32,0);
    }
    const float du=1.f/SS_OW,dv=1.f/SS_OH;
    static const float corner[4][2]={{0,0},{1,0},{1,1},{0,1}};
    GX_Begin(GX_QUADS,GX_VTXFMT7,4);
    for(unsigned c=0;c<4;c++){
        float u=corner[c][0],v=corner[c][1];
        GX_Position3f32(SS_OX+u*SS_OW,SS_OY+v*SS_OH,-0.5f);
        GX_TexCoord2f32(u,v);
        GX_TexCoord2f32(u,v-dv);GX_TexCoord2f32(u+du,v);GX_TexCoord2f32(u,v+dv);GX_TexCoord2f32(u-du,v);
    }
    GX_End();
    /* As the composite left them (it ran with these): */
    GX_SetNumChans(1);GX_SetNumTexGens(1);GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
}
#endif
static void ss_resolve(void){
    ss_result=SS_NONE;
    if(!ss_recording)return;
    ss_recording=0;
    u32 n=ss_seg_end(),total=0;int empty=1;
    for(unsigned i=0;i<ss_nseg;i++){total+=ss_seg[i].n;if(ss_seg[i].n>32)empty=0;}
    HANG_NOTE('R',total,ss_nseg);
    if(total>ss_dl_peak)ss_dl_peak=total;
    if(!n)ss_dl_overflows++;
    SS_TRACE("VIPER WII SS resolve frame=%u bytes=%lu proj=%d %ux%u\n",render_frame,(unsigned long)n,ss_proj_set,ss_proj_w,ss_proj_h);
#ifdef VIPER_WII_SUPERSAMPLE_TRACE
    {static unsigned dumps;
     if(dumps<3&&n<=1024){dumps++;DCInvalidateRange(ss_dl,(n+31)&~31u);
        for(u32 i=0;i<n;i+=32){
            const u8 *b=ss_dl+i;
            rt_log("VIPER WII SS dl %03lx: %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x %02x%02x%02x%02x\n",(unsigned long)i,
              b[0],b[1],b[2],b[3],b[4],b[5],b[6],b[7],b[8],b[9],b[10],b[11],b[12],b[13],b[14],b[15],
              b[16],b[17],b[18],b[19],b[20],b[21],b[22],b[23],b[24],b[25],b[26],b[27],b[28],b[29],b[30],b[31]);
        }}}
#endif
    if(!n){ss_plain++;gx_shadow.proj_w=gx_shadow.proj_h=0;return;}   /* overflowed: the frame is lost, the next one is not */
    /* Nothing drawn (32 bytes: only ss_begin's descriptor setup; any draw adds
     * far more): the game is loading and presents the same picture. The EFB
     * still holds the last composite, so the tiles would copy it again at
     * half size: a 2x2 grid, then 4x4, then flat grey. Replay the list once
     * for its state (libogc's model already counts it as sent) and keep the
     * picture, as the plain renderer does. */
    if(empty){ss_load_projection(-1);ss_replay_segments(-1);ss_plain++;ss_result=SS_EMPTY;return;}
#ifdef VIPER_WII_SS_PLAIN_RESOLVE
    /* Diagnostic: record and replay once at 1x (no tiles, copies or composite). */
    ss_load_projection(-1);ss_replay_segments(-1);ss_frames++;return;
#endif
    ss_frames++;
#ifdef VIPER_WII_SS_PIPELINE
    {uint64_t t=gettime();
     gx_wait(4);ss_waited=1;   /* frame N - 1 done: its list is free for frame N + 1 */
     if(ticks_to_microsecs(gettime()-t)>SS_BEHIND_US){
        ss_backoff=ss_backoff_len;
        if(ss_backoff_len<SS_BACKOFF_MAX)ss_backoff_len*=2;
     }else if(ss_prev_tiled)ss_backoff_len=SS_BACKOFF_MIN;   /* only a supersampled frame proves it keeps up */
     ss_prev_tiled=1;}
#endif
    for(unsigned t=0;t<4;t++){
        unsigned tx=t&1,ty=t>>1;
        (void)tx;(void)ty;
        GX_SetViewport(RB_X,RB_Y,RB_W,RB_H,0,1);
        ss_load_projection((int)t);
        ss_replay_segments((int)t);
        GX_SetTexCopySrc(RB_X,RB_Y,RB_W,RB_H);
        GX_SetTexCopyDst(SS_TILE_W,SS_TILE_H,GX_TF_RGBA8,SS_TILE_BOX);
#ifdef VIPER_WII_SS_TILE_MAGENTA
        /* Diagnostic: each copy clears the render box to magenta (Z far), so
         * pixels a frame never draws show in the next tile's quarter. */
        GX_SetCopyClear((GXColor){255,0,255,255},0xffffff);
        GX_CopyTex(ss_tile[t],GX_TRUE);
        GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);
#else
        GX_CopyTex(ss_tile[t],GX_FALSE);
#endif
    }
    GX_PixModeSync();GX_InvalidateTexAll();
    /* The replayed list set the hardware state (its scissor is the render
     * box), while the shadow still holds what was set before each replay:
     * forget it, or the composite's full-screen scissor is skipped and the
     * picture is clipped to the render box (VIPER_WII_NATIVE_RES). */
    gx_shadow_reset();
    /* Composite: four textured quads, point sampled (one texel per pixel). */
    GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
    {Mtx44 p;guOrtho(p,0,480,0,640,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);}
    GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetCullMode(GX_CULL_NONE);GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GX_SetNumChans(1);GX_SetNumTexGens(1);GX_SetNumTevStages(1);GX_SetNumIndStages(0);
    GX_SetTevDirect(GX_TEVSTAGE0);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevSwapModeTable(GX_TEV_SWAP0,GX_CH_RED,GX_CH_GREEN,GX_CH_BLUE,GX_CH_ALPHA);
    GX_SetTevSwapMode(GX_TEVSTAGE0,GX_TEV_SWAP0,GX_TEV_SWAP0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    /* Explicitly: the descriptor shadows describe the CPU's view, while the
     * hardware now holds whatever the replayed list left. */
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetVtxAttrFmt(GX_VTXFMT7,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    for(unsigned t=0;t<4;t++){
        GXTexObj o;GX_InitTexObj(&o,ss_tile[t],SS_TILE_W,SS_TILE_H,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
        GX_InitTexObjLOD(&o,SS_TILE_FILTER,SS_TILE_FILTER,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
        GX_LoadTexObj(&o,GX_TEXMAP0);
        float x0=SS_OX+(t&1)*(SS_OW/2),y0=SS_OY+(t>>1)*(SS_OH/2),x1=x0+SS_OW/2,y1=y0+SS_OH/2;
        GX_Begin(GX_QUADS,GX_VTXFMT7,4);
        GX_Position3f32(x0,y0,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(0,0);
        GX_Position3f32(x1,y0,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(1,0);
        GX_Position3f32(x1,y1,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(1,1);
        GX_Position3f32(x0,y1,-0.5f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(0,1);
        GX_End();
    }
#ifdef VIPER_WII_SS_SHARPEN
    ss_sharpen();
#endif
    ss_result=SS_TILED;
    GX_SetAlphaUpdate(GX_TRUE);
#ifdef VIPER_WII_VERTEX_STQ
    stq_desc=-1;stq_set_mode(0);   /* back to the renderer's mode 0, re-sent */
#endif
    TEXLOAD_FORGET();
}
#endif
static void present(void *user,const WiiVoodooView *v,unsigned base) {
    TEXLOAD_FORGET();
#ifdef VIPER_WII_TRIANGLE_MEMO
    tri_memo.valid=0;
#endif
    int draw_frame=render_this_frame();render_frame++;
#ifdef VIPER_WII_AUTO_FRAMESKIP
    skip_frame=draw_frame&&wii_pace_behind_us>17000;
#endif
    if(!draw_frame)return;
    wii_gx_own_thread();
    (void)user;(void)base;
    if(!geometry)scanout(v,base);
    depth_texture_off();GX_SetNumTexGens(0);
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    wii_material_run_invalidate(&material_run);
#endif
    stq_set_mode(0);
#ifdef VIPER_WII_VERTEX_STQ
    u8 menu_tex0=stq_tex0_desc;
#endif
#ifdef VIPER_WII_GX_BATCH
    gx_batch_flush();
#endif
#ifdef VIPER_WII_SUPERSAMPLE
    ss_resolve();
#endif
#ifdef VIPER_WII_NATIVE_RES
#ifdef VIPER_WII_SUPERSAMPLE
    /* A tiled frame is already in the output box; an empty one after it
     * keeps that picture (the render box under it is overdrawn). */
#ifdef VIPER_WII_NATIVE_TRACE
    {static unsigned n;if(n<400&&render_frame>1500){n++;rt_log("VIPER WII NATIVE f=%u ss=%d last=%d scaled=%d rb=%d,%d,%d,%d out=%d,%d,%d,%d\n",
      render_frame,ss_result,ss_last_tiled,native_scaled,RB_X,RB_Y,RB_W,RB_H,OUT_X,OUT_Y,OUT_W,OUT_H);}}
#endif
    if(ss_result==SS_TILED)ss_last_tiled=1;
    else if(!(ss_result==SS_EMPTY&&ss_last_tiled)){ss_last_tiled=0;native_upscale();}
#else
    native_upscale();
#endif
#endif
#ifdef VIPER_WII_FRAME_CAPTURE
    frame_capture();
    edge_sprite_len=0;edge_sprite_text[0]=0;state_tally_n=0;
#endif
#ifdef VIPER_WII_DEPTH_TRACE
    if(!(render_frame%15)){char b[400];unsigned o=0;
     for(unsigned e=0;e<32;e++)if(depth_trace_hist[e])o+=snprintf(b+o,sizeof b-o," %u:%u",e,depth_trace_hist[e]);
     rt_log("VIPER WII DEPTH f=%u wb=%.3g..%.3g flat=%u hist(-log2)%s\n",render_frame,(double)depth_trace_min,(double)depth_trace_max,depth_trace_flat,b);}
#endif
#if defined(VIPER_WII_DEPTH_TRACE) || defined(VIPER_WII_FRAME_CAPTURE)
    depth_trace_min=1e30f;depth_trace_max=0;depth_trace_flat=0;memset(depth_trace_hist,0,sizeof depth_trace_hist);
#endif
    if(OUT_W!=640||OUT_H!=480)letterbox_borders();
    wii_menu_draw();
    gx_shadow_reset(); /* menu.c sets the shadowed GX state directly. */
    /* menu.c sets its own viewport; the game uses the box. */
    GX_SetViewport(RB_X,RB_Y,RB_W,RB_H,0,1);gx_shadow.proj_w=gx_shadow.proj_h=0;
#ifdef VIPER_WII_VERTEX_STQ
    GX_SetVtxDesc(GX_VA_TEX0,menu_tex0); /* menu.c also sets TEX0 directly. */
#endif
#ifdef VIPER_WII_PRESENT_ASYNC
    /* The copy is queued, not awaited: later draws follow it in FIFO order,
     * the next present's site-4 wait covers it, and the CPU writes no XFB
     * before the end-of-run GX_DrawDone. */
#if defined(VIPER_WII_SS_PIPELINE) && defined(VIPER_WII_SUPERSAMPLE)
    if(ss_waited)ss_waited=0;else
#endif
    gx_wait(4);
    (void)v;GX_CopyDisp(framebuffer,GX_FALSE);GX_Flush();
#ifdef VIPER_WII_NATIVE_RES
    native_restore();   /* queued after the copy: the render box back at 1x */
#endif
#else
#if defined(VIPER_WII_SS_PIPELINE) && defined(VIPER_WII_SUPERSAMPLE)
    ss_waited=0;
#endif
    (void)v;gx_wait(4);GX_CopyDisp(framebuffer,GX_FALSE);gx_wait(5);
#ifdef VIPER_WII_NATIVE_RES
    native_restore();
#endif
#endif
    VIDEO_SetNextFramebuffer(framebuffer);VIDEO_Flush();
#ifdef VIPER_WII_DISPLAY_CYCLE
    {static unsigned n;if(!(++n%120)){wii_gx_display_cycle();rt_log("VIPER WII DISPLAY mode=%d frame=%u\n",display_mode,render_frame);}}
#endif
#ifdef VIPER_WII_DISPLAY_MULTI
    if(display_applied!=display_mode){
        /* A frame boundary: nothing of the old box is recorded or queued. */
        int first=display_applied<0;
        display_applied=display_mode;
        wii_wide_set(DISPLAY_WIDE?85:0);   /* 16:9 at 384 lines (runtime/enhanced.c) */
        output_box_init();clip_key_stale=1;gx_shadow.proj_w=gx_shadow.proj_h=0;
        GX_SetViewport(RB_X,RB_Y,RB_W,RB_H,0,1);
        TEXLOAD_FORGET();
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
        wii_combiner_program_invalidate(&combiner_cache);
#endif
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
        for(unsigned i=0;i<COLOR_CACHE_SLOTS;i++)texture_resources[i].valid=0;   /* filters rebuilt */
#endif
        if(!first){display_mode_save();wii_menu_notice(display_names[display_mode]);}   /* changes only, not the restored mode at boot */
    }
#endif
#ifdef VIPER_WII_SUPERSAMPLE
    ss_begin();
#endif
}
#ifdef VIPER_WII_GX_CPU_FLOOR
static void cpu_floor_triangle(void *user,const WiiVoodooView *view,const WiiVoodooVertex vertices[3],uint32_t command){
    (void)user;(void)view;(void)vertices;(void)command;
}
static void cpu_floor_clear(void *user,const WiiVoodooView *view){(void)user;(void)view;}
static void cpu_floor_present(void *user,const WiiVoodooView *view,unsigned base){(void)user;(void)view;(void)base;}
#endif
#ifdef VIPER_WII_FRAME_CAPTURE
/* Diagnostic: save the finished output box (RGB8, row-major) of every STEPth
 * showroom frame (all linear-depth W in 2..20, which only the attract/result
 * car showroom uses), COUNT in all, as sd:/viper/capNNNNN.bin. A remote run
 * (report=) sends them back after the last one and returns to the loader. */
#ifndef VIPER_WII_FRAME_CAPTURE_STEP
#define VIPER_WII_FRAME_CAPTURE_STEP 1
#endif
#ifndef VIPER_WII_FRAME_CAPTURE_COUNT
#define VIPER_WII_FRAME_CAPTURE_COUNT 40
#endif
static void frame_capture(void){
    static unsigned taken,seen;static char names[VIPER_WII_FRAME_CAPTURE_COUNT][16];
    unsigned tris=0;for(unsigned e=0;e<32;e++)tris+=depth_trace_hist[e];
#if VIPER_WII_FRAME_CAPTURE+0>1
    (void)tris;if(taken>=VIPER_WII_FRAME_CAPTURE_COUNT||render_frame<VIPER_WII_FRAME_CAPTURE)return;   /* from a fixed frame */
#else
    if(taken>=VIPER_WII_FRAME_CAPTURE_COUNT||tris<60||depth_trace_min<0.05f||depth_trace_max>0.5f)return;
#endif
    if(seen++%VIPER_WII_FRAME_CAPTURE_STEP)return;
    wii_gx_own_thread();GX_DrawDone();
    snprintf(names[taken],sizeof names[taken],"cap%05u.bin",render_frame);
    char path[40];snprintf(path,sizeof path,"sd:/viper/%s",names[taken]);
    FILE *f=fopen(path,"wb");
    if(f){
        static uint8_t row[640*3];
        for(int y=0;y<OUT_H;y++){
            for(int x=0;x<OUT_W;x++){GXColor c;GX_PeekARGB(OUT_X+x,OUT_Y+y,&c);row[x*3]=c.r;row[x*3+1]=c.g;row[x*3+2]=c.b;}
            fwrite(row,1,OUT_W*3,f);
        }
#ifdef VIPER_WII_SUPERSAMPLE
        /* Trailer: how this frame was drawn. */
        fprintf(f,"ss_result=%d ss_backoff=%u ss_len=%u sprite_near_tris=%llu display=%d\n",ss_result,ss_backoff,ss_backoff_len,sprite_near_tris,display_mode);
        fputs(edge_sprite_text,f);
        for(unsigned i=0;i<state_tally_n;i++)fprintf(f,"state fbz=%08lx alpha=%08lx cp=%08lx tris=%u wb=%.4f..%.4f\n",(unsigned long)state_tally[i].fbz,(unsigned long)state_tally[i].alpha,(unsigned long)state_tally[i].cp,state_tally[i].n,(double)state_tally[i].wmin,(double)state_tally[i].wmax);
#endif
        fclose(f);
    }
    rt_log("VIPER WII CAPTURE frame=%u %dx%d %s\n",render_frame,OUT_W,OUT_H,f?"ok":"failed");
    if(++taken==VIPER_WII_FRAME_CAPTURE_COUNT&&wii_net_report_wanted()){
        const char *list[VIPER_WII_FRAME_CAPTURE_COUNT+1];
        for(unsigned i=0;i<VIPER_WII_FRAME_CAPTURE_COUNT;i++)list[i]=names[i];
        list[VIPER_WII_FRAME_CAPTURE_COUNT]=NULL;
        wii_net_report_send("sd:/viper",list);
        VIDEO_SetBlack(TRUE);VIDEO_Flush();VIDEO_WaitVSync();exit(0);
    }
}
#endif
void wii_gx_renderer_init(GXRModeObj *mode,void *xfb) {
#ifdef VIPER_WII_HANG_TRACE
    {static lwp_t t;LWP_CreateThread(&t,hang_watch,NULL,NULL,16384,90);}
#endif
#ifdef VIPER_WII_DISPLAY_MULTI
    display_mode_load();
#endif
    /* GX_Init before any other GX call (wii_menu_init invalidates the texture
     * cache). Until then the write-gather pipe still targets the Homebrew
     * Channel's old FIFO, which on hardware lies inside our guest RAM: stray
     * commands there broke the RAM oracle, moving with code layout. */
    memset(fifo,0,sizeof fifo);GX_Init(fifo,sizeof fifo);
#ifdef VIPER_WII_GX_BATCH
    gx_lazy_init();
#else
    gx_shadow_reset();
#endif
#ifdef VIPER_WII_GX_CPU_FLOOR
    rt_log("VIPER WII GX CPU FLOOR renderer callbacks disabled; native frontend retained\n");
#endif
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    wii_combiner_program_reset(&combiner_cache);
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    memset(&material_run,0,sizeof material_run);
#endif
#ifdef VIPER_WII_GX_BIND_PROFILE
    memset(bind_ticks,0,sizeof bind_ticks);
#endif
    render_frame=0;
    rt_log("VIPER WII GX frame_divisor=%u\n",VIPER_WII_GX_FRAME_DIVISOR);
    rt_log("VIPER WII GX render_fraction=%u/%u\n",VIPER_WII_GX_FRAME_NUMERATOR,VIPER_WII_GX_FRAME_DENOMINATOR);
#ifdef VIPER_WII_GX_DISABLE_FOG
    rt_log("VIPER WII GX EXPERIMENT fog rendering disabled\n");
#endif
#ifdef VIPER_WII_GX_DISABLE_TEXTURES
    rt_log("VIPER WII GX EXPERIMENT texture rendering disabled\n");
#endif
#ifdef VIPER_WII_WDEPTH_CONSTANT
    rt_log("VIPER WII EXPERIMENT constant W depth, no lookup\n");
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    rt_log("VIPER WII EXPERIMENT material-run GX skip\n");
#ifdef VIPER_WII_MATERIAL_BIND_SKIP
    rt_log("VIPER WII EXPERIMENT material-run bind and texgen skip\n");
#endif
#endif
    wdepth_approximations=0;
#ifdef VIPER_WII_GX_WDEPTH_APPROX
    active_depth_band=-1;
#ifdef VIPER_WII_GX_FOG_APPROX
    fog_valid=0;
#endif
#endif
    wii_menu_init();
#ifdef VIPER_WII_GX_RESIDENT_CACHE
    if(texture_resident_bytes){
        wii_gx_own_thread();gx_wait(2);
        for(unsigned i=0;i<COLOR_CACHE_SLOTS;i++)if(texture_slot_images[i])discard_texture_slot(i);
    }
#endif
    memset(texture_cache,0,sizeof texture_cache);texture_victim=0;texture_last_slot=COLOR_CACHE_SLOTS;
#ifdef VIPER_WII_TEXTURE_HINT
    for(unsigned i=0;i<256;i++)texture_hint[i]=COLOR_CACHE_SLOTS;
#endif
    memset(&profile,0,sizeof profile);memset(plane_ticks,0,sizeof plane_ticks);memset(setup_ticks,0,sizeof setup_ticks);
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
    memset(texture_resources,0,sizeof texture_resources);
#endif
#ifdef VIPER_WII_NATIVE_TEXTURE_SOURCE
    memset(&native_texture_sources,0,sizeof native_texture_sources);
#endif
#ifdef VIPER_WII_GX_TEXTURE_TRACE
    memset(texture_sources,0,sizeof texture_sources);
#endif
    video=mode;framebuffer=xfb;geometry=0;dither_approximations=0;
#ifdef VIPER_WII_GX_TMU_PIPELINE
    wii_gx_constant_fog_init();
#endif
#ifdef VIPER_WII_GX_RESIDENT_CACHE
    rt_log("VIPER WII GX resident_cache slots=%u budget=%u\n",COLOR_CACHE_SLOTS,COLOR_RESIDENT_BUDGET);
#else
    if(!texture_images)texture_images=memalign(32,COLOR_CACHE_SLOTS*COLOR_IMAGE_BYTES);
    if(!texture_images)rt_fatal("GX colour texture cache allocation");
    rt_log("VIPER WII GX color_cache=%08lx slots=%u bytes=%u\n",
        (unsigned long)(uintptr_t)texture_images,COLOR_CACHE_SLOTS,COLOR_CACHE_SLOTS*COLOR_IMAGE_BYTES);
    profile.color_cache_bytes=profile.color_cache_peak_bytes=COLOR_CACHE_SLOTS*COLOR_IMAGE_BYTES;
#endif
#if defined(VIPER_WII_GX_WDEPTH_APPROX) && !(defined(VIPER_WII_WDEPTH_LINEAR) && defined(VIPER_WII_GX_DISABLE_FOG))
    /* Linear W depth with fog off never reads the lookup tables: about 1 MB
     * and their startup generation are skipped. */
    depth_tables=memalign(32,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);
    if(!depth_tables)rt_fatal("GX immutable depth table allocation");
    rt_log("VIPER WII GX depth_tables=%08lx bytes=%u\n",(unsigned long)(uintptr_t)depth_tables,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);
    uint64_t start=gettime();
    for(unsigned band=0;band<DEPTH_TABLE_COUNT;band++){
        uint8_t *image=depth_tables+band*DEPTH_TABLE_BYTES;
        /* Preserve each original quarter sample and GX RGBA8 tile ordering. */
        for(unsigned y=0;y<DEPTH_TABLE_HEIGHT;y++)for(unsigned x=0;x<1024;x++){
            unsigned d=wdepth(lookup_sample(band,x,y));
            unsigned o=lookup_offset(x,y);
            image[o]=255;image[o+1]=d>>8;image[o+32]=d&255;image[o+33]=0;
        }
    }
    DCFlushRange(depth_tables,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);GX_InvalidateTexAll();
    profile.depth_upload_us=ticks_to_microsecs(gettime()-start);
#ifdef VIPER_WII_GX_LOOKUP_OBJECTS
    init_lookup_objects(depth_objects,depth_tables);
#endif
#ifdef VIPER_WII_GX_FOG_APPROX
    fog_tables=memalign(32,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);
    if(!fog_tables)rt_fatal("GX fog table allocation");
#ifdef VIPER_WII_GX_LOOKUP_OBJECTS
    init_lookup_objects(fog_objects,fog_tables);
#endif
    rt_log("VIPER WII GX fog_tables=%08lx bytes=%u\n",(unsigned long)(uintptr_t)fog_tables,DEPTH_TABLE_COUNT*DEPTH_TABLE_BYTES);
#endif
#endif
    output_box_init();
    GX_SetViewport(RB_X,RB_Y,RB_W,RB_H,0,1);GX_SetScissor(0,0,640,480);
    GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(video->fbWidth,video->xfbHeight);
    GX_SetDispCopyYScale((float)video->xfbHeight/480);
    /* RGB565_Z16 implicitly enables 3x multisampling and limits physical EFB
     * height to 264 lines. This renderer draws 480 lines without MSAA. */
    GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);
    rt_log("VIPER WII VIDEO format=RGB8_Z24 efb_render=640x480 fb_width=%u xfb_height=%u vi_height=%u tv_mode=%u\n",
        video->fbWidth,video->xfbHeight,video->viHeight,(unsigned)video->viTVMode);
    GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetColorUpdate(GX_TRUE);GX_CopyDisp(framebuffer,GX_TRUE);gx_wait(6);
    Mtx m;guMtxIdentity(m);GX_LoadPosMtxImm(m,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
#ifdef VIPER_WII_VERTEX_STQ
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_NRM,GX_NRM_XYZ,GX_F32,0);
    GX_SetVtxAttrFmt(GX_VTXFMT1,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT1,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetVtxAttrFmt(GX_VTXFMT1,GX_VA_NBT,GX_NRM_NBT,GX_F32,0);
    {Mtx identity;guMtxIdentity(identity);GX_LoadTexMtxImm(identity,STQ_IDENTITY,GX_MTX3x4);}
    stq_desc=-1;stq_set_mode(0);
#endif
    GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
    GX_SetNumTexGens(0);GX_SetNumTevStages(1);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    GX_SetCullMode(GX_CULL_NONE);GX_SetZCompLoc(GX_FALSE);GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
    wii_voodoo_set_material_invalidate(material_plans_reset);
#if defined(VIPER_WII_PLAN_KEEP) && defined(VIPER_WII_MATERIAL_RUN)
    wii_voodoo_set_material_texture_invalidate(material_texture_reset);
#endif
#endif
    WiiVoodooRenderer renderer={triangle,clear,present
    };
#ifdef VIPER_WII_GX_CPU_FLOOR
    /* Keep the native backing and frontend admission rules. Only renderer
     * callbacks are replaced; device packets and logical presents remain. */
    renderer.triangle=cpu_floor_triangle;
    renderer.clear=cpu_floor_clear;
    renderer.present=cpu_floor_present;
#endif
    wii_voodoo_set_renderer(&renderer,NULL);
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    wii_voodoo_set_layout_invalidate(texture_layout_reset);
#endif
}
/* main.c fences outside this file must see every buffered triangle. */
void wii_gx_batch_flush(void){
#ifdef VIPER_WII_GX_BATCH
    gx_batch_flush();   /* into the recorded frame first, as present does */
#endif
#ifdef VIPER_WII_SUPERSAMPLE
    ss_abort();   /* callers wait for the GPU next (main.c) */
#endif
}
void wii_gx_triangle_memo_stats(unsigned long long *hits,unsigned long long *misses){
#ifdef VIPER_WII_TRIANGLE_MEMO
    *hits=tri_memo.hits;*misses=tri_memo.misses;
#ifdef VIPER_WII_MEMO_MULTI
    rt_log("VIPER WII GX MEMO MULTI restores=%llu records=%llu\n",(unsigned long long)memo_multi_hits,(unsigned long long)memo_multi_records);
#endif
#ifdef VIPER_WII_TEXLOAD_SKIP
    rt_log("VIPER WII GX TEXLOAD skipped=%llu\n",(unsigned long long)texload.skipped);
#endif
    rt_log("VIPER WII GX MEMO MISS state=%llu texture=%llu gx=%llu packet=%llu alpha=%llu range=%llu depth=%llu stq=%llu invalid=%llu band=%llu\n",
        memo_miss[0],memo_miss[1],memo_miss[2],memo_miss[3],memo_miss[4],memo_miss[5],memo_miss[6],memo_miss[7],memo_miss[8],memo_miss[9]);
#else
    *hits=*misses=0;
#endif
}
void wii_gx_batch_stats(unsigned long long *draws,unsigned long long *triangles){
#ifdef VIPER_WII_GX_BATCH
    *draws=gx_batch.draws;*triangles=gx_batch.vertices/3;
#else
    *draws=*triangles=0;
#endif
}
