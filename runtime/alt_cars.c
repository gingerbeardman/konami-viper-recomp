/*
 * Alternate player cars (GTI Club 2 experiment, enhanced mode).
 *
 * At the start of a race the game loads the chosen car's models by name, game/mdldata/P<car>_mdl.zin
 * (TP<car> for the tuned-up version), through its queue of file reads: 0x49300 queues (name,
 * destination) in a request slot, and the worker 0x45dd8 reads it. The "file_loaded" hook sits
 * where that read has completed (r3 = 0 when it succeeded), with the slot in r26: name at +4,
 * destination at +0x10.
 *
 * A _mdl.zin container is: u32 model count, u32 texture count, the texture names (NUL-terminated),
 * padding to 4, u32 size per model, then the models. A model starts with u16 flags, u16 type,
 * f32 100.0, u16 vertex count at +8, and its int16 vertex triples at +0x1c. The model indices are
 * the part names in alphabetical order (blight, bodyA..C, dview, sujiA..D, tire, wind, ref_...).
 *
 * The prototype replaces the loaded container with one of the same model count that uses the
 * traffic cars' texture list (TCAR_mdl.zin, whose textures stay in VRAM for the traffic): the
 * largest part, the main body, becomes traffic car N, and every other part becomes a small
 * traffic model with all its vertices at the origin, so it draws nothing. The new container must
 * fit in the original's size, the buffer the game allocated for it.
 *
 * RT_ALT_CAR=N (0..15) picks the traffic car; RT_ALT_CAR_LOG=1 logs every queued file read.
 */
#include "alt_cars.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_enhanced;

#define TCAR_CARS 16
#define TCAR_EMPTY_SRC 32            /* mrr_car00a: the smallest traffic model */

static uint8_t *g_tcar;              /* TCAR_mdl.zin, unpacked */
static size_t g_tcar_len;
static int g_alt = -1;

static uint32_t name_hash(const char *s) {
    uint32_t r = 0;
    for (; *s; s++) {
        int ch = (signed char)*s;
        for (int b = 0; b < 6; b++) {
            uint32_t nb = (r << 1) | ((uint32_t)(ch >> b) & 1);
            r = nb ^ ((r & 0x80000000u) ? 0x04c11db7u : 0);
        }
    }
    return r;
}

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

typedef struct { uint32_t nmodels, ntex, names_len, sizes_at; } Container;

/* Header of a container held in `read(i)` (host or guest bytes); 0 if it does not look like one. */
static int parse_header(Container *k, uint32_t (*read)(const void *, uint32_t), const void *src, uint32_t limit) {
    k->nmodels = read(src, 0);
    k->ntex = read(src, 4);
    if (!k->nmodels || k->nmodels > 4096 || k->ntex > 256) return 0;
    uint32_t p = 8;
    for (uint32_t t = 0; t < k->ntex; t++) {
        while (p < limit && (read(src, p & ~3u) >> (24 - 8 * (p & 3)) & 0xff)) p++;
        if (p++ >= limit) return 0;
    }
    k->names_len = p - 8;
    k->sizes_at = (p + 3) & ~3u;
    return k->sizes_at + 4 * k->nmodels <= limit;
}

static uint32_t read_host(const void *src, uint32_t off) { return be32((const uint8_t *)src + off); }
static uint32_t read_guest(const void *src, uint32_t off) { return LD32((uint32_t)(uintptr_t)src + off); }

void alt_cars_init(const char *work) {
    const char *v = getenv("RT_ALT_CAR");
    if (!v) return;
    int n = atoi(v);
    if (n < 0 || n >= TCAR_CARS) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/fs/_unk/%08x.bin", work, name_hash("game/mdldata/TCAR_mdl.zin"));
    FILE *f = fopen(path, "rb");
    if (!f) { rt_log("alt cars: cannot read %s\n", path); return; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_tcar = malloc((size_t)len);
    if (!g_tcar || fread(g_tcar, 1, (size_t)len, f) != (size_t)len) { fclose(f); free(g_tcar); g_tcar = NULL; return; }
    fclose(f);
    g_tcar_len = (size_t)len;
    Container k;
    if (!parse_header(&k, read_host, g_tcar, (uint32_t)len) || k.nmodels <= TCAR_EMPTY_SRC) {
        rt_log("alt cars: unexpected TCAR_mdl.zin\n");
        free(g_tcar); g_tcar = NULL; return;
    }
    g_alt = n;
    rt_log("alt cars: player car -> traffic car %d\n", n);
}

/* offset and size of model i of the TCAR container */
static void tcar_model(int i, uint32_t *off, uint32_t *size) {
    Container k;
    parse_header(&k, read_host, g_tcar, (uint32_t)g_tcar_len);
    uint32_t o = k.sizes_at + 4 * k.nmodels;
    for (int j = 0; j < i; j++) o += be32(g_tcar + k.sizes_at + 4 * j);
    *off = o;
    *size = be32(g_tcar + k.sizes_at + 4 * i);
}

static int is_player_models(const char *name) {
    const char *p = "game/mdldata/", *s = "_mdl.zin";
    size_t n = strlen(name), lp = strlen(p), ls = strlen(s);
    if (n <= lp + ls || strncmp(name, p, lp) || strcmp(name + n - ls, s)) return 0;
    return name[lp] == 'P' || (name[lp] == 'T' && name[lp + 1] == 'P');
}

void alt_cars_file_loaded(PPCContext *c) {
    if (!g_enhanced || g_alt < 0) return;
    if (c->r[3] != 0) return;
    uint32_t name_ea = LD32(c->r[26] + 4), dest = LD32(c->r[26] + 0x10);
    char name[64];
    size_t i = 0;
    for (; i < sizeof name - 1 && (name[i] = (char)LD8(name_ea + (uint32_t)i)); i++) {}
    name[i] = 0;
    if (getenv("RT_ALT_CAR_LOG")) rt_log("alt cars: loaded %s at %#x\n", name, dest);  /* every queued read */
    if (!is_player_models(name)) return;

    /* the loaded original: model count and total size, its largest model */
    Container orig;
    if (!parse_header(&orig, read_guest, (const void *)(uintptr_t)dest, 0x10000)) return;
    uint32_t orig_len = orig.sizes_at + 4 * orig.nmodels, body = 0, body_size = 0;
    for (uint32_t m = 0; m < orig.nmodels; m++) {
        uint32_t size = LD32(dest + orig.sizes_at + 4 * m);
        if (size > body_size) { body = m; body_size = size; }
        orig_len += size;
    }

    Container tc;
    parse_header(&tc, read_host, g_tcar, (uint32_t)g_tcar_len);
    uint32_t car_off, car_size, empty_off, empty_size;
    tcar_model(g_alt, &car_off, &car_size);
    tcar_model(TCAR_EMPTY_SRC, &empty_off, &empty_size);
    uint32_t sizes_at = tc.sizes_at, models_at = sizes_at + 4 * orig.nmodels;
    uint32_t len = models_at + car_size + (orig.nmodels - 1) * empty_size;
    if (len > orig_len) {
        rt_log("alt cars: %s: replacement needs %#x bytes, the buffer has %#x\n", name, len, orig_len);
        return;
    }

    uint8_t *out = calloc(1, len);
    if (!out) return;
    put32(out, orig.nmodels);
    put32(out + 4, tc.ntex);
    memcpy(out + 8, g_tcar + 8, tc.names_len);
    uint32_t o = models_at;
    for (uint32_t m = 0; m < orig.nmodels; m++) {
        int is_body = m == body;
        uint32_t size = is_body ? car_size : empty_size;
        put32(out + sizes_at + 4 * m, size);
        memcpy(out + o, g_tcar + (is_body ? car_off : empty_off), size);
        if (!is_body) {
            uint32_t nverts = (uint32_t)out[o + 8] << 8 | out[o + 9];
            if (0x1c + 6 * nverts <= size) memset(out + o + 0x1c, 0, 6 * nverts);
        }
        o += size;
    }
    for (uint32_t b = 0; b < len; b++) ST8(dest + b, out[b]);
    free(out);
    rt_log("alt cars: %s -> traffic car %d (model %u of %u, %#x of %#x bytes)\n", name, g_alt, body, orig.nmodels, len, orig_len);
}
