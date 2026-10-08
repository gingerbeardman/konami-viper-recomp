#ifndef VIPER_WII_GX_RENDERER_H
#define VIPER_WII_GX_RENDERER_H
#include <gccore.h>
/* GX device backend; unsupported device states stop explicitly. */
typedef struct {
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
    uint64_t texture_resource_hits,texture_resource_misses;
#endif
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    uint64_t combiner_hits,combiner_misses;
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    uint64_t material_run_resumes,material_run_emits;
#endif
    uint64_t fence_calls[8],fence_us[8];
    uint64_t split_us,split_children,depth_upload_us,color_upload_us;
    uint64_t split_depth_draws,lookup_plane_reuses;
    uint64_t tmu_pipeline_draws,dual_texture_draws;
    uint64_t color_cache_hits,color_cache_misses;
    uint64_t color_cache_bytes,color_cache_peak_bytes,color_cache_evictions;
    uint64_t texture_sources,texture_source_bytes,texture_source_updates,texture_palette_updates,texture_trace_overflow;
    uint64_t plane_us[3],plane_calls[3];
    uint64_t setup_us[5],setup_calls[5];
#ifdef VIPER_WII_GX_BIND_PROFILE
    uint64_t bind_us[2],bind_calls[2];
#endif
} WiiGXProfile;
WiiGXProfile wii_gx_profile(void);
void wii_gx_renderer_init(GXRModeObj *mode,void *xfb);
uint64_t wii_gx_dither_approximations(void);
uint64_t wii_gx_wdepth_approximations(void);
void wii_gx_batch_flush(void);
void wii_gx_batch_stats(unsigned long long *draws,unsigned long long *triangles);
void wii_gx_triangle_memo_stats(unsigned long long *hits,unsigned long long *misses);
void wii_gx_own_thread(void);
/* The game picture area in the 640x480 frame (aspect and 1:1 options). */
void wii_gx_output_box(int *x,int *y,int *w,int *h);
#endif
