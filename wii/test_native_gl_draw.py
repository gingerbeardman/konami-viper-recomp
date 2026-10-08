"""Differential proof for wii/native_gl_draw.c against the generated
f_gl_0002adac it transcribes: same random entry states, then compare the full
context, all 16 MiB of RAM, the ordered MMIO write log, and every call to the
FIFO-space routine and the checkpoint handler. Covers multi-packet draws,
continuation commands, the FIFO-space branch both ways, checkpoints that fire,
and the bulk writer accepting or refusing a block.
Run: python3 wii/test_native_gl_draw.py [cases]"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
cases = int(sys.argv[1]) if len(sys.argv) > 1 else 20000
gl = (root / 'generated/gticlub2/gl_000.c').read_text()
m = re.search(r'\nvoid f_gl_0002adac\(PPCContext \*c\) \{.*?\n\}\n', gl, re.S)
if not m:
    raise SystemExit('generated f_gl_0002adac not found')
reference = m.group(0).replace('void f_gl_0002adac(', 'void reference_gl_0002adac(')

harness = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
static FILE *trace;   /* ordered MMIO writes and calls, per run */
static int bulk_mode; /* 0 never, 1 always, 2 address-dependent */
static int direct_mode; /* same, for direct triangle packets */
static unsigned long long direct_packets;
uint32_t rt_mmio_r8(uint32_t ea) { fprintf(trace, "R8 %08x\n", ea); return 0; }
uint32_t rt_mmio_r32(uint32_t ea) { fprintf(trace, "R32 %08x\n", ea); return 0; }
uint32_t rt_mmio_r16(uint32_t ea) { fprintf(trace, "R16 %08x\n", ea); return 0; }
void rt_mmio_w32(uint32_t ea, uint32_t v) { fprintf(trace, "W %08x %08x\n", ea, v); }
void rt_mmio_w16(uint32_t ea, uint32_t v) { fprintf(trace, "W16 %08x %04x\n", ea, v); }
void rt_mmio_w8(uint32_t ea, uint32_t v) { fprintf(trace, "W8 %08x %02x\n", ea, v); }
void rt_check(PPCContext *c, uint32_t pc) {
    fprintf(trace, "CHK %08x %lld\n", pc, (long long)c->budget);
    c->budget += 300;
}
void rt_hook(PPCContext *c, uint32_t pc) { (void)c; (void)pc; }
void rt_call(PPCContext *c, uint32_t t) { (void)c; (void)t; abort(); }
/* FIFO-space wait: deterministic new write pointer and limit. */
void f_gl_000211d4(PPCContext *c) {
    fprintf(trace, "SPACE r3=%08x lr=%08x\n", c->r[3], c->lr);
    uint32_t base = 0x84100000u + (c->r[3] << 4);
    ST32(0x2358u, base); ST32(0x235cu, base + 0x2000u);
    c->r[3] = 1; c->r[4] ^= 0x55u; c->f[1] = 3.25; c->cr[0] = 2;
}
int rt_wii_bulk_lfb_allowed(void) { return bulk_mode != 0; }
int wii_voodoo_bulk_writer_ready(uint32_t ea, unsigned count) {
    /* Like the device: only aligned LFB-window words, then the mode. */
    if (!count || (ea & 3) || ea < 0x84000000u || ea >= 0x86000000u) return 0;
    return bulk_mode == 1 || (bulk_mode == 2 && ((ea >> 4) & 1));
}
void wii_voodoo_bulk_writer_be(uint32_t ea, const uint32_t *w, unsigned n) {
    for (unsigned i = 0; i < n; i++) fprintf(trace, "W %08x %08x\n", ea + 4 * i, w[i]);
}
/* A direct packet's effect, logged as the guest stores it replaces: vertices
 * 1..n-1, the header (ST32LE), then the completing last vertex. */
int wii_voodoo_direct_triangles(uint32_t ea, uint32_t cmd, const uint32_t *w, unsigned n) {
    if (direct_mode == 0 || (direct_mode == 2 && ((ea >> 5) & 1))) return 0;
    fprintf(trace, "DIRECT %08x\n", ea); direct_packets++;
    for (unsigned i = 0; i + 10 < n; i++) fprintf(trace, "W %08x %08x\n", ea + 4 + 4 * i, w[i]);
    fprintf(trace, "W %08x %08x\n", ea, __builtin_bswap32(cmd));
    for (unsigned i = n - 10; i < n; i++) fprintf(trace, "W %08x %08x\n", ea + 4 + 4 * i, w[i]);
    return 1;
}
void wii_native_gl_0002adac(PPCContext *c);
''' + reference + r'''
static uint64_t rng;
static uint32_t next(void) { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (uint32_t)(rng >> 16); }
static float rnd(float scale) { return ((int32_t)next() / 2147483648.0f) * scale; }
static void put_f(uint32_t ea, float f) { uint32_t b; memcpy(&b, &f, 4); ST32(ea, b); }
static void setup(PPCContext *c, uint8_t *ram) {
    memset(c, 0, sizeof *c);
    g_ram = ram; memset(ram, 0, RAM_SIZE);
    for (int i = 0; i < 32; i++) { c->r[i] = next(); c->f[i] = rnd(100); }
    for (int i = 0; i < 8; i++) c->cr[i] = next() & 15;
    c->xer_so = next() & 1; c->xer_ca = next() & 1; c->ctr = next(); c->lr = next();
    c->budget = (next() & 3) ? 1000000 : (int)(next() % 200) + 1;
    ST8(0x3451u, next() % 4); ST8(0x3452u, next() % 2);
    for (uint32_t a = 0x23dcu; a < 0x23dcu + 4 * 48 + 48; a += 4) put_f(a, rnd(2));
    for (uint32_t a = 0x26dcu; a < 0x26dcu + 48; a += 4) put_f(a, rnd(3));
    for (uint32_t a = 0x2940u; a < 0x2958u; a += 4) put_f(a, rnd(300));
    for (uint32_t a = 0x3374u; a < 0x3398u; a += 4) put_f(a, rnd(1));
    put_f(0x2030u, 2.0f); put_f(0x2020u, 0.0f);
    put_f(0x275cu, rnd(1)); put_f(0x2760u, rnd(1));
    ST32(0x2780u, next()); ST32(0x3360u, next());
    for (uint32_t a = 0x3398u; a < 0x33c0u; a += 4) ST32(a, next());
    uint32_t fifo = 0x84000000u + ((next() & 0xfff) << 4);
    ST32(0x2358u, fifo);
    ST32(0x235cu, fifo + ((next() & 1) ? 0x4000u : 0x100u));
    ST16(0x3434u, 0x4700u + (next() & 3));
    /* Command stream: segments of packed vertices, continued while the next
     * word's high half matches 0x3434. */
    uint32_t p = 0x100000u + ((next() & 0xff) << 5);
    c->r[31] = p; c->r[30] = 1 + next() % 40; c->r[29] = next();
    unsigned segments = 1 + next() % 3, count = c->r[30];
    for (unsigned s = 0; s < segments; s++) {
        for (unsigned v = 0; v < count; v++, p += 32)
            for (unsigned k = 0; k < 8; k++) {
                float f = rnd(k == 7 ? 50 : 500);
                if ((next() & 63) == 0) f = 0.0f;
                if ((next() & 255) == 0) f = -f;
                put_f(p + 4 * k, f);
            }
        count = 1 + next() % 40;
        uint32_t marker = (s + 1 < segments) ? LD16(0x3434u) : 0x1234u;
        ST32(p, (marker << 16) | count);
        for (unsigned k = 1; k < 8; k++) ST32(p + 4 * k, next());
        p += 4;
    }
}
int main(int argc, char **argv) {
    int cases = atoi(argv[1]);
    uint8_t *ra = malloc(RAM_SIZE), *rb = malloc(RAM_SIZE);
    static PPCContext ca, cb;
    char *ta = NULL, *tb = NULL; size_t la = 0, lb = 0;
    for (int n = 0; n < cases; n++) {
        bulk_mode = n % 3; direct_mode = (n / 3) % 3;
        rng = 0x9e3779b97f4a7c15ull + (uint64_t)n * 0x632be59bd9b4e019ull;
        setup(&ca, ra);
        rng = 0x9e3779b97f4a7c15ull + (uint64_t)n * 0x632be59bd9b4e019ull;
        setup(&cb, rb);
        trace = open_memstream(&ta, &la); g_ram = ra; reference_gl_0002adac(&ca); fclose(trace);
        trace = open_memstream(&tb, &lb); g_ram = rb; wii_native_gl_0002adac(&cb); fclose(trace);
        int ctx = memcmp(&ca, &cb, sizeof ca), ram = memcmp(ra, rb, RAM_SIZE);
        /* Drop DIRECT markers so the logs compare as store sequences. */
        { char *src = tb, *dst = tb; size_t left = lb;
          while (left) { char *nl = memchr(src, '\n', left); size_t len = nl ? (size_t)(nl - src) + 1 : left;
            if (strncmp(src, "DIRECT ", 7)) { memmove(dst, src, len); dst += len; }
            src += len; left -= len; }
          lb = (size_t)(dst - tb); }
        int log = la != lb || memcmp(ta, tb, la);
        if (ctx || ram || log) {
            printf("MISMATCH case %d: context=%d ram=%d log=%d\n", n, !!ctx, !!ram, log);
            for (int i = 0; i < 32; i++) if (ca.r[i] != cb.r[i]) printf(" r%d %08x %08x\n", i, ca.r[i], cb.r[i]);
            for (int i = 0; i < 32; i++) if (memcmp(&ca.f[i], &cb.f[i], 8)) printf(" f%d %a %a\n", i, ca.f[i], cb.f[i]);
            printf(" budget %lld %lld\n", (long long)ca.budget, (long long)cb.budget);
            return 1;
        }
        free(ta); free(tb); ta = tb = NULL;
    }
    printf("native f_gl_0002adac: %d cases (%llu direct packets), context/RAM/MMIO log/calls identical PASS\n", cases, direct_packets);
    if (!direct_packets) { printf("direct path never exercised\n"); return 1; }
    return 0;
}
'''

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'test.c').write_text(harness)
    flags = ['clang', '-O2', '-std=gnu11', '-frounding-math', '-ffp-contract=off', '-fno-strict-aliasing',
             '-fsanitize=address,undefined', '-DVIPER_WII_BULK_WRITER', '-DVIPER_WII_DIRECT_TRIANGLES', '-Wno-unused-label', '-Wno-unused-variable',
             '-I' + str(root / 'runtime'), '-I' + str(root / 'wii'),
             '-I' + str(root / 'generated/gticlub2')]
    subprocess.run(flags + [str(tmp / 'test.c'), str(root / 'wii/native_gl_draw.c'), '-o', str(tmp / 'test'), '-lm'],
                   check=True)
    subprocess.run([str(tmp / 'test'), str(cases)], check=True)
