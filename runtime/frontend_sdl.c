/*
 * SDL2 frontend: window, real-time pacing, audio, keyboard/gamepad input.
 *
 * Threads: the host main thread runs SDL (events, presentation); the guest runs on
 * runtime fibers.  Shared state is small: input bytes/words (single writer), the video
 * frame (mutex inside the voodoo bridge) and a lock-free audio ring buffer.
 *
 * Controls (driving-game inputs as in MAME's viper.cpp "thrild2" / "gticlub2"):
 *   Left/Right or A/D  steering   Up or W  gas   Down or S  brake   Space  handbrake (GTI Club 2)
 *   E  shift up   Q  shift down   5  coin   1  start   F2  test   9  service
 *   (enhanced mode: test, service and coin are not passed to the game: TEST MODE cannot be
 *   opened, and the game is on free play. In the rankings' name entry the keyboard types the
 *   letters, Backspace deletes, Enter ends, Left/Right and the D-pad step through the letters)
 *   Gamepad: left stick = steering, R2/L2 = gas/brake, R1/L1 = shift up/down,
 *            X = handbrake, Start = start, Back = coin
 *   Switch: optional ZL = handbrake; ZR = gas, L/R = shift down/up,
 *           + = pause in enhanced play, - = view change
 *   F11 fullscreen, Esc quit
 */
#include "runtime.h"
#include "track_explorer.h"
#include "game_config.h"
#include "controller_math.h"

#include "controller_gyro.h"
#include <SDL.h>
#ifdef VIPER_NATIVE_HAPTICS
#include "controller_haptics_mac.h"
static int send_controller_rumble(SDL_GameController *pad, Uint16 low, Uint16 high, Uint32 duration);
#define CONTROLLER_RUMBLE_SEND send_controller_rumble
#endif
#include "controller_rumble.h"
#include "window_state.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

static const char *g_rumble_backend = "SDL";
#ifdef VIPER_NATIVE_HAPTICS
static int send_controller_rumble(SDL_GameController *pad, Uint16 low, Uint16 high, Uint32 duration) {
    const char *backend = getenv("RT_RUMBLE_BACKEND");
    if (SDL_NumJoysticks() == 1 && SDL_GameControllerGetType(pad) == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO &&
        (backend && !strcmp(backend, "apple"))) {
        int result = controller_haptics_rumble((float)SDL_max(low, high) / 65535.0f, duration / 1000.0);
        if (result > 0) { g_rumble_backend = "Apple"; return 0; }
        if (result < 0) { g_rumble_backend = "Apple retry"; return SDL_SetError("Apple controller haptics temporarily unavailable"); }
        /* Native discovery may be delayed or the device may not expose haptics. */
    }
    controller_haptics_stop();
    g_rumble_backend = "SDL";
    return SDL_GameControllerRumble(pad, low, high, duration);
}
#endif
static char g_window_state_path[1024];
void frontend_set_settings_path(const char *path) {
    if (snprintf(g_window_state_path, sizeof g_window_state_path, "%s.window", path) >= (int)sizeof g_window_state_path)
        g_window_state_path[0] = 0;
}

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
    if (!g_audio || enh_turbo()) return;       /* muted while the enhanced mode fast-forwards */
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
    if (enh_paused()) { memset(stream, 0, (size_t)len); return; }
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
    if (enh_turbo()) {                  /* enhanced mode boot/apply: as fast as possible */
        g_pace_t0 = SDL_GetPerformanceCounter() - (uint64_t)(virt * g_pace_freq);
        return;
    }
    if (enh_paused()) {                 /* enhanced mode pause: the game waits here */
        while (enh_paused() && g_frontend_active) SDL_Delay(5);
        g_pace_t0 = SDL_GetPerformanceCounter() - (uint64_t)(virt * g_pace_freq);
        return;
    }
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
/* each digital control may be held by several keys and buttons: one bit per source, so
 * releasing one of them does not release the others */
enum { SRC_KEY = 1, SRC_KEY2 = 2, SRC_PAD = 4 };
typedef struct {
    int steer_left, steer_right, gas, brake, handbrake;
    double steer;                   /* -1..1 (keyboard, ramped) */
    int shift_up, shift_down, coin, start, test, service;
} Controls;
static Controls ctl;

static void hold(int *f, int src, int down) { *f = down ? *f | src : *f & ~src; }
static SDL_GameController *g_pad;
static SDL_JoystickID g_pad_id = -1;
static int g_input_focus = 1;
static int g_controller_log;
static Uint32 g_controller_log_tick;
static double g_stick_deadzone = 0.10, g_stick_curve = 3.0, g_trigger_deadzone = 0.03;
static int g_switch_trigger_layout = 1;
int frontend_switch_trigger_layout(void) { return g_switch_trigger_layout; }
void frontend_set_switch_trigger_layout(int layout) {
    g_switch_trigger_layout = layout == 0 ? 0 : 1;
}
int frontend_stick_response(void) { return (int)lround(g_stick_curve) - 1; }
void frontend_set_stick_response(int response) {
    g_stick_curve = response >= 0 && response <= 2 ? response + 1.0 : 3.0;
}


static double controller_option(const char *name, double fallback, double lo, double hi) {
    const char *value = getenv(name);
    if (!value) return fallback;
    char *end;
    double result = strtod(value, &end);
    if (end == value || *end || !isfinite(result) || result < lo || result > hi) {
        rt_log("%s: expected a number between %g and %g; using %g\n", name, lo, hi, fallback);
        return fallback;
    }
    return result;
}

static int pad_matches(SDL_JoystickID id) { return g_pad && id == g_pad_id; }
static int switch_pad(void) {
    if (!g_pad) return 0;
    SDL_GameControllerType type = SDL_GameControllerGetType(g_pad);
    if (type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_PRO) return 1;
#if SDL_VERSION_ATLEAST(2, 0, 22)
    if (type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_LEFT ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT ||
        type == SDL_CONTROLLER_TYPE_NINTENDO_SWITCH_JOYCON_PAIR) return 1;
#endif
    return 0;
}
int frontend_shift_up_held(void) {
    return g_input_focus && ((ctl.shift_up & ~SRC_PAD) ||
        (g_pad && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER)));
}

static void update_rumble(int active);
static int g_rumble_testing;
static int g_menu_repeat_button = -1;
static int g_gate_repeat_button=-1,g_gate_repeat_action=-1;
static Uint32 g_gate_repeat_at;
/* Check actual window flags as well as focus events: some SDL backends do not
 * deliver every focus transition. Retry while inactive if play begins there. */
static void input_focus(int focused) {
    if (!focused) {
        enh_focus_lost();
        g_menu_repeat_button = -1;
        if (g_input_focus) memset(&ctl, 0, sizeof ctl);
    }
    g_input_focus = !!focused;
}

static void window_focus(Uint32 flags) {
    input_focus((flags & SDL_WINDOW_INPUT_FOCUS) &&
                !(flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)));
}
static Uint32 g_menu_repeat_at;

static void close_pad(void) {
    g_menu_repeat_button = -1;
    g_rumble_testing = 0;
    update_rumble(0);
    if (g_pad) SDL_GameControllerClose(g_pad);
    g_pad = NULL;
    g_pad_id = -1;
    /* Releasing a controller must not release a held keyboard key. */
    ctl.gas &= ~SRC_PAD; ctl.brake &= ~SRC_PAD; ctl.handbrake &= ~SRC_PAD;
    ctl.shift_up &= ~SRC_PAD; ctl.shift_down &= ~SRC_PAD;
    ctl.coin &= ~SRC_PAD; ctl.start &= ~SRC_PAD;
}

static void open_pad(void) {
    if (g_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        if (!SDL_IsGameController(i)) continue;
        g_pad = SDL_GameControllerOpen(i);
        if (!g_pad) continue;
        g_pad_id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad));
        rt_log("gamepad: %s\n", SDL_GameControllerName(g_pad));
        break;
    }
}


static atomic_uchar g_motor_output;
static atomic_uint g_motor_writes, g_motor_energized_writes;
static Uint32 g_rumble_trace_started, g_rumble_trace_tick;
static int g_rumble_trace;
static ControllerRumble g_rumble = { .id = -1 };
static double g_rumble_gain = 0.5;

int frontend_rumble_multiplier(void) { return (int)lround(g_rumble_gain * 200); }
void frontend_set_rumble_multiplier(int percent) {
    g_rumble_gain = SDL_clamp(percent, 0, 400) / 200.0;
}
static Uint32 g_rumble_test_started;

static void test_rumble(void) {
    if (!g_pad) { rt_log("rumble test: no controller connected\n"); return; }
    if (g_rumble_testing) return; /* let the current pulse finish */
    g_rumble_test_started = SDL_GetTicks();
    if (controller_rumble_pulse(&g_rumble, g_pad, g_rumble_gain, g_rumble_test_started) < 0) {
        rt_log("rumble test failed: %s\n", SDL_GetError());
        return;
    }
    g_rumble_testing = 1;
    g_rumble_trace = 1;
    g_rumble_trace_started = g_rumble_test_started;
    g_rumble_trace_tick = g_rumble_test_started - 1000;
    rt_log("rumble test: single 1000 ms pulse at %.1fx strength (output=%u)\n",
           frontend_rumble_multiplier() / 100.0, g_rumble.strength);
}

/* Guest publishes motor commands; SDL calls stay on the host main thread. */
void frontend_set_motor(uint8_t command) {
    atomic_store_explicit(&g_motor_output, command, memory_order_relaxed);
    atomic_fetch_add_explicit(&g_motor_writes, 1, memory_order_relaxed);
    if ((command & 0x80) && (command & 15)) atomic_fetch_add_explicit(&g_motor_energized_writes, 1, memory_order_relaxed);
}

static void update_rumble(int active) {
    Uint32 now = SDL_GetTicks();
    SDL_Window *window = SDL_GetKeyboardFocus();
    if (g_rumble_testing && (!window || (Uint32)(now - g_rumble_test_started) >= 1000))
        g_rumble_testing = 0;
    uint8_t motor = atomic_load_explicit(&g_motor_output, memory_order_relaxed);
    if (!g_rumble_testing)
        controller_rumble_update(&g_rumble, g_pad, motor, g_rumble_gain, active, now);
    if (g_rumble_trace && (Uint32)(now - g_rumble_trace_started) >= 30000) g_rumble_trace = 0;
    if ((g_rumble_trace || g_controller_log) && (Uint32)(now - g_rumble_trace_tick) >= 1000) {
        g_rumble_trace_tick = now;
        rt_log("rumble: motor=%02x writes=%u energized=%u active=%d test=%d gain=%.1fx output=%u retry=%d backend=%s paused=%d menu=%d turbo=%d owned=%d focus=%d\n",
            motor, atomic_load_explicit(&g_motor_writes, memory_order_relaxed),
            atomic_load_explicit(&g_motor_energized_writes, memory_order_relaxed),
            active, g_rumble_testing, frontend_rumble_multiplier() / 100.0,
            g_rumble.strength, g_rumble.failed, g_rumble_backend,
            enh_paused(), enh_menu_active(), enh_turbo(), enh_inputs_owned(), window != NULL);
    }
}

static int g_gyro_enabled, g_gyro_active = 1;
static double g_gyro_range = 35 * 3.141592653589793 / 180;
static ControllerGyro g_gyro, g_drone_pitch;
static double g_drone_pitch_position;
static SDL_JoystickID g_gyro_pad = -1;
static int g_gyro_available, g_gyro_suspended;
static double g_gyro_position;

double frontend_gyro_position(void) { return g_gyro_position; }
int frontend_gyro_ready(void) {
    return g_gyro_enabled && g_gyro_available && g_gyro.ready && g_pad && SDL_GameControllerGetAttached(g_pad);
}
int frontend_gyro_enabled(void) { return g_gyro_enabled; }
int frontend_gyro_sensitivity(void) {
    return (int)lround(3500.0 / (g_gyro_range * 180 / 3.141592653589793));
}
void frontend_gyro_set_sensitivity(int percent) {
    percent = SDL_clamp(percent, 50, 350);
    g_gyro_range = (3500.0 / percent) * 3.141592653589793 / 180;
}
void frontend_gyro_recenter(void) { g_gyro.ready = 0; g_drone_pitch.ready = 0; g_gyro_position = 0; }
void frontend_gyro_set_enabled(int on) {
    g_gyro_enabled = !!on;
    g_gyro_position = 0;
    g_gyro.ready = 0; g_drone_pitch.ready = 0;
    g_gyro_pad = -1;
    g_gyro_available = 0;
#if SDL_VERSION_ATLEAST(2, 0, 14)
    if (!on && g_pad) {
        SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_GYRO, SDL_FALSE);
        SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_ACCEL, SDL_FALSE);
    }
#endif
}
int frontend_gyro_available(void) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    return g_pad && SDL_GameControllerHasSensor(g_pad, SDL_SENSOR_GYRO) &&
           SDL_GameControllerHasSensor(g_pad, SDL_SENSOR_ACCEL);
#else
    return 0;
#endif
}

static double gyro_steering(double dt) {
#if SDL_VERSION_ATLEAST(2, 0, 14)
    g_gyro_position = 0;
    g_drone_pitch_position = 0;
    if (!explorer_active()) g_drone_pitch.ready = 0;
    if (!g_gyro_enabled || !g_pad || !SDL_GameControllerGetAttached(g_pad)) return 0;
    SDL_JoystickID id = SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad));
    if (id != g_gyro_pad) {
        g_gyro_pad = id;
        g_gyro = (ControllerGyro){0};
        g_drone_pitch = (ControllerGyro){0};
        g_gyro_available = SDL_GameControllerHasSensor(g_pad, SDL_SENSOR_GYRO) &&
                           SDL_GameControllerHasSensor(g_pad, SDL_SENSOR_ACCEL);
        if (g_gyro_available) {
            g_gyro_available = SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_GYRO, SDL_TRUE) == 0 &&
                               SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_ACCEL, SDL_TRUE) == 0;
        }
        if (!g_gyro_available) {
            SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_GYRO, SDL_FALSE);
            SDL_GameControllerSetSensorEnabled(g_pad, SDL_SENSOR_ACCEL, SDL_FALSE);
        }
        rt_log("gyro steering: %s (click left stick to recenter)\n", g_gyro_available ? "enabled" : "unavailable; using stick");
    }
    if (!g_gyro_available) return 0;
    if (!g_gyro_active || enh_turbo()) {
        g_gyro.ready = 0; g_drone_pitch.ready = 0;
        return 0;
    }
    /* Keep the preview live in menus without steering the guest. Recenter on
     * entering/leaving a menu so resuming never inherits a paused tilt. */
    int suspended = enh_menu_active() || enh_paused();
    if (suspended != g_gyro_suspended) { g_gyro.ready = 0; g_drone_pitch.ready = 0; g_gyro_suspended = suspended; }
    float accel[3], gyro[3];
    if (SDL_GameControllerGetSensorData(g_pad, SDL_SENSOR_ACCEL, accel, 3) < 0 ||
        SDL_GameControllerGetSensorData(g_pad, SDL_SENSOR_GYRO, gyro, 3) < 0) {
        g_gyro.ready = 0; g_drone_pitch.ready = 0;
        return 0;
    }
    if (!explorer_active()) g_drone_pitch.ready = 0;
    if (explorer_active() && !suspended)
        g_drone_pitch_position = controller_gyro_pitch_step(&g_drone_pitch, accel, gyro, dt,
                                  SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK));
    g_gyro_position = controller_gyro_step(&g_gyro, accel, gyro, dt, g_gyro_range,
                               SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSTICK));
    /* Auto-drive owns the ADC, but sensor tracking must retain its centre so
     * a held steering tilt survives the handover. */
    return suspended || enh_inputs_owned() ? 0 : g_gyro_position;
#else
    (void)dt;
    return 0;
#endif
}


double frontend_stick_position(void) {
    return g_pad && g_input_focus ? controller_axis(
        SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX), 0, 1) : 0;
}
double frontend_steering_position(void) { return g_analog[0] / (double)ANALOG_RANGE; }

static void apply_inputs(double dt) {
    if(enh_gate_editor_active()) {
        if(g_pad && g_input_focus) enh_gate_editor_axes(
            controller_axis(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_LEFTX),.15,1),
            controller_axis(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_LEFTY),.15,1),
            controller_axis(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_RIGHTX),.15,1),
            controller_axis(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_RIGHTY),.15,1),
            controller_trigger(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_TRIGGERLEFT),g_trigger_deadzone),
            controller_trigger(SDL_GameControllerGetAxis(g_pad,SDL_CONTROLLER_AXIS_TRIGGERRIGHT),g_trigger_deadzone),dt);
        g_analog[0]=0;g_analog[1]=-ANALOG_RANGE;g_analog[2]=g_analog[3]=ANALOG_RANGE;return;
    }
    /* Menu confirmation can consume a pedal button's down event. These controls
     * represent held state, so reconcile them with the device every frame. */
    int pad_active = g_pad && g_input_focus;
    hold(&ctl.gas, SRC_PAD, pad_active && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_A));
    hold(&ctl.brake, SRC_PAD, pad_active && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_B));
    hold(&ctl.handbrake, SRC_PAD, pad_active && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_X));

    /* keyboard steering: ramp towards target */
    double target = !!ctl.steer_right - !!ctl.steer_left;
    double speed = 4.0 * SDL_min(dt, 0.05);
    if (ctl.steer < target) ctl.steer = SDL_min(target, ctl.steer + speed);
    else if (ctl.steer > target) ctl.steer = SDL_max(target, ctl.steer - speed);
    double steer = ctl.steer;
    double tilt = gyro_steering(dt);
    if (tilt != 0 && !ctl.steer_left && !ctl.steer_right) steer = tilt;
    /* Take the strongest pedal source: a slightly pressed trigger must not reduce a
     * fully held key/button. Right-stick Y supplies proportional pedals on Switch Pro. */
    double gas = ctl.gas ? 1.0 : 0.0, brake = ctl.brake ? 1.0 : 0.0;
    double handbrake = ctl.handbrake ? 1.0 : 0.0;
    if (g_pad && g_input_focus) {
        double stick = controller_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX),
                                       g_stick_deadzone, g_stick_curve);
        if (stick != 0) steer = stick;
        double pedals = controller_axis(SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY),
                                        g_stick_deadzone, 1.0);
        double left_trigger = controller_trigger(
            SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT), g_trigger_deadzone);
        double right_trigger = controller_trigger(
            SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT), g_trigger_deadzone);
        gas = fmax(gas, fmax(-pedals, right_trigger));
        brake = fmax(brake, pedals);
        if (GAME_HAS_HANDBRAKE && switch_pad() && g_switch_trigger_layout) {
            handbrake = fmax(handbrake, left_trigger);
        } else {
            brake = fmax(brake, left_trigger);
        }
    }
    if (explorer_active()) {
        explorer_drive(0, 0);
        if (g_input_focus && !enh_paused() && !enh_menu_active() && !enh_inputs_owned()) {
            /* Tour adjusts cruise settings; free roam moves only while held. */
            double step = fmax(0, fmin(dt, .05));
            int raise = !!(ctl.shift_up & ~SRC_PAD) ||
                (pad_active && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER));
            int lower = !!(ctl.shift_down & ~SRC_PAD) ||
                (pad_active && SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_LEFTSHOULDER));
            if (explorer_free()) explorer_drive((float)(gas - brake), (float)(raise - lower));
            else explorer_adjust((float)((gas - brake) * 40 * step), (float)((raise - lower) * 12 * step));
            explorer_look((float)steer);
            explorer_pitch((float)g_drone_pitch_position);
        } else { explorer_look(0); explorer_pitch(0); }
        steer = gas = brake = 0;  /* drone controls must not drive the car */
    } else { explorer_look(0); explorer_pitch(0); explorer_drive(0, 0); }
    if (enh_inputs_owned()) return;     /* preserve controls driven by the enhanced layer */
    /* signed positions for the differential ADC: steering -200..+200, pedals released=-200 */
    if (enh_name_entry_active()) steer = 0;   /* the letters come from the keyboard */
    g_analog[0] = (int16_t)lround(steer * ANALOG_RANGE);
    if (g_controller_log && g_pad && (Uint32)(SDL_GetTicks() - g_controller_log_tick) >= 100) {
        g_controller_log_tick = SDL_GetTicks();
        rt_log("controller: left_x=%d steering_adc=%d gas=%.3f brake=%.3f\n",
               SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX), g_analog[0], gas, brake);
    }
    g_analog[1] = (int16_t)(-ANALOG_RANGE + gas * 2 * ANALOG_RANGE);
    g_analog[2] = (int16_t)(-ANALOG_RANGE + brake * 2 * ANALOG_RANGE);
    if (GAME_HAS_HANDBRAKE) g_analog[3] = (int16_t)(-ANALOG_RANGE + handbrake * 2 * ANALOG_RANGE);
    uint8_t in3 = 0xff, in4 = 0xff;
    if (ctl.service && !g_enhanced) in3 &= ~0x01;
    if (ctl.test && !g_enhanced) in3 &= ~0x02;
    if (ctl.coin && !g_enhanced) in3 &= ~0x04;
    if (ctl.start || enh_start_held()) in3 &= ~0x10;
    if (ctl.shift_down && !explorer_active()) in3 &= ~0x40;
    if (ctl.shift_up && !explorer_active()) in4 &= ~0x01;
    if (enh_menu_active()) {            /* the menu owns the controls: the attract gets nothing */
        in3 = enh_start_held() ? 0xef : 0xff;
        in4 = 0xff;
        g_analog[1] = g_analog[2] = (int16_t)-ANALOG_RANGE;
    }
    g_in[3] = in3;
    g_in[4] = in4;
}

/* enhanced mode: keys and buttons that drive the attract menu while it is on screen */
static int menu_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_UP: case SDLK_w: return ENH_UP;
    case SDLK_DOWN: case SDLK_s: return ENH_DOWN;
    case SDLK_LEFT: case SDLK_a: return ENH_LEFT;
    case SDLK_RIGHT: case SDLK_d: return ENH_RIGHT;
    case SDLK_RETURN: case SDLK_KP_ENTER: case SDLK_1: return ENH_OK;
    case SDLK_BACKSPACE: return ENH_BACK;
    case SDLK_q: case SDLK_PAGEUP: return ENH_PAGE_UP;
    case SDLK_e: case SDLK_PAGEDOWN: return ENH_PAGE_DOWN;
    default: return -1;
    }
}

static int menu_button(int b) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return ENH_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return ENH_DOWN;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return ENH_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return ENH_RIGHT;
    case SDL_CONTROLLER_BUTTON_A: case SDL_CONTROLLER_BUTTON_START: return ENH_OK;
    case SDL_CONTROLLER_BUTTON_B: return ENH_BACK;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return ENH_PAGE_UP;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return ENH_PAGE_DOWN;
    default: return -1;
    }
}

/* Repeat navigation and value adjustments, never confirmation or cancellation. */
static int menu_action_repeats(int action) {
    return action==ENH_UP || action==ENH_DOWN || action==ENH_LEFT || action==ENH_RIGHT;
}
static void menu_pad_press(int button, Uint32 now) {
    int action = menu_button(button);
    g_menu_repeat_button = menu_action_repeats(action) ? button : -1;
    g_menu_repeat_at = now + 400;
    enh_menu_action(action);
}

static void menu_pad_repeat(Uint32 now) {
    if (!g_input_focus || !(enh_menu_active() || enh_paused())) g_menu_repeat_button = -1;
    if (g_menu_repeat_button >= 0 && (Sint32)(now - g_menu_repeat_at) >= 0) {
        enh_menu_action(menu_button(g_menu_repeat_button));
        g_menu_repeat_at = now + 80;
    }
}

/* enhanced mode, name entry: 1 if the key is the name entry's (the characters themselves come
 * as SDL_TEXTINPUT, which follows the keyboard layout) */
static int name_key(SDL_Keycode k) {
    switch (k) {
    case SDLK_BACKSPACE: enh_name_type('\b'); return 1;
    case SDLK_RETURN: case SDLK_KP_ENTER: enh_name_type('\r'); return 1;
    case SDLK_LEFT: enh_name_step(-1); return 1;
    case SDLK_RIGHT: enh_name_step(1); return 1;
    default: return (k >= SDLK_SPACE && k <= SDLK_z) || (k >= SDLK_KP_1 && k <= SDLK_KP_PERIOD);
    }
}

static void key(SDL_Keycode k, int down) {
    switch (k) {
    case SDLK_LEFT: hold(&ctl.steer_left, SRC_KEY, down); break;
    case SDLK_RIGHT: hold(&ctl.steer_right, SRC_KEY, down); break;
    case SDLK_UP: hold(&ctl.gas, SRC_KEY, down); break;
    case SDLK_DOWN: hold(&ctl.brake, SRC_KEY, down); break;
    case SDLK_a: hold(&ctl.steer_left, SRC_KEY2, down); break;
    case SDLK_d: hold(&ctl.steer_right, SRC_KEY2, down); break;
    case SDLK_w: hold(&ctl.gas, SRC_KEY2, down); break;
    case SDLK_s: hold(&ctl.brake, SRC_KEY2, down); break;
    case SDLK_SPACE: hold(&ctl.handbrake, SRC_KEY, down); break;
    case SDLK_e: hold(&ctl.shift_up, SRC_KEY, down); break;
    case SDLK_q: hold(&ctl.shift_down, SRC_KEY, down); break;
    case SDLK_5: hold(&ctl.coin, SRC_KEY, down); break;
    case SDLK_1: hold(&ctl.start, SRC_KEY, down); break;
    case SDLK_F2: ctl.test = down; break;
    case SDLK_9: ctl.service = down; break;
    default: break;
    }
}

static void pad_button(int b, int down) {
    switch (b) {
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: hold(&ctl.shift_up, SRC_PAD, down); break;
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: hold(&ctl.shift_down, SRC_PAD, down); break;
    case SDL_CONTROLLER_BUTTON_START:
        if (!(g_enhanced && switch_pad())) hold(&ctl.start, SRC_PAD, down);
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        hold(switch_pad() ? &ctl.start : &ctl.coin, SRC_PAD, down);
        break;
    case SDL_CONTROLLER_BUTTON_A: hold(&ctl.gas, SRC_PAD, down); break;
    case SDL_CONTROLLER_BUTTON_B: hold(&ctl.brake, SRC_PAD, down); break;
    case SDL_CONTROLLER_BUTTON_X: hold(&ctl.handbrake, SRC_PAD, down); break;
    default: break;
    }
}

static int pad_pause_press(int button) {
    if (g_enhanced && switch_pad() && button == SDL_CONTROLLER_BUTTON_START && !enh_menu_active() && !enh_paused()) {
        enh_escape();
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ main loop */
void nvram_save(void);

int frontend_run(int scale) {
#ifdef VIPER_NATIVE_HAPTICS
    const char *rumble_backend = getenv("RT_RUMBLE_BACKEND");
    if (rumble_backend && !strcmp(rumble_backend, "apple")) controller_haptics_init();
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER) != 0) {
        rt_log("SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    const char *rumble = getenv("RT_RUMBLE");
    if (rumble) {
        char *end;
        double gain = strtod(rumble, &end);
        if (end != rumble && !*end && isfinite(gain) && gain >= 0 && gain <= 2) g_rumble_gain = gain;
        else rt_log("RT_RUMBLE: expected 0..2; keeping current setting\n");
    }

    const char *gyro = getenv("RT_GYRO");
    if (gyro) frontend_gyro_set_enabled(!strcmp(gyro, "1"));
    const char *range = getenv("RT_GYRO_RANGE");
    if (range) {
        char *end;
        double degrees = strtod(range, &end);
        if (end != range && !*end && isfinite(degrees) && degrees >= 10 && degrees <= 70)
            g_gyro_range = degrees * 3.141592653589793 / 180;
        else rt_log("RT_GYRO_RANGE: expected 10..70 degrees; using 35\n");
    }
#if !SDL_VERSION_ATLEAST(2, 0, 14)
    if (g_gyro_enabled) rt_log("gyro steering requires SDL 2.0.14 or newer\n");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    SDL_Rect window_rect = { SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 512 * scale, 384 * scale };
    int restored_window = window_state_load(g_window_state_path, &window_rect);
    int count = SDL_GetNumVideoDisplays(), usable = 0;
    SDL_Rect *displays = count > 0 ? calloc((size_t)count, sizeof *displays) : NULL;
    if (displays) {
        for (int i = 0; i < count; i++)
            if (SDL_GetDisplayUsableBounds(i, &displays[usable]) == 0 || SDL_GetDisplayBounds(i, &displays[usable]) == 0)
                usable++;
        window_state_fit(&window_rect, displays, usable);
        free(displays);
    }
    SDL_Window *win = SDL_CreateWindow(g_enhanced ? GAME_TITLE " - enhanced" : GAME_TITLE,
        window_rect.x, window_rect.y, window_rect.w, window_rect.h,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { rt_log("SDL_CreateWindow failed: %s\n", SDL_GetError()); SDL_Quit(); return -1; }
    SDL_SetWindowMinimumSize(win, 320, 240);
    window_state_capture(win, &window_rect);
    int window_dirty = 0;
    Uint32 window_changed = 0;
    if (getenv("RT_RESTARTED")) {       /* enhanced mode, after a restart: macOS does not reactivate */
        unsetenv("RT_RESTARTED");       /* the re-executed program, so take the focus back */
        SDL_SetHint("SDL_FORCE_RAISEWINDOW", "1");
        SDL_RaiseWindow(win);
    }
    SDL_Renderer *ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    SDL_RenderSetLogicalSize(ren, 512, 384);
    SDL_Texture *tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 512, 384);
    int tw = 512, th = 384;
    int window_filter_applied = -1;

    SDL_AudioSpec want = {0}, have;
    want.freq = 44100;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = audio_cb;
    g_audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (g_audio) SDL_PauseAudioDevice(g_audio, 0);
    else rt_log("audio: %s\n", SDL_GetError());

    g_stick_deadzone = controller_option("RT_STICK_DEADZONE", 0.10, 0.0, 0.5);
    g_stick_curve = controller_option("RT_STICK_CURVE", g_stick_curve, 1.0, 3.0);
    g_trigger_deadzone = controller_option("RT_TRIGGER_DEADZONE", 0.03, 0.0, 0.5);
    open_pad();

    g_pace_freq = (double)SDL_GetPerformanceFrequency();
    g_pace_t0 = SDL_GetPerformanceCounter() - (uint64_t)((double)rt_now() / CPU_HZ * g_pace_freq);
    g_frontend_active = 1;

    static uint32_t frame[2048 * 2048], raw[2048 * 2048];
    static uint16_t scene_depth[2048 * 2048];
    uint64_t last_frame = 0, last_tick = SDL_GetPerformanceCounter(), last_save = last_tick;
    int running = 1, fs_applied = 0, restart = 0, text_on = 1;
    while (running) {
        /* text input only for the name entry: SDL starts it with the video, and while it is on
         * macOS opens its accent picker on a held letter key (W, A, S, D while driving) */
        if (enh_name_entry_active() != text_on) {
            text_on = enh_name_entry_active();
            if (text_on) SDL_StartTextInput(); else SDL_StopTextInput();
        }
        if (enh_want_fullscreen() != fs_applied) {      /* enhanced mode: DISPLAY option */
            fs_applied = enh_want_fullscreen();
            SDL_SetWindowFullscreen(win, fs_applied ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        }
        g_gyro_active = (SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) != 0;
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT: running = 0; break;
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    input_focus(0);
                } else if (ev.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) input_focus(1);
                break;
            case SDL_KEYDOWN:
                if(g_enhanced && ev.key.keysym.sym==SDLK_c && (ev.key.keysym.mod&KMOD_GUI)) {
                    char position[64];
                    int copied=(ev.key.keysym.mod&KMOD_ALT) ?
                        enh_track_debug_heading_text(position,sizeof position) :
                        enh_track_debug_position_text(position,sizeof position);
                    if(copied) {
                        if(!ev.key.repeat && SDL_SetClipboardText(position)!=0)
                            rt_log("track debug: could not copy: %s\n",SDL_GetError());
                        break;
                    }
                }
#ifdef GAME_ENH_HOOK_EXPLORER_CAMERA
                if (g_enhanced && ev.key.keysym.sym == SDLK_F6) { if (!ev.key.repeat) explorer_toggle(); break; }
                if (g_enhanced && ev.key.keysym.sym == SDLK_F7) { if (!ev.key.repeat) explorer_free_toggle(); break; }
#endif
                if (explorer_active()) {
                    SDL_Keycode k = ev.key.keysym.sym;
                    if (k == SDLK_LEFTBRACKET || k == SDLK_RIGHTBRACKET) { explorer_adjust(k == SDLK_LEFTBRACKET ? -10 : 10, 0); break; }
                    if (!explorer_free() && (k == SDLK_MINUS || k == SDLK_EQUALS)) { explorer_adjust(0, k == SDLK_MINUS ? -2 : 2); break; }
                }
                if (g_enhanced && ev.key.keysym.sym == SDLK_F10) { if (!ev.key.repeat) enh_track_debug_toggle(); break; }
                if (ev.key.keysym.sym == SDLK_F8) { if (!ev.key.repeat) test_rumble(); break; }
                if (ev.key.keysym.sym == SDLK_F9) {
                    if (!ev.key.repeat) {
                        g_controller_log = !g_controller_log;
                        rt_log("controller input logging: %s\n", g_controller_log ? "on" : "off");
                    }
                    break;
                }
                if(g_enhanced && (ev.key.keysym.sym==SDLK_UP || ev.key.keysym.sym==SDLK_DOWN) &&
                   enh_track_debug_step(ev.key.keysym.sym==SDLK_UP ? 1 : -1)) break;
                if ((enh_menu_active() || enh_paused()) && menu_key(ev.key.keysym.sym) >= 0) {
                    int action = menu_key(ev.key.keysym.sym);
                    g_menu_repeat_button = -1;
                    if (!ev.key.repeat || menu_action_repeats(action)) enh_menu_action(action);
                } else if (ev.key.keysym.sym == SDLK_ESCAPE) {
                    if (!ev.key.repeat && !enh_escape()) running = 0;   /* enhanced: pause / back */
                }
                else if (ev.key.keysym.sym == SDLK_F11) {
                    Uint32 fs = SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                    SDL_SetWindowFullscreen(win, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                    enh_set_fullscreen(!fs);    /* enhanced mode: remembered in the settings */
                    fs_applied = !fs;
                } else if (!(enh_name_entry_active() && !enh_paused() && name_key(ev.key.keysym.sym)))
                    key(ev.key.keysym.sym, 1);
                break;
            case SDL_KEYUP: key(ev.key.keysym.sym, 0); break;
            case SDL_TEXTINPUT:
                if (enh_name_entry_active() && !enh_paused())
                    for (const char *p = ev.text.text; *p; p++) enh_name_type((unsigned char)*p);
                break;
            case SDL_CONTROLLERDEVICEADDED: open_pad(); break;
            case SDL_CONTROLLERDEVICEREMOVED:
                if (pad_matches(ev.cdevice.which)) { close_pad(); open_pad(); }
                break;
            case SDL_CONTROLLERBUTTONDOWN:
                if (pad_matches(ev.cbutton.which)) g_menu_repeat_button = -1;
                if (g_gyro_active && g_pad && ev.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)) &&
                    ev.cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
                    frontend_gyro_set_enabled(!g_gyro_enabled);
                    enh_controller_settings_changed();
                    rt_log("gyro steering: %s (right-stick click toggles; left-stick click recenters)\n",
                           g_gyro_enabled ? "on" : "off");
                    break;
                }
                if (!pad_matches(ev.cbutton.which) || !g_input_focus) break;
#if SDL_VERSION_ATLEAST(2, 0, 14)
                if (g_pad && ev.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)) &&
                    SDL_GetKeyboardFocus() && ev.cbutton.button == SDL_CONTROLLER_BUTTON_MISC1) {
                    test_rumble(); break;  /* Switch Capture: test the transport independently of game FFB. */
                }
#endif
                if(ev.cbutton.button==SDL_CONTROLLER_BUTTON_BACK && enh_gate_editor_button(GATE_EDIT_SELECT)) break;
                if(enh_gate_editor_active()) {
                    int action=-1;
                    switch(ev.cbutton.button) {
                    case SDL_CONTROLLER_BUTTON_START:action=GATE_EDIT_ACCEPT;break;
                    case SDL_CONTROLLER_BUTTON_GUIDE:action=GATE_EDIT_SET_START;break;
                    case SDL_CONTROLLER_BUTTON_LEFTSTICK:action=GATE_EDIT_CAMERA;break;
                    case SDL_CONTROLLER_BUTTON_A:action=GATE_EDIT_ADD;break;
                    case SDL_CONTROLLER_BUTTON_X:action=GATE_EDIT_DELETE;break;
                    case SDL_CONTROLLER_BUTTON_B:action=GATE_EDIT_PREV;break;
                    case SDL_CONTROLLER_BUTTON_Y:action=GATE_EDIT_NEXT;break;
                    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:action=GATE_EDIT_ROLE_UP;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_UP:action=GATE_EDIT_RAISE;break;
                    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:action=GATE_EDIT_ROLE_DOWN;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:action=GATE_EDIT_LOWER;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:action=GATE_EDIT_ROTATE_LEFT;break;
                    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:action=GATE_EDIT_ROTATE_RIGHT;break;
                    }
                    if(action>=0) {
                        enh_gate_editor_button(action);
                        int repeats=action==GATE_EDIT_ROTATE_LEFT || action==GATE_EDIT_ROTATE_RIGHT ||
                            action==GATE_EDIT_RAISE || action==GATE_EDIT_LOWER ||
                            action==GATE_EDIT_PREV || action==GATE_EDIT_NEXT;
                        g_gate_repeat_button=repeats?ev.cbutton.button:-1;
                        g_gate_repeat_action=action;g_gate_repeat_at=SDL_GetTicks()+350;
                    }
                    break;
                }
                if(ev.cbutton.button==SDL_CONTROLLER_BUTTON_Y && enh_mission_retry()) break;
                if (pad_pause_press(ev.cbutton.button)) break;
                if(g_enhanced && (ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP || ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_DOWN) &&
                   enh_track_debug_step(ev.cbutton.button==SDL_CONTROLLER_BUTTON_DPAD_UP ? 1 : -1)) break;
                if ((enh_menu_active() || enh_paused()) && menu_button(ev.cbutton.button) >= 0) menu_pad_press(ev.cbutton.button, SDL_GetTicks());
                else if (ev.cbutton.button == SDL_CONTROLLER_BUTTON_GUIDE) {
                    if (!enh_escape()) running = 0;  /* Home: pause/back in play, quit from main menu. */
                }
                else if (enh_name_entry_active() && !enh_paused() && (ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
                                                                       ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
                    enh_name_step(ev.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ? -1 : 1);
                else pad_button(ev.cbutton.button, 1);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if(ev.button.button==SDL_BUTTON_LEFT && enh_gate_editor_active())
                    enh_gate_editor_mouse((float)ev.button.x/tw,(float)ev.button.y/th);
                break;
            case SDL_CONTROLLERBUTTONUP:
                if (pad_matches(ev.cbutton.which)) {
                    if (g_menu_repeat_button == ev.cbutton.button) g_menu_repeat_button = -1;
                    if(g_gate_repeat_button==ev.cbutton.button) g_gate_repeat_button=-1;
                    pad_button(ev.cbutton.button, 0);
                }
                break;
            default: break;
            }
        }
        window_focus(SDL_GetWindowFlags(win));
        menu_pad_repeat(SDL_GetTicks());
        if(!g_input_focus || !g_pad || !enh_gate_editor_active()) g_gate_repeat_button=-1;
        if(g_gate_repeat_button>=0 && (Sint32)(SDL_GetTicks()-g_gate_repeat_at)>=0) {
            if(SDL_GameControllerGetButton(g_pad,g_gate_repeat_button)) {
                enh_gate_editor_button(g_gate_repeat_action);g_gate_repeat_at=SDL_GetTicks()+80;
            } else g_gate_repeat_button=-1;
        }
        update_rumble((SDL_GetWindowFlags(win) & SDL_WINDOW_INPUT_FOCUS) &&
                      !enh_paused() && !enh_menu_active() && !enh_turbo() && !enh_inputs_owned());
        uint64_t now = SDL_GetPerformanceCounter();
        apply_inputs((double)(now - last_tick) / g_pace_freq);
        last_tick = now;
        if ((double)(now - last_save) / g_pace_freq > 60.0) { nvram_save(); last_save = now; }
        if (enh_quit_requested()) running = 0;
        if (enh_restart_requested()) { running = 0; restart = 1; }

        int w, h;
        uint64_t cnt = voodoo_get_frame(NULL, 0, &w, &h);
        int fresh = cnt != last_frame && w > 0 && h > 0;
        if (fresh) {
            last_frame = cnt;
            unsigned depth_mode;
            last_frame = voodoo_get_frame_depth(raw, scene_depth, 2048 * 2048, &w, &h, &depth_mode);
            enh_overlay_depth(scene_depth, w, h, depth_mode);
            if (w != tw || h != th) {
                /* Fit new windows to the game aspect ratio, but preserve restored user dimensions. */
                if (!restored_window && (long)w * th != (long)h * tw && !(SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN_DESKTOP)) {
                    int ww, wh;
                    SDL_GetWindowSize(win, &ww, &wh);
                    SDL_SetWindowSize(win, (int)((long)wh * w / h), wh);
                }
                SDL_DestroyTexture(tex);
                tex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, w, h);
                window_filter_applied = -1;
                SDL_RenderSetLogicalSize(ren, w, h);
                tw = w; th = h;
            }
        }
        /* the overlay is redrawn over the last game frame every loop, so the enhanced menus
         * respond while the game is paused */
        if (fresh || (g_enhanced && last_frame)) {
            memcpy(frame, raw, (size_t)tw * th * 4);
            enh_draw_overlay(frame, tw, th);
            SDL_UpdateTexture(tex, NULL, frame, tw * 4);
        }
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
        SDL_RenderClear(ren);
        int window_filter = enh_texture_filter() == 1 ? SDL_ScaleModeNearest : SDL_ScaleModeLinear;
        if (window_filter != window_filter_applied) {
            SDL_SetTextureScaleMode(tex, (SDL_ScaleMode)window_filter);
            window_filter_applied = window_filter;
        }
        SDL_RenderCopy(ren, tex, NULL, NULL);
        SDL_RenderPresent(ren);       /* vsync paces this loop */
        Uint32 window_now = SDL_GetTicks();
        if (window_state_capture(win, &window_rect)) { window_dirty = 1; window_changed = window_now; }
        if (window_dirty && (Uint32)(window_now - window_changed) >= 500) {
            if (!window_state_save(g_window_state_path, &window_rect)) rt_log("could not save game window position\n");
            window_dirty = 0;
        }
    }
    g_rumble_testing = 0;
    update_rumble(0);
    window_state_capture(win, &window_rect);
    window_state_save(g_window_state_path, &window_rect);
    nvram_save();
    if (g_audio) SDL_CloseAudioDevice(g_audio);
    close_pad();
#ifdef VIPER_NATIVE_HAPTICS
    controller_haptics_stop();
#endif
    SDL_Quit();
    return restart ? 2 : 0;
}
