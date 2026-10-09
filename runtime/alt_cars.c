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
 * Car select: the game's handler 0x8b528 runs every frame of the car select screen; the
 * "car_select" hook (0x8b5c4, after it has set the tuned-up bit from the shift lever) marks the
 * screen as shown and steps through k_cars on each shift DOWN press (IN3 bit 6, active low), which
 * the game ignores there. The pick starts off at each car select and is dropped when the game
 * leaves play (the phase in the settings word's top byte falls below 0xa3, the car select).
 * The car select's own 3D scene still shows the original car; the overlay names the pick.
 *
 * RT_ALT_CAR=N (0..15) forces traffic car N; RT_ALT_CAR_LOG=1 logs every queued file read and
 * each change of the settings word.
 */
#include "alt_cars.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int g_enhanced;

#define TCAR_CARS 16
#define TCAR_EMPTY_SRC 32            /* mrr_car00a: the smallest traffic model */

/* the shift-down list: traffic car index (TCAR_carNNa) and overlay name */
static const struct { int tcar; const char *name; } k_cars[] = {
    { 1, "FIAT PANDA" }, { 15, "CITROEN 2CV" }, { 2, "LANCIA" }, { 10, "PEUGEOT" },
    { 5, "VOLVO ESTATE" }, { 7, "TAXI" }, { 3, "VAN" }, { 8, "RED CAR" },
};
#define N_CARS ((int)(sizeof k_cars / sizeof k_cars[0]))

extern uint8_t g_in[8];
static uint8_t *g_tcar;              /* TCAR_mdl.zin, unpacked */
static size_t g_tcar_len;
static int g_forced = -1;            /* RT_ALT_CAR */
static int g_pick = -1;              /* index in k_cars, -1: the original car */
static uint64_t g_frame, g_select_frame;
static int g_shift_down;

static int alt_tcar(void) { return g_forced >= 0 ? g_forced : g_pick >= 0 ? k_cars[g_pick].tcar : -1; }

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
    const char *v = getenv("RT_ALT_CAR");
    if (v && atoi(v) >= 0 && atoi(v) < TCAR_CARS) {
        g_forced = atoi(v);
        rt_log("alt cars: player car -> traffic car %d\n", g_forced);
    }
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
    int tcar = alt_tcar();
    if (!g_enhanced || !g_tcar || tcar < 0) return;
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
    tcar_model(tcar, &car_off, &car_size);
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
    rt_log("alt cars: %s -> traffic car %d (model %u of %u, %#x of %#x bytes)\n", name, tcar, body, orig.nmodels, len, orig_len);
}

/* the race settings word: car in bits 9-11, tuned-up in bit 8 (read at race setup, 0x8e378) */
#define GAME_TOC 0x154da8u
static uint32_t settings_word_ea(void) {
    uint32_t p = LD32(GAME_TOC + 0x54);
    return (p >= 0x100000 && p < RAM_SIZE - 8) ? p + 4 : 0;
}

/* the car select screen is up: its handler ran within the last few frames (it runs at 30 Hz) */
static int in_car_select(void) { return g_select_frame && g_frame - g_select_frame <= 4; }

void alt_cars_car_select(PPCContext *c) {
    (void)c;
    if (!g_enhanced || !g_tcar) return;
    if (!in_car_select()) { g_pick = -1; g_shift_down = 1; }   /* a new car select: original car */
    g_select_frame = g_frame ? g_frame : 1;
    int down = !(g_in[3] & 0x40);
    if (down && !g_shift_down) {
        g_pick = g_pick + 1 < N_CARS ? g_pick + 1 : -1;
        if (getenv("RT_ALT_CAR_LOG")) rt_log("alt cars: car select pick %s\n", g_pick >= 0 ? k_cars[g_pick].name : "original");
    }
    g_shift_down = down;
}

const char *alt_cars_select_label(void) {
    if (!g_enhanced || !g_tcar || !in_car_select()) return NULL;
    return g_pick >= 0 ? k_cars[g_pick].name : "";
}

void alt_cars_on_frame(uint64_t frame) {
    static uint32_t last = 0xffffffffu;
    g_frame = frame;
    uint32_t ea = settings_word_ea();
    uint32_t w = ea ? LD32(ea) : 0;
    if (w != last && getenv("RT_ALT_CAR_LOG")) rt_log("alt cars: frame %llu settings %08x\n", (unsigned long long)frame, w);
    last = w;
    if ((w >> 24) < 0xa3) g_pick = -1;                         /* out of play: attract, coin-up */
}
