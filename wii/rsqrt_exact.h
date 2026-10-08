/* Exact fast 1.0/sqrt(x): bit-identical to the recompiler's reference
 * lowering of guest frsqrte, without newlib's bit-by-bit software sqrt.
 *
 * Seed from a hardware estimate, refine with Newton steps, correct with one
 * Markstein fma step, then prove s == RN(sqrt(x)) with an fma residual:
 * |sqrt(x)-s| < ulp/2  <=>  |x-s*s| < (ulp/2)(sqrt(x)+s), and sqrt(x)+s is
 * 2s to within 2^-53 relative (ulp/4 when s is a power of two, where the
 * spacing below halves). A result inside a 2^-40 margin of that bound,
 * or any input that is not a positive normal in round-to-nearest, takes the
 * reference path.
 * The final reciprocal RN(1/s) is not divided either: y already approximates
 * 1/s, one fma Newton step gives q, and the exact residual 1-s*q proves
 * |1/s-q| < ulp(q)/2 (ulp/4 when q is a power of two), the same kind of
 * bound. 1/s can never be a rounding tie (s*m=1 would need two odd 53/54-bit
 * significands to multiply to a power of two), so the proof is sufficient.
 * Anything not proved uses the correctly rounded divide.
 * Estimate quality only changes how often the reference paths run. */
#ifndef VIPER_WII_RSQRT_EXACT_H
#define VIPER_WII_RSQRT_EXACT_H
#include <math.h>
#include <stdint.h>
#include <string.h>

#ifndef WII_RSQRT_ESTIMATE
static inline double wii_rsqrt_hw_estimate(double x) {
    double y;
    __asm__("frsqrte %0,%1" : "=f"(y) : "f"(x));
    return y;
}
#define WII_RSQRT_ESTIMATE(x) wii_rsqrt_hw_estimate(x)
#endif
#ifndef WII_RSQRT_FALLBACK
#define WII_RSQRT_FALLBACK() ((void)0)
#endif
#ifndef WII_RSQRT_DIV_FALLBACK
#define WII_RSQRT_DIV_FALLBACK() ((void)0)
#endif
#ifndef WII_RSQRT_STEPS
#define WII_RSQRT_STEPS 3
#endif

/* Set by the runtime whenever the guest rounding mode is applied to the host. */
extern int rt_round_nearest;

static inline double wii_bits_double(uint64_t b) { double d; memcpy(&d, &b, sizeof d); return d; }

static inline __attribute__((always_inline)) double wii_rsqrt_exact_inline(double x) {
    uint64_t xb;
    memcpy(&xb, &x, sizeof xb);
    uint32_t biased = (uint32_t)(xb >> 52);
    /* Biased exponent 1..0x7fe with a clear sign: positive, normal, finite. */
    if (__builtin_expect(biased - 1u >= 0x7feu || !rt_round_nearest, 0))
        return 1.0 / sqrt(x);
    /* x = m 2^E, m in [1,2): sqrt(x) lies in [p, 2p) and 1/sqrt(x) in
     * (ip/2, ip] for p = 2^floor(E/2), ip = 1/p. Built from the input's
     * exponent, off the dependency chain of the refinement below. */
    int32_t es = ((int32_t)biased - 1023) >> 1;
    double p = wii_bits_double((uint64_t)(uint32_t)(es + 1023) << 52);
    double ip = wii_bits_double((uint64_t)(uint32_t)(1023 - es) << 52);
    double y = WII_RSQRT_ESTIMATE(x), h = 0.5 * x;
#if WII_RSQRT_STEPS == 2
    y = __builtin_fma(y, __builtin_fma(-(h * y), y, 0.5), y);
    y = __builtin_fma(y, __builtin_fma(-(h * y), y, 0.5), y);
#else
    for (int i = 0; i < WII_RSQRT_STEPS; i++)
        y = __builtin_fma(y, __builtin_fma(-(h * y), y, 0.5), y);
#endif
    double s = x * y;
    s = __builtin_fma(__builtin_fma(-s, s, x), 0.5 * y, s);
    double r = __builtin_fma(-s, s, x);
    /* With s in [p, 2p], ulp(s) is p 2^-52 below 2p and 2p 2^-52 at 2p, and
     * s is a power of two exactly at p or 2p. Outside, the proof is not
     * attempted. Below a power of two the spacing halves: |error| < ulp/4. */
    if (__builtin_expect(s >= p && s <= 2.0 * p, 1)) {
        double ulp = (s < 2.0 * p ? p : 2.0 * p) * 0x1p-52;
        double limit = (s * ulp) * ((s != p && s != 2.0 * p) ? 1.0 - 0x1p-40 : 0.5);
        if (__builtin_expect(fabs(r) < limit, 1)) {
            double q = __builtin_fma(y, __builtin_fma(-s, y, 1.0), y);
            double e = __builtin_fma(-s, q, 1.0);
            /* 1/s lies in [ip/2, ip]; for q in that range ulp(q) is
             * (ip/2) 2^-52 below ip and ip 2^-52 at ip. */
            if (__builtin_expect(q >= 0.5 * ip && q <= ip, 1)) {
                double ulpq = (q < ip ? 0.5 * ip : ip) * 0x1p-52;
                double qlimit = (s * ulpq) * ((q != ip && q != 0.5 * ip) ? 0.5 * (1.0 - 0x1p-40) : 0.25);
                if (__builtin_expect(fabs(e) < qlimit, 1))
                    return q;
            }
            WII_RSQRT_DIV_FALLBACK();
            return 1.0 / s;
        }
    }
    WII_RSQRT_FALLBACK();
    return 1.0 / sqrt(x);
}
/* Callers get GCC's inlining choice; hot callers may use the forced form. */
static inline double wii_rsqrt_exact(double x) { return wii_rsqrt_exact_inline(x); }
#endif
