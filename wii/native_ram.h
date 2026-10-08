/* Guest RAM accessors for the native gl transcriptions (wii/native_gl_*.c).
 * Same results as runtime/ppc_rt.h LD32/ST32/LDF32/STF32/LD32LE/ST32LE:
 * an access wholly below RAM_SIZE is the same memcpy at ea & RAM_MASK == ea;
 * everything else (mirror, last word, device space) takes the original
 * accessor. The base is a local copy of g_ram taken by NATIVE_RAM_BEGIN:
 * g_ram never changes after boot, and the copy's address is never taken, so
 * guest stores cannot alias it and it is not reloaded after each store. */
#ifndef WII_NATIVE_RAM_H
#define WII_NATIVE_RAM_H
#include "ppc_rt.h"

#if !defined(VIPER_MEMORY_AUDIT)
#if defined(VIPER_WII_SPLIT_RAM_HELPERS) && defined(VIPER_WII_RAM_SLOW_CALL)
/* Everything but a plain in-RAM access, out of line (wii/ram_access_cold.c):
 * each is exactly the original accessor, so every call site stays small.
 * Text 9.13 -> 7.04 MB, Dolphin +0.36% (it does not model caches). */
uint32_t wii_ram_slow_ld8(uint32_t ea) __attribute__((cold,noinline));
uint32_t wii_ram_slow_ld16(uint32_t ea) __attribute__((cold,noinline));
uint32_t wii_ram_slow_ld32(uint32_t ea) __attribute__((cold,noinline));
void wii_ram_slow_st8(uint32_t ea,uint32_t v) __attribute__((cold,noinline));
void wii_ram_slow_st16(uint32_t ea,uint32_t v) __attribute__((cold,noinline));
void wii_ram_slow_st32(uint32_t ea,uint32_t v) __attribute__((cold,noinline));
#define NATIVE_SLOW_LD8 wii_ram_slow_ld8
#define NATIVE_SLOW_LD16 wii_ram_slow_ld16
#define NATIVE_SLOW_LD32 wii_ram_slow_ld32
#define NATIVE_SLOW_ST8 wii_ram_slow_st8
#define NATIVE_SLOW_ST16 wii_ram_slow_st16
#define NATIVE_SLOW_ST32 wii_ram_slow_st32
#ifdef VIPER_WII_PRESERVE_SLOW_ALL
#include "preserve_call.h"
#undef NATIVE_SLOW_LD8
#undef NATIVE_SLOW_LD16
#undef NATIVE_SLOW_LD32
#undef NATIVE_SLOW_ST8
#undef NATIVE_SLOW_ST16
#undef NATIVE_SLOW_ST32
#define NATIVE_SLOW_LD8(ea) wii_pcall_u_u(wii_ram_slow_ld8, (ea))
#define NATIVE_SLOW_LD16(ea) wii_pcall_u_u(wii_ram_slow_ld16, (ea))
#define NATIVE_SLOW_LD32(ea) wii_pcall_u_u(wii_ram_slow_ld32, (ea))
#define NATIVE_SLOW_ST8(ea, v) wii_pcall_v_uu(wii_ram_slow_st8, (ea), (v))
#define NATIVE_SLOW_ST16(ea, v) wii_pcall_v_uu(wii_ram_slow_st16, (ea), (v))
#define NATIVE_SLOW_ST32(ea, v) wii_pcall_v_uu(wii_ram_slow_st32, (ea), (v))
#endif
#else
#define NATIVE_SLOW_LD8 LD8
#define NATIVE_SLOW_LD16 LD16
#define NATIVE_SLOW_LD32 LD32
#define NATIVE_SLOW_ST8 ST8
#define NATIVE_SLOW_ST16 ST16
#define NATIVE_SLOW_ST32 ST32
#endif
/* In-RAM tests. With VIPER_WII_RAM_MASK_TEST an access takes the fast path
 * when it is below RAM_SIZE *and* naturally aligned: one rlwinm. with a
 * wrap-around mask instead of building RAM_SIZE-n and comparing. Unaligned
 * in-RAM accesses take the original accessor (same result). g_ram is at
 * least 8-byte aligned (checked at boot), so aligned ea means aligned host. */
#ifdef VIPER_WII_RAM_MASK_TEST
#define NATIVE_IN_RAM(ea, n) (!((ea) & (0xff000000u | ((n) - 1u))))
#else
#define NATIVE_IN_RAM(ea, n) ((ea) <= RAM_SIZE - (n))
#endif
static inline uint32_t native_ld32(uint8_t *ram, uint32_t ea) {
    if (LIKELY(NATIVE_IN_RAM(ea, 4))) { uint32_t v; memcpy(&v, ram + ea, 4); return guest_be32(v); }
    return NATIVE_SLOW_LD32(ea);
}
static inline void native_st32(uint8_t *ram, uint32_t ea, uint32_t v) {
    if (LIKELY(NATIVE_IN_RAM(ea, 4))) { v = guest_be32(v); memcpy(ram + ea, &v, 4); return; }
    NATIVE_SLOW_ST32(ea, v);
}
#if defined(VIPER_WII_DIRECT_FP) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* Big-endian host: guest RAM bytes are already host order, so an aligned
 * in-RAM float or double is loaded/stored straight into/from an FPR (lfs,
 * stfs, lfd, stfd): the same bits as the integer path, without its
 * FPR<->GPR trip through a stack slot (a load-hit-store stall on Broadway). */
#define NATIVE_FP_ALIGNED(p, n) (!((uintptr_t)(p) & ((n) - 1)))
#ifdef VIPER_WII_RAM_MASK_TEST
#define NATIVE_FP_IN_RAM(ram, ea, n) NATIVE_IN_RAM(ea, n)
#else
#define NATIVE_FP_IN_RAM(ram, ea, n) ((ea) <= RAM_SIZE - (n) && NATIVE_FP_ALIGNED((ram) + (ea), n))
#endif
#if defined(VIPER_WII_FP_SLOW_CALL) && defined(VIPER_WII_SPLIT_RAM_HELPERS) && defined(VIPER_WII_RAM_SLOW_CALL)
/* Both paths stay in FPRs. With the slow path building the float from an
 * integer inline, GCC merged the two through one stack slot and turned the
 * fast path into lwzx + stw + lfs (a load-hit-store). Typed accesses and
 * out-of-line slow paths (wii/ram_access_cold.c: exactly LDF32/STF32/
 * LDF64/STF64, the original accessors) keep lfs/stfs/lfd/stfd. */
double wii_ram_slow_ldf32(uint32_t ea) __attribute__((cold,noinline));
void wii_ram_slow_stf32(uint32_t ea, double d) __attribute__((cold,noinline));
double wii_ram_slow_ldf64(uint32_t ea) __attribute__((cold,noinline));
void wii_ram_slow_stf64(uint32_t ea, double d) __attribute__((cold,noinline));
#ifdef VIPER_WII_PRESERVE_SLOW_ALL
#define WII_SLOW_LDF32(ea) wii_pcall_d_u(wii_ram_slow_ldf32, (ea))
#define WII_SLOW_STF32(ea, d) wii_pcall_v_ud(wii_ram_slow_stf32, (ea), (d))
#define WII_SLOW_LDF64(ea) wii_pcall_d_u(wii_ram_slow_ldf64, (ea))
#define WII_SLOW_STF64(ea, d) wii_pcall_v_ud(wii_ram_slow_stf64, (ea), (d))
#else
#define WII_SLOW_LDF32 wii_ram_slow_ldf32
#define WII_SLOW_STF32 wii_ram_slow_stf32
#define WII_SLOW_LDF64 wii_ram_slow_ldf64
#define WII_SLOW_STF64 wii_ram_slow_stf64
#endif
typedef float __attribute__((may_alias)) native_aliased_f32;
typedef double __attribute__((may_alias)) native_aliased_f64;
/* Bisect switches (hardware RAM divergence 2026-10-08): VIPER_WII_FP_SLOW_PARTS
 * bit 0 ldf32, bit 1 stf32, bit 2 ldf64/stf64; default all. */
#ifndef VIPER_WII_FP_SLOW_PARTS
#define VIPER_WII_FP_SLOW_PARTS 7
#endif
static inline double native_ldf32(uint8_t *ram, uint32_t ea) {
#if VIPER_WII_FP_SLOW_PARTS & 1
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) return (double)*(const native_aliased_f32 *)(void *)(ram + ea);
    return WII_SLOW_LDF32(ea);
#else
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) {
        float f; memcpy(&f, __builtin_assume_aligned(ram + ea, 4), 4); return (double)f;
    }
    uint32_t b = native_ld32(ram, ea); float f; memcpy(&f, &b, 4); return (double)f;
#endif
}
static inline double native_ldf32_ram(uint8_t *ram, uint32_t ea) { return native_ldf32(ram, ea); }
static inline void native_stf32(uint8_t *ram, uint32_t ea, double d) {
#if VIPER_WII_FP_SLOW_PARTS & 2
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) { *(native_aliased_f32 *)(void *)(ram + ea) = (float)d; return; }
    WII_SLOW_STF32(ea, d);
#else
    float f = (float)d;
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) {
        memcpy(__builtin_assume_aligned(ram + ea, 4), &f, 4); return;
    }
    uint32_t b; memcpy(&b, &f, 4); native_st32(ram, ea, b);
#endif
}
static inline double native_ldf64(uint8_t *ram, uint32_t ea) {
#if VIPER_WII_FP_SLOW_PARTS & 4
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) return *(const native_aliased_f64 *)(void *)(ram + ea);
    return WII_SLOW_LDF64(ea);
#else
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) {
        double d; memcpy(&d, __builtin_assume_aligned(ram + ea, 8), 8); return d;
    }
    return LDF64(ea);
#endif
}
static inline void native_stf64(uint8_t *ram, uint32_t ea, double d) {
#if VIPER_WII_FP_SLOW_PARTS & 4
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) { *(native_aliased_f64 *)(void *)(ram + ea) = d; return; }
    WII_SLOW_STF64(ea, d);
#else
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) {
        memcpy(__builtin_assume_aligned(ram + ea, 8), &d, 8); return;
    }
    STF64(ea, d);
#endif
}
#else
static inline double native_ldf32(uint8_t *ram, uint32_t ea) {
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) {
        float f; memcpy(&f, __builtin_assume_aligned(ram + ea, 4), 4); return (double)f;
    }
    uint32_t b = native_ld32(ram, ea); float f; memcpy(&f, &b, 4); return (double)f;
}
static inline double native_ldf32_ram(uint8_t *ram, uint32_t ea) { return native_ldf32(ram, ea); }
static inline void native_stf32(uint8_t *ram, uint32_t ea, double d) {
    float f = (float)d;
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 4))) {
        memcpy(__builtin_assume_aligned(ram + ea, 4), &f, 4); return;
    }
    uint32_t b; memcpy(&b, &f, 4); native_st32(ram, ea, b);
}
static inline double native_ldf64(uint8_t *ram, uint32_t ea) {
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) {
        double d; memcpy(&d, __builtin_assume_aligned(ram + ea, 8), 8); return d;
    }
    return LDF64(ea);
}
static inline void native_stf64(uint8_t *ram, uint32_t ea, double d) {
    if (LIKELY(NATIVE_FP_IN_RAM(ram, ea, 8))) {
        memcpy(__builtin_assume_aligned(ram + ea, 8), &d, 8); return;
    }
    STF64(ea, d);
}
#endif /* VIPER_WII_FP_SLOW_CALL */
#else
static inline double native_ldf32(uint8_t *ram, uint32_t ea) {
    uint32_t b = native_ld32(ram, ea); float f; memcpy(&f, &b, 4); return (double)f;
}
/* The in-RAM case only: the caller has established ea <= RAM_SIZE - 4. */
static inline double native_ldf32_ram(uint8_t *ram, uint32_t ea) {
    uint32_t v; memcpy(&v, ram + ea, 4); v = guest_be32(v); float f; memcpy(&f, &v, 4); return (double)f;
}
static inline void native_stf32(uint8_t *ram, uint32_t ea, double d) {
    float f = (float)d; uint32_t b; memcpy(&b, &f, 4); native_st32(ram, ea, b);
}
static inline double native_ldf64(uint8_t *ram, uint32_t ea) { (void)ram; return LDF64(ea); }
static inline void native_stf64(uint8_t *ram, uint32_t ea, double d) { (void)ram; STF64(ea, d); }
#endif
#define NATIVE_RAM_BEGIN() uint8_t *const native_ram_base = g_ram
#define NLD32(ea) native_ld32(native_ram_base, (ea))
#define NST32(ea, v) native_st32(native_ram_base, (ea), (v))
#define NLD32LE(ea) bswap32(native_ld32(native_ram_base, (ea)))
#define NST32LE(ea, v) native_st32(native_ram_base, (ea), bswap32(v))
#define NLDF32(ea) native_ldf32(native_ram_base, (ea))
#define NSTF32(ea, d) native_stf32(native_ram_base, (ea), (d))
static inline uint32_t native_ld16(uint8_t *ram, uint32_t ea) {
    if (LIKELY(NATIVE_IN_RAM(ea, 2))) { uint16_t v; memcpy(&v, ram + ea, 2); return guest_be16(v); }
    return NATIVE_SLOW_LD16(ea);
}
static inline void native_st16(uint8_t *ram, uint32_t ea, uint32_t v) {
    if (LIKELY(NATIVE_IN_RAM(ea, 2))) { uint16_t s = guest_be16((uint16_t)v); memcpy(ram + ea, &s, 2); return; }
    NATIVE_SLOW_ST16(ea, v);
}
static inline uint32_t native_ld8(uint8_t *ram, uint32_t ea) {
    if (LIKELY(NATIVE_IN_RAM(ea, 1))) return ram[ea];
    return NATIVE_SLOW_LD8(ea);
}
static inline void native_st8(uint8_t *ram, uint32_t ea, uint32_t v) {
    if (LIKELY(NATIVE_IN_RAM(ea, 1))) { ram[ea] = (uint8_t)v; return; }
    NATIVE_SLOW_ST8(ea, v);
}
#else
#define NATIVE_RAM_BEGIN() ((void)0)
#define NLD32(ea) LD32(ea)
#define NST32(ea, v) ST32((ea), (v))
#define NLD32LE(ea) LD32LE(ea)
#define NST32LE(ea, v) ST32LE((ea), (v))
#define NLDF32(ea) LDF32(ea)
#define NSTF32(ea, d) STF32((ea), (d))
#endif

/* Cache touch of guest RAM at ea (a hint: no architectural effect). */
#if defined(VIPER_WII_CACHE_HINTS) && !defined(VIPER_MEMORY_AUDIT)
#ifdef VIPER_WII_RAM_BASE_LOCAL
/* Prefetch distance in the 0x28fc4 strip loops (32-byte vertex records). */
#ifndef VIPER_WII_STRIP_PREFETCH
#define VIPER_WII_STRIP_PREFETCH 0x40u
#endif
#if defined(VIPER_WII_DIRECT_MASK) || defined(VIPER_WII_PRESERVE_SLOW)
/* The same hint behind a one-instruction test (no RAM_SIZE constant). */
#define RAM_PREFETCH(ea) do { uint32_t pf_ = (ea); if (!(pf_ & 0xff000000u)) __builtin_prefetch(native_ram_base + pf_); } while (0)
#define RAM_PREFETCH_W(ea) do { uint32_t pf_ = (ea); if (!(pf_ & 0xff000000u)) __builtin_prefetch(native_ram_base + pf_, 1); } while (0)
#else
#define RAM_PREFETCH(ea) do { uint32_t pf_ = (ea); if (pf_ < RAM_SIZE) __builtin_prefetch(native_ram_base + pf_); } while (0)
#define RAM_PREFETCH_W(ea) do { uint32_t pf_ = (ea); if (pf_ < RAM_SIZE) __builtin_prefetch(native_ram_base + pf_, 1); } while (0)
#endif
#else
#define RAM_PREFETCH(ea) do { uint32_t pf_ = (ea); if (pf_ < RAM_SIZE) __builtin_prefetch(g_ram + pf_); } while (0)
#define RAM_PREFETCH_W(ea) do { uint32_t pf_ = (ea); if (pf_ < RAM_SIZE) __builtin_prefetch(g_ram + pf_, 1); } while (0)
#endif
#else
#define RAM_PREFETCH(ea) ((void)0)
#define RAM_PREFETCH_W(ea) ((void)0)
#endif

/* Generated code (wii/specialize_gpr_multiple.py ram_base pass): RAM_BEGIN()
 * opens every function body and the R* accessors replace the originals.
 * Without VIPER_WII_RAM_BASE_LOCAL they are exactly the originals. */
#if defined(VIPER_WII_RAM_BASE_LOCAL) && !defined(VIPER_MEMORY_AUDIT)
#define RAM_BEGIN() uint8_t *const native_ram_base __attribute__((unused)) = g_ram
#define RLD8(ea) native_ld8(native_ram_base, (ea))
#define RLD16(ea) native_ld16(native_ram_base, (ea))
#define RLD32(ea) native_ld32(native_ram_base, (ea))
#define RLD32LE(ea) bswap32(native_ld32(native_ram_base, (ea)))
#define RLDF32(ea) native_ldf32(native_ram_base, (ea))
#define RST8(ea, v) native_st8(native_ram_base, (ea), (v))
#define RST16(ea, v) native_st16(native_ram_base, (ea), (v))
#define RST32(ea, v) native_st32(native_ram_base, (ea), (v))
#define RST32LE(ea, v) native_st32(native_ram_base, (ea), bswap32(v))
#define RSTF32(ea, d) native_stf32(native_ram_base, (ea), (d))
#define RLDF64(ea) native_ldf64(native_ram_base, (ea))
#define RSTF64(ea, d) native_stf64(native_ram_base, (ea), (d))
#else
#define RAM_BEGIN() ((void)0)
#define RLD8 LD8
#define RLD16 LD16
#define RLD32 LD32
#define RLD32LE LD32LE
#define RLDF32 LDF32
#define RST8 ST8
#define RST16 ST16
#define RST32 ST32
#define RST32LE ST32LE
#define RSTF32 STF32
#define RLDF64 LDF64
#define RSTF64 STF64
#endif
/* Pending cycle charges (wii/specialize_gpr_multiple.py budget_local pass):
 * a generated function adds its block charges to the local budget_pending
 * and writes them to c->budget before anything that can observe the budget
 * (checkpoints, calls, returns, device accesses). The R* accessors write it
 * back only on their device path: an access that is not plainly in RAM. */
#if defined(VIPER_WII_BUDGET_LOCAL) && defined(VIPER_WII_RAM_BASE_LOCAL) && !defined(VIPER_MEMORY_AUDIT)
#define BUDGET_FLUSH() do { c->budget -= budget_pending; budget_pending = 0; } while (0)
/* One in-RAM test per access: the plain RAM case is the accessor's own fast
 * path; anything else writes the pending charges back, then takes the
 * original accessor (which repeats its own test on this cold path only). */
#define BUDGET_GUARD(ea, n, fast, slow) ({ uint32_t bl_ea_ = (ea); \
    __typeof__(fast) bl_v_; \
    if (LIKELY(NATIVE_IN_RAM(bl_ea_, n))) bl_v_ = (fast); else { BUDGET_FLUSH(); bl_v_ = (slow); } bl_v_; })
#define BUDGET_GUARD_ST(ea, n, fast, slow) do { uint32_t bl_ea_ = (ea); \
    if (LIKELY(NATIVE_IN_RAM(bl_ea_, n))) { fast; } else { BUDGET_FLUSH(); slow; } } while (0)
#undef RLD8
#undef RLD16
#undef RLD32
#undef RLD32LE
#undef RLDF32
#undef RST8
#undef RST16
#undef RST32
#undef RST32LE
#undef RSTF32
#undef RLDF64
#undef RSTF64
static inline uint32_t bl_ld32(const uint8_t *ram, uint32_t ea) { uint32_t v; memcpy(&v, ram + ea, 4); return guest_be32(v); }
static inline uint32_t bl_ld16(const uint8_t *ram, uint32_t ea) { uint16_t v; memcpy(&v, ram + ea, 2); return guest_be16(v); }
static inline void bl_st32(uint8_t *ram, uint32_t ea, uint32_t v) { v = guest_be32(v); memcpy(ram + ea, &v, 4); }
static inline void bl_st16(uint8_t *ram, uint32_t ea, uint32_t v) { uint16_t h = guest_be16((uint16_t)v); memcpy(ram + ea, &h, 2); }
#define RLD8(ea) BUDGET_GUARD(ea, 1, (uint32_t)native_ram_base[bl_ea_], native_ld8(native_ram_base, bl_ea_))
#define RLD16(ea) BUDGET_GUARD(ea, 2, bl_ld16(native_ram_base, bl_ea_), native_ld16(native_ram_base, bl_ea_))
#define RLD32(ea) BUDGET_GUARD(ea, 4, bl_ld32(native_ram_base, bl_ea_), native_ld32(native_ram_base, bl_ea_))
#define RLD32LE(ea) BUDGET_GUARD(ea, 4, bswap32(bl_ld32(native_ram_base, bl_ea_)), bswap32(native_ld32(native_ram_base, bl_ea_)))
#if defined(VIPER_WII_DIRECT_FP) && defined(VIPER_WII_RAM_MASK_TEST) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
/* The mask test proves natural alignment: straight lfs/lfd/stfs/stfd. */
static inline double bl_ldf32(const uint8_t *ram, uint32_t ea) { float f; memcpy(&f, __builtin_assume_aligned(ram + ea, 4), 4); return (double)f; }
static inline double bl_ldf64(const uint8_t *ram, uint32_t ea) { double d; memcpy(&d, __builtin_assume_aligned(ram + ea, 8), 8); return d; }
static inline void bl_stf32(uint8_t *ram, uint32_t ea, double d) { float f = (float)d; memcpy(__builtin_assume_aligned(ram + ea, 4), &f, 4); }
static inline void bl_stf64(uint8_t *ram, uint32_t ea, double d) { memcpy(__builtin_assume_aligned(ram + ea, 8), &d, 8); }
#else
#define bl_ldf32 native_ldf32
#define bl_ldf64 native_ldf64
#define bl_stf32 native_stf32
#define bl_stf64 native_stf64
#endif
#define RLDF32(ea) BUDGET_GUARD(ea, 4, bl_ldf32(native_ram_base, bl_ea_), native_ldf32(native_ram_base, bl_ea_))
#define RLDF64(ea) BUDGET_GUARD(ea, 8, bl_ldf64(native_ram_base, bl_ea_), native_ldf64(native_ram_base, bl_ea_))
#define RST8(ea, v) BUDGET_GUARD_ST(ea, 1, native_ram_base[bl_ea_] = (uint8_t)(v), native_st8(native_ram_base, bl_ea_, (v)))
#define RST16(ea, v) BUDGET_GUARD_ST(ea, 2, bl_st16(native_ram_base, bl_ea_, (v)), native_st16(native_ram_base, bl_ea_, (v)))
#define RST32(ea, v) BUDGET_GUARD_ST(ea, 4, bl_st32(native_ram_base, bl_ea_, (v)), native_st32(native_ram_base, bl_ea_, (v)))
#define RST32LE(ea, v) BUDGET_GUARD_ST(ea, 4, bl_st32(native_ram_base, bl_ea_, bswap32(v)), native_st32(native_ram_base, bl_ea_, bswap32(v)))
#define RSTF32(ea, d) BUDGET_GUARD_ST(ea, 4, bl_stf32(native_ram_base, bl_ea_, (d)), native_stf32(native_ram_base, bl_ea_, (d)))
#define RSTF64(ea, d) BUDGET_GUARD_ST(ea, 8, bl_stf64(native_ram_base, bl_ea_, (d)), native_stf64(native_ram_base, bl_ea_, (d)))
#endif
#define WII_HOT_GUEST_PROTOTYPES
#include "hot_layout.h"
#endif
