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
 * The loaded container is replaced by one of the same model count that uses the traffic cars'
 * texture list (TCAR_mdl.zin, whose textures stay in VRAM for the traffic) after its own: the main
 * body becomes traffic car N (its texture index shifted past the original list); the floor pan
 * (bodyA), the dark underside that reads as the car's shadow, is kept and fitted under the new
 * body; every other part becomes a small traffic model with all its vertices at the origin, so it
 * draws nothing. The new container must fit in the original's size, the buffer the game allocated.
 *
 * Car select: like the shift lever's UP position picks the tuned-up car, holding it DOWN picks the
 * highlighted car's alternate, k_cars[car], which the game ignores there. The game's handler
 * 0x8b528 runs every frame of the car select; the "car_select" hook (0x8b5c4, after it has set the
 * tuned-up bit) reads the lever (IN3 bit 6, active low). What it holds when the car select ends is
 * raced; it is dropped when the game leaves play (the settings word's phase falls below 0xa3).
 * As UP makes every car tuned-up, the rivals too, DOWN makes every car its alternate: the game
 * keeps each loaded section in a table at *(toc + 0x40) (entries of 0x24 bytes from +8: section
 * id, flags, model count, model array), and a model is 16 bytes (header word, bounding radius,
 * graphics handle, data). Each car's R<car> section gets the traffic car's models for its bodies
 * (R1 main body, R2 and R3 lower details) and a negative radius, culled, for every other part.
 * They stay so from the car select through the race and come back when the lever leaves DOWN at
 * a car select or the game leaves play.
 *
 * RT_ALT_CAR=N (0..15) forces traffic car N; RT_ALT_CAR_LOG=1 logs every queued file read and
 * each change of the settings word.
 */
#include "alt_cars.h"
#include "runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

extern int g_enhanced;

#define TCAR_CARS 16
#define TCAR_EMPTY_SRC 32            /* mrr_car00a: the smallest traffic model */

/* per player car (settings word order: mini, fiat, super7, cobra, juguar, wagen, gtv, ferr): its
 * alternate, a traffic car (TCAR_carNNa; the log name), and its R<car> section with the indices of
 * the R1 main body and the first R2 and R3 bodies (the T<car> tuned-up sections share them) */
static const struct { int tcar; const char *name; int sec, body, mid, far; } k_cars[] = {
    { 1, "FIAT PANDA", 25, 5, 24, 26 },  { 2, "LANCIA", 22, 5, 24, 26 },
    { 3, "VAN", 26, 6, 28, 34 },         { 5, "VOLVO ESTATE", 20, 4, 24, 26 },
    { 7, "TAXI", 24, 4, 40, 42 },        { 8, "RED CAR", 27, 5, 24, 26 },
    { 11, "BEETLE", 23, 5, 24, 26 },     { 15, "CITROEN 2CV", 21, 7, 27, 28 },
};
#define N_CARS ((int)(sizeof k_cars / sizeof k_cars[0]))
#define SEC_TCAR 31

extern uint8_t g_in[8];
static uint8_t *g_tcar;              /* TCAR_mdl.zin, unpacked */
static size_t g_tcar_len;
static int g_forced = -1;            /* RT_ALT_CAR */
static int g_pick = -1;              /* index in k_cars, -1: the original car */
static uint64_t g_frame, g_select_frame;
static int g_shown;                  /* the R sections show the alternates */
static uint8_t g_saved[N_CARS][64][16];   /* their original model entries */

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

/* The player car sections' parts (indices = part names in alphabetical order): the main body,
 * the floor pan (bodyA: the dark underside that reads as the car's shadow), the driver's-view
 * dashboard parts and the tyre parts (drawn at each wheel, the front ones steered) */
static const struct { const char *sec; int body, pan; uint32_t dview, tires; } k_parts[] = {
    { "Pmini", 3, 1, 1u << 4, 1u << 9 },     { "TPmini", 3, 1, 1u << 4, 1u << 9 },
    { "Pfiat", 3, 1, 1u << 4, 1u << 9 },     { "TPfiat", 3, 1, 1u << 4, 1u << 9 },
    { "Psuper7", 6, 5, 7u << 8, 7u << 16 },  { "TPsuper7", 6, 5, 7u << 8, 7u << 15 },
    { "Pcobra", 2, 1, 1u << 4, 1u << 9 },    { "TPcobra", 2, 1, 1u << 4, 1u << 9 },
    { "Pjuguar", 2, 1, 1u << 4, 1u << 13 },  { "TPjuguar", 2, 1, 1u << 4, 1u << 13 },
    { "Pwagen", 5, 3, 1u << 6, 1u << 11 },   { "TPwagen", 3, 1, 1u << 4, 1u << 9 },
    { "Pgtv", 3, 1, 1u << 4, 1u << 9 },      { "TPgtv", 3, 1, 1u << 4, 1u << 9 },
    { "Pferr", 5, 3, 1u << 6, 3u },          { "TPferr", 5, 3, 1u << 6, 3u },
};

/* the k_parts entry of game/mdldata/<sec>_mdl.zin, or -1 */
static int player_section(const char *name) {
    const char *p = "game/mdldata/", *s = "_mdl.zin";
    size_t n = strlen(name), lp = strlen(p), ls = strlen(s);
    if (n <= lp + ls || strncmp(name, p, lp) || strcmp(name + n - ls, s)) return -1;
    for (int k = 0; k < (int)(sizeof k_parts / sizeof k_parts[0]); k++)
        if (strlen(k_parts[k].sec) == n - lp - ls && !strncmp(name + lp, k_parts[k].sec, n - lp - ls)) return k;
    return -1;
}

/* extents of a model's vertices (int16 triples at +0x1c): min and max per axis */
typedef struct { int lo[3], hi[3]; } Extent;
static Extent model_extent(const uint8_t *m) {
    Extent e = { { 0x7fff, 0x7fff, 0x7fff }, { -0x8000, -0x8000, -0x8000 } };
    int n = m[8] << 8 | m[9];
    for (int i = 0; i < n && i < 4096; i++)
        for (int k = 0; k < 3; k++) {
            int v = (int16_t)(m[0x1c + 6 * i + 2 * k] << 8 | m[0x1c + 6 * i + 2 * k + 1]);
            if (v < e.lo[k]) e.lo[k] = v;
            if (v > e.hi[k]) e.hi[k] = v;
        }
    return e;
}

/* A traffic model moved behind the original texture list: its texture index (it has one material:
 * after the vertices, normals and texture coordinates, 6, 6 and 4 bytes each, and the 24-byte
 * material record; header +8 vertices, +10 normals, +12 coordinates, +14 materials) grows by
 * the original's texture count. */
static void shift_texture(uint8_t *m, uint32_t size, uint32_t by) {
    uint32_t at = 0x1c + 6u * (m[8] << 8 | m[9]) + 6u * (m[10] << 8 | m[11]) + 4u * (m[12] << 8 | m[13]) + 24;
    if ((m[14] << 8 | m[15]) != 1 || at + 2 > size) return;
    uint32_t t = (uint32_t)(m[at] << 8 | m[at + 1]) + by;
    m[at] = (uint8_t)(t >> 8); m[at + 1] = (uint8_t)t;
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
    int sec = player_section(name);
    if (sec < 0) return;

    /* the loaded original, copied out */
    Container orig;
    if (!parse_header(&orig, read_guest, (const void *)(uintptr_t)dest, 0x10000) || orig.nmodels > 32) return;
    uint32_t at[32], size[32], orig_len = orig.sizes_at + 4 * orig.nmodels;
    for (uint32_t m = 0; m < orig.nmodels; m++) {
        size[m] = LD32(dest + orig.sizes_at + 4 * m);
        at[m] = orig_len;
        orig_len += size[m];
    }
    if (orig_len > 0x10000) return;
    uint8_t *src = malloc(orig_len);
    if (!src) return;
    for (uint32_t b = 0; b < orig_len; b++) src[b] = (uint8_t)LD8(dest + b);

    /* kept: the floor pan, fitted under the new body (and, to try them, the original dashboard
     * with RT_ALT_DVIEW=1, the original car's bonnet in the driver's view, and the steered tyres
     * with RT_ALT_TIRES=1, at the original car's wheel positions); the rest is the traffic car's
     * body or an empty model, so the driver's view shows the road alone */
    uint32_t keep = 1u << k_parts[sec].pan | (getenv("RT_ALT_DVIEW") ? k_parts[sec].dview : 0) |
                    (getenv("RT_ALT_TIRES") ? k_parts[sec].tires : 0);
    uint32_t body = (uint32_t)k_parts[sec].body;
    Container tc;
    parse_header(&tc, read_host, g_tcar, (uint32_t)g_tcar_len);
    uint32_t car_off, car_size, empty_off, empty_size;
    tcar_model(tcar, &car_off, &car_size);
    tcar_model(TCAR_EMPTY_SRC, &empty_off, &empty_size);
    uint32_t names_len = orig.names_len + tc.names_len;
    uint32_t sizes_at = (8 + names_len + 3) & ~3u, len = sizes_at + 4 * orig.nmodels;
    for (uint32_t m = 0; m < orig.nmodels; m++)
        len += m == body ? car_size : keep >> m & 1 ? size[m] : empty_size;
    if (len > orig_len) {
        rt_log("alt cars: %s: replacement needs %#x bytes, the buffer has %#x\n", name, len, orig_len);
        free(src);
        return;
    }

    uint8_t *out = calloc(1, len);
    if (!out) { free(src); return; }
    put32(out, orig.nmodels);
    put32(out + 4, orig.ntex + tc.ntex);
    memcpy(out + 8, src + 8, orig.names_len);
    memcpy(out + 8 + orig.names_len, g_tcar + 8, tc.names_len);
    Extent car = model_extent(g_tcar + car_off);
    uint32_t o = sizes_at + 4 * orig.nmodels;
    for (uint32_t m = 0; m < orig.nmodels; m++) {
        uint8_t *dst = out + o;
        uint32_t n;
        if (m == body) {
            memcpy(dst, g_tcar + car_off, n = car_size);
            shift_texture(dst, n, orig.ntex);
        } else if (keep >> m & 1) {
            memcpy(dst, src + at[m], n = size[m]);
            if ((int)m == k_parts[sec].pan) {                   /* inside the new footprint */
                Extent e = model_extent(dst);
                float sx = 0.95f * (float)(car.hi[0] - car.lo[0]) / (float)(e.hi[0] - e.lo[0] ? e.hi[0] - e.lo[0] : 1);
                float sz = 0.95f * (float)(car.hi[2] - car.lo[2]) / (float)(e.hi[2] - e.lo[2] ? e.hi[2] - e.lo[2] : 1);
                float cx = 0.5f * (car.hi[0] + car.lo[0]), cz = 0.5f * (car.hi[2] + car.lo[2]);
                float ex = 0.5f * (e.hi[0] + e.lo[0]), ez = 0.5f * (e.hi[2] + e.lo[2]);
                for (int v = 0; v < (dst[8] << 8 | dst[9]); v++) {
                    uint8_t *p = dst + 0x1c + 6 * v;
                    int x = (int16_t)(p[0] << 8 | p[1]), z = (int16_t)(p[4] << 8 | p[5]);
                    int nx = (int)lroundf(cx + (x - ex) * sx), nz = (int)lroundf(cz + (z - ez) * sz);
                    p[0] = (uint8_t)(nx >> 8); p[1] = (uint8_t)nx; p[4] = (uint8_t)(nz >> 8); p[5] = (uint8_t)nz;
                }
            }
        } else {
            memcpy(dst, g_tcar + empty_off, n = empty_size);
            shift_texture(dst, n, orig.ntex);
            uint32_t nverts = (uint32_t)dst[8] << 8 | dst[9];
            if (0x1c + 6 * nverts <= n) memset(dst + 0x1c, 0, 6 * nverts);
        }
        put32(out + sizes_at + 4 * m, n);
        o += n;
    }
    for (uint32_t b = 0; b < len; b++) ST8(dest + b, out[b]);
    free(out);
    free(src);
    rt_log("alt cars: %s -> traffic car %d (%#x of %#x bytes)\n", name, tcar, len, orig_len);
}

/* the race settings word: car in bits 9-11, tuned-up in bit 8 (read at race setup, 0x8e378) */
#define GAME_TOC 0x154da8u
static uint32_t settings_word_ea(void) {
    uint32_t p = LD32(GAME_TOC + 0x54);
    return (p >= 0x100000 && p < RAM_SIZE - 8) ? p + 4 : 0;
}

/* the car select screen is up: its handler ran within the last few frames (it runs at 30 Hz) */
static int in_car_select(void) { return g_select_frame && g_frame - g_select_frame <= 4; }

/* model array and count of a loaded section, 0 if it is not loaded */
static uint32_t section_models(int sec, uint32_t *count) {
    uint32_t base = LD32(GAME_TOC + 0x40);
    if (base < 0x100000 || base >= RAM_SIZE - 48 * 0x24) return 0;
    for (uint32_t k = 0; k < 48; k++) {
        uint32_t e = base + 8 + k * 0x24;
        if (LD32(e) == (uint32_t)sec) {
            *count = LD32(e + 8);
            uint32_t models = LD32(e + 0xc);
            return *count <= 64 && models >= 0x100000 && models < RAM_SIZE - 64 * 16 ? models : 0;
        }
    }
    return 0;
}

static void show_original(void) {
    if (!g_shown) return;
    for (int car = 0; car < N_CARS; car++) {
        uint32_t n, models = section_models(k_cars[car].sec, &n);
        for (uint32_t m = 0; models && m < n; m++)
            for (int b = 0; b < 16; b++) ST8(models + 16 * m + (uint32_t)b, g_saved[car][m][b]);
    }
    g_shown = 0;
}

static void show_alternates(void) {
    uint32_t tn, tcar = section_models(SEC_TCAR, &tn);
    if (g_shown || !tcar || tn < 64) return;
    for (int car = 0; car < N_CARS; car++) {
        uint32_t n, models = section_models(k_cars[car].sec, &n);
        int t = k_cars[car].tcar;
        for (uint32_t m = 0; models && m < n; m++) {
            for (int b = 0; b < 16; b++) g_saved[car][m][b] = (uint8_t)LD8(models + 16 * m + (uint32_t)b);
            int from = (int)m == k_cars[car].body ? t : (int)m == k_cars[car].mid ? 48 + t : (int)m == k_cars[car].far ? 16 + t : -1;
            if (from >= 0)
                for (int b = 0; b < 16; b++) ST8(models + 16 * m + (uint32_t)b, LD8(tcar + 16 * (uint32_t)from + (uint32_t)b));
            else
                STF32(models + 16 * m + 4, -1e30f);           /* culled */
        }
    }
    g_shown = 1;
}

/* Like UP turns every car tuned-up, DOWN turns every car into its alternate: the rivals too,
 * from the car select through the race, until the game leaves play. */
void alt_cars_car_select(PPCContext *c) {
    (void)c;
    if (!g_enhanced || !g_tcar) return;
    g_select_frame = g_frame ? g_frame : 1;
    uint32_t ea = settings_word_ea();
    int car = ea ? (int)(LD32(ea) >> 9 & 7) : 0, down = !(g_in[3] & 0x40);
    int pick = down ? car : -1;
    if (pick != g_pick && getenv("RT_ALT_CAR_LOG"))
        rt_log("alt cars: car select pick %s\n", pick >= 0 ? k_cars[pick].name : "original");
    g_pick = pick;
    if (down) show_alternates();
    else show_original();
}

void alt_cars_on_frame(uint64_t frame) {
    static uint32_t last = 0xffffffffu;
    g_frame = frame;
    uint32_t ea = settings_word_ea();
    uint32_t w = ea ? LD32(ea) : 0;
    if (w != last && getenv("RT_ALT_CAR_LOG")) rt_log("alt cars: frame %llu settings %08x\n", (unsigned long long)frame, w);
    last = w;
    if ((w >> 24) < 0xa3) { g_pick = -1; show_original(); }   /* out of play: attract, coin-up */
}
