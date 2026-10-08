/* Native replacement for gl f_gl_0002adac (VIPER_WII_NATIVE_GL_DRAW): the
 * packed-vertex draw routine behind gl command 0x47 (draw function 0x2adac).
 *
 * Per packet of up to 15 vertices it reserves FIFO space and writes a type-3
 * header; per vertex it transforms by the combined model/projection/viewport
 * rows, forms 1/w from frsqrte(w*w) and three Newton steps, lights one
 * clamped diffuse term, stages the vertex at 0x3398 and writes the previous
 * vertex's ten FIFO words (software pipelined; the first block lands in the
 * 0x33c0 RAM scratch).
 *
 * This is a line-for-line transcription of the generated C. Every value is
 * computed by the same expression and rounding macro, every guest memory
 * access is the same accessor in the same order, and the budget is charged
 * at the same points. Guest registers live in locals; all of them are written
 * back before each checkpoint, guest call and return, and reloaded after,
 * so an interrupt or callee sees exactly the generated code's context. The
 * ten FIFO words of a vertex go through the bulk writer when it accepts them
 * (the gl_000 specializer's decision), otherwise as ten scalar stores. */
#include "ppc_rt.h"
#include "voodoo_headless.h"
#include "native_ram.h"
#if defined(VIPER_WII_RSQRT_INLINE) && defined(VIPER_WII_EXACT_RSQRT) && defined(VIPER_WII)
/* The per-vertex reciprocal square root inlined: a call here spills the
 * block's live floating-point values around it. Same function, same bits. */
#define rt_frsqrte(x) wii_rsqrt_exact_inline(x)
#endif

void f_gl_000211d4(PPCContext *c);

#define GPRS(X) X(0) X(3) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(29) X(30) X(31)
#define FPRS(X) X(0) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) \
    X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) \
    X(28) X(29) X(30) X(31)
#define DECL_R(n) uint32_t r##n = c->r[n];
#define DECL_F(n) double f##n = c->f[n];
#define OUT_R(n) c->r[n] = r##n;
#define OUT_F(n) c->f[n] = f##n;
#define IN_R(n) r##n = c->r[n];
#define IN_F(n) f##n = c->f[n];
#define SYNC_OUT() do { GPRS(OUT_R) FPRS(OUT_F) c->cr[0] = cr0; c->cr[1] = cr1; c->ctr = ctr; \
    c->lr = lr; c->xer_ca = xer_ca; } while (0)
#define SYNC_IN() do { GPRS(IN_R) FPRS(IN_F) cr0 = c->cr[0]; cr1 = c->cr[1]; ctr = c->ctr; \
    lr = c->lr; xer_ca = c->xer_ca; stage_const = STAGE_REGS_CONST; } while (0)
/* The generated CHK(c, pc, 0) acts only when the budget is already spent,
 * and nothing reads the context otherwise: sync only when it will fire. */
#define NATIVE_CHK(pc) do { if (UNLIKELY(c->budget <= 0)) { \
    held_materialize(); pend_flush(); SYNC_OUT(); CHK(c, (pc), 0); SYNC_IN(); } } while (0)

/* The staging registers hold the constants the routine loads at entry. */
#define STAGE_REGS_CONST (r15 == 0x3398u && r14 == 0x33acu && r19 == 4u && r18 == 8u && r17 == 12u && r16 == 16u)
/* Re-evaluated only where those registers change: entry setup and SYNC_IN. */
#define STAGE_CONST stage_const
/* FIFO words in guest order, as scalar ST32s or one bulk publication. */
static inline void store_words(uint32_t ea, const uint32_t *w, unsigned n) {
    if (rt_wii_bulk_lfb_allowed() && wii_voodoo_bulk_writer_ready(ea, n)) {
        wii_voodoo_bulk_writer_be(ea, w, n);
        return;
    }
    for (unsigned i = 0; i < n; i++) ST32(ea + 4 * i, w[i]);
}
/* Vertices 1..n-1 of a packet fill contiguous slots before its header is
 * written, so they are gathered and published together. Pending words are
 * flushed before the header, any firing checkpoint and any guest call, so
 * every point that can observe the device sees the generated write order. */
/* Ten words without a library call: the compiler keeps them in registers. */
static inline void copy10(uint32_t *d, const uint32_t *w) {
    d[0] = w[0]; d[1] = w[1]; d[2] = w[2]; d[3] = w[3]; d[4] = w[4];
    d[5] = w[5]; d[6] = w[6]; d[7] = w[7]; d[8] = w[8]; d[9] = w[9];
}
/* The pending and held buffers trade places when a packet is held. */
static uint32_t word_buffers[2][150] __attribute__((aligned(32)));
static uint32_t *pend_words = word_buffers[0], pend_ea;
static unsigned pend_n;
static inline void pend_flush(void) {
    if (pend_n) { store_words(pend_ea, pend_words, pend_n); pend_n = 0; }
}
#ifdef VIPER_WII_DIRECT_TRIANGLES
/* A packet whose vertices 1..n-1 and header are not yet stored: its last
 * vertex will complete it, and then the device runs it without the words
 * passing through the FIFO (wii_voodoo_direct_triangles). */
static uint32_t *held_words = word_buffers[1], held_ea, held_cmd;
static unsigned held_n;
static int held;
/* Store a held packet as the generated code would have by now: vertices
 * 1..n-1, then the header, still missing its last vertex. */
static inline void held_materialize(void) {
    if (!held) return;
    held = 0;
    if (held_n) store_words(held_ea + 4, held_words, held_n);
    ST32LE(held_ea, held_cmd);
}
#else
static inline void held_materialize(void) {}
#endif
/* Outcomes of complete_or_store: direct, declined, plain RAM, plain device. */
/* [4]: packets that ended unheld. */
static unsigned long long draw_outcomes[5];
void wii_native_gl_draw_stats(unsigned long long out[5]) { memcpy(out, draw_outcomes, sizeof draw_outcomes); }
/* The ten words that may complete the previous packet. */
static inline void complete_or_store(uint32_t ea, const uint32_t w[10]) {
#ifdef VIPER_WII_DIRECT_TRIANGLES
    if (held && ea == held_ea + 4 + 4 * held_n) {
        held = 0;
        copy10(held_words + held_n, w);
        if (wii_voodoo_direct_triangles(held_ea, held_cmd, held_words, held_n + 10)) { draw_outcomes[0]++; return; }
        draw_outcomes[1]++;
        if (held_n) store_words(held_ea + 4, held_words, held_n);
        ST32LE(held_ea, held_cmd);
        store_words(ea, w, 10);
        return;
    }
#endif
    draw_outcomes[ea < RAM_LIMIT ? 2 : 3]++;
    held_materialize();
    store_words(ea, w, 10);
}

WII_HOT_wii_native_gl_0002adac void wii_native_gl_0002adac(PPCContext *c) {
    /* The entry checkpoint may run another fiber: load the context after it. */
    CHK(c, 0x2adacu, 0);
    NATIVE_RAM_BEGIN();
    GPRS(DECL_R) FPRS(DECL_F)
    uint8_t cr0 = c->cr[0], cr1 = c->cr[1], xer_ca = c->xer_ca;
    uint32_t ctr = c->ctr, lr = c->lr;
    uint32_t w[10];
    int first_block, ordered, stage_const = 0;
#define W(i, v) do { w[i] = (v); if (ordered) NST32(r25 + 4 * (i), w[i]); } while (0)

    /* 0002adac: matrix rows, viewport and lighting setup. */
    c->budget -= 87;
    r6 = LD8(0x3452u);
    r13 = 0x2780u;
    r7 = LD8(0x3451u);
    r13 = NLD32LE(r13);
    r5 = ROTL(r6, 4) & 0xfffffff0u;
    r22 = NLD32(0x3360u);
    r6 = ROTL(r6, 3) & 0xfffffff8u;
    r26 = NLD32(0x2358u);
    r6 = r6 + r5;
    r24 = NLD32(0x235cu);
    r5 = ROTL(r7, 5) & 0xffffffe0u;
    f11 = NLDF32(0x275cu);
    r7 = ROTL(r7, 4) & 0xfffffff0u;
    f10 = NLDF32(0x2760u);
    r7 = r7 + r5;
    f29 = NLDF32(r7 + 0x23e4u);
    r24 = r24 + 0xfffffda4u;
    f22 = NLDF32(r7 + 0x23f0u);
    f21 = NLDF32(r7 + 0x23fcu);
    f20 = NLDF32(r7 + 0x2408u);
    f0 = NLDF32(r6 + 0x26dcu);
    r20 = 0x3cu;
    f1 = NLDF32(r6 + 0x26e0u);
    r19 = 0x4u;
    f2 = NLDF32(0x2940u);
    r18 = 0x8u;
    f3 = NLDF32(0x2944u);
    r17 = 0xcu;
    f31 = NLDF32(r7 + 0x23dcu);
    r16 = 0x10u;
    r15 = 0x3398u;
    f28 = NLDF32(r7 + 0x23e8u);
    r14 = r15 + 0x14u;
    f27 = NLDF32(r7 + 0x23f4u);
    f26 = NLDF32(r7 + 0x2400u);
    r25 = r15 + 0x28u;
    stage_const = STAGE_REGS_CONST;
    f31 = ROUND_S(f0 * f31);
    f28 = ROUND_S(f0 * f28);
    f27 = ROUND_S(f0 * f27);
    f26 = ROUND_S(f0 * f26);
    f31 = ROUND_S(fma(f29, f1, f31));
    f28 = ROUND_S(fma(f22, f1, f28));
    f27 = ROUND_S(fma(f21, f1, f27));
    f26 = ROUND_S(fma(f20, f1, f26));
    f31 = ROUND_S(f2 * f31);
    f28 = ROUND_S(f2 * f28);
    f27 = ROUND_S(f2 * f27);
    f26 = ROUND_S(f2 * f26);
    f31 = ROUND_S(-fma(f29, f3, -f31));
    f28 = ROUND_S(-fma(f22, f3, -f28));
    f27 = ROUND_S(-fma(f21, f3, -f27));
    f26 = ROUND_S(-fma(f20, f3, -f26));
    f0 = NLDF32(r6 + 0x26e4u);
    f1 = NLDF32(r6 + 0x26e8u);
    f2 = NLDF32(0x2948u);
    f3 = NLDF32(0x294cu);
    f30 = NLDF32(r7 + 0x23e0u);
    f25 = NLDF32(r7 + 0x23ecu);
    f24 = NLDF32(r7 + 0x23f8u);
    f23 = NLDF32(r7 + 0x2404u);
    f30 = ROUND_S(f0 * f30);
    f25 = ROUND_S(f0 * f25);
    f24 = ROUND_S(f0 * f24);
    f23 = ROUND_S(f0 * f23);
    f30 = ROUND_S(fma(f29, f1, f30));
    f25 = ROUND_S(fma(f22, f1, f25));
    f24 = ROUND_S(fma(f21, f1, f24));
    f23 = ROUND_S(fma(f20, f1, f23));
    f30 = ROUND_S(f2 * f30);
    f25 = ROUND_S(f2 * f25);
    f24 = ROUND_S(f2 * f24);
    f23 = ROUND_S(f2 * f23);
    f30 = ROUND_S(-fma(f29, f3, -f30));
    f25 = ROUND_S(-fma(f22, f3, -f25));
    f24 = ROUND_S(-fma(f21, f3, -f24));
    f23 = ROUND_S(-fma(f20, f3, -f23));
    f9 = NLDF32(0x2950u);
    f8 = NLDF32(0x2954u);
    f19 = NLDF32(0x3374u);
    f18 = NLDF32(0x3378u);
    f17 = NLDF32(0x337cu);
    f16 = NLDF32(0x338cu);
    f15 = NLDF32(0x3390u);
    f14 = NLDF32(0x3394u);
    f13 = NLDF32(0x2030u);
    f12 = NLDF32(0x2020u);
    f7 = NLDF32(0x3380u);

packet: /* 0002af08: up to 15 vertices per type-3 packet. */
    c->budget -= 7;
    r7 = r30 + 0xfffffff1u;
    cr0 = CMPU(r26, r24) | c->xer_so;
    { int32_t v = (int32_t)r7; xer_ca = (v < 0) && (v & 0x7fffffffu); r6 = (uint32_t)(v >> 31); }
    r7 = r7 & r6;
    r21 = r7 + 0xfu;
    r30 = (uint32_t)((uint64_t)~r21 + r30 + 1);
    if (!(cr0 & 8)) {
        c->budget -= 6;
        NST32(0x2358u, r26);
        r3 = 0x25cu;
        lr = 0x2af30u;
        held_materialize();
        SYNC_OUT();
        f_gl_000211d4(c); if (c->unwind) return;
        SYNC_IN();
        r24 = NLD32(0x235cu);
        r26 = NLD32(0x2358u);
        r24 = r24 + 0xfffffda4u;
    }
    /* 0002af3c */
    c->budget -= 5;
    ctr = r21;
    r22 = (ROTL(r21, 6) & 0x3c0u) | (r22 & 0xfffffc3fu);
    cr1 = CMPS((int32_t)r30, (int32_t)0) | c->xer_so;
    r23 = r26;
    r26 = r26 + 0x4u;
    first_block = 1;

vertex: /* 0002af50 */
    c->budget -= 74;
    /* Held and pending words may pass only guest RAM accesses: vertex data
     * in device space (never so in the game) sees the generated order. */
    /* Held and pending words may pass only guest RAM accesses. With vertex
     * data in device space (never so in the game) every word is stored at
     * its generated position instead. */
    /* So may FIFO words bound for guest RAM itself (the per-call scratch
     * block at 0x33c0, or a FIFO pointer in RAM): stored in order too. */
    ordered = UNLIKELY(r31 >= RAM_LIMIT - 0x20u) || r25 < RAM_LIMIT;
    if (ordered) { held_materialize(); pend_flush(); }
#ifdef VIPER_WII_CACHE_HINTS
    /* Vertex records are 32 bytes, one cache line each: touch the one two
     * vertices ahead (a hint only; no architectural effect). */
#ifndef VIPER_WII_2ADAC_PREFETCH
#define VIPER_WII_2ADAC_PREFETCH 0x40u
#endif
#if VIPER_WII_2ADAC_PREFETCH
    if (LIKELY(r31 <= RAM_SIZE - (VIPER_WII_2ADAC_PREFETCH + 0x20u)))
        __builtin_prefetch(native_ram_base + r31 + VIPER_WII_2ADAC_PREFETCH);
#endif
#endif
    if (LIKELY(STAGE_CONST && r31 <= RAM_SIZE - 0x20u)) {
        /* Staging registers at their constant values and vertex data wholly
         * in plain RAM: constant staging addresses fold every bounds test,
         * and the vertex loads are the in-RAM case of the accessor. */
#define VLDF(o) native_ldf32_ram(native_ram_base, r31 + (o))
#define SLD(k) NLD32LE(0x3398u + 4u * (k))
#define SST(k, d) NSTF32(0x3398u + 4u * (k), (d))
#include "native_gl_draw_vertex.h"
#undef VLDF
#undef SLD
#undef SST
    } else {
#define VLDF(o) NLDF32(r31 + (o))
#define SLD(k) ((k) == 0 ? NLD32LE(r15) : (k) == 1 ? NLD32LE(r19 + r15) : (k) == 2 ? NLD32LE(r18 + r15) : \
    (k) == 3 ? NLD32LE(r17 + r15) : (k) == 4 ? NLD32LE(r16 + r15) : (k) == 6 ? NLD32LE(r19 + r14) : \
    (k) == 7 ? NLD32LE(r18 + r14) : (k) == 8 ? NLD32LE(r17 + r14) : NLD32LE(r16 + r14))
#define SST(k, d) NSTF32(r15 + 4u * (k), (d))
#include "native_gl_draw_vertex.h"
#undef VLDF
#undef SLD
#undef SST
    }
    /* The previous vertex's FIFO words; their sources were read above,
     * before this vertex's staging stores overwrote them. */
    if (ordered) {
        first_block = 0;
    } else if (first_block || pend_n + 10 > 150 || (pend_n && r25 != pend_ea + 4 * pend_n)) {
        /* The previous packet's last vertex (or the RAM scratch block). */
        pend_flush();
        if (first_block) complete_or_store(r25, w);
        else { pend_ea = r25; copy10(pend_words, w); pend_n = 10; }
        first_block = 0;
    } else {
        if (!pend_n) pend_ea = r25;
        copy10(pend_words + pend_n, w); pend_n += 10;
    }
    r25 = r26;
    NSTF32(r15 + 0x24u, f6);
    r26 = r26 + 0x28u;
    ctr--;
    if (ctr != 0) { NATIVE_CHK(0x2af50u); goto vertex; }
    c->budget -= 3;
#ifdef VIPER_WII_DIRECT_TRIANGLES
    {
        /* Hold the packet when vertices 1..n-1 are all still gathered. */
        unsigned nv = (r22 >> 6) & 15;
        if (!held && nv && r23 >= RAM_LIMIT && pend_n == 10 * (nv - 1) && (!pend_n || pend_ea == r23 + 4)) {
            held = 1; held_ea = r23; held_cmd = r22;
            uint32_t *t = held_words; held_words = pend_words; pend_words = t;
            held_n = pend_n; pend_n = 0;
        } else {
            draw_outcomes[4]++;
            pend_flush();
            NST32LE(r23, r22);
        }
    }
#else
    pend_flush();
    NST32LE(r23, r22);
#endif
    r22 = (ROTL(r22, 4) & 0x18u) | (r22 & 0xffffffe7u);
    if (cr1 & 4) { NATIVE_CHK(0x2af08u); goto packet; }
    c->budget -= 10;
    if (UNLIKELY(r31 >= RAM_LIMIT - 0x20u)) { held_materialize(); pend_flush(); }
    r7 = NLD32(r31);
    r5 = LD16(0x3434u);
    r0 = NLD32(r31 + 0x1cu);
    r31 = r31 + 0x4u;
    r6 = ROTL(r7, 16) & 0xffffu;
    cr0 = CMPS((int32_t)r6, (int32_t)r5) | c->xer_so;
    r30 = r7 & 0xffffu;
    r22 = (ROTL(r22, 2) & 0x18u) | (r22 & 0xffffffe7u);
    if (cr0 & 2) { NATIVE_CHK(0x2af08u); goto packet; }
    c->budget -= 23;
    lr = r29;
    r31 = r31 + 0xfffffffcu;
    NST32(0x2358u, r26);
    /* Final pipelined vertex. */
    r12 = NLD32LE(r15);
    r11 = NLD32LE(r19 + r15);
    r10 = NLD32LE(r18 + r15);
    r9 = NLD32LE(r17 + r15);
    r8 = NLD32LE(r16 + r15);
    w[0] = r12; w[1] = r11; w[2] = r10; w[3] = r9; w[4] = r8;
    if (UNLIKELY(r25 < RAM_LIMIT || r25 > 0xffffffd8u)) {
        /* Bound for guest RAM: the generated order, words 0-4 stored before
         * slots 6-9 are reloaded (they may be the same words). */
        held_materialize(); pend_flush();
        for (unsigned i = 0; i < 5; i++) NST32(r25 + 4 * i, w[i]);
        r11 = NLD32LE(r19 + r14);
        r10 = NLD32LE(r18 + r14);
        r9 = NLD32LE(r17 + r14);
        r8 = NLD32LE(r16 + r14);
        w[5] = r13; w[6] = r11; w[7] = r10; w[8] = r9; w[9] = r8;
        for (unsigned i = 5; i < 10; i++) NST32(r25 + 4 * i, w[i]);
    } else {
        r11 = NLD32LE(r19 + r14);
        r10 = NLD32LE(r18 + r14);
        r9 = NLD32LE(r17 + r14);
        r8 = NLD32LE(r16 + r14);
        w[5] = r13; w[6] = r11; w[7] = r10; w[8] = r9; w[9] = r8;
        complete_or_store(r25, w);
    }
    SYNC_OUT();
    RETURN(c);
}
