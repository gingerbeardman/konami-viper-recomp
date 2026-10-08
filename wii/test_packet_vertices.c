/* Differential proof for VIPER_WII_PACKET_VERTICES: triangle_packet (vertices
 * decoded once, strip triangles passed in place) against the original
 * triangle_packet_copying, over random packet sequences: every vertex format,
 * packed and float colour, list/strip/continuation codes, fans and strips,
 * and continuations across packets. Compared: every renderer callback's
 * vertices (bit patterns), strip count and command, then the carried strip
 * state, strip count and counters.
 * Run: cc -O1 -std=gnu11 -w -Iwii -Iruntime -DVIPER_WII_PACKET_VERTICES \
 *        -DVIPER_WII_FIFO_FORMAT59 wii/test_packet_vertices.c -o /tmp/tpv && /tmp/tpv */
#include "voodoo_headless.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#ifndef VIPER_WII_PACKET_VERTICES
#error build with -DVIPER_WII_PACKET_VERTICES
#endif
void rt_log(const char *f, ...) { (void)f; }
void rt_fatal(const char *m) { printf("fatal %s\n", m); exit(2); }
void rt_sched_at(uint64_t t, void (*fn)(void *), void *u) { (void)t; (void)fn; (void)u; }
void rt_sched_cancel(void (*fn)(void *), void *u) { (void)fn; (void)u; }
uint64_t rt_now(void) { return 0; }
void epic_raise(int irq) { (void)irq; }
void rt_eat_cycles(uint32_t n) { (void)n; }

typedef struct { uint32_t bits[3][14]; unsigned strip_count; uint32_t cmd; } Call;
static Call calls[2][64];
static unsigned ncalls[2], side;
static void record(void *user, const WiiVoodooView *v, const WiiVoodooVertex p[3], uint32_t cmd) {
    (void)user;
    Call *c = &calls[side][ncalls[side]++];
    assert(ncalls[side] <= 64);
    for (unsigned i = 0; i < 3; i++) memcpy(c->bits[i], &p[i], sizeof c->bits[i]);
    c->strip_count = v->strip_count;
    c->cmd = cmd;
}
static uint64_t rng = 0x243f6a8885a308d3ull;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }

int main(void) {
    vram = calloc(1, WII_VOODOO_VRAM_BYTES);
    WiiVoodooRenderer r = {record, NULL, NULL};
    renderer = r;
    static const unsigned formats[] = {59, 0, 1, 2, 3, 8, 9, 11, 35, 43, 59, 63, 255, 128, 32, 64, 16, 4};
    NativeVertex saved_strip[3]; unsigned saved_count;
    unsigned long long packets = 0, triangles = 0;
    for (int seq = 0; seq < 20000; seq++) {
        memset(strip, 0, sizeof strip); strip_count = next() % 4;
        for (unsigned i = 0; i < 3; i++) for (unsigned j = 0; j < 14; j++) {
            uint32_t b = next(); memcpy((uint32_t *)&strip[i] + j, &b, 4);
        }
        int npackets = 1 + next() % 6;
        for (int k = 0; k < npackets; k++) {
            unsigned format = formats[next() % (sizeof formats / sizeof formats[0])];
            unsigned code = next() % 3, vertices = next() % 16;
            uint32_t cmd = 3u | (code << 3) | (vertices << 6) | (format << 10);
            if (next() & 1) cmd |= 1u << 22;          /* fan */
            if (next() % 4 == 0) cmd |= 1u << 28;     /* packed colour */
            if (format == 59 && (cmd & (1u << 28)) && (next() & 1)) cmd &= ~(1u << 28);
            unsigned pc = (next() % 4096) * 4;
            for (unsigned w = 0; w < 1 + 15 * 20; w++) vram_write(pc / 4 + w, next());
            memcpy(saved_strip, strip, sizeof strip); saved_count = strip_count;
            uint64_t before = counters.triangles;
            side = 0; ncalls[0] = 0; triangle_packet_copying(pc, cmd);
            NativeVertex ref_strip[3]; memcpy(ref_strip, strip, sizeof strip);
            unsigned ref_count = strip_count; uint64_t ref_tris = counters.triangles - before;
            memcpy(strip, saved_strip, sizeof strip); strip_count = saved_count;
            before = counters.triangles;
            side = 1; ncalls[1] = 0; triangle_packet(pc, cmd);
            if (ncalls[0] != ncalls[1] || memcmp(calls[0], calls[1], ncalls[0] * sizeof(Call)) ||
                memcmp(ref_strip, strip, sizeof strip) || ref_count != strip_count ||
                ref_tris != counters.triangles - before) {
                printf("MISMATCH seq %d packet %d cmd=%08x calls %u/%u count %u/%u\n",
                       seq, k, cmd, ncalls[0], ncalls[1], ref_count, strip_count);
                return 1;
            }
            packets++; triangles += ncalls[0];
        }
    }
    printf("PASS: %llu packets, %llu triangles identical\n", packets, triangles);
    return 0;
}
