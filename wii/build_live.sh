#!/bin/sh
# Current GTI Club 2 live-input candidate; keep game simulation at its original rate.
set -eu
cd "$(dirname "$0")/.."
divisor=${1:-1}
case "$divisor" in
    1|2) ;;
    *) echo 'Usage: sh wii/build_live.sh [1|2] (render every frame / alternate frames)' >&2; exit 2 ;;
esac
flags="-DVIPER_WII_IDLE_BATCH -DVIPER_WII_STRING_WORDS -DVIPER_WII_INLINE_VRAM_WRITE -DVIPER_WII_DIRECT_LFB_WORD -DVIPER_WII_FIFO_FRONTIER_WRITE -DVIPER_WII_FIFO_FORMAT59 -DVIPER_WII_WDEPTH_INTERIOR -DVIPER_WII_GX_LOOKUP_OBJECTS -DVIPER_WII_GX_RENDER -DVIPER_WII_GX_DITHER_APPROX -DVIPER_WII_GX_CHROMA_APPROX -DVIPER_WII_GX_WDEPTH_APPROX -DVIPER_WII_GX_FOG_APPROX -DVIPER_WII_GX_RESIDENT_CACHE -DVIPER_WII_GX_FRAME_DIVISOR=$divisor"
output="build/wii/live-f$divisor"
# Retired experiments (measured slower or unsafe; see wii/PERFORMANCE_PLAN.md).
for retired in WII_LIVE_FAST_VERTEX WII_LIVE_NATIVE_VRAM WII_LIVE_NATIVE_FIFO_WORDS WII_LIVE_NATIVE_GX_PRODUCER WII_LIVE_DIRECT_COMPLETION59 WII_LIVE_DIRECT_PARTIAL59 WII_LIVE_FIFO_SKIP_VERTEX_STORE WII_LIVE_FIFO_PAYLOAD_OBSERVERS WII_LIVE_LOCAL_FP WII_LIVE_LOCAL_FP_RESTRICT WII_LIVE_LOCAL_FP_DIAGNOSTIC WII_LIVE_TRANSFORM_RAM WII_LIVE_NATIVE_GX_MEMORY WII_LIVE_NATIVE_GX_PACKETS WII_LIVE_NATIVE_GX_ZERO_COPY WII_LIVE_NATIVE_GX_VERTEX_INIT WII_LIVE_DIRECT_CAPTURE59 WII_LIVE_DIRECT_VERTEX59 WII_LIVE_DIRECT_PREHEADER59 WII_LIVE_BULK_TAIL WII_LIVE_FRSQRTE_MEMO WII_LIVE_FRSQRTE_KEY_HASH WII_LIVE_FRSQRTE_SLOTS WII_LIVE_FRSQRTE_CENSUS WII_LIVE_FCTIW_TRUNC_INLINE WII_LIVE_FAST_FP WII_LIVE_PAIRED_PLANES WII_LIVE_DRAW_PLAN_REUSE WII_LIVE_TMU_PREFIX_CACHE WII_LIVE_SKIP_LEGACY WII_LIVE_PARENT_FOG WII_LIVE_FOG_COMPARE_ONCE WII_LIVE_LOOKUP_BIND_REUSE WII_LIVE_PARENT_TEXTURE_OBJECT WII_LIVE_PARENT_COMBINER_CONSTANTS WII_LIVE_TEXTURE_BINDING_CACHE WII_LIVE_LOOKUP_TEXTURE_CACHE WII_LIVE_TEV_FUSION WII_LIVE_CONFIG_CACHE WII_LIVE_CONFIG_TRACE WII_LIVE_MATERIAL_CENSUS; do
    eval "value=\${$retired:-0}"
    [ "$value" = 0 ] || { echo "$retired is retired" >&2; exit 2; }
done
case "${WII_GX_PIPELINE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_COLOR_EQUATION"; output="build/wii/live-pipeline-f$divisor" ;;
    *) echo 'WII_GX_PIPELINE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_GX_TMU_PIPELINE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_COLOR_EQUATION -DVIPER_WII_GX_TMU_PIPELINE"; output="build/wii/live-tmu-f$divisor" ;;
    *) echo 'WII_GX_TMU_PIPELINE must be 0 or 1' >&2; exit 2 ;;
esac
# Validated optimizations default on; each supports an explicit zero opt-out.
case "${WII_LIVE_FOG:-0}" in
    1) ;;
    0) flags="$flags -DVIPER_WII_GX_DISABLE_FOG"; output="$output-nofog" ;;
    *) echo 'WII_LIVE_FOG must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_MERGED_LOOKUP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_MERGED_LOOKUP"; output="$output-merged-lookup" ;;
    *) echo 'WII_LIVE_MERGED_LOOKUP must be 0 or 1' >&2; exit 2 ;;
esac
# One Z per triangle. Off: road and route arrows draw over the car and
# kerbs (run.LWyEJx versus run.Cc70GR), so it cannot ship.
case "${WII_LIVE_WDEPTH_CONSTANT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_CONSTANT"; output="$output-constdepth" ;;
    *) echo 'WII_LIVE_WDEPTH_CONSTANT must be 0 or 1' >&2; exit 2 ;;
esac
# Per-vertex Z of 1-wb: the W-buffer's per-pixel depth order without the
# band split or lookup texture. Needs fog off; replaces constant W depth.
case "${WII_LIVE_WDEPTH_LINEAR:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_WDEPTH_CONSTANT:-0}" = 0 ] && [ "${WII_LIVE_FOG:-0}" = 0 ] || { echo 'Linear W depth needs fog off and constant W depth off' >&2; exit 2; }
       flags="$flags -DVIPER_WII_WDEPTH_LINEAR"; output="$output-lineardepth" ;;
    *) echo 'WII_LIVE_WDEPTH_LINEAR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_EARLY_CULL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_EARLY_CULL"; output="$output-early-cull" ;;
    *) echo 'WII_LIVE_EARLY_CULL must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_SOLVER_REUSE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOOKUP_PLANE_REUSE -DVIPER_WII_ZERO_PLANE_QUOTIENT"; output="$output-solver-reuse" ;;
    *) echo 'WII_LIVE_SOLVER_REUSE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_FIFO_BOUNDS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FIFO_BOUNDS_CACHE"; output="$output-fifo-bounds" ;;
    *) echo 'WII_LIVE_FIFO_BOUNDS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_FIFO_SPLIT:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FIFO_SPLIT_TRIANGLE"; output="$output-fifo-split" ;;
    *) echo 'WII_LIVE_FIFO_SPLIT must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_CR_UNPACK:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CR_UNPACK"; output="$output-cr-unpack" ;;
    *) echo 'WII_LIVE_CR_UNPACK must be 0 or 1' >&2; exit 2 ;;
esac
# Per-vertex S,T,Q as GX normal/binormal instead of a per-triangle plane
# matrix solve and texture-matrix load (full TMU pipeline).
case "${WII_LIVE_VERTEX_STQ:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_VERTEX_STQ"; output="$output/vstq" ;;
    *) echo 'WII_LIVE_VERTEX_STQ must be 0 or 1' >&2; exit 2 ;;
esac
# Native transcription of the gl packed-vertex draw routine f_gl_0002adac
# (wii/native_gl_draw.c): guest registers in locals, same operations.
case "${WII_LIVE_NATIVE_GL_DRAW:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] || { echo 'Native gl draw needs the bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_GL_DRAW"; output="$output-ngl" ;;
    *) echo 'WII_LIVE_NATIVE_GL_DRAW must be 0 or 1' >&2; exit 2 ;;
esac
# The native draw routine hands complete format-59 packets straight to the
# device decoder instead of storing their words through the FIFO.
case "${WII_LIVE_DIRECT_TRIANGLES:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_NATIVE_GL_DRAW:-1}" = 1 ] || { echo 'Direct triangles need the native gl draw routine' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_TRIANGLES"; output="$output-dtri" ;;
    *) echo 'WII_LIVE_DIRECT_TRIANGLES must be 0 or 1' >&2; exit 2 ;;
esac
# Hot gl functions with guest registers in C locals (wii/localize_function.py).
case "${WII_LIVE_LOCALIZE_GL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_GL"; output="$output-loc" ;;
    *) echo 'WII_LIVE_LOCALIZE_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Inline rt_cr_pack/unpack and the RAM path of rt_lswi/stswi (runtime/ppc_rt.h)
# so generated call sites fold their constant operands.
case "${WII_LIVE_INLINE_HELPERS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_INLINE_HELPERS"; output="$output-inl" ;;
    *) echo 'WII_LIVE_INLINE_HELPERS must be 0 or 1' >&2; exit 2 ;;
esac
# Buffer consecutive triangles that need no GX state change into one draw
# (wii/gx_batch.h). Needs the state shadow and per-vertex STQ.
# Gather the localized gl producers' FIFO stores into bulk writes.
case "${WII_LIVE_GATHER_GL:-0}" in
    0) ;;
    1) [ "${WII_LIVE_LOCALIZE_GL:-1}" = 1 ] || { echo 'WII_LIVE_GATHER_GL needs WII_LIVE_LOCALIZE_GL=1' >&2; exit 2; }
       flags="$flags -DVIPER_WII_GATHER_GL"; output="$output-gather" ;;
    *) echo 'WII_LIVE_GATHER_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Guest byte copies to and from LAN controller RAM skip the bus range chain
# (runtime/hw.c rt_mmio_*; exact while MMIO logging is off).
case "${WII_LIVE_FAST_LANC_RAM:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_FAST_LANC_RAM"; output="$output/lanc" ;;
    *) echo 'WII_LIVE_FAST_LANC_RAM must be 0 or 1' >&2; exit 2 ;;
esac
# Decode each triangle packet's vertices once and pass strip triangles to the
# renderer in place; store direct packets as whole words (exact).
case "${WII_LIVE_PACKET_VERTICES:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PACKET_VERTICES"; output="$output-pverts" ;;
    *) echo 'WII_LIVE_PACKET_VERTICES must be 0 or 1' >&2; exit 2 ;;
esac
# Replay the last triangle's setup when device state, texture epoch, packet
# format and GX state are unchanged (exact; see triangle_memo in gx_renderer.c).
case "${WII_LIVE_TRIANGLE_MEMO:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TRIANGLE_MEMO"; output="$output-tmemo" ;;
    *) echo 'WII_LIVE_TRIANGLE_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
# Native gl 0x29d28 list draw (wii/native_gl_list.c): whole packets go to
# the device through direct triangles instead of FIFO word stores (exact).
case "${WII_LIVE_NATIVE_GL_LIST:-1}" in
    0) ;;
    1) [ "${WII_LIVE_DIRECT_TRIANGLES:-1}" = 1 ] || { echo 'Native gl list needs direct triangles' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_GL_LIST"; output="$output-nlist" ;;
    *) echo 'WII_LIVE_NATIVE_GL_LIST must be 0 or 1' >&2; exit 2 ;;
esac
# Localized gl 0x28fc4 strip producer with its FIFO words gathered and each
# header-completed packet handed to direct triangles (exact).
case "${WII_LIVE_DIRECT_GL:-1}" in
    0) ;;
    1) [ "${WII_LIVE_DIRECT_TRIANGLES:-1}" = 1 ] && [ "${WII_LIVE_LOCALIZE_GL:-1}" = 1 ] || { echo 'Direct gl needs direct triangles and localized gl' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_GL"; output="$output-dgl" ;;
    *) echo 'WII_LIVE_DIRECT_GL must be 0 or 1' >&2; exit 2 ;;
esac
# Generated code keeps a local copy of the guest RAM base and tests one
# bound per access (wii/native_ram.h; same accesses, exact).
case "${WII_LIVE_RAM_BASE_LOCAL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_BASE_LOCAL"; output="$output-rbl" ;;
    *) echo 'WII_LIVE_RAM_BASE_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Out-of-line slow path for the RAM_BASE_LOCAL accessors: much smaller code,
# slightly slower in Dolphin (no cache model). Pending hardware timing.
case "${WII_LIVE_RAM_SLOW_CALL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_SLOW_CALL"; output="$output-rsc" ;;
    *) echo 'WII_LIVE_RAM_SLOW_CALL must be 0 or 1' >&2; exit 2 ;;
esac
# Aligned in-RAM guest floats/doubles straight into/from FPRs (big-endian
# host; no FPR<->GPR stack trip). Same bits; matters on Broadway.
case "${WII_LIVE_DIRECT_FP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_FP"; output="$output-dfp" ;;
    *) echo 'WII_LIVE_DIRECT_FP must be 0 or 1' >&2; exit 2 ;;
esac
# Broadway cache hints with no architectural effect on results: dcbz for
# whole VRAM lines a direct packet overwrites, dcbt ahead of vertex data.
case "${WII_LIVE_CACHE_HINTS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CACHE_HINTS"; output="$output-chint" ;;
    *) echo 'WII_LIVE_CACHE_HINTS must be 0 or 1' >&2; exit 2 ;;
esac
# Hot functions (physical-Wii profile, wii/hot_layout.txt) placed contiguously
# hottest first; placement only.
case "${WII_LIVE_HOT_LAYOUT:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_HOT_LAYOUT"; output="$output-hot" ;;
    *) echo 'WII_LIVE_HOT_LAYOUT must be 0 or 1' >&2; exit 2 ;;
esac
# 32-bit cycle budget (slices <= 20000 cycles; same values, cheaper checks).
case "${WII_LIVE_BUDGET32:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BUDGET32"; output="$output-b32" ;;
    *) echo 'WII_LIVE_BUDGET32 must be 0 or 1' >&2; exit 2 ;;
esac
# Drop guest carry writes overwritten in the same straight-line code before
# any read, branch, checkpoint or call (non-gl modules; exact).
case "${WII_LIVE_DEAD_CARRY:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DEAD_CARRY"; output="$output-dcarry" ;;
    *) echo 'WII_LIVE_DEAD_CARRY must be 0 or 1' >&2; exit 2 ;;
esac
# One-instruction in-RAM test (below RAM_SIZE and naturally aligned; others
# take the original accessor; exact).
case "${WII_LIVE_RAM_MASK_TEST:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RAM_MASK_TEST"; output="$output-rmask" ;;
    *) echo 'WII_LIVE_RAM_MASK_TEST must be 0 or 1' >&2; exit 2 ;;
esac
# Texture base-address changes keep material plans and combiner programs
# (the planner never reads them); only texture reuse ends (exact).
case "${WII_LIVE_PLAN_KEEP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PLAN_KEEP"; output="$output-pkeep" ;;
    *) echo 'WII_LIVE_PLAN_KEEP must be 0 or 1' >&2; exit 2 ;;
esac
# Memo of TMU pipeline plans keyed by every planner input (exact).
case "${WII_LIVE_PLAN_MEMO:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PLAN_MEMO"; output="$output-pmemo" ;;
    *) echo 'WII_LIVE_PLAN_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
# Combiner program stays resident across material resets when the plan bytes match (exact).
case "${WII_LIVE_COMBINER_KEEP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_COMBINER_KEEP"; output="$output-ckeep" ;;
    *) echo 'WII_LIVE_COMBINER_KEEP must be 0 or 1' >&2; exit 2 ;;
esac
# Skip a texture load that repeats the last load exactly (needs native texture resources).
case "${WII_LIVE_TEXLOAD_SKIP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXLOAD_SKIP"; output="$output-tlskip" ;;
    *) echo 'WII_LIVE_TEXLOAD_SKIP must be 0 or 1' >&2; exit 2 ;;
esac
# Present queues the EFB->XFB copy without waiting for it (hardware: ~0.4 ms a frame).
case "${WII_LIVE_PRESENT_ASYNC:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PRESENT_ASYNC"; output="$output-pasync" ;;
    *) echo 'WII_LIVE_PRESENT_ASYNC must be 0 or 1' >&2; exit 2 ;;
esac
# GX command FIFO size in KB (default 256).
case "${WII_LIVE_GX_FIFO_KB:-256}" in
    256) ;;
    512|1024|2048) flags="$flags -DVIPER_WII_GX_FIFO_KB=${WII_LIVE_GX_FIFO_KB}"; output="$output-fifo${WII_LIVE_GX_FIFO_KB}" ;;
    *) echo 'WII_LIVE_GX_FIFO_KB must be 256, 512, 1024 or 2048' >&2; exit 2 ;;
esac
# Generated functions take PPCContext *restrict c (guest stores do not reload context fields).
case "${WII_LIVE_RESTRICT_CTX:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RESTRICT_CTX"; output="$output/rctx" ;;
    *) echo 'WII_LIVE_RESTRICT_CTX must be 0 or 1' >&2; exit 2 ;;
esac
# Block cycle charges kept in a local, written back before any observer (exact).
case "${WII_LIVE_BUDGET_LOCAL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BUDGET_LOCAL"; output="$output/blocal" ;;
    *) echo 'WII_LIVE_BUDGET_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Screen clears drawn with GX clipping fully off (hardware drops the far-plane quad: black bands).
case "${WII_LIVE_CLEAR_NOCLIP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_CLEAR_NOCLIP"; output="$output-cnoclip" ;;
    *) echo 'WII_LIVE_CLEAR_NOCLIP must be 0 or 1' >&2; exit 2 ;;
esac
# Batch flush copies with the word count in a local (no reload per pipe store).
case "${WII_LIVE_BATCH_COPY_LOCAL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BATCH_COPY_LOCAL"; output="$output-bcl" ;;
    *) echo 'WII_LIVE_BATCH_COPY_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
# Triangle memo range checks as integer tests on float bits, one branch (exact for all 2^32 inputs).
case "${WII_LIVE_MEMO_INT_CHECKS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MEMO_INT_CHECKS"; output="$output-mic" ;;
    *) echo 'WII_LIVE_MEMO_INT_CHECKS must be 0 or 1' >&2; exit 2 ;;
esac
# Goto switches on strided addresses switched on rotr(x - base, shift): dense cases (exact).
case "${WII_LIVE_DENSE_SWITCH:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DENSE_SWITCH"; output="$output-dsw" ;;
    *) echo 'WII_LIVE_DENSE_SWITCH must be 0 or 1' >&2; exit 2 ;;
esac
# Exact rsqrt forced inline into the native 2adac vertex block.
case "${WII_LIVE_RSQRT_INLINE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RSQRT_INLINE"; output="$output-rsqi" ;;
    *) echo 'WII_LIVE_RSQRT_INLINE must be 0 or 1' >&2; exit 2 ;;
esac
# Direct packets: fixed format-59 length test, no no-op presence clear (exact).
case "${WII_LIVE_DIRECT_LEAN:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_LEAN"; output="$output-dlean" ;;
    *) echo 'WII_LIVE_DIRECT_LEAN must be 0 or 1' >&2; exit 2 ;;
esac
# Seven more hot gl functions with guest registers localized (exact by construction).
case "${WII_LIVE_LOCALIZE_GL_MORE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_GL_MORE"; output="$output-lglm" ;;
    *) echo 'WII_LIVE_LOCALIZE_GL_MORE must be 0 or 1' >&2; exit 2 ;;
esac
# Hardware A/B: six more hot functions localized despite 2.3-3.9x code growth.
case "${WII_LIVE_LOCALIZE_EXTRA:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_EXTRA"; output="$output-lext" ;;
    *) echo 'WII_LIVE_LOCALIZE_EXTRA must be 0 or 1' >&2; exit 2 ;;
esac
# Direct packets decoded by a format-59-only triangle_packet from the producer's words (exact).
case "${WII_LIVE_DIRECT_PACKET59:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_PACKET59"; output="$output-dp59" ;;
    *) echo 'WII_LIVE_DIRECT_PACKET59 must be 0 or 1' >&2; exit 2 ;;
esac
# The memo-hit triangle path compiled at -O3 (function attribute).
case "${WII_LIVE_RENDER_O3:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RENDER_O3"; output="$output-ro3" ;;
    *) echo 'WII_LIVE_RENDER_O3 must be 0 or 1' >&2; exit 2 ;;
esac
# Triangle memo hits prepare each packet vertex once (strips share vertices; exact).
case "${WII_LIVE_MEMO_VERTEX_PREP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_MEMO_VERTEX_PREP"; output="$output-mvp" ;;
    *) echo 'WII_LIVE_MEMO_VERTEX_PREP must be 0 or 1' >&2; exit 2 ;;
esac
# Guest float/double accesses stay in FPRs: typed fast path, out-of-line slow path (exact).
case "${WII_LIVE_FP_SLOW_CALL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FP_SLOW_CALL"; output="$output-fpsc" ;;
    *) echo 'WII_LIVE_FP_SLOW_CALL must be 0 or 1' >&2; exit 2 ;;
esac
# Fastfill writes whole VRAM words per row (same bytes, versions and epochs; exact).
case "${WII_LIVE_FAST_FILL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_FAST_FILL"; output="$output-ffill" ;;
    *) echo 'WII_LIVE_FAST_FILL must be 0 or 1' >&2; exit 2 ;;
esac
# Exact rsqrt results remembered by input bits; timebase reads without a
# 64-bit division; no memo-hit dithering tally (a log statistic); the gl
# interpreter's return dispatch as a jump table. All exact; together with
# MEMO_INT_CHECKS 2.708 -> 2.648 s on hardware (2026-10-08, batch stack1).
case "${WII_LIVE_RSQRT_MEMO:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_RSQRT_MEMO"; output="$output-rsqmemo" ;;
    *) echo 'WII_LIVE_RSQRT_MEMO must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_MFTB_INCR:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_MFTB_INCREMENTAL"; output="$output-mftbincr" ;;
    *) echo 'WII_LIVE_MFTB_INCR must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_NO_DITHER_STATS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NO_DITHER_STATS"; output="$output-nodstat" ;;
    *) echo 'WII_LIVE_NO_DITHER_STATS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_TEXTURE_SLOT_HINT:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_SLOT_HINT"; output="$output-tslothint" ;;
    *) echo 'WII_LIVE_TEXTURE_SLOT_HINT must be 0 or 1' >&2; exit 2 ;;
esac
# Direct triangle packets consumed without a copy into Voodoo VRAM (RAM/EFB
# exact): 2.551 -> 2.480 s on hardware (2026-10-08, branch wii-port).
case "${WII_LIVE_DIRECT_NO_VRAM:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_NO_VRAM"; output="$output-dnovram" ;;
    *) echo 'WII_LIVE_DIRECT_NO_VRAM must be 0 or 1' >&2; exit 2 ;;
esac
# f_gl_00028fc4 (the direct FIFO producer) register pressure and FIFO path,
# all RAM/EFB exact; hardware, cumulative 2026-10-08 (branch wii-port):
# register-preserving cold calls 2.506 -> 2.458 s, dirty-only write-backs
# -> 2.423, local and grouped FIFO stores -> 2.377, local budget -> 2.373.
case "${WII_LIVE_PRESERVE_SLOW:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PRESERVE_SLOW"; output="$output/pslow" ;;   # new path segment: names stay under 255
    *) echo 'WII_LIVE_PRESERVE_SLOW must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DIRTY_SYNC:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRTY_SYNC"; output="$output-dsync" ;;
    *) echo 'WII_LIVE_DIRTY_SYNC must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DIRECT_LOCAL:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_LOCAL"; output="$output-dlocal" ;;
    *) echo 'WII_LIVE_DIRECT_LOCAL must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DIRECT_GROUP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_GROUP"; output="$output-dgroup" ;;
    *) echo 'WII_LIVE_DIRECT_GROUP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DIRECT_BUDGET:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_BUDGET"; output="$output-dbud" ;;
    *) echo 'WII_LIVE_DIRECT_BUDGET must be 0 or 1' >&2; exit 2 ;;
esac
# The game's sound through the Wii audio DMA (wii/audio.c).
case "${WII_LIVE_AUDIO:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_AUDIO"; output="$output-audio" ;;
    *) echo 'WII_LIVE_AUDIO must be 0 or 1' >&2; exit 2 ;;
esac
# 256-entry rsqrt memo (2.375 -> 2.365 s; 4096 was slower) and packet
# vertices carried in the decode buffer (2.374 -> 2.364 s); hardware, exact.
case "${WII_LIVE_RSQRT_MEMO_SIZE:-256}" in
    64) ;;
    256|1024|4096) n=${WII_LIVE_RSQRT_MEMO_SIZE:-256}; flags="$flags -DVIPER_WII_RSQRT_MEMO_SIZE=$n"; output="$output-rsqm$n" ;;
    *) echo 'WII_LIVE_RSQRT_MEMO_SIZE must be 64, 256, 1024 or 4096' >&2; exit 2 ;;
esac
case "${WII_LIVE_PACKET_CARRY:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PACKET_CARRY"; output="$output-pcarry" ;;
    *) echo 'WII_LIVE_PACKET_CARRY must be 0 or 1' >&2; exit 2 ;;
esac
# 2x2 supersampling (changes the picture: a playable option, off by default).
case "${WII_LIVE_SUPERSAMPLE:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_SUPERSAMPLE"; output="$output-ss" ;;
    *) echo 'WII_LIVE_SUPERSAMPLE must be 0 or 1' >&2; exit 2 ;;
esac
# 0x28fc4 blocks with many RAM accesses versioned on one in-RAM check
# (2.374 -> 2.355 s on hardware, exact).
case "${WII_LIVE_DIRECT_VERSION:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DIRECT_VERSION"; output="$output-dver" ;;
    *) echo 'WII_LIVE_DIRECT_VERSION must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DENSE_LR:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_DENSE_LR"; output="$output-denselr" ;;
    *) echo 'WII_LIVE_DENSE_LR must be 0 or 1' >&2; exit 2 ;;
esac
# Whole-session hardware PC profile (about 1 kHz), dumped to sd:/viper/boot.log
# when quitting with Home/Reset/Power: finds the heavy scenes of real play.
# Release build: no SD log at all (and no session profile).
# Option: all game textures nearest-neighbour (the arcade mixes bilinear and nearest).
case "${WII_LIVE_NEAREST:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NEAREST_TEXTURES"; output="$output-nearest" ;;
    *) echo 'WII_LIVE_NEAREST must be 0 or 1' >&2; exit 2 ;;
esac
# Option: the game's 512x384 shown 1:1, centred in 640x480 with black borders.
case "${WII_LIVE_LETTERBOX:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LETTERBOX"; output="$output-lbox" ;;
    *) echo 'WII_LIVE_LETTERBOX must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_RUMBLE:-1}" in
    1) ;;
    0) flags="$flags -DVIPER_WII_NO_RUMBLE"; output="$output-norumble" ;;
    *) echo 'WII_LIVE_RUMBLE must be 0 or 1' >&2; exit 2 ;;
esac
# Option: skip drawing every other frame while more than a frame behind real time.
case "${WII_LIVE_FRAMESKIP:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_AUTO_FRAMESKIP"; output="$output-fskip" ;;
    *) echo 'WII_LIVE_FRAMESKIP must be 0 or 1' >&2; exit 2 ;;
esac
# Picture aspect: 43 (default: the full frame; on a console set to 16:9 the Wii video mode already gives a 4:3 picture on a stretched display, hardware 2026-10-08), 169 (extra squeeze), or auto (squeeze when the console is 16:9).
case "${WII_LIVE_ASPECT:-43}" in
    auto) ;;
    43|169) flags="$flags -DVIPER_WII_FORCE_ASPECT=${WII_LIVE_ASPECT:-43}"; output="$output-ar${WII_LIVE_ASPECT:-43}" ;;
    *) echo 'WII_LIVE_ASPECT must be auto, 43 or 169' >&2; exit 2 ;;
esac
case "${WII_LIVE_NO_LOG:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NO_LOG"; output="$output-nolog"; WII_LIVE_PLAY_PROFILE=0 ;;
    *) echo 'WII_LIVE_NO_LOG must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_PLAY_PROFILE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PC_PROFILE_PLAY"; output="$output-playprof" ;;
    *) echo 'WII_LIVE_PLAY_PROFILE must be 0 or 1' >&2; exit 2 ;;
esac
# Guest registers in C locals for every generated function the localizer
# accepts (wii/localize_function.py; exact by construction, test_localize.py).
case "${WII_LIVE_LOCALIZE_ALL:-0}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_LOCALIZE_ALL"; output="$output-locall" ;;
    *) echo 'WII_LIVE_LOCALIZE_ALL must be 0 or 1' >&2; exit 2 ;;
esac
# Guest registers in C locals only for the profiled hot functions listed in
# wii/localize_hot.txt (small code growth; same proof as LOCALIZE_ALL).
case "${WII_LIVE_LOCALIZE_HOT:-1}" in
    0) ;;
    1) [ "${WII_LIVE_LOCALIZE_ALL:-0}" = 0 ] || { echo 'LOCALIZE_HOT and LOCALIZE_ALL are alternatives' >&2; exit 2; }
       flags="$flags -DVIPER_WII_LOCALIZE_HOT"; output="$output-lochot" ;;
    *) echo 'WII_LIVE_LOCALIZE_HOT must be 0 or 1' >&2; exit 2 ;;
esac
# Direct-mapped cache in front of the indirect-call dispatcher (runtime/cpu.c;
# only unambiguous targets, keyed by address and first code word: exact).
case "${WII_LIVE_LOOKUP_CACHE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_LOOKUP_CACHE"; output="$output-lcache" ;;
    *) echo 'WII_LIVE_LOOKUP_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
# Texture cache lookup tries the slot last bound for the request's key hash
# before scanning all slots (at most one slot can match: exact).
case "${WII_LIVE_TEXTURE_HINT:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_HINT"; output="$output-thint" ;;
    *) echo 'WII_LIVE_TEXTURE_HINT must be 0 or 1' >&2; exit 2 ;;
esac
# Link code byte copy from LAN RAM in bulk (needs FAST_LANC_RAM; exact).
case "${WII_LIVE_LANC_COPY:-1}" in
    0) ;;
    1) [ "${WII_LIVE_FAST_LANC_RAM:-1}" = 1 ] || { echo 'LANC_COPY needs FAST_LANC_RAM' >&2; exit 2; }
       flags="$flags -DVIPER_WII_LANC_COPY"; output="$output/lcopy" ;;
    *) echo 'WII_LIVE_LANC_COPY must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_GX_BATCH:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_BATCH"; output="$output-batch" ;;
    *) echo 'WII_LIVE_GX_BATCH must be 0 or 1' >&2; exit 2 ;;
esac
# Drop renderer GX fixed-function setter calls that repeat the last value.
case "${WII_LIVE_GX_STATE_SHADOW:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_STATE_SHADOW"; output="$output-gxshadow" ;;
    *) echo 'WII_LIVE_GX_STATE_SHADOW must be 0 or 1' >&2; exit 2 ;;
esac
# Native fctiw/fctiwz inline (runtime/ppc_rt.h); same bits as rt_fctiw.
case "${WII_LIVE_NATIVE_FCTIW:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_NATIVE_FCTIW"; output="$output-fctiw" ;;
    *) echo 'WII_LIVE_NATIVE_FCTIW must be 0 or 1' >&2; exit 2 ;;
esac
# Exact 1.0/sqrt seeded by the Broadway estimate: same guest RAM as the
# reference lowering (9730789c), 9.3% faster driving (run.R3rwss versus
# run.EXJSJ8). The raw estimate changes game state, so it is opt-in only.
case "${WII_LIVE_EXACT_RSQRT:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_NATIVE_FRSQRTE:-0}" = 0 ] && [ "${WII_LIVE_FRSQRTE_MEMO:-0}" = 0 ] || { echo 'Exact rsqrt replaces native frsqrte and the memo' >&2; exit 2; }
       flags="$flags -DVIPER_WII_EXACT_RSQRT -DWII_RSQRT_STEPS=2"; output="$output-exact-rsqrt" ;;
    *) echo 'WII_LIVE_EXACT_RSQRT must be 0 or 1' >&2; exit 2 ;;
esac
# Inexact hardware estimate: guest RAM differs (d220b09a). Opt-in comparison.
case "${WII_LIVE_NATIVE_FRSQRTE:-0}" in
    0) ;;
    1)
       [ "${WII_LIVE_FRSQRTE_MEMO:-0}" = 0 ] && [ "${WII_LIVE_EXACT_RSQRT:-1}" = 0 ] || { echo 'Hardware frsqrte requires the exact memo and exact rsqrt off' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_FRSQRTE"; output="$output-native-frsqrte" ;;
    *) echo 'WII_LIVE_NATIVE_FRSQRTE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_BULK_WRITER:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_BULK_WRITER"; output="$output-bulk-writer" ;;
    *) echo 'WII_LIVE_BULK_WRITER must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_BULK_RAM:-${WII_LIVE_BULK_WRITER:-1}}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] || { echo 'Bulk RAM requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_RAM"; output="$output-bulk-ram" ;;
    *) echo 'WII_LIVE_BULK_RAM must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_NATIVE_TEXTURE_BIND:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_GX_NATIVE_TEXTURE_BIND"; output="$output-native-texture-bind" ;;
    *) echo 'WII_LIVE_NATIVE_TEXTURE_BIND must be 0 or 1' >&2; exit 2 ;;
esac
# Prepared plans default on for the validated full-TMU backend.
case "${WII_LIVE_MATERIAL_PLAN_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" in
    0) ;;
    1)
       [ "${WII_GX_TMU_PIPELINE:-1}" = 1 ] || { echo 'Material plan cache requires full TMU' >&2; exit 2; }
       flags="$flags -DVIPER_WII_MATERIAL_PLAN_CACHE"; output="$output-material-plan-cache" ;;
    *) echo 'WII_LIVE_MATERIAL_PLAN_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_BORROW_CLIP:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_BORROW_CLIP"; output="$output-borrow-clip" ;;
    *) echo 'WII_LIVE_BORROW_CLIP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_BULK_PUBLISH:-${WII_LIVE_BULK_WRITER:-1}}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] || { echo 'Bulk publish requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_BULK_PUBLISH"; output="$output-bulk-publish" ;;
    *) echo 'WII_LIVE_BULK_PUBLISH must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_TEXTURE_LAYOUT_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_TEXTURE_LAYOUT_CACHE"; output="$output-texture-layout-cache" ;;
    *) echo 'WII_LIVE_TEXTURE_LAYOUT_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_COMBINER_PROGRAM_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" in
    0) ;;
    1)
       [ "${WII_GX_TMU_PIPELINE:-1}" = 1 ] &&
       [ "${WII_LIVE_NATIVE_TEXTURE_BIND:-1}" = 1 ] &&
       [ "${WII_LIVE_MATERIAL_PLAN_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" = 1 ] &&
       [ "${WII_LIVE_TMU_PREFIX_CACHE:-0}" = 0 ] || {
           echo 'Combiner cache requires full TMU, native bind, material plans and WII_LIVE_TMU_PREFIX_CACHE=0' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_COMBINER_PROGRAM_CACHE"; output="$output-combiner-program-cache" ;;
    *) echo 'WII_LIVE_COMBINER_PROGRAM_CACHE must be 0 or 1' >&2; exit 2 ;;
esac
# run.U39oOB: 1.33% less than constant depth, 192 colour pixels, Town still a race.
case "${WII_LIVE_MATERIAL_RUN:-1}" in
    0) ;;
    1)
       [ "${WII_GX_TMU_PIPELINE:-1}" = 1 ] &&
       [ "${WII_LIVE_NATIVE_TEXTURE_BIND:-1}" = 1 ] &&
       [ "${WII_LIVE_MATERIAL_PLAN_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" = 1 ] &&
       [ "${WII_LIVE_COMBINER_PROGRAM_CACHE:-${WII_GX_TMU_PIPELINE:-1}}" = 1 ] || {
           echo 'Material-run requires full TMU, native bind, material plans and the combiner cache' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_MATERIAL_RUN"; output="$output-material-run" ;;
    *) echo 'WII_LIVE_MATERIAL_RUN must be 0 or 1' >&2; exit 2 ;;
esac
# run.HMVpf2 versus run.U39oOB: 3.359% less, identical RAM and EFB.
case "${WII_LIVE_MATERIAL_BIND_SKIP:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_MATERIAL_RUN:-1}" = 1 ] || { echo 'Material bind skip requires material-run' >&2; exit 2; }
       flags="$flags -DVIPER_WII_MATERIAL_BIND_SKIP"; output="$output-bindskip" ;;
    *) echo 'WII_LIVE_MATERIAL_BIND_SKIP must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_SPLIT_RAM_HELPERS:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_SPLIT_RAM_HELPERS"; output="$output-split-ram-helpers" ;;
    *) echo 'WII_LIVE_SPLIT_RAM_HELPERS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_SPLIT_COPY_ONCE:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_WDEPTH_COPY_ONCE"; output="$output-splitcopy" ;;
    *) echo 'WII_LIVE_SPLIT_COPY_ONCE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DRIVING_BULK:-${WII_LIVE_BULK_WRITER:-1}}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] || {
           echo 'Driving bulk requires bulk writer' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_DRIVING_BULK -DVIPER_WII_DRIVING_TAIL"; output="$output-dbt" ;;
    *) echo 'WII_LIVE_DRIVING_BULK must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_BULK_INCOMPLETE_HEADER:-0}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] &&
       [ "${WII_LIVE_BULK_PUBLISH:-${WII_LIVE_BULK_WRITER:-1}}" = 1 ] || {
           echo 'Incomplete header requires bulk writer and publication' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_BULK_INCOMPLETE_HEADER"; output="$output/bih" ;;
    *) echo 'WII_LIVE_BULK_INCOMPLETE_HEADER must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_DIRECT_STATE:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_BULK_WRITER:-1}" = 1 ] || { echo 'Direct state requires bulk writer' >&2; exit 2; }
       flags="$flags -DVIPER_WII_DIRECT_STATE"; output="$output/gxstate" ;;
    *) echo 'WII_LIVE_DIRECT_STATE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_NATIVE_TEXTURE_RESOURCE:-1}" in
    0) ;;
    1)
       [ "${WII_LIVE_NATIVE_TEXTURE_BIND:-1}" = 1 ] && [ "${WII_GX_TMU_PIPELINE:-1}" = 1 ] || {
           echo 'Native resources require native binding and full TMU pipeline' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_RESOURCE"; output="$output/gxresource" ;;
    *) echo 'WII_LIVE_NATIVE_TEXTURE_RESOURCE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_NATIVE_TEXTURE_SOURCE:-0}" in
    0) ;;
    1)
       [ "${WII_LIVE_NATIVE_TEXTURE_RESOURCE:-1}" = 1 ] && [ "${WII_LIVE_TEXTURE_LAYOUT_CACHE:-1}" = 1 ] || {
           echo 'Native source descriptors require native resources and device layout invalidation' >&2; exit 2;
       }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_SOURCE"; output="$output/gxsource" ;;
    *) echo 'WII_LIVE_NATIVE_TEXTURE_SOURCE must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_NATIVE_TEXTURE_ATTRS:-${WII_GX_TMU_PIPELINE:-1}}" in
    0) ;;
    1)
       [ "${WII_LIVE_NATIVE_TEXTURE_BIND:-1}" = 1 ] && [ "${WII_GX_TMU_PIPELINE:-1}" = 1 ] || { echo 'Native texture attributes require native binding and full TMU pipeline' >&2; exit 2; }
       flags="$flags -DVIPER_WII_NATIVE_TEXTURE_ATTRS"; output="$output-na" ;;
    *) echo 'WII_LIVE_NATIVE_TEXTURE_ATTRS must be 0 or 1' >&2; exit 2 ;;
esac
case "${WII_LIVE_PARENT_PROJECTION:-1}" in
    0) ;;
    1) flags="$flags -DVIPER_WII_PARENT_PROJECTION"; output="$output-pp" ;;
    *) echo 'WII_LIVE_PARENT_PROJECTION must be 0 or 1' >&2; exit 2 ;;
esac
# Profile-guided optimization from the scripted-race profile in
# build/wii/pgo-profile (gcda files by object path; refresh with
# WII_PERF_PGO=gen, wii/pgo_dump.c, wii/pgo_split.py). Hardware: no unrolling
# or peeling, cold blocks split out (package 10 AE: 3.122 s vs 3.146 s).
# Off by default since 2026-10-08: with the pre-GX_Init fix, non-PGO AJ ran
# the driving window in 2.722 s vs PGO AI 3.004 s on hardware (both exact).
gen_opt=-O2; host_opt=-O2
case "${WII_LIVE_PGO:-0}" in
    0) ;;
    1) if [ -d build/wii/pgo-profile ]; then
           output="$output-pgo"
           mkdir -p "$output"
           rsync -a --include='*/' --include='*.gcda' --exclude='*' build/wii/pgo-profile/ "$output/"
           pgo="-fprofile-use -fprofile-correction -Wno-missing-profile -Wno-coverage-mismatch -fno-unroll-loops -fno-peel-loops -freorder-blocks-and-partition"
           gen_opt="$gen_opt $pgo"; host_opt="$host_opt $pgo"
       else echo 'No build/wii/pgo-profile: building without PGO' >&2; fi ;;
    *) echo 'WII_LIVE_PGO must be 0 or 1' >&2; exit 2 ;;
esac
exec sh wii/build.sh game GAME=gticlub2 "GAME_OUT=$output" DEVICE_LTO=1 "GEN_OPT=$gen_opt" "HOST_OPT=$host_opt" "EXTRA_CFLAGS=$flags" -j8
