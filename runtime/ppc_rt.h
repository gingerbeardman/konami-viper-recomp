/*
 * Runtime ABI for statically recompiled Konami Viper (MPC8240 / PPC603e) code.
 *
 * Guest RAM is kept in guest (big-endian) byte order; accessors adapt to the host.
 * All guest state lives in PPCContext; generated code never caches registers
 * across calls, so one context can be shared by every guest task/fiber.
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LIKELY(x) __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)

#if defined(VIPER_WII_EXACT_RSQRT) && defined(VIPER_WII)
#if defined(VIPER_WII_NATIVE_FRSQRTE) || defined(VIPER_WII_FRSQRTE_MEMO)
#error Exact rsqrt replaces the native estimate and the memo
#endif
#include "rsqrt_exact.h"
#endif
/* Experimental Wii estimate: PPC750 and MPC603e estimate bits may differ.
 * Keep exact legacy lowering unless explicitly selected for comparison. */
#if defined(VIPER_WII_EXACT_RSQRT) && defined(VIPER_WII) && defined(VIPER_WII_RSQRT_MEMO)
/* Strips share vertices, so the same w*w comes back a triangle or two later:
 * remember recent results by the input's exact bits. Only in round-to-nearest
 * (the result can depend on the rounding mode). Empty slots hold the true
 * answer for +0 (bits 0): 1/sqrt(+0) = +inf. */
#ifdef VIPER_WII_PRESERVE_RSQRT
#include "preserve_call.h"
#endif
typedef struct { uint64_t key; double val; } WiiRsqrtMemo;
#ifndef VIPER_WII_RSQRT_MEMO_SIZE
#define VIPER_WII_RSQRT_MEMO_SIZE 64
#endif
extern WiiRsqrtMemo wii_rsqrt_memo[VIPER_WII_RSQRT_MEMO_SIZE];
static inline double wii_rsqrt_memoized(double x) {
    uint64_t b;
    memcpy(&b, &x, sizeof b);
    WiiRsqrtMemo *m = &wii_rsqrt_memo[(unsigned)((b >> 29) ^ (b >> 35) ^ (b >> 41)) & (VIPER_WII_RSQRT_MEMO_SIZE - 1)];
    if (__builtin_expect(m->key == b && rt_round_nearest, 1)) return m->val;
#ifdef VIPER_WII_PRESERVE_RSQRT
    /* The miss through wii/preserve_call.h: callers keep their volatile
     * registers across it. Same function, same result. */
    double r = wii_pcall_d_d(wii_rsqrt_exact, x);
#else
    double r = wii_rsqrt_exact(x);
#endif
    if (rt_round_nearest) { m->key = b; m->val = r; }
    return r;
}
#endif
static inline double rt_frsqrte(double x) {
#if   defined(VIPER_WII_EXACT_RSQRT) && defined(VIPER_WII) && defined(VIPER_WII_RSQRT_MEMO)
    return wii_rsqrt_memoized(x);
#elif defined(VIPER_WII_EXACT_RSQRT) && defined(VIPER_WII)
    /* Same bits as 1.0/sqrt(x), seeded by the Broadway estimate. */
    return wii_rsqrt_exact(x);
#elif defined(VIPER_WII_NATIVE_FRSQRTE) && defined(VIPER_WII)
    double y;
    __asm__ volatile("frsqrte %0,%1" : "=f"(y) : "f"(x));
    return y;
#else
    return 1.0 / sqrt(x);
#endif
}

typedef struct PPCContext {
    uint32_t r[32];
    double f[32];
    uint8_t cr[8];          /* 4-bit fields: LT=8 GT=4 EQ=2 SO=1 */
    uint32_t lr, ctr;
    uint8_t xer_so, xer_ov, xer_ca, xer_bc;
    uint32_t msr, srr0, srr1;
    uint32_t sprg[4];
    uint32_t sr[16];
    uint32_t fpscr;
    uint32_t reserve;
    uint32_t spr[1024];     /* everything not special-cased */

    /* runtime bookkeeping */
#ifdef VIPER_WII_BUDGET32
    /* A slice is at most MAX_SLICE (20000) cycles and the only bulk charge is
     * 1000, so the budget always fits in 32 bits: 32-bit arithmetic for the
     * per-block charge and checkpoint test instead of 64-bit carry pairs. */
    int32_t budget;
#else
    int64_t budget;         /* cycles left before the next device event */
#endif
    uint64_t cycles;        /* virtual CPU cycles elapsed (at last sync) */
    uint64_t tb_base;       /* timebase offset (written by mttbl/mttbu) */
    uint64_t dec_event;     /* cycle at which DEC crosses zero */
    int unwind;             /* non-zero while unwinding host frames after rfi */
} PPCContext;

typedef void (*RtFn)(PPCContext *c);

typedef struct RtFunc {
    uint32_t addr;
    RtFn fn;
    uint32_t first_word;    /* used to tell overlays at the same address apart */
} RtFunc;

typedef struct RtModuleInfo {
    const char *name;
    uint32_t base, end, text_lo, text_hi;
    const RtFunc *funcs;
    unsigned nfuncs;
    uint32_t sig[8];        /* first words of the image: identifies which overlay is resident */
} RtModuleInfo;

/* ------------------------------------------------------------------ memory */
#define RAM_SIZE 0x01000000u
#define RAM_MASK 0x00ffffffu
#define RAM_LIMIT 0x02000000u   /* 16MB mirrored once */

extern uint8_t *g_ram;

/* Opt-in guest memory access census (exactness test probes); absent from ordinary builds. */
#ifdef VIPER_MEMORY_AUDIT
void rt_memory_access(uint32_t ea, unsigned bytes, int write);
void rt_memory_sample(void);
void rt_memory_report(void);
#define MEMORY_ACCESS(a, n, w) rt_memory_access((a), (n), (w))
#else
#define MEMORY_ACCESS(a, n, w) ((void)0)
#endif

uint32_t rt_mmio_r32(uint32_t ea);
uint32_t rt_mmio_r16(uint32_t ea);
uint32_t rt_mmio_r8(uint32_t ea);
void rt_mmio_w32(uint32_t ea, uint32_t v);
void rt_mmio_w16(uint32_t ea, uint32_t v);
void rt_mmio_w8(uint32_t ea, uint32_t v);

static inline uint32_t bswap32(uint32_t v) { return __builtin_bswap32(v); }
static inline uint16_t bswap16(uint16_t v) { return __builtin_bswap16(v); }

/* Guest RAM bytes are big-endian; peripheral LE helpers remain explicit. */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
static inline uint32_t guest_be32(uint32_t v) { return v; }
static inline uint16_t guest_be16(uint16_t v) { return v; }
#elif __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
static inline uint32_t guest_be32(uint32_t v) { return bswap32(v); }
static inline uint16_t guest_be16(uint16_t v) { return bswap16(v); }
#else
#error Unsupported host byte order
#endif

#ifdef VIPER_WII_SPLIT_RAM_HELPERS
/* Isolate rare mirrored-RAM/MMIO crossings so ordinary accesses can inline. */
uint32_t wii_ram_cross_r32(uint32_t ea) __attribute__((cold,noinline));
uint32_t wii_ram_cross_r16(uint32_t ea) __attribute__((cold,noinline));
void wii_ram_cross_w32(uint32_t ea,uint32_t v) __attribute__((cold,noinline));
void wii_ram_cross_w16(uint32_t ea,uint32_t v) __attribute__((cold,noinline));
#endif
static inline uint32_t LD32(uint32_t ea) {
    MEMORY_ACCESS(ea, 4, 0);
    if (ea < RAM_LIMIT && (ea & RAM_MASK) > RAM_MASK-3) {
#ifdef VIPER_WII_SPLIT_RAM_HELPERS
        return wii_ram_cross_r32(ea);
#else
        uint32_t v=0;
        for(unsigned i=0;i<4;i++){uint32_t a=ea+i;v=(v<<8)|(a<RAM_LIMIT?g_ram[a&RAM_MASK]:rt_mmio_r8(a));}
        return v;
#endif
    }
    if (LIKELY(ea < RAM_LIMIT)) { uint32_t v; memcpy(&v, g_ram + (ea & RAM_MASK), 4); return guest_be32(v); }
    return rt_mmio_r32(ea);
}
static inline uint32_t LD16(uint32_t ea) {
    MEMORY_ACCESS(ea, 2, 0);
    if (ea < RAM_LIMIT && (ea & RAM_MASK) == RAM_MASK) {
#ifdef VIPER_WII_SPLIT_RAM_HELPERS
        return wii_ram_cross_r16(ea);
#else
        uint32_t a=ea+1;
        return ((uint32_t)g_ram[RAM_MASK]<<8)|(a<RAM_LIMIT?g_ram[a&RAM_MASK]:rt_mmio_r8(a));
#endif
    }
    if (LIKELY(ea < RAM_LIMIT)) { uint16_t v; memcpy(&v, g_ram + (ea & RAM_MASK), 2); return guest_be16(v); }
    return rt_mmio_r16(ea);
}
static inline uint32_t LD8(uint32_t ea) {
    MEMORY_ACCESS(ea, 1, 0);
    if (LIKELY(ea < RAM_LIMIT)) return g_ram[ea & RAM_MASK];
    return rt_mmio_r8(ea);
}
static inline void ST32(uint32_t ea, uint32_t v) {
    MEMORY_ACCESS(ea, 4, 1);
    if (ea < RAM_LIMIT && (ea & RAM_MASK) > RAM_MASK-3) {
#ifdef VIPER_WII_SPLIT_RAM_HELPERS
        wii_ram_cross_w32(ea,v);return;
#else
        for(unsigned i=0;i<4;i++){uint32_t a=ea+i,b=(v>>(24-8*i))&255;if(a<RAM_LIMIT)g_ram[a&RAM_MASK]=b;else rt_mmio_w8(a,b);}
        return;
#endif
    }
    if (LIKELY(ea < RAM_LIMIT)) { v = guest_be32(v); memcpy(g_ram + (ea & RAM_MASK), &v, 4); return; }
    rt_mmio_w32(ea, v);
}
static inline void ST16(uint32_t ea, uint32_t v) {
    MEMORY_ACCESS(ea, 2, 1);
    if (ea < RAM_LIMIT && (ea & RAM_MASK) == RAM_MASK) {
#ifdef VIPER_WII_SPLIT_RAM_HELPERS
        wii_ram_cross_w16(ea,v);return;
#else
        g_ram[RAM_MASK]=(uint8_t)(v>>8);uint32_t a=ea+1;
        if(a<RAM_LIMIT)g_ram[a&RAM_MASK]=(uint8_t)v;else rt_mmio_w8(a,v&255);
        return;
#endif
    }
    if (LIKELY(ea < RAM_LIMIT)) { uint16_t s = guest_be16((uint16_t)v); memcpy(g_ram + (ea & RAM_MASK), &s, 2); return; }
    rt_mmio_w16(ea, v & 0xffff);
}
static inline void ST8(uint32_t ea, uint32_t v) {
    MEMORY_ACCESS(ea, 1, 1);
    if (LIKELY(ea < RAM_LIMIT)) { g_ram[ea & RAM_MASK] = (uint8_t)v; return; }
    rt_mmio_w8(ea, v & 0xff);
}
static inline uint32_t LD32LE(uint32_t ea) { return bswap32(LD32(ea)); }
static inline uint32_t LD16LE(uint32_t ea) { return bswap16((uint16_t)LD16(ea)); }
static inline void ST32LE(uint32_t ea, uint32_t v) { ST32(ea, bswap32(v)); }
static inline void ST16LE(uint32_t ea, uint32_t v) { ST16(ea, bswap16((uint16_t)v)); }

static inline double LDF32(uint32_t ea) { uint32_t b = LD32(ea); float f; memcpy(&f, &b, 4); return (double)f; }
static inline void STF32(uint32_t ea, double d) { float f = (float)d; uint32_t b; memcpy(&b, &f, 4); ST32(ea, b); }
static inline double LDF64(uint32_t ea) {
#if defined(VIPER_WII_F64_RAM) && !defined(VIPER_MEMORY_AUDIT)
    /* The two original word accesses have no callbacks inside this range.
     * Keep boundary, unaligned and audited accesses on their original path. */
    if (ea < RAM_LIMIT && (ea & RAM_MASK) <= RAM_MASK - 7 &&
        !((uintptr_t)(g_ram + (ea & RAM_MASK)) & 7)) {
        uint64_t b; memcpy(&b, g_ram + (ea & RAM_MASK), 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        b = __builtin_bswap64(b);
#endif
        double d; memcpy(&d, &b, 8); return d;
    }
#endif
    uint64_t b = ((uint64_t)LD32(ea) << 32) | LD32(ea + 4); double d; memcpy(&d, &b, 8); return d;
}
static inline void STF64(uint32_t ea, double d) {
#if defined(VIPER_WII_F64_RAM) && !defined(VIPER_MEMORY_AUDIT)
    if (ea < RAM_LIMIT && (ea & RAM_MASK) <= RAM_MASK - 7 &&
        !((uintptr_t)(g_ram + (ea & RAM_MASK)) & 7)) {
        uint64_t b; memcpy(&b, &d, 8);
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
        b = __builtin_bswap64(b);
#endif
        memcpy(g_ram + (ea & RAM_MASK), &b, 8); return;
    }
#endif
    uint64_t b; memcpy(&b, &d, 8); ST32(ea, (uint32_t)(b >> 32)); ST32(ea + 4, (uint32_t)b);
}
static inline uint64_t FPR_BITS(double d) { uint64_t b; memcpy(&b, &d, 8); return b; }
static inline double BITS_FPR(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }

/* ------------------------------------------------------------------ ALU helpers */
static inline uint32_t ROTL(uint32_t v, uint32_t s) { s &= 31; return s ? (v << s) | (v >> (32 - s)) : v; }
static inline uint32_t CLZ32(uint32_t v) { return v ? (uint32_t)__builtin_clz(v) : 32u; }
#define CMPS(a, b) ((uint8_t)((a) < (b) ? 8 : (a) > (b) ? 4 : 2))
#define CMPU(a, b) ((uint8_t)((a) < (b) ? 8 : (a) > (b) ? 4 : 2))
static inline uint8_t CMPF(double a, double b) {
    if (a < b) return 8;
    if (a > b) return 4;
    if (a == b) return 2;
    return 1; /* unordered */
}
#define ROUND_S(x) ((double)(float)(x))
/* Guest fsel (frA >= 0 ? frC : frB; NaN selects frB, -0 selects frC):
 * Broadway's own fsel selects the same bits without a branch. */
#if defined(VIPER_WII_FSEL) && defined(VIPER_WII)
static inline double wii_fsel(double a, double c_, double b) {
    double d;
    __asm__("fsel %0,%1,%2,%3" : "=f"(d) : "f"(a), "f"(c_), "f"(b));
    return d;
}
#define FSEL(a, c_, b) wii_fsel((a), (c_), (b))
#else
#define FSEL(a, c_, b) (((a) >= 0.0) ? (c_) : (b))
#endif
#if defined(VIPER_WII_CMP_MFCR) && defined(VIPER_WII)
/* Branch-free compares: the host's own cmpw/cmplw/fcmpu into cr7, read with
 * mfcr. A CR field holds LT GT EQ SO/UN as 8 4 2 1, exactly the values
 * above; the host SO bit is masked off (the guest's xer_so is OR'd in by the
 * generated code), the unordered bit kept for floats. */
static inline uint8_t wii_cmps(int32_t a, int32_t b) {
    uint32_t cr;
    __asm__("cmpw 7,%1,%2\n\tmfcr %0" : "=r"(cr) : "r"(a), "r"(b) : "cr7");
    return (uint8_t)(cr & 14);
}
static inline uint8_t wii_cmpu(uint32_t a, uint32_t b) {
    uint32_t cr;
    __asm__("cmplw 7,%1,%2\n\tmfcr %0" : "=r"(cr) : "r"(a), "r"(b) : "cr7");
    return (uint8_t)(cr & 14);
}
static inline uint8_t wii_cmpf(double a, double b) {
    uint32_t cr;
    __asm__("fcmpu 7,%1,%2\n\tmfcr %0" : "=r"(cr) : "f"(a), "f"(b) : "cr7");
    return (uint8_t)(cr & 15);
}
#undef CMPS
#undef CMPU
#define CMPS(a, b) wii_cmps((int32_t)(a), (int32_t)(b))
#define CMPU(a, b) wii_cmpu((uint32_t)(a), (uint32_t)(b))
#define CMPF(a, b) wii_cmpf((a), (b))
#endif

/* ------------------------------------------------------------------ runtime services */
void rt_check(PPCContext *c, uint32_t pc);          /* time/irq checkpoint */
/* Native target only: the dedicated media scan has no loading side
 * effects. Constant PCs let the compiler remove this from every other entry. */
#ifdef VIPER_SKIP_MEDIA_CHECK
void rt_dc_media_check_ok(PPCContext *c);
#define MEDIA_CHECK_OVERRIDE(c, pc) do { if ((pc) == 0x0000de28u) { rt_dc_media_check_ok(c); return; } } while (0)
#else
#define MEDIA_CHECK_OVERRIDE(c, pc) ((void)0)
#endif
#define CHK(c, pc, n) do { if (UNLIKELY(((c)->budget -= (n)) <= 0)) { rt_check((c), (pc)); if ((c)->unwind) return; } MEDIA_CHECK_OVERRIDE(c, pc); } while (0)
#define RETURN(c) return

#ifdef RT_TRACE
extern uint32_t g_trace_pc[65536], g_trace_sp[65536];
extern unsigned g_trace_pos;
extern int g_bp_any;
void rt_trace_bp(PPCContext *c, uint32_t pc);
#define TRACE(c, pc) do { unsigned _p = g_trace_pos++ & 65535; g_trace_pc[_p] = (pc); g_trace_sp[_p] = (c)->r[1]; \
                          if (UNLIKELY(g_bp_any)) rt_trace_bp((c), (pc)); } while (0)
#else
#define TRACE(c, pc) do { } while (0)
#endif

#if defined(VIPER_WII_IDLE_BATCH) && !defined(RT_TRACE) && !defined(VIPER_MEMORY_AUDIT)
#include "idle_worker.h"
#endif

void rt_call(PPCContext *c, uint32_t target);       /* indirect call through dispatch */
void rt_hook(PPCContext *c, uint32_t pc);           /* profile hooks (runtime/enhanced.c) */
int rt_sc(PPCContext *c, uint32_t next_pc);   /* 1: resumed at LR -> return */
void rt_rfi(PPCContext *c);
void rt_mtmsr(PPCContext *c, uint32_t v, uint32_t next_pc);
void rt_trap(PPCContext *c, uint32_t pc);
void rt_bad_insn(PPCContext *c, uint32_t pc, uint32_t word);
void rt_unimpl(PPCContext *c, uint32_t pc, uint32_t word);
void rt_fallthrough(PPCContext *c, uint32_t pc);

uint32_t rt_mfspr(PPCContext *c, int spr);
void rt_mtspr(PPCContext *c, int spr, uint32_t v);
uint32_t rt_mftb(PPCContext *c, int tbr);
#if defined(VIPER_WII_INLINE_HELPERS) && defined(VIPER_WII)
/* Inline so each generated call site folds its constant crm / register
 * numbers. Same semantics as the out-of-line versions in cpu.c. */
static inline uint32_t rt_cr_pack(PPCContext *c) {
    return ((uint32_t)(c->cr[0] & 15) << 28) | ((uint32_t)(c->cr[1] & 15) << 24) |
           ((uint32_t)(c->cr[2] & 15) << 20) | ((uint32_t)(c->cr[3] & 15) << 16) |
           ((uint32_t)(c->cr[4] & 15) << 12) | ((uint32_t)(c->cr[5] & 15) << 8) |
           ((uint32_t)(c->cr[6] & 15) << 4) | (uint32_t)(c->cr[7] & 15);
}
static inline void rt_cr_unpack(PPCContext *c, uint32_t v, uint32_t crm) {
    if (crm & 0x80u) c->cr[0] = (v >> 28) & 15;
    if (crm & 0x40u) c->cr[1] = (v >> 24) & 15;
    if (crm & 0x20u) c->cr[2] = (v >> 20) & 15;
    if (crm & 0x10u) c->cr[3] = (v >> 16) & 15;
    if (crm & 0x08u) c->cr[4] = (v >> 12) & 15;
    if (crm & 0x04u) c->cr[5] = (v >> 8) & 15;
    if (crm & 0x02u) c->cr[6] = (v >> 4) & 15;
    if (crm & 0x01u) c->cr[7] = v & 15;
}
#else
uint32_t rt_cr_pack(PPCContext *c);
void rt_cr_unpack(PPCContext *c, uint32_t v, uint32_t crm);
#endif
uint32_t rt_xer_pack(PPCContext *c);
void rt_xer_unpack(PPCContext *c, uint32_t v);
uint32_t rt_divw(PPCContext *c, uint32_t a, uint32_t b, int oe);
uint32_t rt_divwu(PPCContext *c, uint32_t a, uint32_t b, int oe);
uint32_t rt_sraw(PPCContext *c, uint32_t v, uint32_t n);
void rt_dcbz(PPCContext *c, uint32_t ea);
#if defined(VIPER_WII_INLINE_HELPERS) && defined(VIPER_WII) && defined(VIPER_WII_STRING_WORDS) && \
    !defined(VIPER_MEMORY_AUDIT)
/* RAM fast path inline (as VIPER_WII_STRING_WORDS in cpu.c); MMIO, wrap and
 * empty strings take the byte-wise out-of-line path. */
void rt_lswi_slow(PPCContext *c, uint32_t ea, int rd, int nb);
void rt_stswi_slow(PPCContext *c, uint32_t ea, int rs, int nb);
static inline void rt_lswi(PPCContext *c, uint32_t ea, int rd, int nb) {
    if (nb > 0 && ea < RAM_LIMIT && (unsigned)nb <= RAM_LIMIT - ea &&
        (unsigned)nb <= RAM_SIZE - (ea & RAM_MASK)) {
        const uint8_t *src = g_ram + (ea & RAM_MASK);
        int n = 0, r = rd & 31;
        for (; n + 4 <= nb; n += 4, r = (r + 1) & 31) {
            uint32_t value; memcpy(&value, src + n, 4); c->r[r] = guest_be32(value);
        }
        if (n < nb) {
            uint32_t value = 0;
            for (int i = 0; n + i < nb; i++) value |= (uint32_t)src[n + i] << (24 - 8 * i);
            c->r[r] = value;
        }
        return;
    }
    rt_lswi_slow(c, ea, rd, nb);
}
static inline void rt_stswi(PPCContext *c, uint32_t ea, int rs, int nb) {
    if (nb > 0 && ea < RAM_LIMIT && (unsigned)nb <= RAM_LIMIT - ea &&
        (unsigned)nb <= RAM_SIZE - (ea & RAM_MASK)) {
        uint8_t *dst = g_ram + (ea & RAM_MASK);
        int n = 0, r = rs & 31;
        for (; n + 4 <= nb; n += 4, r = (r + 1) & 31) {
            uint32_t value = guest_be32(c->r[r]); memcpy(dst + n, &value, 4);
        }
        for (int i = 0; n + i < nb; i++) dst[n + i] = (uint8_t)(c->r[r] >> (24 - 8 * i));
        return;
    }
    rt_stswi_slow(c, ea, rs, nb);
}
#define VIPER_INLINE_STRING_HELPERS 1
#else
void rt_lswi(PPCContext *c, uint32_t ea, int rd, int nb);
void rt_stswi(PPCContext *c, uint32_t ea, int rs, int nb);
#endif
#if defined(VIPER_WII_NATIVE_FCTIW) && defined(VIPER_WII)
/* Broadway fctiw/fctiwz give the reference integer for every input: NaN ->
 * 0x80000000, the same saturation, truncation, and rounding in the host mode
 * the runtime mirrors from FPSCR[RN]. Only the upper word is undefined on the
 * 750, so rebuild it as the reference does. Guest FPSCR is not touched. */
static inline double rt_fctiw(PPCContext *c, double v, int trunc) {
    (void)c;
    double d;
    if (trunc) __asm__("fctiwz %0,%1" : "=f"(d) : "f"(v));
    else __asm__("fctiw %0,%1" : "=f"(d) : "f"(v));
    uint64_t b; memcpy(&b, &d, 8);
    return BITS_FPR(0xfff8000000000000ull | (uint32_t)b);
}
#else
double rt_fctiw(PPCContext *c, double v, int trunc);
#endif
void rt_mtfsf(PPCContext *c, uint32_t fm, uint32_t v);
void rt_fpscr_changed(PPCContext *c);

#ifdef __cplusplus
}
#endif
