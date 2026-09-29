/*
 * SDL2 frontend: window, real-time pacing, audio, keyboard/gamepad input.
 *
 * Threads: the host main thread runs SDL (events, presentation); the guest runs on
 * runtime fibers.  Shared state is small: input bytes/words (single writer), the video
 * frame (mutex inside the voodoo bridge) and a lock-free audio ring buffer.
 *
 * Controls (driving-game inputs as in MAME's viper.cpp "thrild2" / "gticlub2"):
 *   Left/Right  steering      Up  gas      Down  brake      Space  handbrake (GTI Club 2)
 *   A  shift up   Z  shift down   5  coin   1  start   F2  test   9  service
 *   Gamepad: left stick = steering, R2/L2 = gas/brake, R1/L1 = shift up/down,
 *            X = handbrake, Start = start, Back = coin
 *   F11 fullscreen, Esc quit
 */
#include "runtime.h"
#include "game_config.h"
#include <SDL.h>
#include <stdatomic.h>
#include <stdlib.h>

extern uint8_t g_in[8];
extern int16_t g_analog[4];
#define ANALOG_RANGE 200     /* keep clear of ADC saturation; matches tools/calibrate.sh */

/* ------------------------------------------------------------------ audio ring (SPSC) */
#define AUDIO_RING (1 << 15)              /* stereo frames */
static int16_t g_ring[AUDIO_RING][2];
static atomic_uint g_ring_w, g_ring_r;
static SDL_AudioDeviceID g_audio;
int g_audio_gain = 16;                    /* samples peak ~1.5% FS; the cabinet has a power amp */

static int16_t sat16(int64_t v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }
static int g_frontend_active;

void audio_frontend_push(const uint8_t *blk) {
    if (!g_audio) return;
    unsigned w = atomic_load_explicit(&g_ring_w, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&g_ring_r, memory_order_acquire);
    for (int i = 0; i < 256; i++) {
        if (w - r >= AUDIO_RING - 1) break;           /* full: drop */
        const uint8_t *p = blk + 8 * i;
        int32_t l = (int32_t)(((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]);
        int32_t rr = (int32_t)(((uint32_t)p[4] << 24) | ((uint32_t)p[5] << 16) | ((uint32_t)p[6] << 8) | p[7]);
        g_ring[w % AUDIO_RING][0] = sat16(((int64_t)l * g_audio_gain) >> 16);
        g_ring[w % AUDIO_RING][1] = sat16(((int64_t)rr * g_audio_gain) >> 16);
        w++;
    }
    atomic_store_explicit(&g_ring_w, w, memory_order_release);
}

static void audio_cb(void *ud, Uint8 *stream, int len) {
    (void)ud;
    int16_t *out = (int16_t *)stream;
    int frames = len / 4;
    unsigned r = atomic_load_explicit(&g_ring_r, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&g_ring_w, memory_order_acquire);
    static int16_t last[2];
    for (int i = 0; i < frames; i++) {
        if (r != w) {
            last[0] = g_ring[r % AUDIO_RING][0];
            last[1] = g_ring[r % AUDIO_RING][1];
            r++;
        }
        out[2 * i] = last[0];
        out[2 * i + 1] = last[1];
    }
    /* keep latency bounded: if far behind, skip ahead */
    if (w - r > 44100 / 5) r = w - 44100 / 20;
    atomic_store_explicit(&g_ring_r, r, memory_order_release);
}

/* ------------------------------------------------------------------ pacing (guest thread) */
static uint64_t g_pace_t0;          /* host ticks at virtual time 0 */
static double g_pace_freq;

void rt_pace_vblank(void) {
    if (!g_frontend_active) return;
    double virt = (double)rt_now() / CPU_HZ;
    for (;;) {
        double real = (double)(SDL_GetPerformanceCounter() - g_pace_t0) / g_pace_freq;
        double ahead = virt - real;
        if (ahead <= 0.0005) {
            if (ahead < -0.25) g_pace_t0 = SDL_GetPerformanceCounter() - (uint64_t)(virt * g_pace_freq); /* resync after stalls */
            return;
        }
        SDL_Delay(ahead > 0.002 ? (Uint32)((ahead - 0.001) * 1000) : 0);
    }
}

/* ------------------------------------------------------------------ input */
typedef struct {
    int steer_left, steer_right, gas, brake, handbrake;
    double steer;                   /* -1..1 (keyboard, ramped) */
    int pad_steer, pad_gas, pad_brake;   /* raw gamepad */
    int shift_up, shift_down, coin, start, test, service;
} Controls;
static Controls ctl;
static SDL_GameController *g_pad;

static void apply_inputs(double dt) {
    /* keyboard steering: ramp towards target */
    double target = (ctl.steer_right - ctl.steer_left);
    double speed = 4.0 * dt;
    if (ctl.steer < target) ctl.steer = SDL_min(target, ctl.steer + speed);
    else if (ctl.steer > target) ctl.steer = SDL_max(target, ctl.steer - speed);
    double steer = ctl.steer;
    if (g_pad && abs(ctl.pad_steer) > 3000) steer = ctl.pad_steer / 32767.0;
    /* signed positions for the differential ADC (hw.c): steering -200..+200, pedals -200 (released)..+200 */
    int gas = ctl.gas ? 255 : 0, brake = ctl.brake ? 255 : 0;
    if (g_pad) {
        if (ctl.pad_gas > 1000) gas = ctl.pad_gas * 255 / 32767;
        if (ctl.pad_brake > 1000) brake = ctl.pad_brake * 255 / 32767;
    }
    g_analog[0] = (int16_t)(steer * ANALOG_RANGE);
    g_analog[1] = (int16_t)(-ANALOG_RANGE + gas * 2 * ANALOG_RANGE / 255);
    g_analog[2] = (int16_t)(-ANALOG_RANGE + brake * 2 * ANALOG_RANGE / 255);
    if (GAME_HAS_HANDBRAKE) g_analog[3] = (int16_t)(ctl.handbrake ? ANALOG_RANGE : -ANALOG_RANGE);
    uint8_t in3 = 0xff, in4 = 0xff;
    if (ctl.service) in3 &= ~0x01;
    if (ctl.test) in3 &= ~0x02;
    if (ctl.coin) in3 &= ~0x04;
    if (ctl.start) in3 &= ~0x10;
    if (ctl.shift_down) in3 &= ~0x40;
    if (ctl.shift_up) in4 &= ~0x01;
    g_in[3] = in3;
    g_in[4] = in4;
}

static void key(SDL_Keycode k, int down) {
    switch (k) {
    case SDLK_LEFT: ctl.steer_left = down; break;
    case SDLK_RIGHT: ctl.steer_right = down; break;
    case SDLK_UP: ctl.gas = down; break;
    case SDLK_DOWN: ctl.brake = down; break;
    case SDLK_SPACE: ctl.handbrake = down; break;
    case SDLK_a: ctl.shift_up = down; break;
    case SDLK_z: ctl.shift_down = down; break;
    case SDLK_5: ctl.coin = down; break;
    case SDLK_1: ctl.start = down; break;
    case SDLK_F2: ctl.test = down; break;
    case SDLK_9: ctl.service = down; break;
    default: break;
    }
}

static void pad_button(int b, int down) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: ctl.shift_up = down; break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: ctl.shift_down = down; break;
    case SDL_CONTROLLER_BUTTON_START: ctl.start = down; break;
    case SDL_CONTROLLER_BUTTON_BACK: ctl.coin = down; break;
    case SDL_CONTROLLER_BUTTON_A: ctl.gas = down; break;
    case SDL_CONTROLLER_BUTTON_B: ctl.brake = down; break;
    case SDL_CONTROLLER_BUTTON_X: ctl.handbrake = down; break;
    default: break;
    }
}

/* ------------------------------------------------------------------ main loop */
void nvram_save(void);

int frontend_run(int scale) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        rt_log("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Window *win = SDL_CreateWindow(GAME_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                       512 * scale, 384 * scale, SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, 512, 384);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 384);
    int tw = 512, th = 384;

    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
    else rt_log("audio: %s\n", SDL_GetError());

    for (int i = 0; i < SDL_NumJoysticks(); i++)
        if (SDL_IsGameController(i) && (g_pad = SDL_GameControllerOpen(i))) {
            rt_log("gamepad: %s\n", SDL_GameControllerName(g_pad));
            break;
        }

    g_pace_freq = (double)SDL_GetPerformanceFrequency();
    g_pace_t0 = SDL_GetPerformanceCounter() - (uint64_t)((double)rt_now() / CPU_HZ * g_pace_freq);
    g_frontend_active = 1;

    static uint32_t frame[2048 * 2048];
    uint64_t last_frame = 0, last_tick = SDL_GetPerformanceCounter(), last_save = last_tick;
    int running = 1;
    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_KEYDOWN:
                if (ev.key.keysym.sym == SDLK_ESCAPE) running = 0;
                else if (ev.key.keysym.sym == SDLK_F11) {
                    Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                } else key(ev.key.keysym.sym, 1);
                break;
            case SDL_KEYUP: key(ev.key.keysym.sym, 0); break;
            case SDL_CONTROLLERDEVICEADDED:
                if (!g_pad) g_pad = SDL_GameControllerOpen(ev.cdevice.which);
                break;
            case SDL_CONTROLLERAXISMOTION:
                if (ev.caxis.axis == SDL_CONTROLLER_AXIS_LEFTX) ctl.pad_steer = ev.caxis.value;
                else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT) ctl.pad_gas = ev.caxis.value;
                else if (ev.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT) ctl.pad_brake = ev.caxis.value;
                break;
            case SDL_CONTROLLERBUTTONDOWN: pad_button(ev.cbutton.button, 1); break;
            case SDL_CONTROLLERBUTTONUP: pad_button(ev.cbutton.button, 0); break;
            default: break;
            }
        }
        uint64_t now = SDL_GetPerformanceCounter();
        apply_inputs((double)(now - last_tick) / g_pace_freq);
        last_tick = now;
        if ((double)(now - last_save) / g_pace_freq > 60.0) { nvram_save(); last_save = now; }

        int w, h;
        uint64_t cnt = voodoo_get_frame(NULL, 0, &w, &h);
        if (cnt != last_frame && w > 0 && h > 0) {
            last_frame = cnt;
            voodoo_get_frame(frame, 2048 * 2048, &w, &h);
            if (w != tw || h != th) {
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
                SDL_RenderSetLogicalSize(ren, w, h);
                tw = w; th = h;
            }
            SDL_UpdateTexture(tex, NULL, frame, w * 4);
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);       /* vsync paces this loop */
    }
    nvram_save();
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    SDL_Quit();
    return 0;
}
