/* Store-forwarding probe for f_gl_00028fc4's FIFO words: a float stored to
 * guest RAM and then reloaded as a byte-reversed word (stfs + lwbrx), as the
 * generated code does, against forwarding the bits through a stack slot
 * (Broadway has no FPR -> GPR move, so that is the only forward), against
 * the store alone (the bound for any forwarding). Timed with the time base;
 * sent to report=HOST:PORT as boot.log (wii/net_report.c), then back to the
 * loader. Run: python3 wii/hw_remote.py --wii IP build/wii/probe/lhs_probe.dol OUT */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "net_report.h"

#define N 1000000u
static float ram[64] __attribute__((aligned(32)));   /* stands in for guest RAM */

/* Each variant: 10 words per iteration, like one vertex's FIFO words. */
static uint32_t variant_a(float f) {   /* current: store, reload byte-reversed */
    uint32_t sum = 0;
    for (unsigned i = 0; i < N; i++) {
        float v = f + (float)i;
        for (unsigned k = 0; k < 10; k++) {
            uint32_t w;
            __asm__ volatile("stfs %1,0(%2)\n\tlwbrx %0,0,%2" : "=r"(w) : "f"(v), "b"(&ram[k]) : "memory");
            sum += w;
        }
    }
    return sum;
}
static uint32_t variant_b(float f) {   /* forwarded: store, plus bits through a stack slot */
    uint32_t sum = 0;
    for (unsigned i = 0; i < N; i++) {
        float v = f + (float)i;
        for (unsigned k = 0; k < 10; k++) {
            float tmp;uint32_t w;
            __asm__ volatile("stfs %2,0(%3)\n\tstfs %2,0(%1)\n\tlwz %0,0(%1)"
                             : "=r"(w) : "b"(&tmp), "f"(v), "b"(&ram[k]) : "memory");
            sum += __builtin_bswap32(w);
        }
    }
    return sum;
}
static uint32_t variant_c(float f) {   /* bound: the store alone */
    uint32_t sum = 0;
    for (unsigned i = 0; i < N; i++) {
        float v = f + (float)i;
        for (unsigned k = 0; k < 10; k++) {
            __asm__ volatile("stfs %0,0(%1)" : : "f"(v), "b"(&ram[k]) : "memory");
            sum += k;
        }
    }
    return sum;
}

int main(int argc, char **argv) {
    wii_net_report_args(argc, argv);
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_ClearFrameBuffer(mode, fb, COLOR_BLACK);
    console_init(fb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode); VIDEO_SetNextFramebuffer(fb); VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    static char text[1024];
    int n = 0;
    uint32_t (*const fn[3])(float) = {variant_a, variant_b, variant_c};
    const char *const name[3] = {"A stfs+lwbrx (current)", "B stfs+stack forward", "C store only (bound)"};
    for (int round = 0; round < 2; round++)
        for (int v = 0; v < 3; v++) {
            uint64_t t0 = gettime();
            uint32_t s = fn[v](1.5f);
            uint64_t us = ticks_to_microsecs(gettime() - t0);
            n += snprintf(text + n, sizeof text - n, "VIPER LHS %s round=%d us=%llu ns_per_word=%.2f sum=%08lx\n",
                          name[v], round, (unsigned long long)us, us * 1000.0 / (N * 10.0), (unsigned long)s);
        }
    n += snprintf(text + n, sizeof text - n, "SCRIPTED END\n");
    printf("%s", text);
    wii_net_report_text("boot.log", text);
    exit(0);
}
