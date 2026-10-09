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
 * (bodyA), the dark underside that reads as the car's shadow, is kept and fitted to the new body
 * (fit_pan); every other part becomes a small traffic model with all its vertices at the origin,
 * so it draws nothing, the driver's view included. The new container must fit in the original's
 * size, the buffer the game allocated.
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
 * (R1 main body, R2 and R3 lower details), its floor pan fitted to the alternate (an extra model
 * appended to the section when it loads at boot, which the game itself never draws), and a
 * negative radius, culled, for every other part.
 * They stay so from the car select through the race and come back when the lever leaves DOWN at
 * a car select or the game leaves play.
 *
 * The game module's TOC is the second word of its header, loaded at 0x38040 (0x154da8 in ver JAB,
 * 0x154dc8 in ver EAA, whose code is otherwise 0x24 bytes later here and whose model data is the
 * same).
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
 * alternate, a traffic car (TCAR_carNNa; the log name), and its R<car> section: name, id, model
 * count, the indices of the R1 main body, the first R2 and R3 bodies and the R1 floor pan */
static const struct { int tcar; const char *name, *rsec; int sec, count, body, mid, far, pan; } k_cars[] = {
    { 1, "FIAT PANDA", "Rmini", 25, 27, 5, 24, 26, 3 },     { 15, "CITROEN 2CV", "Rfiat", 22, 27, 5, 24, 26, 3 },
    { 11, "BEETLE", "Rsuper7", 26, 36, 6, 28, 34, 5 },      { 2, "LANCIA", "Rcobra", 20, 27, 4, 24, 26, 3 },
    { 7, "TAXI", "Rjuguar", 24, 43, 4, 40, 42, 3 },         { 3, "VAN", "Rwagen", 27, 27, 5, 24, 26, 3 },
    { 5, "VOLVO ESTATE", "Rgtv", 23, 27, 5, 24, 26, 3 },    { 8, "RED CAR", "Rferr", 21, 29, 7, 27, 28, 5 },
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

/* The player car sections' parts (indices = part names in alphabetical order): the main body
 * and the floor pan (bodyA: the dark underside that reads as the car's shadow) */
static const struct { const char *sec; int body, pan; } k_parts[] = {
    { "Pmini", 3, 1 },   { "TPmini", 3, 1 },   { "Pfiat", 3, 1 },   { "TPfiat", 3, 1 },
    { "Psuper7", 6, 5 }, { "TPsuper7", 6, 5 }, { "Pcobra", 2, 1 },  { "TPcobra", 2, 1 },
    { "Pjuguar", 2, 1 }, { "TPjuguar", 2, 1 }, { "Pwagen", 5, 3 },  { "TPwagen", 3, 1 },
    { "Pgtv", 3, 1 },    { "TPgtv", 3, 1 },    { "Pferr", 5, 3 },   { "TPferr", 5, 3 },
};

/* is name game/mdldata/<sec>_mdl.zin */
static int is_section_file(const char *name, const char *sec) {
    char want[64];
    snprintf(want, sizeof want, "game/mdldata/%s_mdl.zin", sec);
    return !strcmp(name, want);
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

/* The floor pan, fitted to a new body: placed and sized relative to the new body as it was to the
 * original body (width and length; its height is kept). */
static void fit_pan(uint8_t *pan, const uint8_t *orig_body, const uint8_t *new_body) {
    Extent ob = model_extent(orig_body), nb = model_extent(new_body);
    float s[3], oc[3], nc[3];
    for (int k = 0; k < 3; k += 2) {
        s[k] = (float)(nb.hi[k] - nb.lo[k]) / (float)(ob.hi[k] - ob.lo[k] ? ob.hi[k] - ob.lo[k] : 1);
        oc[k] = 0.5f * (ob.hi[k] + ob.lo[k]);
        nc[k] = 0.5f * (nb.hi[k] + nb.lo[k]);
    }
    for (int v = 0; v < (pan[8] << 8 | pan[9]); v++)
        for (int k = 0; k < 3; k += 2) {
            uint8_t *p = pan + 0x1c + 6 * v + 2 * k;
            int n = (int)lroundf(nc[k] + ((int16_t)(p[0] << 8 | p[1]) - oc[k]) * s[k]);
            p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n;
        }
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

/* a loaded container copied out of the guest: its header, bytes and each model's offset and size */
typedef struct { Container k; uint8_t *bytes; uint32_t len, at[64], size[64]; } Loaded;
static int load_container(Loaded *l, uint32_t ea) {
    if (!parse_header(&l->k, read_guest, (const void *)(uintptr_t)ea, 0x10000) || l->k.nmodels > 64) return 0;
    l->len = l->k.sizes_at + 4 * l->k.nmodels;
    for (uint32_t m = 0; m < l->k.nmodels; m++) {
        l->size[m] = LD32(ea + l->k.sizes_at + 4 * m);
        l->at[m] = l->len;
        l->len += l->size[m];
    }
    if (l->len > 0x10000 || !(l->bytes = malloc(l->len))) return 0;
    for (uint32_t b = 0; b < l->len; b++) l->bytes[b] = (uint8_t)LD8(ea + b);
    return 1;
}

/* A rival section, at boot: one more model, its R1 floor pan fitted to the alternate's body, for
 * show_alternates to put in the pan's place. The staging buffer holds far larger files (CAB). */
static void add_rival_pan(int car, uint32_t dest) {
    Loaded l;
    if (!load_container(&l, dest)) return;
    if ((int)l.k.nmodels != k_cars[car].count) { free(l.bytes); return; }
    uint32_t car_off, car_size, pan = (uint32_t)k_cars[car].pan, body = (uint32_t)k_cars[car].body;
    tcar_model(k_cars[car].tcar, &car_off, &car_size);
    uint32_t n = l.k.nmodels, models = l.k.sizes_at + 4 * n, len = l.len + 4 + l.size[pan];
    uint8_t *out = malloc(len);
    if (!out) { free(l.bytes); return; }
    memcpy(out, l.bytes, l.k.sizes_at + 4 * n);               /* header, names, sizes */
    put32(out, n + 1);
    put32(out + l.k.sizes_at + 4 * n, l.size[pan]);
    memcpy(out + models + 4, l.bytes + models, l.len - models);
    uint8_t *extra = out + l.len + 4;
    memcpy(extra, l.bytes + l.at[pan], l.size[pan]);
    fit_pan(extra, l.bytes + l.at[body], g_tcar + car_off);
    for (uint32_t b = 0; b < len; b++) ST8(dest + b, out[b]);
    free(out);
    free(l.bytes);
}

/* The player's section, at the start of a race: see the top of the file. */
static void swap_player(int sec, int tcar, uint32_t dest, const char *name) {
    Loaded l;
    if (!load_container(&l, dest)) return;
    uint32_t body = (uint32_t)k_parts[sec].body, pan = (uint32_t)k_parts[sec].pan, n = l.k.nmodels;
    Container tc;
    parse_header(&tc, read_host, g_tcar, (uint32_t)g_tcar_len);
    uint32_t car_off, car_size, empty_off, empty_size;
    tcar_model(tcar, &car_off, &car_size);
    tcar_model(TCAR_EMPTY_SRC, &empty_off, &empty_size);
    uint32_t sizes_at = (8 + l.k.names_len + tc.names_len + 3) & ~3u, len = sizes_at + 4 * n;
    for (uint32_t m = 0; m < n; m++) len += m == body ? car_size : m == pan ? l.size[m] : empty_size;
    if (len > l.len) {
        rt_log("alt cars: %s: replacement needs %#x bytes, the buffer has %#x\n", name, len, l.len);
        free(l.bytes);
        return;
    }
    uint8_t *out = calloc(1, len);
    if (!out) { free(l.bytes); return; }
    put32(out, n);
    put32(out + 4, l.k.ntex + tc.ntex);
    memcpy(out + 8, l.bytes + 8, l.k.names_len);
    memcpy(out + 8 + l.k.names_len, g_tcar + 8, tc.names_len);
    uint32_t o = sizes_at + 4 * n;
    for (uint32_t m = 0; m < n; m++) {
        uint8_t *dst = out + o;
        uint32_t size;
        if (m == body) {
            memcpy(dst, g_tcar + car_off, size = car_size);
            shift_texture(dst, size, l.k.ntex);
        } else if (m == pan) {
            memcpy(dst, l.bytes + l.at[m], size = l.size[m]);
            fit_pan(dst, l.bytes + l.at[body], g_tcar + car_off);
        } else {
            memcpy(dst, g_tcar + empty_off, size = empty_size);
            shift_texture(dst, size, l.k.ntex);
            uint32_t nverts = (uint32_t)dst[8] << 8 | dst[9];
            if (0x1c + 6 * nverts <= size) memset(dst + 0x1c, 0, 6 * nverts);
        }
        put32(out + sizes_at + 4 * m, size);
        o += size;
    }
    for (uint32_t b = 0; b < len; b++) ST8(dest + b, out[b]);
    free(out);
    free(l.bytes);
    rt_log("alt cars: %s -> traffic car %d (%#x of %#x bytes)\n", name, tcar, len, l.len);
}

void alt_cars_file_loaded(PPCContext *c) {
    if (!g_enhanced || !g_tcar || c->r[3] != 0) return;
    uint32_t name_ea = LD32(c->r[26] + 4), dest = LD32(c->r[26] + 0x10);
    char name[64];
    size_t i = 0;
    for (; i < sizeof name - 1 && (name[i] = (char)LD8(name_ea + (uint32_t)i)); i++) {}
    name[i] = 0;
    if (getenv("RT_ALT_CAR_LOG")) rt_log("alt cars: loaded %s at %#x\n", name, dest);  /* every queued read */
    for (int car = 0; car < N_CARS; car++)
        if (is_section_file(name, k_cars[car].rsec)) { add_rival_pan(car, dest); return; }
    int tcar = alt_tcar();
    for (int sec = 0; tcar >= 0 && sec < (int)(sizeof k_parts / sizeof k_parts[0]); sec++)
        if (is_section_file(name, k_parts[sec].sec)) { swap_player(sec, tcar, dest, name); return; }
}

/* the race settings word: car in bits 9-11, tuned-up in bit 8 (read at race setup, 0x8e378) */
#define GAME_TOC LD32(0x38044u)       /* the game module's header: entry, TOC */
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
            else if ((int)m == k_cars[car].pan && (int)n == k_cars[car].count + 1)   /* the fitted pan */
                for (int b = 0; b < 16; b++) ST8(models + 16 * m + (uint32_t)b, LD8(models + 16 * (n - 1) + (uint32_t)b));
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
