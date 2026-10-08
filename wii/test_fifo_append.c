/* Differential proof for wii_voodoo_fifo_append_be: random header-first
 * packet streams written once as scalar full-word voodoo_lfb_write stores
 * and once with each packet's payload in random chunks through the append
 * path (scalar stores for whatever it declines). Compared after every
 * stream: FIFO VRAM bytes, page versions, presence bits, AGP registers,
 * header cache, strip state, counters and every renderer callback.
 * Run: cc -O1 -std=gnu11 -w -Iwii -Iruntime -DVIPER_WII_BULK_WRITER -DVIPER_WII_FIFO_FRONTIER_WRITE \
 *        -DVIPER_WII_FIFO_FORMAT59 wii/test_fifo_append.c -o /tmp/tfa && /tmp/tfa */
#include "voodoo_headless.c"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
void rt_log(const char *f, ...) { (void)f; }
void rt_fatal(const char *m) { printf("fatal %s\n", m); exit(2); }
void rt_sched_at(uint64_t t, SchedCb cb, void *u) { (void)t; (void)cb; (void)u; }
void rt_sched_cancel(SchedCb cb, void *u) { (void)cb; (void)u; }
uint64_t rt_now(void) { return 0; }
void rt_eat_cycles(uint32_t n) { (void)n; }
void epic_raise(int irq) { (void)irq; }

typedef struct { uint32_t bits[3][14]; unsigned strip_count; uint32_t cmd; } Call;
static Call calls[2][4096];
static unsigned ncalls[2], side;
static void record(void *user, const WiiVoodooView *v, const WiiVoodooVertex p[3], uint32_t cmd) {
    (void)user;
    assert(ncalls[side] < 4096);
    Call *c = &calls[side][ncalls[side]++];
    for (unsigned i = 0; i < 3; i++) memcpy(c->bits[i], &p[i], sizeof c->bits[i]);
    c->strip_count = v->strip_count; c->cmd = cmd;
}
static uint64_t rng;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)rng; }
enum { BASE = 0x1000, PAGES = 64 };
typedef struct {
    uint8_t vram[PAGES * 4096]; uint32_t versions[2048]; uint8_t present[sizeof fifo_present];
    uint32_t agp[128]; int hdr_valid; unsigned hdr_pc, hdr_count, hdr_checked; uint32_t hdr_cmd;
    NativeVertex strip[3]; unsigned strip_count; WiiVoodooStats stats;
} State;
static State snap[2];
static void capture(State *s) {
    memcpy(s->vram, vram + BASE, sizeof s->vram); memcpy(s->versions, vram_versions, sizeof s->versions);
    memcpy(s->present, fifo_present, sizeof s->present); memcpy(s->agp, agp, sizeof s->agp);
    s->hdr_valid = fifo_hdr_valid; s->hdr_pc = fifo_hdr_pc; s->hdr_count = fifo_hdr_count;
    s->hdr_checked = fifo_hdr_checked; s->hdr_cmd = fifo_hdr_cmd;
    memcpy(s->strip, strip, sizeof strip); s->strip_count = strip_count; s->stats = wii_voodoo_stats();
}
static void reset(void) {
    voodoo_init();
    WiiVoodooRenderer r = {record, NULL, NULL}; wii_voodoo_set_renderer(&r, NULL);
    voodoo_reg_write(0x80000 + 8 * 4, BASE >> 12, ~0u);
    voodoo_reg_write(0x80000 + 9 * 4, 256 | (PAGES - 2), ~0u);
    agp[11] = BASE;
}
static unsigned long long appended, declined;
static void run(int use_append, uint64_t seed) {
    rng = seed; reset(); side = use_append; ncalls[side] = 0;
    unsigned off = BASE;
    int npackets = 1 + next() % 12;
    for (int k = 0; k < npackets; k++) {
        static const unsigned formats[] = {59, 59, 59, 0, 3, 11, 35};
        unsigned format = formats[next() % 7], code = next() % 3, nv = next() % 16;
        uint32_t cmd = 3u | (code << 3) | (nv << 6) | (format << 10) | ((next() & 1) << 22);
        unsigned count; fifo_describe(off, cmd, &count);
        if (off + count * 4 > BASE + (PAGES - 2) * 4096) break;
        uint32_t payload[1 + 15 * 20];
        for (unsigned i = 1; i < count; i++) { float f = (float)((int)(next() % 2000) - 1000) / 7.0f; memcpy(&payload[i], &f, 4); }
        int header_last = next() % 8 == 0;
        if (!header_last) voodoo_lfb_write(off, cmd, ~0u);
        unsigned i = 1;
        while (i < count) {
            unsigned chunk = 1 + next() % 24; if (i + chunk > count) chunk = count - i;
            if (use_append) {
                uint32_t be[300];
                for (unsigned j = 0; j < chunk; j++) be[j] = __builtin_bswap32(payload[i + j]);  /* ST32 value */
                unsigned took = wii_voodoo_fifo_append_be(0x84000000u + off + 4 * i, be, chunk);
                appended += took; declined += chunk - took;
                for (unsigned j = took; j < chunk; j++) voodoo_lfb_write(off + 4 * (i + j), payload[i + j], ~0u);
            } else {
                for (unsigned j = 0; j < chunk; j++) voodoo_lfb_write(off + 4 * (i + j), payload[i + j], ~0u);
            }
            i += chunk;
        }
        if (header_last) voodoo_lfb_write(off, cmd, ~0u);
        off += count * 4;
    }
    capture(&snap[side]);
}
int main(void) {
    unsigned long long tris = 0;
    for (int seq = 0; seq < 20000; seq++) {
        uint64_t seed = 0x9e3779b97f4a7c15ull * (seq + 1);
        run(0, seed); run(1, seed);
        if (ncalls[0] != ncalls[1] || memcmp(calls[0], calls[1], ncalls[0] * sizeof(Call)) ||
            memcmp(&snap[0], &snap[1], sizeof snap[0])) {
            printf("MISMATCH seq %d calls %u/%u\n", seq, ncalls[0], ncalls[1]);
            return 1;
        }
        tris += ncalls[0];
    }
    printf("PASS: 20000 streams, %llu renderer calls identical; %llu words appended, %llu declined\n",
           tris, appended, declined);
    return 0;
}
