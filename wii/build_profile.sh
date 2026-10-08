#!/bin/sh
# Repeatable guest60..65 race profiling; scripted checkpoint at guest75.
set -eu
cd "$(dirname "$0")/.."
divisor=${1:-1}
case "$divisor" in
    1|2) ;;
    *) echo 'Usage: sh wii/build_live.sh [1|2] (render every frame / alternate frames)' >&2; exit 2 ;;
esac
flags="-DVIPER_WII_IDLE_BATCH -DVIPER_WII_STRING_WORDS -DVIPER_WII_INLINE_VRAM_WRITE -DVIPER_WII_DIRECT_LFB_WORD -DVIPER_WII_FIFO_FRONTIER_WRITE -DVIPER_WII_FIFO_FORMAT59 -DVIPER_WII_WDEPTH_INTERIOR -DVIPER_WII_GX_LOOKUP_OBJECTS -DVIPER_WII_GX_RENDER -DVIPER_WII_GX_DITHER_APPROX -DVIPER_WII_GX_CHROMA_APPROX -DVIPER_WII_GX_WDEPTH_APPROX -DVIPER_WII_GX_FOG_APPROX -DVIPER_WII_GX_RESIDENT_CACHE -DVIPER_WII_GX_FRAME_DIVISOR=$divisor"
output="build/wii/perf-f$divisor"
# Retired experiments (measured slower or unsafe; see wii/PERFORMANCE_PLAN.md).
for retired in WII_PERF_FAST_VERTEX WII_PERF_NATIVE_VRAM WII_PERF_NATIVE_FIFO_WORDS WII_PERF_NATIVE_GX_PRODUCER WII_PERF_DIRECT_COMPLETION59 WII_PERF_DIRECT_PARTIAL59 WII_PERF_FIFO_SKIP_VERTEX_STORE WII_PERF_FIFO_PAYLOAD_OBSERVERS WII_PERF_LOCAL_FP WII_PERF_LOCAL_FP_RESTRICT WII_PERF_LOCAL_FP_DIAGNOSTIC WII_PERF_TRANSFORM_RAM WII_PERF_NATIVE_GX_MEMORY WII_PERF_NATIVE_GX_PACKETS WII_PERF_NATIVE_GX_ZERO_COPY WII_PERF_NATIVE_GX_VERTEX_INIT WII_PERF_DIRECT_CAPTURE59 WII_PERF_DIRECT_VERTEX59 WII_PERF_DIRECT_PREHEADER59 WII_PERF_BULK_TAIL WII_PERF_FRSQRTE_MEMO WII_PERF_FRSQRTE_KEY_HASH WII_PERF_FRSQRTE_SLOTS WII_PERF_FRSQRTE_CENSUS WII_PERF_FCTIW_TRUNC_INLINE WII_PERF_FAST_FP WII_PERF_PAIRED_PLANES WII_PERF_DRAW_PLAN_REUSE WII_PERF_TMU_PREFIX_CACHE WII_PERF_SKIP_LEGACY WII_PERF_PARENT_FOG WII_PERF_FOG_COMPARE_ONCE WII_PERF_LOOKUP_BIND_REUSE WII_PERF_PARENT_TEXTURE_OBJECT WII_PERF_PARENT_COMBINER_CONSTANTS WII_PERF_TEXTURE_BINDING_CACHE WII_PERF_LOOKUP_TEXTURE_CACHE WII_PERF_TEV_FUSION WII_PERF_CONFIG_CACHE WII_PERF_CONFIG_TRACE WII_PERF_MATERIAL_CENSUS; do
    eval "value=\${$retired:-0}"
    [ "$value" = 0 ] || { echo "$retired is retired" >&2; exit 2; }
done
case "${WII_PERF_RENDER_RATE:-full}" in
    full) ;;
    20) flags="$flags -DVIPER_WII_GX_FRAME_NUMERATOR=20 -DVIPER_WII_GX_FRAME_DENOMINATOR=29" ;;
    *) echo 'WII_PERF_RENDER_RATE must be full or 20' >&2; exit 2 ;;
esac
case "${WII_PERF_SPLIT_COPY_ONCE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_COPY_ONCE" ;;
    *) echo 'WII_PERF_SPLIT_COPY_ONCE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_PARENT_PROJECTION:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PARENT_PROJECTION" ;;
    *) echo 'WII_PERF_PARENT_PROJECTION must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_GX_PIPELINE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_COLOR_EQUATION"; output="build/wii/perf-pipeline-f$divisor" ;;
    *) echo 'WII_GX_PIPELINE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_GX_TMU_PIPELINE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_COLOR_EQUATION -DVIPER_WII_GX_TMU_PIPELINE"; output="build/wii/perf-tmu-f$divisor" ;;
    *) echo 'WII_GX_TMU_PIPELINE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_GRAPHICS:-1}" in
    1) ;;
    0) flags=$(printf '%s' "$flags" | sed 's/ -DVIPER_WII_GX_RENDER//'); output="build/wii/perf-nogx" ;;
    *) echo 'WII_PERF_GRAPHICS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_NATIVE_TEXTURE_ATTRS:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_NATIVE_TEXTURE_BIND:-0}" = 1 ] && [ "${WII_GX_TMU_PIPELINE:-0}" = 1 ] || { echo 'Native texture attributes require native binding and full TMU pipeline' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_ATTRS" ;;
    *) echo 'WII_PERF_NATIVE_TEXTURE_ATTRS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_PARENT_PROJECTION:-0}${WII_PERF_PARENT_COMBINER_CONSTANTS:-0}${WII_PERF_NATIVE_TEXTURE_ATTRS:-0}" in
    100) output="$output-pp" ;;
    010) output="$output-cc" ;;
    110) output="$output-pc" ;;
    001) output="$output-na" ;;
    101) output="$output-pa" ;;
    011) output="$output-ca" ;;
    111) output="$output-aa" ;;
esac
# Experimental object reuse gets its own short component, avoiding the limit
# reached by a full set of profiling suffixes.
if [ "${WII_PERF_PARENT_TEXTURE_OBJECT:-0}" = 1 ]; then output="$output/pto"; fi
case "${WII_PERF_NATIVE_TEXTURE_RESOURCE:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_NATIVE_TEXTURE_BIND:-0}" = 1 ] && [ "${WII_GX_TMU_PIPELINE:-0}" = 1 ] && [ "${WII_PERF_GRAPHICS:-1}" = 1 ] || {
           echo 'Native resources require rendering, native binding and full TMU pipeline' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_RESOURCE"; output="$output/gxresource" ;;
    *) echo 'WII_PERF_NATIVE_TEXTURE_RESOURCE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_NATIVE_TEXTURE_SOURCE:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_NATIVE_TEXTURE_RESOURCE:-0}" = 1 ] && [ "${WII_PERF_TEXTURE_LAYOUT_CACHE:-0}" = 1 ] || {
           echo 'Native source descriptors require native resources and device layout invalidation' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_SOURCE"; output="$output/gxsource" ;;
    *) echo 'WII_PERF_NATIVE_TEXTURE_SOURCE must be 0 or 1' >&2; exit 2 ;;
esac
if [ "${WII_PERF_SPLIT_COPY_ONCE:-0}" = 1 ]; then output="$output-splitcopy"; fi
case "${WII_PERF_SPLIT_RAM_HELPERS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_SPLIT_RAM_HELPERS"; output="$output-split-ram-helpers" ;;
    *) echo 'WII_PERF_SPLIT_RAM_HELPERS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_F64_RAM:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_F64_RAM"; output="$output-f64-ram" ;;
    *) echo 'WII_PERF_F64_RAM must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BOUNDED_SUBMISSION:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Bounded submission requires bulk writer specialization' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BOUNDED_SUBMISSION"; output="$output-bounded-submission" ;;
    *) echo 'WII_PERF_BOUNDED_SUBMISSION must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_PUBLISH:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Bulk publish requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_PUBLISH"; output="$output-bulk-publish" ;;
    *) echo 'WII_PERF_BULK_PUBLISH must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_TEXTURE_LAYOUT_CACHE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_LAYOUT_CACHE"; output="$output-texture-layout-cache" ;;
    *) echo 'WII_PERF_TEXTURE_LAYOUT_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_PC_WINDOW:-start}" in
    start) pc_begin=60; pc_end=65 ;;
    driving) pc_begin=70; pc_end=75; output="$output-drivepc" ;;
    *) echo 'WII_PERF_PC_WINDOW must be start or driving' >&2; exit 2 ;;
esac
flags="$flags -DVIPER_WII_SCRIPTED_RACE -DVIPER_WII_PC_PROFILE -DVIPER_WII_PC_PROFILE_BEGIN=$pc_begin -DVIPER_WII_PC_PROFILE_END=$pc_end"
# Renderer/lookup timing (clock reads per triangle and per guest-call lookup):
# measurement only, absent from playable builds.
case "${WII_PERF_PLANE_PROFILE:-1}" in
    1) flags="$flags -DVIPER_WII_GX_PLANE_PROFILE" ;;
    0) output="$output-noplane" ;;
    *) echo 'WII_PERF_PLANE_PROFILE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_SUBMISSION:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_SUBMISSION_PROFILE"; output="$output/submission" ;;
    *) echo 'WII_PERF_SUBMISSION must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_WRITER:-0}" in
    0) ;;
    1)
       flags="$flags -DVIPER_WII_BULK_WRITER"; output="$output-bulk-writer" ;;
    *) echo 'WII_PERF_BULK_WRITER must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_RAM:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Bulk RAM requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_RAM"; output="$output-bulk-ram" ;;
    *) echo 'WII_PERF_BULK_RAM must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_NOALIAS:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] && [ "${WII_PERF_BULK_RAM:-0}" = 1 ] || {
           echo 'Bulk noalias requires bulk writer and bulk RAM' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_BULK_NOALIAS"; output="$output-noalias" ;;
    *) echo 'WII_PERF_BULK_NOALIAS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_STAGING_GUARD_OR:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Staging guard requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_STAGING_GUARD_OR"; output="$output/sgo" ;;
    *) echo 'WII_PERF_STAGING_GUARD_OR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_ADMISSION_DIAGNOSTIC:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Bulk admission diagnostic requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_ADMISSION_DIAGNOSTIC"; output="$output/bad" ;;
    *) echo 'WII_PERF_BULK_ADMISSION_DIAGNOSTIC must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BULK_INCOMPLETE_HEADER:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] && [ "${WII_PERF_BULK_PUBLISH:-0}" = 1 ] || { echo 'Incomplete header requires bulk writer and publication' >&2; exit 2; }
       [ "${WII_PERF_DIRECT_CAPTURE59:-0}" = 0 ] || { echo 'Incomplete header excludes direct capture' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_INCOMPLETE_HEADER"; output="$output/bih" ;;
    *) echo 'WII_PERF_BULK_INCOMPLETE_HEADER must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_DRIVING_BULK:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || {
           echo 'Driving bulk requires bulk writer' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_DRIVING_BULK"; output="$output-db" ;;
    *) echo 'WII_PERF_DRIVING_BULK must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_DRIVING_TAIL:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_DRIVING_BULK:-0}" = 1 ] || {
           echo 'Driving tail requires driving bulk' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_DRIVING_TAIL"; output="${output}t" ;;
    *) echo 'WII_PERF_DRIVING_TAIL must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_DIRECT_STATE:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Direct state requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_STATE"; output="$output/gxstate" ;;
    *) echo 'WII_PERF_DIRECT_STATE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_CR_UNPACK:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CR_UNPACK"; output="$output-cr-unpack" ;;
    *) echo 'WII_PERF_CR_UNPACK must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_PC_BUCKET_BYTES:-64}" in
    64) ;;
    4) flags="$flags -DVIPER_WII_PC_PROFILE_BUCKET_BYTES=4"; output="$output-pc4" ;;
    *) echo 'WII_PERF_PC_BUCKET_BYTES must be 4 or 64' >&2; exit 2 ;;
esac
# Diagnostic: count MMIO accesses by guest region and host call site.
case "${WII_PERF_MMIO_CENSUS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MMIO_CENSUS"; output="$output-mmio-census" ;;
    *) echo 'WII_PERF_MMIO_CENSUS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_PC_LR:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PC_PROFILE_LR"; output="$output-pc-lr" ;;
    *) echo 'WII_PERF_PC_LR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BIND_PROFILE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_BIND_PROFILE"; output="$output-bind-profile" ;;
    *) echo 'WII_PERF_BIND_PROFILE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_CPU_FLOOR:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_GRAPHICS:-1}" = 1 ] || { echo 'CPU floor retains GX initialization; use WII_PERF_GRAPHICS=1' >&2; exit 2; }
       flags="$flags -DVIPER_WII_GX_CPU_FLOOR"; output="$output-cpu-floor" ;;
    *) echo 'WII_PERF_CPU_FLOOR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_GPR_MULTIPLE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GPR_MULTIPLE"; output="$output-gpr-multiple" ;;
    *) echo 'WII_PERF_GPR_MULTIPLE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_SAMPLE_US:-997}" in
    997) ;;
    733|1301) flags="$flags -DVIPER_WII_PC_PROFILE_PERIOD_US=$WII_PERF_SAMPLE_US"; output="$output-sample-$WII_PERF_SAMPLE_US" ;;
    *) echo 'WII_PERF_SAMPLE_US must be 733, 997 or 1301' >&2; exit 2 ;;
esac
case "${WII_PERF_EARLY_CULL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_EARLY_CULL"; output="$output-early-cull" ;;
    *) echo 'WII_PERF_EARLY_CULL must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_NATIVE_TEXTURE_BIND:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_NATIVE_TEXTURE_BIND"; output="$output-native-texture-bind" ;;
    *) echo 'WII_PERF_NATIVE_TEXTURE_BIND must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_MATERIAL_PLAN_CACHE:-0}" in
    0) ;;
    1)
       [ "${WII_GX_TMU_PIPELINE:-0}" = 1 ] && [ "${WII_PERF_DRAW_PLAN_REUSE:-0}" = 0 ] || { echo 'Material plan cache requires full TMU and excludes parent reuse' >&2; exit 2; }
       flags="$flags -DVIPER_WII_MATERIAL_PLAN_CACHE"; output="$output-material-plan-cache" ;;
    *) echo 'WII_PERF_MATERIAL_PLAN_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_FOG:-0}" in
    1) ;;
    0) flags="$flags -DVIPER_WII_GX_DISABLE_FOG"; output="$output/nofog" ;;
    *) echo 'WII_PERF_FOG must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_TEXTURES:-1}" in
    1) ;;
    0) flags="$flags -DVIPER_WII_GX_DISABLE_TEXTURES"; output="$output-notex" ;;
    *) echo 'WII_PERF_TEXTURES must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_COMBINER_PROGRAM_CACHE:-0}" in
    0) ;;
    1)
        if [ "${WII_GX_TMU_PIPELINE:-0}" != 1 ] || [ "${WII_PERF_NATIVE_TEXTURE_BIND:-0}" != 1 ] || [ "${WII_PERF_MATERIAL_PLAN_CACHE:-0}" != 1 ]; then
            echo 'Combiner program cache requires full TMU, native texture bind and material plan cache' >&2; exit 2
        fi
        if [ "${WII_PERF_TMU_PREFIX_CACHE:-0}" = 1 ] || [ "${WII_PERF_CONFIG_TRACE:-0}" = 1 ] || [ "${WII_PERF_CONFIG_CACHE:-0}" = 1 ] || [ "${WII_PERF_TEV_FUSION:-0}" = 1 ]; then
            echo 'Combiner program cache excludes the TMU prefix cache, setter trace/cache and TEV fusion' >&2; exit 2
        fi
        flags="$flags -DVIPER_WII_COMBINER_PROGRAM_CACHE"; output="$output-combiner-program-cache" ;;
    *) echo 'WII_PERF_COMBINER_PROGRAM_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_MATERIAL_RUN:-0}" in
    0) ;;
    1)
        if [ "${WII_GX_TMU_PIPELINE:-0}" != 1 ] || [ "${WII_PERF_NATIVE_TEXTURE_BIND:-0}" != 1 ] || [ "${WII_PERF_MATERIAL_PLAN_CACHE:-0}" != 1 ] || [ "${WII_PERF_COMBINER_PROGRAM_CACHE:-0}" != 1 ]; then
            echo 'Material-run resume requires full TMU, native texture bind, material plans and the combiner cache' >&2; exit 2
        fi
        flags="$flags -DVIPER_WII_MATERIAL_RUN"; output="$output-material-run" ;;
    *) echo 'WII_PERF_MATERIAL_RUN must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_MATERIAL_BIND_SKIP:-0}" in
    0) ;;
    1)
        if [ "${WII_PERF_MATERIAL_RUN:-0}" != 1 ]; then
            echo 'Material bind skip requires material-run' >&2; exit 2
        fi
        flags="$flags -DVIPER_WII_MATERIAL_BIND_SKIP"; output="$output-bindskip" ;;
    *) echo 'WII_PERF_MATERIAL_BIND_SKIP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_MERGED_LOOKUP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_MERGED_LOOKUP"; output="$output-merged-lookup" ;;
    *) echo 'WII_PERF_MERGED_LOOKUP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_WDEPTH_CONSTANT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_CONSTANT"; output="$output-constdepth" ;;
    *) echo 'WII_PERF_WDEPTH_CONSTANT must be 0 or 1' >&2; exit 2 ;;
esac
# Per-vertex Z of 1-wb: the same per-pixel depth order as the W-buffer, no
# band split or lookup texture. Needs fog off; replaces constant W depth.
case "${WII_PERF_WDEPTH_LINEAR:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_WDEPTH_CONSTANT:-0}" = 0 ] && [ "${WII_PERF_FOG:-0}" = 0 ] || { echo 'Linear W depth needs fog off and constant W depth off' >&2; exit 2; }
       flags="$flags -DVIPER_WII_WDEPTH_LINEAR"; output="$output-lineardepth" ;;
    *) echo 'WII_PERF_WDEPTH_LINEAR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_BORROW_CLIP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_BORROW_CLIP"; output="$output-borrow-clip" ;;
    *) echo 'WII_PERF_BORROW_CLIP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_LOOKUP_REUSE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOOKUP_PLANE_REUSE"; output="$output-lookup-reuse" ;;
    *) echo 'WII_PERF_LOOKUP_REUSE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_ZERO_PLANE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_ZERO_PLANE_QUOTIENT"; output="$output-zero-plane" ;;
    *) echo 'WII_PERF_ZERO_PLANE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_FIFO_BOUNDS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FIFO_BOUNDS_CACHE"; output="$output-fifo-bounds" ;;
    *) echo 'WII_PERF_FIFO_BOUNDS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_FIFO_SPLIT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FIFO_SPLIT_TRIANGLE"; output="$output-fifo-split" ;;
    *) echo 'WII_PERF_FIFO_SPLIT must be 0 or 1' >&2; exit 2 ;;
esac
# Per-vertex S,T,Q as GX normal/binormal instead of a per-triangle plane
# matrix solve and texture-matrix load (full TMU pipeline).
case "${WII_PERF_VERTEX_STQ:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_VERTEX_STQ"; output="$output-vstq" ;;
    *) echo 'WII_PERF_VERTEX_STQ must be 0 or 1' >&2; exit 2 ;;
esac
# Native transcription of the gl packed-vertex draw routine f_gl_0002adac
# (wii/native_gl_draw.c): guest registers in locals, same operations.
case "${WII_PERF_NATIVE_GL_DRAW:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_BULK_WRITER:-0}" = 1 ] || { echo 'Native gl draw needs the bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_GL_DRAW"; output="$output-ngl" ;;
    *) echo 'WII_PERF_NATIVE_GL_DRAW must be 0 or 1' >&2; exit 2 ;;
esac
# The native draw routine hands complete format-59 packets straight to the
# device decoder instead of storing their words through the FIFO.
case "${WII_PERF_DIRECT_TRIANGLES:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_NATIVE_GL_DRAW:-0}" = 1 ] || { echo 'Direct triangles need the native gl draw routine' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_TRIANGLES"; output="$output-dtri" ;;
    *) echo 'WII_PERF_DIRECT_TRIANGLES must be 0 or 1' >&2; exit 2 ;;
esac
# Hot gl functions with guest registers in C locals (wii/localize_function.py).
case "${WII_PERF_LOCALIZE_GL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_GL"; output="$output-loc" ;;
    *) echo 'WII_PERF_LOCALIZE_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Inline rt_cr_pack/unpack and the RAM path of rt_lswi/stswi (runtime/ppc_rt.h)
# so generated call sites fold their constant operands.
case "${WII_PERF_INLINE_HELPERS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_INLINE_HELPERS"; output="$output-inl" ;;
    *) echo 'WII_PERF_INLINE_HELPERS must be 0 or 1' >&2; exit 2 ;;
esac
# Buffer consecutive triangles that need no GX state change into one draw
# (wii/gx_batch.h). Needs the state shadow and per-vertex STQ.
# Gather the localized gl producers' FIFO stores into bulk writes.
case "${WII_PERF_GATHER_GL:-0}" in
    0) ;;
    1) [ "${WII_PERF_LOCALIZE_GL:-0}" = 1 ] || { echo 'WII_PERF_GATHER_GL needs WII_PERF_LOCALIZE_GL=1' >&2; exit 2; }
       flags="$flags -DVIPER_WII_GATHER_GL"; output="$output-gather" ;;
    *) echo 'WII_PERF_GATHER_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Guest byte copies to and from LAN controller RAM skip the bus range chain
# (runtime/hw.c rt_mmio_*; exact while MMIO logging is off).
case "${WII_PERF_FAST_LANC_RAM:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_FAST_LANC_RAM"; output="$output-lanc" ;;
    *) echo 'WII_PERF_FAST_LANC_RAM must be 0 or 1' >&2; exit 2 ;;
esac
# Decode each triangle packet's vertices once and pass strip triangles to the
# renderer in place; store direct packets as whole words (exact).
case "${WII_PERF_PACKET_VERTICES:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PACKET_VERTICES"; output="$output-pverts" ;;
    *) echo 'WII_PERF_PACKET_VERTICES must be 0 or 1' >&2; exit 2 ;;
esac
# Replay the last triangle's setup when device state, texture epoch, packet
# format and GX state are unchanged (exact; see triangle_memo in gx_renderer.c).
case "${WII_PERF_TRIANGLE_MEMO:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TRIANGLE_MEMO"; output="$output-tmemo" ;;
    *) echo 'WII_PERF_TRIANGLE_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
# Native gl 0x29d28 list draw (wii/native_gl_list.c): whole packets go to
# the device through direct triangles instead of FIFO word stores (exact).
case "${WII_PERF_NATIVE_GL_LIST:-0}" in
    0) ;;
    1) [ "${WII_PERF_DIRECT_TRIANGLES:-0}" = 1 ] || { echo 'Native gl list needs direct triangles' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_GL_LIST"; output="$output-nlist" ;;
    *) echo 'WII_PERF_NATIVE_GL_LIST must be 0 or 1' >&2; exit 2 ;;
esac
# Localized gl 0x28fc4 strip producer with its FIFO words gathered and each
# header-completed packet handed to direct triangles (exact).
case "${WII_PERF_DIRECT_GL:-0}" in
    0) ;;
    1) [ "${WII_PERF_DIRECT_TRIANGLES:-0}" = 1 ] && [ "${WII_PERF_LOCALIZE_GL:-0}" = 1 ] || { echo 'Direct gl needs direct triangles and localized gl' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_GL"; output="$output-dgl" ;;
    *) echo 'WII_PERF_DIRECT_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Generated code keeps a local copy of the guest RAM base and tests one
# bound per access (wii/native_ram.h; same accesses, exact).
case "${WII_PERF_RAM_BASE_LOCAL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_BASE_LOCAL"; output="$output-rbl" ;;
    *) echo 'WII_PERF_RAM_BASE_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Out-of-line slow path for the RAM_BASE_LOCAL accessors: much smaller code,
# slightly slower in Dolphin (no cache model). Pending hardware timing.
case "${WII_PERF_RAM_SLOW_CALL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_SLOW_CALL"; output="$output-rsc" ;;
    *) echo 'WII_PERF_RAM_SLOW_CALL must be 0 or 1' >&2; exit 2 ;;
esac
# Aligned in-RAM guest floats/doubles straight into/from FPRs (big-endian
# host; no FPR<->GPR stack trip). Same bits; matters on Broadway.
case "${WII_PERF_DIRECT_FP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_FP"; output="$output-dfp" ;;
    *) echo 'WII_PERF_DIRECT_FP must be 0 or 1' >&2; exit 2 ;;
esac
# Broadway cache hints with no architectural effect on results: dcbz for
# whole VRAM lines a direct packet overwrites, dcbt ahead of vertex data.
case "${WII_PERF_CACHE_HINTS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CACHE_HINTS"; output="$output-chint" ;;
    *) echo 'WII_PERF_CACHE_HINTS must be 0 or 1' >&2; exit 2 ;;
esac
# Hot functions (physical-Wii profile, wii/hot_layout.txt) placed contiguously
# hottest first; placement only.
case "${WII_PERF_HOT_LAYOUT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_HOT_LAYOUT"; output="$output-hot" ;;
    *) echo 'WII_PERF_HOT_LAYOUT must be 0 or 1' >&2; exit 2 ;;
esac
# 32-bit cycle budget (slices <= 20000 cycles; same values, cheaper checks).
case "${WII_PERF_BUDGET32:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BUDGET32"; output="$output-b32" ;;
    *) echo 'WII_PERF_BUDGET32 must be 0 or 1' >&2; exit 2 ;;
esac
# Drop guest carry writes overwritten in the same straight-line code before
# any read, branch, checkpoint or call (non-gl modules; exact).
case "${WII_PERF_DEAD_CARRY:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DEAD_CARRY"; output="$output-dcarry" ;;
    *) echo 'WII_PERF_DEAD_CARRY must be 0 or 1' >&2; exit 2 ;;
esac
# One-instruction in-RAM test (below RAM_SIZE and naturally aligned; others
# take the original accessor; exact).
case "${WII_PERF_RAM_MASK_TEST:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_MASK_TEST"; output="$output-rmask" ;;
    *) echo 'WII_PERF_RAM_MASK_TEST must be 0 or 1' >&2; exit 2 ;;
esac
# Texture base-address changes keep material plans and combiner programs
# (the planner never reads them); only texture reuse ends (exact).
case "${WII_PERF_PLAN_KEEP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PLAN_KEEP"; output="$output-pkeep" ;;
    *) echo 'WII_PERF_PLAN_KEEP must be 0 or 1' >&2; exit 2 ;;
esac
# Memo of TMU pipeline plans keyed by every planner input (exact).
case "${WII_PERF_PLAN_MEMO:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PLAN_MEMO"; output="$output/pmemo" ;;
    *) echo 'WII_PERF_PLAN_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
# Combiner program stays resident across material resets when the plan bytes match (exact).
case "${WII_PERF_COMBINER_KEEP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_COMBINER_KEEP"; output="$output-ckeep" ;;
    *) echo 'WII_PERF_COMBINER_KEEP must be 0 or 1' >&2; exit 2 ;;
esac
# Skip a texture load that repeats the last load exactly (needs native texture resources).
case "${WII_PERF_TEXLOAD_SKIP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXLOAD_SKIP"; output="$output-tlskip" ;;
    *) echo 'WII_PERF_TEXLOAD_SKIP must be 0 or 1' >&2; exit 2 ;;
esac
# Present queues the EFB->XFB copy without waiting for it (hardware: ~0.4 ms a frame).
case "${WII_PERF_PRESENT_ASYNC:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PRESENT_ASYNC"; output="$output-pasync" ;;
    *) echo 'WII_PERF_PRESENT_ASYNC must be 0 or 1' >&2; exit 2 ;;
esac
# GX command FIFO size in KB (default 256).
case "${WII_PERF_GX_FIFO_KB:-256}" in
    256) ;;
    512|1024|2048) flags="$flags -DVIPER_WII_GX_FIFO_KB=${WII_PERF_GX_FIFO_KB}"; output="$output-fifo${WII_PERF_GX_FIFO_KB}" ;;
    *) echo 'WII_PERF_GX_FIFO_KB must be 256, 512, 1024 or 2048' >&2; exit 2 ;;
esac
# Generated functions take PPCContext *restrict c (guest stores do not reload context fields).
case "${WII_PERF_RESTRICT_CTX:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RESTRICT_CTX"; output="$output/rctx" ;;
    *) echo 'WII_PERF_RESTRICT_CTX must be 0 or 1' >&2; exit 2 ;;
esac
# Block cycle charges kept in a local, written back before any observer (exact).
case "${WII_PERF_BUDGET_LOCAL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BUDGET_LOCAL"; output="$output/blocal" ;;
    *) echo 'WII_PERF_BUDGET_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Screen clears drawn with GX clipping fully off (hardware drops the far-plane quad: black bands).
case "${WII_PERF_CLEAR_NOCLIP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CLEAR_NOCLIP"; output="$output-cnoclip" ;;
    *) echo 'WII_PERF_CLEAR_NOCLIP must be 0 or 1' >&2; exit 2 ;;
esac
# Batch flush copies with the word count in a local (no reload per pipe store).
case "${WII_PERF_BATCH_COPY_LOCAL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BATCH_COPY_LOCAL"; output="$output-bcl" ;;
    *) echo 'WII_PERF_BATCH_COPY_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Triangle memo range checks as integer tests on float bits, one branch (exact for all 2^32 inputs).
case "${WII_PERF_MEMO_INT_CHECKS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MEMO_INT_CHECKS"; output="$output-mic" ;;
    *) echo 'WII_PERF_MEMO_INT_CHECKS must be 0 or 1' >&2; exit 2 ;;
esac
# Goto switches on strided addresses switched on rotr(x - base, shift): dense cases (exact).
case "${WII_PERF_DENSE_SWITCH:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DENSE_SWITCH"; output="$output-dsw" ;;
    *) echo 'WII_PERF_DENSE_SWITCH must be 0 or 1' >&2; exit 2 ;;
esac
# Exact rsqrt forced inline into the native 2adac vertex block.
case "${WII_PERF_RSQRT_INLINE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RSQRT_INLINE"; output="$output-rsqi" ;;
    *) echo 'WII_PERF_RSQRT_INLINE must be 0 or 1' >&2; exit 2 ;;
esac
# Direct packets: fixed format-59 length test, no no-op presence clear (exact).
case "${WII_PERF_DIRECT_LEAN:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_LEAN"; output="$output-dlean" ;;
    *) echo 'WII_PERF_DIRECT_LEAN must be 0 or 1' >&2; exit 2 ;;
esac
# Seven more hot gl functions with guest registers localized (exact by construction).
case "${WII_PERF_LOCALIZE_GL_MORE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_GL_MORE"; output="$output-lglm" ;;
    *) echo 'WII_PERF_LOCALIZE_GL_MORE must be 0 or 1' >&2; exit 2 ;;
esac
# Hardware A/B: six more hot functions localized despite 2.3-3.9x code growth.
case "${WII_PERF_LOCALIZE_EXTRA:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_EXTRA"; output="$output-lext" ;;
    *) echo 'WII_PERF_LOCALIZE_EXTRA must be 0 or 1' >&2; exit 2 ;;
esac
# Direct packets decoded by a format-59-only triangle_packet from the producer's words (exact).
case "${WII_PERF_DIRECT_PACKET59:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_PACKET59"; output="$output-dp59" ;;
    *) echo 'WII_PERF_DIRECT_PACKET59 must be 0 or 1' >&2; exit 2 ;;
esac
# The memo-hit triangle path compiled at -O3 (function attribute).
case "${WII_PERF_RENDER_O3:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RENDER_O3"; output="$output-ro3" ;;
    *) echo 'WII_PERF_RENDER_O3 must be 0 or 1' >&2; exit 2 ;;
esac
# Triangle memo hits prepare each packet vertex once (strips share vertices; exact).
case "${WII_PERF_MEMO_VERTEX_PREP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MEMO_VERTEX_PREP"; output="$output-mvp" ;;
    *) echo 'WII_PERF_MEMO_VERTEX_PREP must be 0 or 1' >&2; exit 2 ;;
esac
# Guest float/double accesses stay in FPRs: typed fast path, out-of-line slow path (exact).
case "${WII_PERF_FP_SLOW_CALL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FP_SLOW_CALL"; output="$output-fpsc" ;;
    *) echo 'WII_PERF_FP_SLOW_CALL must be 0 or 1' >&2; exit 2 ;;
esac
# Fastfill writes whole VRAM words per row (same bytes, versions and epochs; exact).
case "${WII_PERF_FAST_FILL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FAST_FILL"; output="$output-ffill" ;;
    *) echo 'WII_PERF_FAST_FILL must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_HEAP_POISON:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_HEAP_POISON"; output="$output-poison" ;;
    *) echo 'WII_PERF_HEAP_POISON must be 0 or 1' >&2; exit 2 ;;
esac
# Exact rsqrt results remembered by input bits (exact by construction).
case "${WII_PERF_RSQRT_MEMO:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RSQRT_MEMO"; output="$output-rsqmemo" ;;
    *) echo 'WII_PERF_RSQRT_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
# Timebase reads without a 64-bit division per read (exact).
case "${WII_PERF_MFTB_INCR:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_MFTB_INCREMENTAL"; output="$output-mftbincr" ;;
    *) echo 'WII_PERF_MFTB_INCR must be 0 or 1' >&2; exit 2 ;;
esac
# Skip the memo-hit dithering tally (a log statistic, no effect on output).
case "${WII_PERF_NO_DITHER_STATS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NO_DITHER_STATS"; output="$output-nodstat" ;;
    *) echo 'WII_PERF_NO_DITHER_STATS must be 0 or 1' >&2; exit 2 ;;
esac
# The gl interpreter's shared return dispatch as a jump table (exact).
case "${WII_PERF_DENSE_LR:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DENSE_LR"; output="$output-denselr" ;;
    *) echo 'WII_PERF_DENSE_LR must be 0 or 1' >&2; exit 2 ;;
esac
# Texture cache scan compares full keys only where the stored key hash agrees.
case "${WII_PERF_TEXTURE_SLOT_HINT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_SLOT_HINT"; output="$output-tslothint" ;;
    *) echo 'WII_PERF_TEXTURE_SLOT_HINT must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_FIBER_COUNT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_FIBER_COUNT"; output="$output-fibercount" ;;
    *) echo 'WII_PERF_FIBER_COUNT must be 0 or 1' >&2; exit 2 ;;
esac
# Measurement: log buffered in RAM, written at the end (no SD writes timed).
case "${WII_PERF_LOG_BUFFERED:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOG_BUFFERED"; output="$output-logbuf" ;;
    *) echo 'WII_PERF_LOG_BUFFERED must be 0 or 1' >&2; exit 2 ;;
esac
# Prefetch distance in the 0x28fc4 strip loops (bytes; default 0x40).
if [ -n "${WII_PERF_STRIP_PREFETCH:-}" ]; then
    flags="$flags -DVIPER_WII_STRIP_PREFETCH=${WII_PERF_STRIP_PREFETCH}u"; output="$output-spf$WII_PERF_STRIP_PREFETCH"
fi
# Direct triangle packets are not copied into Voodoo VRAM (RAM/EFB exact;
# the device-state VRAM hash changes).
case "${WII_PERF_DIRECT_NO_VRAM:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_NO_VRAM"; output="$output-dnovram" ;;
    *) echo 'WII_PERF_DIRECT_NO_VRAM must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_MEMO_REVISIT_STATS:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MEMO_REVISIT_STATS"; output="$output-revisit" ;;
    *) echo 'WII_PERF_MEMO_REVISIT_STATS must be 0 or 1' >&2; exit 2 ;;
esac
# Leaf guest callees (0x211d4) inlined into localized gl callers (exact).
case "${WII_PERF_INLINE_LEAF:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_INLINE_LEAF"; output="$output-inleaf" ;;
    *) echo 'WII_PERF_INLINE_LEAF must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_ALLOC_LOG:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_ALLOC_LOG"; output="$output-alloclog" ;;
    *) echo 'WII_PERF_ALLOC_LOG must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_STRAY_WATCH:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_STRAY_WATCH"; output="$output-stray" ;;
    *) echo 'WII_PERF_STRAY_WATCH must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_FP_SLOW_PARTS:-7}" in
    7) ;;
    0|1|2|3|4|5|6) flags="$flags -DVIPER_WII_FP_SLOW_PARTS=$WII_PERF_FP_SLOW_PARTS"; output="$output-fpp$WII_PERF_FP_SLOW_PARTS" ;;
    *) echo 'WII_PERF_FP_SLOW_PARTS must be 0-7' >&2; exit 2 ;;
esac
case "${WII_PERF_FORMAT_PROFILE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FIFO_FORMAT_PROFILE"; output="$output-fmtprof" ;;
    *) echo 'WII_PERF_FORMAT_PROFILE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_EPOCH_TRACE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_EPOCH_TRACE"; output="$output-etrace" ;;
    *) echo 'WII_PERF_EPOCH_TRACE must be 0 or 1' >&2; exit 2 ;;
esac
# Hardware A/B: 2adac vertex prefetch distance in bytes (0 = none; default 64).
case "${WII_PERF_2ADAC_PREFETCH:-64}" in
    64) ;;
    0|32|96|128|160) flags="$flags -DVIPER_WII_2ADAC_PREFETCH=${WII_PERF_2ADAC_PREFETCH}u"; output="$output-pf${WII_PERF_2ADAC_PREFETCH}" ;;
    *) echo 'WII_PERF_2ADAC_PREFETCH must be 0, 32, 64, 96, 128 or 160' >&2; exit 2 ;;
esac
# Guest registers in C locals for every generated function the localizer
# accepts (wii/localize_function.py; exact by construction, test_localize.py).
case "${WII_PERF_LOCALIZE_ALL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_ALL"; output="$output-locall" ;;
    *) echo 'WII_PERF_LOCALIZE_ALL must be 0 or 1' >&2; exit 2 ;;
esac
# Guest registers in C locals only for the profiled hot functions listed in
# wii/localize_hot.txt (small code growth; same proof as LOCALIZE_ALL).
case "${WII_PERF_LOCALIZE_HOT:-0}" in
    0) ;;
    1) [ "${WII_PERF_LOCALIZE_ALL:-0}" = 0 ] || { echo 'LOCALIZE_HOT and LOCALIZE_ALL are alternatives' >&2; exit 2; }
       flags="$flags -DVIPER_WII_LOCALIZE_HOT"; output="$output-lochot" ;;
    *) echo 'WII_PERF_LOCALIZE_HOT must be 0 or 1' >&2; exit 2 ;;
esac
# Direct-mapped cache in front of the indirect-call dispatcher (runtime/cpu.c;
# only unambiguous targets, keyed by address and first code word: exact).
case "${WII_PERF_LOOKUP_CACHE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_LOOKUP_CACHE"; output="$output-lcache" ;;
    *) echo 'WII_PERF_LOOKUP_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
# Texture cache lookup tries the slot last bound for the request's key hash
# before scanning all slots (at most one slot can match: exact).
case "${WII_PERF_TEXTURE_HINT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_HINT"; output="$output-thint" ;;
    *) echo 'WII_PERF_TEXTURE_HINT must be 0 or 1' >&2; exit 2 ;;
esac
# Link code byte copy from LAN RAM in bulk (needs FAST_LANC_RAM; exact).
case "${WII_PERF_LANC_COPY:-0}" in
    0) ;;
    1) [ "${WII_PERF_FAST_LANC_RAM:-0}" = 1 ] || { echo 'LANC_COPY needs FAST_LANC_RAM' >&2; exit 2; }
       flags="$flags -DVIPER_WII_LANC_COPY"; output="$output/lcopy" ;;
    *) echo 'WII_PERF_LANC_COPY must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_GX_BATCH:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_BATCH"; output="$output-batch" ;;
    *) echo 'WII_PERF_GX_BATCH must be 0 or 1' >&2; exit 2 ;;
esac
# Drop renderer GX fixed-function setter calls that repeat the last value.
case "${WII_PERF_GX_STATE_SHADOW:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_STATE_SHADOW"; output="$output-gxshadow" ;;
    *) echo 'WII_PERF_GX_STATE_SHADOW must be 0 or 1' >&2; exit 2 ;;
esac
# Native fctiw/fctiwz inline (runtime/ppc_rt.h); same bits as rt_fctiw.
case "${WII_PERF_NATIVE_FCTIW:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NATIVE_FCTIW"; output="$output-fctiw" ;;
    *) echo 'WII_PERF_NATIVE_FCTIW must be 0 or 1' >&2; exit 2 ;;
esac
# Exact 1.0/sqrt seeded by Broadway frsqrte (wii/rsqrt_exact.h); same RAM as
# the reference lowering. WII_PERF_RSQRT_STEPS sets the Newton step count.
case "${WII_PERF_EXACT_RSQRT:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_NATIVE_FRSQRTE:-0}" = 0 ] && [ "${WII_PERF_FRSQRTE_MEMO:-0}" = 0 ] || { echo 'Exact rsqrt replaces native frsqrte and the memo' >&2; exit 2; }
       flags="$flags -DVIPER_WII_EXACT_RSQRT -DWII_RSQRT_STEPS=${WII_PERF_RSQRT_STEPS:-3}"; output="$output-exact-rsqrt${WII_PERF_RSQRT_STEPS:-3}" ;;
    *) echo 'WII_PERF_EXACT_RSQRT must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_NATIVE_FRSQRTE:-0}" in
    0) ;;
    1)
       [ "${WII_PERF_FRSQRTE_MEMO:-0}" = 0 ] || { echo 'Hardware frsqrte comparison requires memo disabled' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_FRSQRTE"; output="$output-native-frsqrte" ;;
    *) echo 'WII_PERF_NATIVE_FRSQRTE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_PERF_CAPTURE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_CAPTURE_CHECKPOINT"; output="$output-capture" ;;
    *) echo 'WII_PERF_CAPTURE must be 0 or 1' >&2; exit 2 ;;
esac
# Diagnostic: log each distinct renderer clear (rect, colour, fbz).
case "${WII_PERF_CLEAR_TRACE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CLEAR_TRACE"; output="$output-ctrace" ;;
    *) echo 'WII_PERF_CLEAR_TRACE must be 0 or 1' >&2; exit 2 ;;
esac
# Diagnostic: every renderer clear magenta (hardware black-band test).
case "${WII_PERF_CLEAR_DIAG:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CLEAR_DIAG"; output="$output-cdiag" ;;
    *) echo 'WII_PERF_CLEAR_DIAG must be 0 or 1' >&2; exit 2 ;;
esac
# Black-band hardware test variants of the clear quad (1 clip, 2 depth, 3 winding, 4 all + magenta).
case "${WII_PERF_CLEAR_VARIANT:-0}" in
    0) ;;
    1|2|3|4) flags="$flags -DVIPER_WII_CLEAR_VARIANT=$WII_PERF_CLEAR_VARIANT"; output="$output-cvar$WII_PERF_CLEAR_VARIANT" ;;
    *) echo 'WII_PERF_CLEAR_VARIANT must be 0-4' >&2; exit 2 ;;
esac
# Hardware benchmark label printed on the result screen (wii/main.c).
case "${WII_PERF_BENCH_ID:-0}" in
    0) ;;
    5|6|7|8|9|10|11|12|13|14|15|16|17|18|19|20|21|22|23|24|25|26|27|28|29|30|31|32|33|34|35|36|37|38|39|40|41|42|43|44|45|46) flags="$flags -DVIPER_WII_BENCH_ID=$WII_PERF_BENCH_ID"; output="$output-bench$WII_PERF_BENCH_ID" ;;
    *) echo 'WII_PERF_BENCH_ID must be 0 or 5-46' >&2; exit 2 ;;
esac
# Keep compiler experiments separate so previous objects cannot mask a flag change.
case "${WII_PERF_GEN_OPT:-2}" in
    2) gen_opt=-O2 ;;
    3) gen_opt=-O3; output="$output-gen-o3" ;;
    1) gen_opt=-O1; output="$output-gen-o1" ;;
    s) gen_opt=-Os; output="$output-gen-os" ;;
    *) echo 'WII_PERF_GEN_OPT must be 1, 2, 3 or s' >&2; exit 2 ;;
esac
case "${WII_PERF_GENERATED_LTO:-0}" in
    0) ;;
    1) gen_opt="$gen_opt -flto"; output="$output-genlto" ;;
    *) echo 'WII_PERF_GENERATED_LTO must be 0 or 1' >&2; exit 2 ;;
esac
[ "${WII_PERF_RENDER_RATE:-full}" != 20 ] || output="$output/rate20"
[ -z "${WII_PERF_HOST_O3_FILES:-}" ] || output="$output-o3f-$(printf %s "$WII_PERF_HOST_O3_FILES" | tr ' ' '+' | sed 's/native_gl_draw/ngd/;s/gx_renderer/gxr/;s/voodoo_headless/vh/')"
case "${WII_PERF_HOST_OPT:-2}" in
    2) host_opt=-O2 ;;
    3) host_opt=-O3; output="$output-host-o3" ;;
    s) host_opt=-Os; output="$output-host-os" ;;
    *) echo 'WII_PERF_HOST_OPT must be 2, 3 or s' >&2; exit 2 ;;
esac
# Extra optimisation flags for generated and host code (hardware experiments),
# e.g. WII_PERF_EXTRA_OPT="-fsched-pressure". The output path gets a hash.
if [ -n "${WII_PERF_EXTRA_OPT:-}" ]; then
    gen_opt="$gen_opt $WII_PERF_EXTRA_OPT"; host_opt="$host_opt $WII_PERF_EXTRA_OPT"
    output="$output-xopt$(printf '%s' "$WII_PERF_EXTRA_OPT" | shasum | cut -c1-6)"
fi
# Debug line info (no code change) so hardware PC samples map to generated C
# lines and their guest-instruction comments.
case "${WII_PERF_DEBUG_LINES:-0}" in
    0) ;;
    1) gen_opt="$gen_opt -g1"; host_opt="$host_opt -g1"; output="$output-g1" ;;
    *) echo 'WII_PERF_DEBUG_LINES must be 0 or 1' >&2; exit 2 ;;
esac
# Layout experiment: align every function (and optionally loop heads) to a
# cache line, so unrelated code changes cannot shift hot code's alignment.
if [ -n "${WII_PERF_ALIGN:-}" ]; then
    gen_opt="$gen_opt -falign-functions=$WII_PERF_ALIGN"; host_opt="$host_opt -falign-functions=$WII_PERF_ALIGN"
    output="$output-alignf$WII_PERF_ALIGN"
fi
if [ -n "${WII_PERF_ALIGN_LOOPS:-}" ]; then
    gen_opt="$gen_opt -falign-loops=$WII_PERF_ALIGN_LOOPS"; host_opt="$host_opt -falign-loops=$WII_PERF_ALIGN_LOOPS"
    output="$output-alignl$WII_PERF_ALIGN_LOOPS"
fi
# Profile-guided optimization: gen and use share one tree so the use build
# finds each object's .gcda beside it (wii/pgo_dump.c, wii/pgo_split.py).
case "${WII_PERF_PGO:-0}" in
    0) ;;
    gen) flags="$flags -DVIPER_WII_PGO_GEN"; output="$output-pgo"
         pgo="-fprofile-arcs -fprofile-info-section=gcov_info -fprofile-update=single"
         gen_opt="$gen_opt $pgo"; host_opt="$host_opt $pgo" ;;
    use) output="$output-pgo${WII_PERF_PGO_TAG:-}"
         pgo="-fprofile-use -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch ${WII_PERF_PGO_EXTRA:-}"
         gen_opt="$gen_opt $pgo"; host_opt="$host_opt $pgo" ;;
    # Hardware winner (package 10 AE): the stored profile, no unrolling or
    # peeling, cold blocks split out. Profile copied beside the objects.
    ae) output="$output-pgoae"
        mkdir -p "$output"
        rsync -a --include='*/' --include='*.gcda' --exclude='*' build/wii/pgo-profile/ "$output/"
        pgo="-fprofile-use -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch -fno-unroll-loops -fno-peel-loops -freorder-blocks-and-partition"
        gen_opt="$gen_opt $pgo"; host_opt="$host_opt $pgo" ;;
    *) echo 'WII_PERF_PGO must be 0, gen, use or ae' >&2; exit 2 ;;
esac
echo "VIPER BUILD OUTPUT $output/viper.dol"
exec sh wii/build.sh game GAME=gticlub2 "GAME_OUT=$output" DEVICE_LTO=1 "GEN_OPT=$gen_opt" "HOST_OPT=$host_opt" "HOST_O3_FILES=${WII_PERF_HOST_O3_FILES:-}" "EXTRA_CFLAGS=$flags" -j8
