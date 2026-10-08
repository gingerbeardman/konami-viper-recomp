/* Host proof for rsqrt_exact.h: every result must be bit-identical to
 * 1.0/sqrt(x) for any estimate quality. Run: make -f wii/Makefile? No:
 * cc -O2 -ffp-contract=off -frounding-math -Iwii wii/test_rsqrt_exact.c -lm */
#include <fenv.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
static int estimate_mode;
static double estimate(double x) {
    double y = 1.0 / sqrt(x);
    uint64_t b; memcpy(&b, &y, 8);
    switch (estimate_mode) {
    case 0: b &= ~((1ull << 47) - 1); break;          /* 5 bits */
    case 1: b &= ~((1ull << 40) - 1); break;          /* 12 bits */
    case 2: y *= 1.0 + 1.0 / 40; memcpy(&b, &y, 8); break; /* 2.5% high */
    case 3: y *= 1.0 - 1.0 / 40; memcpy(&b, &y, 8); break; /* 2.5% low */
    default: break;                                    /* exact */
    }
    memcpy(&y, &b, 8);
    return y;
}
static unsigned long long fallbacks;
#define WII_RSQRT_ESTIMATE(x) estimate(x)
#define WII_RSQRT_FALLBACK() (fallbacks++)
static unsigned long long div_fallbacks;
#define WII_RSQRT_DIV_FALLBACK() (div_fallbacks++)
int rt_round_nearest = 1;
#include "rsqrt_exact.h"
static uint64_t rng = 0x9e3779b97f4a7c15ull;
static uint64_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return rng; }
static unsigned long long checked, bad;
static void check(double x) {
    volatile double vx = x;
    double want = 1.0 / sqrt(vx), got = wii_rsqrt_exact(vx);
    uint64_t a, b; memcpy(&a, &want, 8); memcpy(&b, &got, 8);
    checked++;
    if (a != b && !(isnan(want) && isnan(got))) {
        if (bad++ < 10) printf("MISMATCH mode=%d x=%a want=%a got=%a\n", estimate_mode, x, want, got);
    }
}
static double from_bits(uint64_t b) { double d; memcpy(&d, &b, 8); return d; }
int main(int argc, char **argv) {
    long n = argc > 1 ? atol(argv[1]) : 4000000;
    for (estimate_mode = 0; estimate_mode < 5; estimate_mode++) {
        unsigned long long f0 = fallbacks, c0 = checked, d0 = div_fallbacks;
        for (long i = 0; i < n; i++) check(from_bits(next() & 0x7fffffffffffffffull));
        /* Near squares, where rounding is tightest. */
        for (long i = 0; i < n / 4; i++) {
            double s = from_bits((next() & 0x000fffffffffffffull) | ((uint64_t)(0x3ff + (int)(next() % 1000) - 500) << 52));
            double x = s * s;
            uint64_t xb; memcpy(&xb, &x, 8);
            for (int d = -3; d <= 3; d++) check(from_bits(xb + d));
        }
        /* Powers of two and neighbours, specials, denormals, negatives. */
        for (int e = 1; e < 0x7ff; e++)
            for (int d = -2; d <= 2; d++) check(from_bits(((uint64_t)e << 52) + d));
        const double sp[] = {0.0, -0.0, -1.0, INFINITY, -INFINITY, NAN, 0x1p-1074, 0x1p-1060, 0x1.fffffffffffffp-1023, 0x1p-1022, 0x1.fffffffffffffp+1023, 1.0, 4.0, 2.0};
        for (unsigned i = 0; i < sizeof sp / sizeof sp[0]; i++) check(sp[i]);
        /* Reciprocal near a rounding midpoint: s close to 1/(q + ulp(q)/2). */
        for (long i = 0; i < n / 4; i++) {
            double q = from_bits((next() & 0x000fffffffffffffull) | ((uint64_t)(0x3ff + (int)(next() % 600) - 300) << 52));
            uint64_t qb; memcpy(&qb, &q, 8);
            double ulp = from_bits((qb & 0x7ff0000000000000ull) - (52ull << 52));
            double s0 = 1.0 / q, s = s0 - s0 * s0 * (0.5 * ulp);
            uint64_t sb; memcpy(&sb, &s, 8);
            for (int d = -2; d <= 2; d++) {
                double sd = from_bits(sb + d), x = sd * sd;
                uint64_t xb; memcpy(&xb, &x, 8);
                for (int e = -2; e <= 2; e++) check(from_bits(xb + e));
            }
        }
        printf("estimate mode %d: checked %llu, fallbacks %llu (%.4f%%), divide fallbacks %llu (%.4f%%)\n", estimate_mode,
               checked - c0, fallbacks - f0, 100.0 * (fallbacks - f0) / (checked - c0),
               div_fallbacks - d0, 100.0 * (div_fallbacks - d0) / (checked - c0));
    }
    /* Directed rounding must take the reference path and still match. */
    rt_round_nearest = 0; fesetround(FE_UPWARD); estimate_mode = 1;
    for (long i = 0; i < n / 10; i++) check(from_bits(next() & 0x7fffffffffffffffull));
    fesetround(FE_TONEAREST); rt_round_nearest = 1;
    printf("%s: %llu checked, %llu mismatches\n", bad ? "FAIL" : "PASS", checked, bad);
    return bad != 0;
}
