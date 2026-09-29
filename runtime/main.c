/*
 * Konami Viper recompiled game - entry point.
 *
 * Does what BIOS 941B01 does before handing over: the kernel (decompressed
 * boot block of the game file on the CF card) is placed at RAM 0 and entered
 * at 0x10 with MSR = 0x2070 (FP, IP, IR, DR).
 */
#include "runtime.h"
#include "modules.h"
#include "game_config.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

uint8_t *g_ram;
static int g_verbose;
static double g_run_seconds = 0;
static FILE *g_wav;
static uint32_t g_wav_bytes;

int rt_verbose(void) { return g_verbose; }

void rt_log(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%9.4f ", (double)rt_now() / CPU_HZ);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void voodoo_stats(void);

static void wav_close(void) {
    if (!g_wav) return;
    uint32_t riff = 36 + g_wav_bytes;
    fseek(g_wav, 4, SEEK_SET); fwrite(&riff, 4, 1, g_wav);
    fseek(g_wav, 40, SEEK_SET); fwrite(&g_wav_bytes, 4, 1, g_wav);
    fclose(g_wav);
    g_wav = NULL;
}

static uint8_t *g_kernel_img;
static size_t g_kernel_len;

static void diff_kernel_text(void) {
    if (!g_kernel_img || g_kernel_len < 0x1c) return;
    uint32_t end = bswap32(*(uint32_t *)(g_kernel_img + 0x18));   /* text end, as read by recomp.py */
    if (end > g_kernel_len) end = (uint32_t)g_kernel_len;
    int shown = 0;
    for (uint32_t a = 0x10; a + 4 <= end && shown < 40; a += 4) {
        if (memcmp(g_ram + a, g_kernel_img + a, 4)) {
            uint32_t s = a;
            while (a + 4 <= end && memcmp(g_ram + a, g_kernel_img + a, 4)) a += 4;
            rt_log("  kernel text modified %08x-%08x: %08x -> %08x\n", s, a, bswap32(*(uint32_t *)(g_kernel_img + s)),
                   bswap32(*(uint32_t *)(g_ram + s)));
            shown++;
        }
    }
}

static const char *g_dump_ram;

void rt_fatal(const char *why) {
    if (!strcmp(why, "window closed")) { hw_shutdown(); wav_close(); fflush(stderr); _exit(0); }
    rt_log("STOP: %s\n", why);
    if (g_dump_ram) { FILE *f = fopen(g_dump_ram, "wb"); if (f) { fwrite(g_ram, 1, RAM_SIZE, f); fclose(f); } }
    diff_kernel_text();
    rt_dump_state();
    voodoo_stats();
    extern void epic_dump(void);
    epic_dump();
    hw_shutdown();
    wav_close();
    fflush(stderr);
    exit(strcmp(why, "time limit") ? 1 : 0);
}

/* Audio: each IRQ3 block is 0x800 bytes = 256 stereo frames of 32-bit words. For now
 * the blocks are dumped to a WAV file (16-bit, upper half of each word). */
void audio_push_block(const uint8_t *blk) {
    audio_frontend_push(blk);
    if (!g_wav) return;
    for (int i = 0; i < 512; i++) {
        uint32_t w = ((uint32_t)blk[4 * i] << 24) | ((uint32_t)blk[4 * i + 1] << 16) | ((uint32_t)blk[4 * i + 2] << 8) | blk[4 * i + 3];
        int16_t s = (int16_t)(w >> 16);
        fwrite(&s, 2, 1, g_wav);
        g_wav_bytes += 2;
    }
}

static void wav_open(const char *path) {
    g_wav = fopen(path, "wb");
    if (!g_wav) return;
    static const uint8_t hdr[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,1,0,2,0,
                                    0x44,0xac,0,0,0x10,0xb1,2,0,4,0,16,0,'d','a','t','a',0,0,0,0};
    fwrite(hdr, 1, 44, g_wav);
}

static const char *g_frames_dir;
static int g_frame_every = 30;

void rt_frame_published(uint64_t cnt, const uint32_t *buf, int w, int h) {
    static int fps_stats = -1;
    static uint64_t last_hash, uniq, last_sec;
    if (fps_stats < 0) fps_stats = getenv("RT_FPS_STATS") != NULL;
    if (fps_stats) {
        uint64_t hsh = 1469598103934665603ull;
        for (int i = 0; i < w * h; i += 7) hsh = (hsh ^ buf[i]) * 1099511628211ull;
        if (hsh != last_hash) uniq++;
        last_hash = hsh;
        uint64_t sec = (uint64_t)((double)rt_now() / CPU_HZ);
        if (sec != last_sec) { rt_log("fps: %llu distinct frames in second %llu\n", (unsigned long long)uniq, (unsigned long long)last_sec); uniq = 0; last_sec = sec; }
    }
    if (!g_frames_dir || cnt % (uint64_t)g_frame_every) return;
    char path[1024];
    snprintf(path, sizeof path, "%s/frame_%06llu.ppm", g_frames_dir, (unsigned long long)cnt);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fprintf(f, "P6\n%d %d\n255\n", w, h);
    for (int i = 0; i < w * h; i++) {
        uint8_t px[3] = {(uint8_t)(buf[i] >> 16), (uint8_t)(buf[i] >> 8), (uint8_t)buf[i]};
        fwrite(px, 1, 3, f);
    }
    fclose(f);
}

static void dump_frames_loop(void) { for (;;) pause(); }

/* scripted input for tests: RT_INPUT="20.0:3=fb,20.2:3=ff" (seconds:port=hex);
 * ports 0-7 = digital IN0-7, 10-13 = analog positions AN0-3 (16-bit two's complement, -255..255) */
extern uint8_t g_in[8];
extern int16_t g_analog[4];
typedef struct { double t; int port; uint16_t v; } InEv;
static InEv g_inev[200];
static void inev_fire(void *arg) {
    InEv *e = (InEv *)arg;
    if (e->port >= 10) g_analog[(e->port - 10) & 3] = (int16_t)e->v;
    else g_in[e->port & 7] = (uint8_t)e->v;
    rt_log("input: %s%d = %02x\n", e->port >= 10 ? "AN" : "IN", e->port >= 10 ? e->port - 10 : e->port, e->v);
}
static void inev_setup(void) {
    const char *s = getenv("RT_INPUT");
    int n = 0;
    while (s && *s && n < 200) {
        double t; int port; unsigned v; int used = 0;
        if (sscanf(s, "%lf:%d=%x%n", &t, &port, &v, &used) != 3) break;
        g_inev[n] = (InEv){t, port, (uint16_t)v};
        rt_sched_at((uint64_t)(t * CPU_HZ), inev_fire, &g_inev[n]);
        n++;
        s += used;
        while (*s == ',') s++;
    }
}

/* First-run calibration: drives the game's own TEST MODE -> CALIBRATION with scripted
 * inputs (steering centre/left/right, accelerator and brake rest/full), then SAVE AND EXIT.
 * The resulting NVRAM matches the frontend's analog ranges exactly. The script is per game
 * (games/<id>/game.json, "calibration"); NULL means the game has none yet. */
static const char *k_calibration_script = GAME_CALIBRATION_SCRIPT;

static int file_exists(const char *p) { FILE *f = fopen(p, "rb"); if (f) fclose(f); return f != NULL; }

static int run_first_time_calibration(const char *self, const char *work, const char *nvram, const char *nvsave) {
    if (!k_calibration_script) return 0;
    fprintf(stderr, "first run: calibrating steering and pedals in TEST MODE (a few seconds)...\n");
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "RT_INPUT='%s' '%s' --headless --work '%s' --nvram '%s' --nvram-save '%s' --seconds %d >/dev/null 2>&1",
             k_calibration_script, self, work, nvram, nvsave, GAME_CALIBRATION_SECONDS);
    int rc = system(cmd);
    if (rc != 0 || !file_exists(nvsave)) { fprintf(stderr, "calibration failed (rc=%d)\n", rc); return -1; }
    fprintf(stderr, "calibration saved to %s\n", nvsave);
    return 0;
}

static void stop_event(void *arg) { (void)arg; rt_fatal("time limit"); }
static void on_sigint(int s) { (void)s; rt_fatal("interrupted"); }

static const RtModuleInfo *const k_modules[] = { RT_ALL_MODULES };

static const char *g_argv0 = "";

static void usage(void) {
    fprintf(stderr,
            GAME_TITLE ", recompiled\n"
            "usage: %s [options]\n"
            "  --work DIR      extracted data (kernel.bin), default: " GAME_DEFAULT_WORK "\n"
            "  --cf FILE       raw CF image, default: DIR/cf.img\n"
            "  --nvram FILE    M48T58 dump, default: " GAME_DEFAULT_NVRAM "\n"
            "  --ds2430 FILE   DS2430A dump, default: " GAME_DEFAULT_DS2430 "\n"
            "  --bios FILE     BIOS (optional), default: " GAME_DEFAULT_BIOS "\n"
            "  --seconds N     stop after N seconds of emulated time\n"
            "  --wav FILE      dump game audio\n"
            "  --headless      no window/audio (tests); runs as fast as possible\n"
            "  --scale N       window scale (default 2)\n"
            "  --volume N      audio gain (default 16)\n"
            "  --nvram-save F  persistent NVRAM file (default " GAME_NVRAM_SAVE ")\n"
            "  --frames DIR    dump every Nth video frame as PPM into DIR (headless)\n"
            "  --frame-every N (default 30)\n"
            "  -v              verbose\n", g_argv0);
    exit(2);
}

int main(int argc, char **argv) {
    const char *work = GAME_DEFAULT_WORK, *cf = NULL, *nvram = GAME_DEFAULT_NVRAM, *ds = GAME_DEFAULT_DS2430,
               *bios = GAME_DEFAULT_BIOS, *wav = NULL, *nvsave = GAME_NVRAM_SAVE;
    g_argv0 = argv[0];
    int headless = 0, scale = 2, nvsave_explicit = 0;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!strcmp(a, "--work") && v) { work = v; i++; }
        else if (!strcmp(a, "--cf") && v) { cf = v; i++; }
        else if (!strcmp(a, "--nvram") && v) { nvram = v; i++; }
        else if (!strcmp(a, "--ds2430") && v) { ds = v; i++; }
        else if (!strcmp(a, "--bios") && v) { bios = v; i++; }
        else if (!strcmp(a, "--seconds") && v) { g_run_seconds = atof(v); i++; }
        else if (!strcmp(a, "--wav") && v) { wav = v; i++; }
        else if (!strcmp(a, "--headless")) headless = 1;
        else if (!strcmp(a, "--volume") && v) { extern int g_audio_gain; g_audio_gain = atoi(v); i++; }
        else if (!strcmp(a, "--scale") && v) { scale = atoi(v); i++; }
        else if (!strcmp(a, "--nvram-save") && v) { nvsave = v; nvsave_explicit = 1; i++; }
        else if (!strcmp(a, "--dump-ram") && v) { g_dump_ram = v; i++; }
        else if (!strcmp(a, "--frames") && v) { g_frames_dir = v; i++; }
        else if (!strcmp(a, "--frame-every") && v) { g_frame_every = atoi(v); i++; }
        else if (!strcmp(a, "-v")) g_verbose = 1;
        else usage();
    }
    if (!headless && !file_exists(nvsave)) run_first_time_calibration(argv[0], work, nvram, nvsave);
    static char cfbuf[1024], kbuf[1024];
    if (!cf) { snprintf(cfbuf, sizeof cfbuf, "%s/cf.img", work); cf = cfbuf; }
    snprintf(kbuf, sizeof kbuf, "%s/kernel.bin", work);

    g_ram = (uint8_t *)calloc(1, RAM_SIZE);
    FILE *f = fopen(kbuf, "rb");
    if (!f) { fprintf(stderr, "cannot open %s (run 'make extract GAME=" GAME_ID "' first)\n", kbuf); return 1; }
    size_t n = fread(g_ram, 1, RAM_SIZE, f);
    fclose(f);
    g_kernel_img = (uint8_t *)malloc(n);
    memcpy(g_kernel_img, g_ram, n);
    g_kernel_len = n;

    signal(SIGINT, on_sigint);
    if (wav) wav_open(wav);
    for (size_t i = 0; i < sizeof k_modules / sizeof k_modules[0]; i++) rt_register_module(k_modules[i]);
    rt_log("kernel: %zu bytes at 0x00000000\n", n);

    extern void rt_bp_init(void);
    rt_bp_init();
    HwConfig cfg = { cf, nvram, ds, bios, (headless && !nvsave_explicit) ? NULL : nvsave };
    hw_init(&cfg);
    inev_setup();
    if (g_run_seconds > 0) rt_sched_at((uint64_t)(g_run_seconds * CPU_HZ), stop_event, NULL);

    /* CPU state as BIOS 941B01 stage 2 leaves it (verified against MAME at kernel entry 0x10):
     * every GPR/SPRG/LR/CR = 0xdeadbeef, FPRs = 0x7ff5beef4afc0721, CTR = entry, MSR = 0x2070,
     * r31 = boot parameter word (0x63ffffff on a normal boot; the kernel saves it at 0xfc and the
     * game derives the boot-time switch state from it, e.g. "TEST held -> initialise RTC"). */
    PPCContext *c = &g_ctx;
    for (int i = 0; i < 32; i++) c->r[i] = 0xdeadbeefu;
    for (int i = 0; i < 32; i++) c->f[i] = BITS_FPR(0x7ff5beef4afc0721ull);
    for (int i = 0; i < 4; i++) c->sprg[i] = 0xdeadbeefu;
    c->r[31] = 0x63ffffffu;
    c->lr = 0xdeadbeefu;
    rt_cr_unpack(c, 0xdeadbeefu, 0xff);
    c->ctr = 0x10;
    c->msr = 0x2070;
    rt_check(c, 0x10);          /* arms the first time slice */
    rt_start(0x10);
    if (headless) dump_frames_loop();   /* guest runs on fibers; rt_fatal() exits the process */
    frontend_run(scale);
    rt_fatal("window closed");
}
