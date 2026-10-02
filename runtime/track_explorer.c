#include "runtime.h"
#include "track_explorer.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Addresses are exclusively for GTI Club 2 JAB; only its profile installs hooks.
 * Route nodes: count u16, points +4, previous +8, next +12. Points contain
 * x,z,dx,dz,length,heading,distance,unused. Read from the running game. */
typedef struct { float x, z, length; } Point;
static Point points[2048];
static int count;
static float length, distance, altitude;
static uint32_t root, last_tick;
static uint64_t last_frame, last_cycles;
static atomic_int requested, active;
static _Atomic float speed = 40, height = 12;

void explorer_toggle(void) { atomic_fetch_xor(&requested, 1); }
void explorer_on_frame(uint64_t frame) {
    if (atomic_load(&active) && frame > last_frame + 4) {
        atomic_store(&active, 0); atomic_store(&requested, 0); last_frame = 0;
    }
}
int explorer_active(void) { return atomic_load(&active); }
void explorer_adjust(float s, float h) {
    float v = atomic_load(&speed) + s;
    atomic_store(&speed, v < 0 ? 0 : v > 160 ? 160 : v);
    v = atomic_load(&height) + h;
    atomic_store(&height, v < 3 ? 3 : v > 60 ? 60 : v);
}
static int valid(uint32_t p, size_t n) { return p >= 0x38040 && p < RAM_SIZE && n <= RAM_SIZE - p; }
static int route(uint32_t start) {
    uint32_t node = start;
    count = 0; length = 0;
    for (int n = 0; n < 128; n++) {
        if (!valid(node, 0x44)) return 0;
        unsigned num = LD16(node), data = LD32(node + 4);
        if (num < 2 || num > 512 || !valid(data, num * 32)) return 0;
        for (unsigned i = 0; i < num; i++) {
            float x = LDF32(data + i * 32), z = LDF32(data + i * 32 + 4);
            if (!isfinite(x) || !isfinite(z) || fabsf(x) > 100000 || fabsf(z) > 100000) return 0;
            if (count && hypotf(x - points[count-1].x, z - points[count-1].z) < .01f) continue;
            if (count == 2048) return 0;
            points[count++] = (Point){x, z, 0};
        }
        node = LD32(node + 12);
        if (node == start) break;
        if (n == 127) return 0;
    }
    for (int i = 0; i < count; i++) {
        Point *a = points + i, *b = points + (i + 1) % count;
        a->length = hypotf(a->x - b->x, a->z - b->z);
        length += a->length;
    }
    return count > 2 && length > 100;
}
static Point sample(float d) {
    d = fmodf(d, length);
    for (int i = 0; i < count; i++) {
        Point a = points[i], b = points[(i + 1) % count];
        if (d <= a.length && a.length > 0) {
            float t = d / a.length;
            return (Point){a.x + (b.x-a.x)*t, a.z + (b.z-a.z)*t, 0};
        }
        d -= a.length;
    }
    return points[0];
}
/* Invoke the game's read-only collision query with a copied register context.
 * Save/restore the entire temporary stack area; never consume the live budget.
 * 5576c selects the terrain tile, 566f0 interpolates its triangle plane. */
static float ground(PPCContext *live, Point p, float fallback) {
    unsigned sp = live->r[1];
    if (sp < 4096 || sp >= RAM_SIZE - 256) return fallback;
    unsigned base = sp - 2048, scratch = sp - 256;
    uint8_t saved[2048];
    memcpy(saved, g_ram + base, sizeof saved);
    PPCContext c = *live;
    c.r[1] = sp - 512; c.budget = 10000000; c.unwind = 0;
    c.f[1] = p.x; c.f[2] = p.z; c.r[5] = scratch;
    RtFn tile = rt_lookup(0x5576c), query = rt_lookup(0x566f0);
    if (!tile || !query) return fallback;
    tile(&c);
    STF32(scratch + 16, p.x); STF32(scratch + 20, fallback); STF32(scratch + 24, p.z);
    c.r[3] = scratch + 16; c.r[4] = scratch;
    if (!c.unwind) query(&c);
    float y = (float)c.f[1];
    memcpy(g_ram + base, saved, sizeof saved);
    return !c.unwind && isfinite(y) && fabsf(y) < 1000 ? y : fallback;
}
void explorer_camera(PPCContext *c, uint64_t frame) {
    static int init;
    if (!init) { init = 1; const char *v = getenv("RT_TRACK_EXPLORER"); if (v && atoi(v)) atomic_store(&requested, 1); }
    uint32_t tick = LD32(LD32(c->r[2] + 0xc) + 4);
    if (atomic_load(&active) && last_frame && frame - last_frame <= 2) {
        uint32_t timer = LD32(c->r[2] + 0x558);
        ST32(timer, LD32(timer) + tick - last_tick);
    }
    last_tick = tick;
    if (!atomic_load(&requested)) { atomic_store(&active, 0); last_frame = 0; return; }
    uint32_t start = LD32(0x8c0188);
    if (!last_frame || frame > last_frame + 2 || start != root) {
        if (!route(start)) { atomic_store(&active, 0); return; }
        root = start; distance = 0; altitude = ground(c, points[0], 15) + atomic_load(&height);
        rt_log("track explorer: %d points, %.1f metres; F6 exits\n", count, length);
    }
    atomic_store(&active, 1);
    uint64_t now = rt_now();
    float dt = last_frame && frame - last_frame <= 2 ? fminf(.1f, (float)(now - last_cycles) / CPU_HZ) : 0;
    last_cycles = now;
    last_frame = frame;
    float next = distance + atomic_load(&speed) * dt;
    if (next >= length) rt_log("track explorer: completed full course loop (%.1f metres)\n", length);
    distance = fmodf(next, length);
    Point eye = sample(distance), target = sample(distance + 28);
    float floor = ground(c, eye, altitude - atomic_load(&height));
    altitude += (floor + atomic_load(&height) - altitude) * fminf(1, dt * 4);
    float target_y = ground(c, target, floor) + 2;
    STF32(0x8c1cf8, eye.x); STF32(0x8c1cfc, altitude); STF32(0x8c1d00, eye.z);
    STF32(0x8c1d04, atan2f(target_y-altitude, hypotf(target.x-eye.x, target.z-eye.z)));
    /* The camera looks along local -Z, opposite the road heading convention. */
    STF32(0x8c1d08, atan2f(eye.x-target.x, eye.z-target.z));
    STF32(0x8c1d0c, 0);
}
void explorer_race(PPCContext *c) {
    /* At b403c, r4 is the countdown/race state. An out-of-range value takes
     * the existing no-transition exit after the world has rendered. */
    if (explorer_active()) c->r[4] = 8;
}
