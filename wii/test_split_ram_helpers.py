#!/usr/bin/env python3
"""Original memory bodies versus split wrappers, including MMIO byte order."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-native', type=Path)
args = parser.parse_args()

header = (root / 'runtime/ppc_rt.h').read_text()
start = header.index('#else\n#ifdef VIPER_WII_SPLIT_RAM_HELPERS')
source = header[start:]
reference = ''
for name in ['LD32', 'LD16', 'ST32', 'ST16']:
    match = re.search(r'static inline [^\n]+ ' + name + r'\(', source)
    assert match
    brace = source.index('{', match.start())
    end = brace + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[match.start():end]
    body = re.sub(
        r'#ifdef VIPER_WII_SPLIT_RAM_HELPERS\n(.*?)#else\n(.*?)#endif',
        lambda found: found[2],
        body,
        flags=re.S,
    )
    reference += re.sub(r'\b' + name + r'\b', 'ref_' + name, body) + '\n'

for name in ['ref_LD32', 'ref_LD16', 'ref_ST32', 'ref_ST16']:
    if name not in reference:
        raise SystemExit('missing extracted %s' % name)
if 'wii_ram_cross' in reference:
    raise SystemExit('extraction kept split helpers; expected macro-off bodies')
if 'rt_mmio_r8' not in reference or 'g_ram[' not in reference:
    raise SystemExit('extraction dropped original cross-boundary bodies')

code = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#ifndef VIPER_WII_SPLIT_RAM_HELPERS
#error Compile with -DVIPER_WII_SPLIT_RAM_HELPERS so the live helpers call the cold split bodies
#endif
#ifndef VIPER_MEMORY_AUDIT
#error Compile with -DVIPER_MEMORY_AUDIT so every helper records an audit event
#endif
static char probe_fail_text[768];
static void probe_fail(const char *fmt, ...) __attribute__((format(printf, 1, 2), noreturn));
static void probe_fail(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(probe_fail_text, sizeof probe_fail_text, fmt, ap);
    va_end(ap);
    printf("VIPER WII SPLIT RAM ACCESS FAIL: %s\n", probe_fail_text);
    fflush(stdout);
#ifdef VIPER_SPLIT_RAM_NATIVE
    for (;;)
        VIDEO_WaitVSync();
#else
    fprintf(stderr, "VIPER WII SPLIT RAM ACCESS FAIL: %s\n", probe_fail_text);
    fflush(stderr);
    abort();
#endif
}
uint8_t *g_ram;
static struct Event { uint32_t ea, value, width, write; } events[16], expected[16];
static unsigned used, seed;
static void record(uint32_t ea, uint32_t value, unsigned width, unsigned write) {
    if (used >= 16)
        probe_fail("audit/MMIO trace overflow ea=%08x width=%u direction=%u", ea, width, write);
    events[used++] = (struct Event){ea, value, width, write};
}
static uint32_t read_bus(uint32_t ea, unsigned width) {
    uint32_t v = ea * 0x517cc1b7u + seed + used;
    record(ea, v, width, 0);
    return v & (width == 1 ? 255 : width == 2 ? 65535 : ~0u);
}
uint32_t rt_mmio_r8(uint32_t ea) { return read_bus(ea, 1); }
uint32_t rt_mmio_r16(uint32_t ea) { return read_bus(ea, 2); }
uint32_t rt_mmio_r32(uint32_t ea) { return read_bus(ea, 4); }
void rt_mmio_w8(uint32_t ea, uint32_t v) { record(ea, v, 1, 1); }
void rt_mmio_w16(uint32_t ea, uint32_t v) { record(ea, v, 2, 1); }
void rt_mmio_w32(uint32_t ea, uint32_t v) { record(ea, v, 4, 1); }
void rt_memory_access(uint32_t ea, unsigned width, int write) { record(ea, 0, width, (unsigned)write + 2); }
/*REFERENCE*/
static uint32_t rng = 0x68163792u;
static uint32_t next(void) {
    rng ^= rng << 13;
    rng ^= rng >> 17;
    rng ^= rng << 5;
    return rng;
}
static const char *direction_name(unsigned direction) {
    if (direction == 0) return "mmio-read";
    if (direction == 1) return "mmio-write";
    if (direction == 2) return "audit-read";
    if (direction == 3) return "audit-write";
    return "unknown";
}
static const char *op_name(unsigned op) {
    return op == 0 ? "LD32" : op == 1 ? "LD16" : op == 2 ? "ST32" : "ST16";
}
static void expect_trace(unsigned n, unsigned op, uint32_t ea, uint32_t stored, uint32_t ref, uint32_t split, unsigned size) {
    if (ref != split)
        probe_fail("read value mismatch n=%u op=%s ea=%08x stored=%08x ref=%08x split=%08x",
                   n, op_name(op), (unsigned)ea, (unsigned)stored, (unsigned)ref, (unsigned)split);
    if (used != size)
        probe_fail("trace length mismatch n=%u op=%s ea=%08x ref=%u split=%u",
                   n, op_name(op), (unsigned)ea, size, used);
    if (memcmp(expected, events, used * sizeof events[0]) == 0)
        return;
    for (unsigned i = 0; i < used; i++) {
        struct Event x = expected[i], y = events[i];
        if (x.ea != y.ea || x.value != y.value || x.width != y.width || x.write != y.write)
            probe_fail("trace mismatch n=%u op=%s ea=%08x index=%u ref address=%08x width=%u value=%08x direction=%s split address=%08x width=%u value=%08x direction=%s",
                       n, op_name(op), (unsigned)ea, i,
                       (unsigned)x.ea, (unsigned)x.width, (unsigned)x.value, direction_name(x.write),
                       (unsigned)y.ea, (unsigned)y.width, (unsigned)y.value, direction_name(y.write));
    }
    probe_fail("trace mismatch not located n=%u op=%s ea=%08x", n, op_name(op), (unsigned)ea);
}
static void expect_window(unsigned n, unsigned op, uint32_t ea, const uint8_t *a, const uint8_t *b) {
    for (unsigned i = 0; i < 4; i++) {
        uint32_t addr = ea + i;
        if (ea < RAM_LIMIT && addr < RAM_LIMIT && a[addr & RAM_MASK] != b[addr & RAM_MASK])
            probe_fail("RAM byte mismatch n=%u op=%s ea=%08x addr=%08x ref=%02x split=%02x",
                       n, op_name(op), (unsigned)ea, (unsigned)addr,
                       (unsigned)a[addr & RAM_MASK], (unsigned)b[addr & RAM_MASK]);
    }
}
static void expect_ram(unsigned n, unsigned op, uint32_t ea, const uint8_t *a, const uint8_t *b) {
    if (memcmp(a, b, RAM_SIZE) == 0)
        return;
    for (uint32_t i = 0; i < RAM_SIZE; i++)
        if (a[i] != b[i])
            probe_fail("full RAM mismatch n=%u op=%s ea=%08x off=%08x ref=%02x split=%02x",
                       n, op_name(op), (unsigned)ea, (unsigned)i, (unsigned)a[i], (unsigned)b[i]);
    probe_fail("full RAM mismatch not located n=%u op=%s ea=%08x", n, op_name(op), (unsigned)ea);
}
static void check(void) {
    unsigned count = 0;
    /*ALLOC*/
    if ((uintptr_t)a < (uintptr_t)b + RAM_SIZE && (uintptr_t)b < (uintptr_t)a + RAM_SIZE)
        probe_fail("RAM images overlap a=%p b=%p", (void *)a, (void *)b);
    for (unsigned i = 0; i < RAM_SIZE; i++)
        a[i] = b[i] = (uint8_t)(i * 17 + (i >> 11));
    const uint32_t edge[] = {0, 1, 2, 3, 0x00fffff9, 0x00fffffa, 0x00fffffb, 0x00fffffc, 0x00fffffd, 0x00fffffe, 0x00ffffff, 0x01000000, 0x01000001, 0x01fffff9, 0x01fffffa, 0x01fffffb, 0x01fffffc, 0x01fffffd, 0x01fffffe, 0x01ffffff, 0x02000000, 0x02000001, 0x80000000, 0xfffffffd, 0xfffffffe, 0xffffffff};
    unsigned edges = (unsigned)(sizeof edge / sizeof edge[0]);
    for (unsigned n = 0; n < CASE_COUNT; n++)
        for (unsigned op = 0; op < 4; op++) {
            uint32_t ea = n < edges ? edge[n] : next() & (n % 3 ? 0x01ffffff : ~0u);
            uint32_t v = next(), r = 0, c = 0;
            seed = n;
            used = 0;
            g_ram = a;
            if (op == 0) r = ref_LD32(ea);
            if (op == 1) r = ref_LD16(ea);
            if (op == 2) ref_ST32(ea, v);
            if (op == 3) ref_ST16(ea, v);
            unsigned size = used;
            memcpy(expected, events, sizeof events);
            used = 0;
            g_ram = b;
            if (op == 0) c = LD32(ea);
            if (op == 1) c = LD16(ea);
            if (op == 2) ST32(ea, v);
            if (op == 3) ST16(ea, v);
            expect_trace(n, op, ea, v, r, c, size);
            expect_window(n, op, ea, a, b);
            if ((n & 255) == 0)
                expect_ram(n, op, ea, a, b);
            count++;
        }
    expect_ram(CASE_COUNT, 0, 0, a, b);
#ifdef VIPER_SPLIT_RAM_NATIVE
    if (count != 8192u)
        probe_fail("native operation count %u, expected 8192", count);
#endif
    /*RELEASE*/
    printf("VIPER WII SPLIT RAM ACCESS PASS cases=%u; values, complete RAM, audit/MMIO order+width+value\n", count);
    fflush(stdout);
}
'''
code = code.replace('/*REFERENCE*/', reference)

host_alloc = r'''uint8_t *a = malloc(RAM_SIZE), *b = malloc(RAM_SIZE);
    if (!a || !b)
        probe_fail("host malloc of two 16MiB buffers failed a=%p b=%p", (void *)a, (void *)b);'''
host_release = 'free(a); free(b);'
native_alloc = r'''uint8_t *a = probe_malloc(RAM_SIZE), *b = probe_malloc(RAM_SIZE);
    if (!a || !b)
        probe_fail("MEM2 allocation of two 16MiB buffers failed a=%p b=%p arena2_lo=%p arena2_hi=%p",
                   (void *)a, (void *)b, (void *)SYS_GetArena2Lo(), (void *)SYS_GetArena2Hi());
    if (((uintptr_t)a | (uintptr_t)b) & 31u)
        probe_fail("MEM2 buffers are not 32-byte aligned a=%p b=%p", (void *)a, (void *)b);'''
native_release = 'probe_free(a); probe_free(b);'

native_prefix = r'''#include <gccore.h>
#include <stddef.h>
#include <stdint.h>
#define CASE_COUNT 2048
#define VIPER_SPLIT_RAM_NATIVE 1
/* 2048 addresses times LD32, LD16, ST32, and ST16 is 8192 operations.
 * Both 16MiB images are 32-byte aligned bumps from MEM2. A request that
 * does not fit returns NULL and does not move the arena. probe_free does
 * not reclaim: libc free must not see these pointers, and the images stay
 * valid until the DOL halts on the vsync loop. Video uses MEM1 first, so
 * an allocation failure can still reach the console. */
static void *probe_malloc(size_t size) {
    uintptr_t lo = (uintptr_t)SYS_GetArena2Lo();
    uintptr_t hi = (uintptr_t)SYS_GetArena2Hi();
    uintptr_t p = (lo + 31u) & ~(uintptr_t)31u;
    size = (size + 31u) & ~(size_t)31u;
    if (p < lo || p >= hi || size > hi - p)
        return NULL;
    SYS_SetArena2Lo((void *)(p + size));
    return (void *)p;
}
static void probe_free(void *p) { (void)p; }
'''
native_main = r'''
int main(void) {
    VIDEO_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *fb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * 2);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    if (mode->viTVMode & VI_NON_INTERLACE)
        VIDEO_WaitVSync();
    printf("VIPER WII SPLIT RAM ACCESS NATIVE PROBE\n");
    fflush(stdout);
    check();
    for (;;)
        VIDEO_WaitVSync();
}
'''
host_main = 'int main(void) { check(); return 0; }\n'

if args.emit_native:
    native = native_prefix + code.replace('/*ALLOC*/', native_alloc).replace('/*RELEASE*/', native_release) + native_main
    for token in ['SYS_GetArena2Lo', 'SYS_GetArena2Hi', 'SYS_SetArena2Lo', 'probe_free',
                  'VIPER WII SPLIT RAM ACCESS PASS', 'expected 8192', 'ref_LD32', 'ref_ST16']:
        if token not in native:
            raise SystemExit('native probe missing %s' % token)
    if re.search(r'(?<![\w])malloc\(', native) or re.search(r'(?<![\w])free\(', native) or 'assert(' in native:
        raise SystemExit('native probe must allocate from MEM2 and print failures')
    if native.count('probe_malloc(RAM_SIZE)') != 2:
        raise SystemExit('native probe must allocate two RAM images')
    args.emit_native.parent.mkdir(parents=True, exist_ok=True)
    args.emit_native.write_text(native)
else:
    host = ('#define CASE_COUNT 65536\n' + code.replace('/*ALLOC*/', host_alloc).replace('/*RELEASE*/', host_release) + host_main)
    if 'SYS_GetArena2Lo' in host or 'probe_malloc' in host:
        raise SystemExit('host probe must keep malloc/free')
    with tempfile.TemporaryDirectory(prefix='ram-split-') as directory:
        path = Path(directory)
        (path / 'test.c').write_text(host)
        subprocess.run(
            ['clang', '-O2', '-std=c11', '-DVIPER_WII_SPLIT_RAM_HELPERS', '-DVIPER_MEMORY_AUDIT',
             '-fsanitize=address,undefined', '-I' + str(root / 'runtime'),
             str(path / 'test.c'), str(root / 'wii/ram_access_cold.c'), '-o', str(path / 'test')],
            check=True,
        )
        subprocess.run([str(path / 'test')], check=True)
