/* Triangle batching for gx_renderer.c (VIPER_WII_GX_BATCH).
 *
 * GX_Begin takes its vertex count up front, so consecutive triangles that
 * need no GX state change are buffered here and drawn by one GX_Begin.
 * That is exact only if every state change reaches the hardware after the
 * buffered vertices, so this header must be included before anything that
 * issues GX calls, and it wraps them all:
 *  - Hot fixed-function and TEV setters are lazy: they record the wanted
 *    value. A draw compares wanted against last sent; only a real difference
 *    flushes the batch and sends it. A value set and reset within one
 *    triangle (NumTevStages(1) then the real count) costs nothing.
 *  - Every other GX call that affects drawing flushes the batch first.
 * Scissor, projection and vertex descriptors are immediate but skip calls
 * that repeat the last value. menu.c is the only other GX owner: the
 * renderer calls gx_shadow_reset() after it draws, which re-sends every
 * wanted value at the next draw. */
#ifndef VIPER_WII_GX_BATCH_H
#define VIPER_WII_GX_BATCH_H
#include "hot_layout.h"
#include <gccore.h>
#include <string.h>

/* ------------------------------------------------------------ batch */
enum { GX_BATCH_TRIANGLES = 256, GX_BATCH_WORDS = 13 };
static struct {
    unsigned count, words;     /* vertices; 32-bit words per vertex */
    u8 format;
    u32 data[GX_BATCH_TRIANGLES * 3 * GX_BATCH_WORDS];
    unsigned long long draws, vertices;
} gx_batch;
WII_HOT_gx_batch_flush static void gx_batch_flush(void) {
    if (!gx_batch.count) return;
    unsigned n = gx_batch.count;
    gx_batch.count = 0;
    gx_batch.draws++; gx_batch.vertices += n;
    (GX_Begin)(GX_TRIANGLES, gx_batch.format, n);
    const u32 *d = gx_batch.data;
#ifdef VIPER_WII_BATCH_COPY_LOCAL
    /* The word count and pipe address in locals: a volatile pipe store may
     * alias gx_batch, so the plain loop reloaded the count for every word. */
    const unsigned words = gx_batch.words, total = n * words;
    volatile u32 *const pipe = &wgPipe->U32;
    unsigned w = 0;
    for (; w + 4 <= total; w += 4) { u32 a = d[w], b = d[w + 1], c = d[w + 2], e = d[w + 3];
        *pipe = a; *pipe = b; *pipe = c; *pipe = e; }
    for (; w < total; w++) *pipe = d[w];
#else
    for (unsigned i = 0; i < n; i++, d += gx_batch.words) {
        /* Position, then 3 or 9 normal floats, then RGBA8 colour. */
        for (unsigned w = 0; w + 1 < gx_batch.words; w++) wgPipe->U32 = d[w];
        wgPipe->U32 = d[gx_batch.words - 1];
    }
#endif
    (GX_End)();
}

/* Bumped by every wrapper below that sets GX state or draws immediately
 * (not by batched triangles or flushes). Equal values mean the renderer made
 * no GX state call in between, so wanted and sent state are both unchanged. */
static unsigned gx_state_gen;

/* ------------------------------------------------------------ lazy state */
typedef struct {
    u8 dither, color, alpha, zloc, numtev, numtexgens;
    u8 zmode[3], acmp[5], blend[4], ztex_op, ztex_fmt;
    u32 ztex_bias;
    u32 tg[8][3];
    u32 to[16][3];
    GXColor kc[4], tc[4];
} GXLazyState;
static GXLazyState gx_want, gx_sent;
static u32 gx_dirty, gx_dirty_tg, gx_dirty_to, gx_dirty_kc, gx_dirty_tc;
enum { LZ_DITHER = 1, LZ_COLOR = 2, LZ_ALPHA = 4, LZ_ZLOC = 8, LZ_NUMTEV = 16,
       LZ_NUMTEXGENS = 32, LZ_ZMODE = 64, LZ_ACMP = 128, LZ_BLEND = 256, LZ_ZTEX = 512 };

/* Dirtiness is decided when a value is set: equal to the last value sent
 * clears the mark, so the per-draw check is a single test of the masks. */
#define LZ_MARK(field, bit) do { gx_state_gen++; if (!memcmp(&gx_want.field, &gx_sent.field, sizeof gx_want.field)) \
    gx_dirty &= ~(bit); else gx_dirty |= (bit); } while (0)
static void lz_dither(u8 v) { gx_want.dither = v; LZ_MARK(dither, LZ_DITHER); }
static void lz_color(u8 v) { gx_want.color = v; LZ_MARK(color, LZ_COLOR); }
static void lz_alpha(u8 v) { gx_want.alpha = v; LZ_MARK(alpha, LZ_ALPHA); }
static void lz_zloc(u8 v) { gx_want.zloc = v; LZ_MARK(zloc, LZ_ZLOC); }
static void lz_numtev(u8 v) { gx_want.numtev = v; LZ_MARK(numtev, LZ_NUMTEV); }
static void lz_numtexgens(u32 v) { gx_want.numtexgens = (u8)v; LZ_MARK(numtexgens, LZ_NUMTEXGENS); }
static void lz_zmode(u8 en, u8 fn, u8 up) {
    gx_want.zmode[0] = en; gx_want.zmode[1] = fn; gx_want.zmode[2] = up; LZ_MARK(zmode, LZ_ZMODE);
}
static void lz_acmp(u8 c0, u8 r0, u8 op, u8 c1, u8 r1) {
    gx_want.acmp[0] = c0; gx_want.acmp[1] = r0; gx_want.acmp[2] = op;
    gx_want.acmp[3] = c1; gx_want.acmp[4] = r1; LZ_MARK(acmp, LZ_ACMP);
}
static void lz_blend(u8 bm, u8 src, u8 dst, u8 lo) {
    gx_want.blend[0] = bm; gx_want.blend[1] = src; gx_want.blend[2] = dst;
    gx_want.blend[3] = lo; LZ_MARK(blend, LZ_BLEND);
}
static void lz_ztex(u8 op, u8 fmt, u32 bias) {
    gx_state_gen++;
    gx_want.ztex_op = op; gx_want.ztex_fmt = fmt; gx_want.ztex_bias = bias;
    if (op == gx_sent.ztex_op && fmt == gx_sent.ztex_fmt && bias == gx_sent.ztex_bias) gx_dirty &= ~LZ_ZTEX;
    else gx_dirty |= LZ_ZTEX;
}
static void lz_texcoordgen(u16 coord, u32 type, u32 src, u32 mtx) {
    gx_state_gen++;
    gx_want.tg[coord][0] = type; gx_want.tg[coord][1] = src; gx_want.tg[coord][2] = mtx;
    if (type == gx_sent.tg[coord][0] && src == gx_sent.tg[coord][1] && mtx == gx_sent.tg[coord][2])
        gx_dirty_tg &= ~(1u << coord);
    else gx_dirty_tg |= 1u << coord;
}
static void lz_tevorder(u8 stage, u8 coord, u32 map, u8 color) {
    gx_state_gen++;
    gx_want.to[stage][0] = coord; gx_want.to[stage][1] = map; gx_want.to[stage][2] = color;
    if (coord == gx_sent.to[stage][0] && map == gx_sent.to[stage][1] && color == gx_sent.to[stage][2])
        gx_dirty_to &= ~(1u << stage);
    else gx_dirty_to |= 1u << stage;
}
static void lz_kcolor(u8 sel, GXColor c) {
    gx_state_gen++;
    gx_want.kc[sel] = c;
    if (!memcmp(&c, &gx_sent.kc[sel], sizeof c)) gx_dirty_kc &= ~(1u << sel); else gx_dirty_kc |= 1u << sel;
}
static void lz_tevcolor(u8 reg, GXColor c) {
    gx_state_gen++;
    gx_want.tc[reg] = c;
    if (!memcmp(&c, &gx_sent.tc[reg], sizeof c)) gx_dirty_tc &= ~(1u << reg); else gx_dirty_tc |= 1u << reg;
}

static inline int gx_lazy_changed(void) {
    return (gx_dirty | gx_dirty_tg | gx_dirty_to | gx_dirty_kc | gx_dirty_tc) != 0;
}
/* Send every changed value. Call only with the batch already flushed. */
static void gx_lazy_send(void) {
    u32 d = gx_dirty;
    if (d & LZ_DITHER) GX_SetDither(gx_want.dither);
    if (d & LZ_COLOR) GX_SetColorUpdate(gx_want.color);
    if (d & LZ_ALPHA) GX_SetAlphaUpdate(gx_want.alpha);
    if (d & LZ_ZLOC) GX_SetZCompLoc(gx_want.zloc);
    if (d & LZ_NUMTEV) GX_SetNumTevStages(gx_want.numtev);
    if (d & LZ_NUMTEXGENS) GX_SetNumTexGens(gx_want.numtexgens);
    if (d & LZ_ZMODE) GX_SetZMode(gx_want.zmode[0], gx_want.zmode[1], gx_want.zmode[2]);
    if (d & LZ_ACMP) GX_SetAlphaCompare(gx_want.acmp[0], gx_want.acmp[1], gx_want.acmp[2],
                                        gx_want.acmp[3], gx_want.acmp[4]);
    if (d & LZ_BLEND) GX_SetBlendMode(gx_want.blend[0], gx_want.blend[1], gx_want.blend[2], gx_want.blend[3]);
    if (d & LZ_ZTEX) GX_SetZTexture(gx_want.ztex_op, gx_want.ztex_fmt, gx_want.ztex_bias);
    for (u32 m = gx_dirty_tg; m; m &= m - 1) {
        unsigned i = __builtin_ctz(m);
        GX_SetTexCoordGen(i, gx_want.tg[i][0], gx_want.tg[i][1], gx_want.tg[i][2]);
    }
    for (u32 m = gx_dirty_to; m; m &= m - 1) {
        unsigned i = __builtin_ctz(m);
        GX_SetTevOrder(i, gx_want.to[i][0], gx_want.to[i][1], gx_want.to[i][2]);
    }
    for (u32 m = gx_dirty_kc; m; m &= m - 1) { unsigned i = __builtin_ctz(m); GX_SetTevKColor(i, gx_want.kc[i]); }
    for (u32 m = gx_dirty_tc; m; m &= m - 1) { unsigned i = __builtin_ctz(m); GX_SetTevColor(i, gx_want.tc[i]); }
    gx_sent = gx_want;
    gx_dirty = gx_dirty_tg = gx_dirty_to = gx_dirty_kc = gx_dirty_tc = 0;
}
/* Before any immediate draw: pending triangles first, then changed state. */
static void gx_draw_prepare(void) {
    gx_batch_flush();
    if (gx_lazy_changed()) gx_lazy_send();
}
/* Append one triangle; words 4 + normals, format of its vertex descriptor. */
WII_HOT_gx_batch_reserve static u32 *gx_batch_reserve(u8 format, unsigned words) {
    if (gx_lazy_changed()) { gx_batch_flush(); gx_lazy_send(); }
    if (gx_batch.count && (gx_batch.format != format || gx_batch.words != words ||
        gx_batch.count + 3 > GX_BATCH_TRIANGLES * 3))
        gx_batch_flush();
    gx_batch.format = format; gx_batch.words = words;
    u32 *out = gx_batch.data + gx_batch.count * words;
    gx_batch.count += 3;
    return out;
}

/* ------------------------------------------------------------ immediate */
static struct { u16 sx, sy, sw, sh; unsigned proj_w, proj_h; s16 desc[32]; } gx_shadow;
/* After menu.c: everything the renderer wants is re-sent at the next draw. */
static void gx_shadow_reset(void) {
    gx_state_gen++;
    gx_batch_flush();
    memset(&gx_sent, 0xfe, sizeof gx_sent);
    /* Re-send exactly what the renderer wants; fields it never set match the
     * sentinel and stay clean. */
#define LZ_RECHECK(field, bit) if (memcmp(&gx_want.field, &gx_sent.field, sizeof gx_want.field)) gx_dirty |= (bit)
    gx_dirty = gx_dirty_tg = gx_dirty_to = gx_dirty_kc = gx_dirty_tc = 0;
    LZ_RECHECK(dither, LZ_DITHER); LZ_RECHECK(color, LZ_COLOR); LZ_RECHECK(alpha, LZ_ALPHA);
    LZ_RECHECK(zloc, LZ_ZLOC); LZ_RECHECK(numtev, LZ_NUMTEV); LZ_RECHECK(numtexgens, LZ_NUMTEXGENS);
    LZ_RECHECK(zmode, LZ_ZMODE); LZ_RECHECK(acmp, LZ_ACMP); LZ_RECHECK(blend, LZ_BLEND);
    if (gx_want.ztex_op != gx_sent.ztex_op || gx_want.ztex_fmt != gx_sent.ztex_fmt ||
        gx_want.ztex_bias != gx_sent.ztex_bias) gx_dirty |= LZ_ZTEX;
    for (unsigned i = 0; i < 8; i++) if (memcmp(gx_want.tg[i], gx_sent.tg[i], sizeof gx_want.tg[i])) gx_dirty_tg |= 1u << i;
    for (unsigned i = 0; i < 16; i++) if (memcmp(gx_want.to[i], gx_sent.to[i], sizeof gx_want.to[i])) gx_dirty_to |= 1u << i;
    for (unsigned i = 0; i < 4; i++) {
        if (memcmp(&gx_want.kc[i], &gx_sent.kc[i], sizeof(GXColor))) gx_dirty_kc |= 1u << i;
        if (memcmp(&gx_want.tc[i], &gx_sent.tc[i], sizeof(GXColor))) gx_dirty_tc |= 1u << i;
    }
#undef LZ_RECHECK
    memset(&gx_shadow, 0xff, sizeof gx_shadow);
}
static void gx_lazy_init(void) {
    memset(&gx_want, 0xfe, sizeof gx_want);
    gx_shadow_reset();
}
static void shadow_scissor(u32 x, u32 y, u32 w, u32 h) {
    if (gx_shadow.sx != x || gx_shadow.sy != y || gx_shadow.sw != w || gx_shadow.sh != h) {
        gx_batch_flush(); gx_state_gen++;
        gx_shadow.sx = x; gx_shadow.sy = y; gx_shadow.sw = w; gx_shadow.sh = h; GX_SetScissor(x, y, w, h);
    }
}
/* Any direct projection load ends reuse of the renderer's w x h ortho. */
static void shadow_projection(Mtx44 m, u8 type) {
    gx_state_gen++;
    gx_batch_flush(); gx_shadow.proj_w = gx_shadow.proj_h = ~0u; GX_LoadProjectionMtx(m, type);
}
static void shadow_vtx_desc(u8 attr, u8 type) {
    if (attr < 32 && gx_shadow.desc[attr] == type) return;
    gx_batch_flush(); gx_state_gen++;
    if (attr < 32) gx_shadow.desc[attr] = type;
    GX_SetVtxDesc(attr, type);
}
static void shadow_clear_vtx_desc(void) {
    gx_state_gen++;
    gx_batch_flush();
    for (unsigned i = 0; i < 32; i++) gx_shadow.desc[i] = GX_NONE;
    GX_ClearVtxDesc();
}
static u8 gx_desc_of(u8 attr) { return gx_shadow.desc[attr] < 0 ? GX_NONE : (u8)gx_shadow.desc[attr]; }

#define GX_SetDither lz_dither
#define GX_SetColorUpdate lz_color
#define GX_SetAlphaUpdate lz_alpha
#define GX_SetZCompLoc lz_zloc
#define GX_SetNumTevStages lz_numtev
#define GX_SetNumTexGens lz_numtexgens
#define GX_SetZMode lz_zmode
#define GX_SetAlphaCompare lz_acmp
#define GX_SetBlendMode lz_blend
#define GX_SetZTexture lz_ztex
#define GX_SetTexCoordGen lz_texcoordgen
#define GX_SetTevOrder lz_tevorder
#define GX_SetTevKColor lz_kcolor
#define GX_SetTevColor lz_tevcolor
#define GX_SetScissor shadow_scissor
#define GX_LoadProjectionMtx shadow_projection
#define GX_SetVtxDesc shadow_vtx_desc
#define GX_ClearVtxDesc shadow_clear_vtx_desc
/* Drawing: pending triangles, then changed state, then the caller's draw. */
#define GX_Begin(...) (gx_draw_prepare(), gx_state_gen++, GX_Begin(__VA_ARGS__))
/* Everything else that changes GPU state, memory or sync flushes first. */
#define GX_BATCH_FLUSHING(fn, ...) (gx_batch_flush(), gx_state_gen++, fn(__VA_ARGS__))
#define GX_SetVtxAttrFmt(...) GX_BATCH_FLUSHING(GX_SetVtxAttrFmt, __VA_ARGS__)
#define GX_SetTevAlphaIn(...) GX_BATCH_FLUSHING(GX_SetTevAlphaIn, __VA_ARGS__)
#define GX_SetTevColorOp(...) GX_BATCH_FLUSHING(GX_SetTevColorOp, __VA_ARGS__)
#define GX_SetTevColorIn(...) GX_BATCH_FLUSHING(GX_SetTevColorIn, __VA_ARGS__)
#define GX_SetTevAlphaOp(...) GX_BATCH_FLUSHING(GX_SetTevAlphaOp, __VA_ARGS__)
#define GX_SetTevOp(...) GX_BATCH_FLUSHING(GX_SetTevOp, __VA_ARGS__)
#define GX_SetTevKAlphaSel(...) GX_BATCH_FLUSHING(GX_SetTevKAlphaSel, __VA_ARGS__)
#define GX_SetTevKColorSel(...) GX_BATCH_FLUSHING(GX_SetTevKColorSel, __VA_ARGS__)
#define GX_LoadTexMtxImm(...) GX_BATCH_FLUSHING(GX_LoadTexMtxImm, __VA_ARGS__)
#define GX_LoadPosMtxImm(...) GX_BATCH_FLUSHING(GX_LoadPosMtxImm, __VA_ARGS__)
#define GX_LoadTexObj(...) GX_BATCH_FLUSHING(GX_LoadTexObj, __VA_ARGS__)
#define GX_InvalidateTexAll(...) GX_BATCH_FLUSHING(GX_InvalidateTexAll, __VA_ARGS__)
#define GX_CopyDisp(...) GX_BATCH_FLUSHING(GX_CopyDisp, __VA_ARGS__)
#define GX_SetViewport(...) GX_BATCH_FLUSHING(GX_SetViewport, __VA_ARGS__)
#define GX_SetPixelFmt(...) GX_BATCH_FLUSHING(GX_SetPixelFmt, __VA_ARGS__)
#define GX_SetNumChans(...) GX_BATCH_FLUSHING(GX_SetNumChans, __VA_ARGS__)
#define GX_SetChanCtrl(...) GX_BATCH_FLUSHING(GX_SetChanCtrl, __VA_ARGS__)
#define GX_SetFog(...) GX_BATCH_FLUSHING(GX_SetFog, __VA_ARGS__)
#define GX_SetCullMode(...) GX_BATCH_FLUSHING(GX_SetCullMode, __VA_ARGS__)
#define GX_SetClipMode(...) GX_BATCH_FLUSHING(GX_SetClipMode, __VA_ARGS__)
#define GX_SetCopyClear(...) GX_BATCH_FLUSHING(GX_SetCopyClear, __VA_ARGS__)
#define GX_SetCurrentMtx(...) GX_BATCH_FLUSHING(GX_SetCurrentMtx, __VA_ARGS__)
#define GX_SetDispCopySrc(...) GX_BATCH_FLUSHING(GX_SetDispCopySrc, __VA_ARGS__)
#define GX_SetDispCopyDst(...) GX_BATCH_FLUSHING(GX_SetDispCopyDst, __VA_ARGS__)
#define GX_SetDispCopyYScale(...) GX_BATCH_FLUSHING(GX_SetDispCopyYScale, __VA_ARGS__)
#define GX_DrawDone(...) GX_BATCH_FLUSHING(GX_DrawDone, __VA_ARGS__)
#endif
