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
 * The loaded container is replaced by one of the same model count with the traffic cars' texture
 * list (TCAR_mdl.zin, whose textures stay in VRAM for the traffic) before its own, so traffic
 * models keep their texture indices: the main body becomes traffic car N; the floor pan
 * (bodyA), the dark underside that reads as the car's shadow, becomes the Mini's, a near box,
 * stretched to a rectangle under the new body (rect_pan); every other part becomes a small traffic model with all its vertices at the origin,
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
 * (R1 main body, R2 and R3 lower details), the rectangle shadow of the alternate (an extra model
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
    { 8, "RED CAR", "Rgtv", 23, 27, 5, 24, 26, 3 },         { 5, "VOLVO ESTATE", "Rferr", 21, 29, 7, 27, 28, 5 },
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

/* an extracted CF file, by its path (fs/_unk/<name hash>.bin), malloc'd */
static uint8_t *read_cf_file(const char *work, const char *name, size_t *len) {
    char path[1024];
    snprintf(path, sizeof path, "%s/fs/_unk/%08x.bin", work, name_hash(name));
    FILE *f = fopen(path, "rb");
    if (!f) { rt_log("alt cars: cannot read %s (%s)\n", path, name); return NULL; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *b = n > 0 ? malloc((size_t)n) : NULL;
    if (b && fread(b, 1, (size_t)n, f) != (size_t)n) { free(b); b = NULL; }
    fclose(f);
    *len = (size_t)n;
    return b;
}

/* the shadow template: the Mini's floor pan (Rmini model 3, R1mini_bodyA), nearly a box, and the
 * name of its texture */
#define PAN_SRC_SECTION "game/mdldata/Rmini_mdl.zin"
#define PAN_SRC_MODEL 3
static uint8_t *g_pan;
static uint32_t g_pan_size;
static char g_pan_tex[32];
static uint32_t texture_at(const uint8_t *m, uint32_t size);

static int load_pan_template(const char *work) {
    size_t len;
    uint8_t *f = read_cf_file(work, PAN_SRC_SECTION, &len);
    Container k;
    int ok = f && parse_header(&k, read_host, f, (uint32_t)len) && k.nmodels > PAN_SRC_MODEL;
    if (ok) {
        uint32_t o = k.sizes_at + 4 * k.nmodels;
        for (int j = 0; j < PAN_SRC_MODEL; j++) o += be32(f + k.sizes_at + 4 * j);
        g_pan_size = be32(f + k.sizes_at + 4 * PAN_SRC_MODEL);
        uint32_t t = o + g_pan_size <= len ? texture_at(f + o, g_pan_size) : 0;
        if ((ok = t != 0)) {
            uint32_t idx = (uint32_t)(f[o + t] << 8 | f[o + t + 1]), p = 8;
            for (uint32_t n = 0; n < idx && n < k.ntex; n++) p += (uint32_t)strlen((const char *)f + p) + 1;
            snprintf(g_pan_tex, sizeof g_pan_tex, "%s", (const char *)f + p);
            g_pan = malloc(g_pan_size);
            if ((ok = g_pan != NULL)) memcpy(g_pan, f + o, g_pan_size);
        }
    }
    free(f);
    return ok;
}

void alt_cars_init(const char *work) {
    size_t len;
    g_tcar = read_cf_file(work, "game/mdldata/TCAR_mdl.zin", &len);
    if (!g_tcar) return;
    g_tcar_len = len;
    Container k;
    if (!parse_header(&k, read_host, g_tcar, (uint32_t)len) || k.nmodels <= TCAR_EMPTY_SRC || !load_pan_template(work)) {
        rt_log("alt cars: unexpected TCAR_mdl.zin or shadow template\n");
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

/* offset of a single-material model's texture index: after the vertices and normals (6 bytes
 * each; header +8, +10; padded to 4), the texture coordinates (4 bytes each; +12) and the 24-byte
 * material record (+14: material count); 0 if the model has another number of materials */
static uint32_t texture_at(const uint8_t *m, uint32_t size) {
    uint32_t at = ((0x1c + 6u * (m[8] << 8 | m[9]) + 6u * (m[10] << 8 | m[11]) + 3) & ~3u) + 4u * (m[12] << 8 | m[13]) + 24;
    return (m[14] << 8 | m[15]) == 1 && at + 2 <= size ? at : 0;
}
static void set_texture(uint8_t *m, uint32_t size, uint32_t t) {
    uint32_t at = texture_at(m, size);
    if (at) { m[at] = (uint8_t)(t >> 8); m[at + 1] = (uint8_t)t; }
}

/* The shadow: the template pan stretched to a rectangle under a new body, its full width and 92%
 * of its length, centred (the Mini's own pan to body proportions); its height is kept. */
static void rect_pan(uint8_t *pan, const uint8_t *body) {
    Extent te = model_extent(pan), nb = model_extent(body);
    float cz = 0.5f * (nb.hi[2] + nb.lo[2]), half = 0.46f * (float)(nb.hi[2] - nb.lo[2]);
    float lo[3] = { (float)nb.lo[0], 0, cz - half }, hi[3] = { (float)nb.hi[0], 0, cz + half };
    for (int v = 0; v < (pan[8] << 8 | pan[9]); v++)
        for (int k = 0; k < 3; k += 2) {
            uint8_t *p = pan + 0x1c + 6 * v + 2 * k;
            float u = ((int16_t)(p[0] << 8 | p[1]) - te.lo[k]) / (float)(te.hi[k] - te.lo[k] ? te.hi[k] - te.lo[k] : 1);
            int n = (int)lroundf(lo[k] + u * (hi[k] - lo[k]));
            p[0] = (uint8_t)(n >> 8); p[1] = (uint8_t)n;
        }
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

/* index of a texture name in a NUL-separated list of n names, -1 if absent */
static int texture_index(const uint8_t *names, uint32_t n, const char *want) {
    for (uint32_t t = 0, p = 0; t < n; t++, p += (uint32_t)strlen((const char *)names + p) + 1)
        if (!strcmp((const char *)names + p, want)) return (int)t;
    return -1;
}

/* writes a container (texture names, models) to the guest at dest if it fits in limit bytes */
static uint32_t emit_container(uint32_t dest, uint32_t limit, const uint8_t *names, uint32_t names_len,
                               uint32_t ntex, uint32_t n, uint8_t *const *models, const uint32_t *sizes) {
    uint32_t sizes_at = (8 + names_len + 3) & ~3u, len = sizes_at + 4 * n;
    for (uint32_t m = 0; m < n; m++) len += sizes[m];
    if (len > limit) return 0;
    uint8_t *out = calloc(1, len);
    if (!out) return 0;
    put32(out, n);
    put32(out + 4, ntex);
    memcpy(out + 8, names, names_len);
    uint32_t o = sizes_at + 4 * n;
    for (uint32_t m = 0; m < n; m++) {
        put32(out + sizes_at + 4 * m, sizes[m]);
        memcpy(out + o, models[m], sizes[m]);
        o += sizes[m];
    }
    for (uint32_t b = 0; b < len; b++) ST8(dest + b, out[b]);
    free(out);
    return len;
}

/* a texture list with the shadow template's texture, appended if missing: the names, their length
 * and count, and the template's index in it */
typedef struct { uint8_t names[1024]; uint32_t len, n, pan_tex; } Names;
static int names_with_pan(Names *t, const uint8_t *a, uint32_t alen, uint32_t an, const uint8_t *b, uint32_t blen, uint32_t bn) {
    if (alen + blen + sizeof g_pan_tex > sizeof t->names) return 0;
    memcpy(t->names, a, alen);
    memcpy(t->names + alen, b, blen);
    t->len = alen + blen;
    t->n = an + bn;
    int i = texture_index(t->names, t->n, g_pan_tex);
    if (i < 0) {
        memcpy(t->names + t->len, g_pan_tex, strlen(g_pan_tex) + 1);
        t->len += (uint32_t)strlen(g_pan_tex) + 1;
        i = (int)t->n++;
    }
    t->pan_tex = (uint32_t)i;
    return 1;
}

/* A rival section, at boot: one more model, the shadow under the alternate's body, for
 * show_alternates to put in the floor pan's place. The staging buffer holds far larger files. */
static void add_rival_pan(int car, uint32_t dest) {
    Loaded l;
    if (!load_container(&l, dest)) return;
    Names t;
    uint32_t n = l.k.nmodels, car_off, car_size;
    if ((int)n != k_cars[car].count || !names_with_pan(&t, l.bytes + 8, l.k.names_len, l.k.ntex, NULL, 0, 0)) { free(l.bytes); return; }
    tcar_model(k_cars[car].tcar, &car_off, &car_size);
    uint8_t *models[65], *pan = malloc(g_pan_size);
    uint32_t sizes[65];
    if (!pan) { free(l.bytes); return; }
    memcpy(pan, g_pan, g_pan_size);
    rect_pan(pan, g_tcar + car_off);
    set_texture(pan, g_pan_size, t.pan_tex);
    for (uint32_t m = 0; m < n; m++) { models[m] = l.bytes + l.at[m]; sizes[m] = l.size[m]; }
    models[n] = pan;
    sizes[n] = g_pan_size;
    emit_container(dest, 0x40000, t.names, t.len, t.n, n + 1, models, sizes);
    free(pan);
    free(l.bytes);
}

/* The player's section, at the start of a race: see the top of the file. */
static void swap_player(int sec, int tcar, uint32_t dest, const char *name) {
    Loaded l;
    if (!load_container(&l, dest)) return;
    Container tc;
    parse_header(&tc, read_host, g_tcar, (uint32_t)g_tcar_len);
    Names t;
    if (!names_with_pan(&t, g_tcar + 8, tc.names_len, tc.ntex, l.bytes + 8, l.k.names_len, l.k.ntex)) { free(l.bytes); return; }
    uint32_t car_off, car_size, empty_off, empty_size, n = l.k.nmodels;
    tcar_model(tcar, &car_off, &car_size);
    tcar_model(TCAR_EMPTY_SRC, &empty_off, &empty_size);
    uint8_t *models[64], *body = malloc(car_size), *pan = malloc(g_pan_size), *empty = malloc(empty_size);
    uint32_t sizes[64];
    if (body && pan && empty) {
        memcpy(body, g_tcar + car_off, car_size);
        memcpy(pan, g_pan, g_pan_size);
        rect_pan(pan, body);
        set_texture(pan, g_pan_size, t.pan_tex);
        memcpy(empty, g_tcar + empty_off, empty_size);
        uint32_t nverts = (uint32_t)empty[8] << 8 | empty[9];
        if (0x1c + 6 * nverts <= empty_size) memset(empty + 0x1c, 0, 6 * nverts);
        for (uint32_t m = 0; m < n; m++) {
            int is_body = (int)m == k_parts[sec].body, is_pan = (int)m == k_parts[sec].pan;
            models[m] = is_body ? body : is_pan ? pan : empty;
            sizes[m] = is_body ? car_size : is_pan ? g_pan_size : empty_size;
        }
        uint32_t len = emit_container(dest, l.len, t.names, t.len, t.n, n, models, sizes);
        if (len) rt_log("alt cars: %s -> traffic car %d (%#x of %#x bytes)\n", name, tcar, len, l.len);
        else rt_log("alt cars: %s: the replacement does not fit in %#x bytes\n", name, l.len);
    }
    free(body); free(pan); free(empty);
    free(l.bytes);
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
