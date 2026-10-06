/* Restart through the game's own course loader, preserving its pre-race selections.
 * This saves only game control records, never CPU stacks, kernel state or devices. */
#pragma once
#include <stdatomic.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifndef GAME_ENH_RACE_RESTART_STYLE
#define GAME_ENH_RACE_RESTART_STYLE 0
#endif

typedef struct { uint32_t toc_offset, offset, size; } RaceRecord;
typedef struct { uint32_t address, size; uint8_t data[0x1000]; } RaceCopy;
static RaceCopy g_race_copy[5];
static uint32_t g_race_toc;
static int g_race_captured, g_race_previous_mode = -1;
static atomic_int g_race_ready, g_race_restart;
static atomic_int g_race_time_trial, g_race_unlimited_laps;
static atomic_int g_race_mission_practice;
static atomic_int g_practice_reset = 1;
static atomic_uint g_practice_lap = 1, g_practice_last_ms, g_practice_best_ms;
static atomic_uint g_practice_current_ms, g_practice_checkpoint_ms, g_practice_checkpoint;
static atomic_int g_practice_course;
static const char *const g_practice_courses[] = { "TOWN", "COAST", "MOUNTAIN" };
static double g_practice_lap_end;
static uint64_t g_practice_checkpoint_until;

static int race_practice_checkpoint_visible(void) {
    return atomic_load(&g_practice_checkpoint) && rt_now() < g_practice_checkpoint_until;
}

static int race_restart_available(void) { return atomic_load(&g_race_ready); }
static int race_restart_pending(void) { return atomic_load(&g_race_restart); }
static int race_time_trial(void) { return atomic_load(&g_race_time_trial); }
static int race_unlimited_laps(void) { return atomic_load(&g_race_unlimited_laps); }
static int race_practice_hud_visible(void) {
    return race_unlimited_laps() && race_time_trial() && race_restart_available() &&
        !race_restart_pending() && !atomic_load(&g_practice_reset);
}
static void race_unlimited_toggle(void) {
    if (race_time_trial()) {
        atomic_store(&g_race_unlimited_laps, !race_unlimited_laps());
        rt_log("enhanced: time trial unlimited laps %s\n", race_unlimited_laps() ? "on" : "off");
    }
}
static void race_restart_cancel(void) {
    atomic_store(&g_race_ready, 0);
    atomic_store(&g_race_restart, 0);
    atomic_store(&g_race_time_trial, 0);
    atomic_store(&g_race_unlimited_laps, 0);
    atomic_store(&g_practice_reset, 1);
    g_race_captured = 0;
}
static void race_restart_request(void) {
    if (race_restart_available()) atomic_store(&g_race_restart, 1);
}
static int race_valid(uint32_t address, uint32_t size) {
    return address >= 0x38040 && address < RAM_SIZE && size <= RAM_SIZE - address;
}

static void race_dispatch_hook(PPCContext *c) {
    if (!GAME_ENH_RACE_RESTART_STYLE) return;
    const int gti = GAME_ENH_RACE_RESTART_STYLE == 1;
    uint32_t toc = c->r[2];
    if (!race_valid(toc, 0x758)) { race_restart_cancel(); return; }
    uint32_t state = LD32(toc + (gti ? 0x54 : 0x604));
    if (!race_valid(state, 0x18)) { race_restart_cancel(); return; }
    int mode = gti ? (int)((LD32(state + 4) >> 23) & 15) : (int)LD32(state + 8);
    const int load_mode = gti ? 9 : 21, race_mode = gti ? 11 : 27;
    if (getenv("RT_ENH_LOG") && mode != g_race_previous_mode) rt_log("enhanced: race control mode %d\n", mode);
    const RaceRecord gti_records[5] = {
        {0x54, 0, 0x20}, {0xcc, 0, 0x1000}, {0x124, 0, 0x100},
        {0x3c, 0x1fffe8, 0x18}, {0x754, 0, 0x10}
    };
    const RaceRecord td2_records[5] = {
        {0x60, 0, 0x20}, {0x114, 0, 0x1000}, {0x178, 0, 0x100},
        {0x604, 0, 0x18}, {0x648, 0, 0x20}
    };
    const RaceRecord *records = gti ? gti_records : td2_records;
    if (atomic_load(&g_race_restart) == 1) {
        if (!g_race_captured || toc != g_race_toc || mode != race_mode) {
            race_restart_cancel();
        } else {
            for (int i = 0; i < 5; i++)
                memcpy(g_ram + g_race_copy[i].address, g_race_copy[i].data, g_race_copy[i].size);
            mode = load_mode;
            atomic_store(&g_race_ready, 0);
            atomic_store(&g_race_restart, 2);
            atomic_store(&g_practice_reset, 1);
            rt_log("enhanced: restarting current race with its saved selections\n");
        }
    }
    if (mode == load_mode && g_race_previous_mode != load_mode && !race_restart_pending()) {
        atomic_store(&g_race_unlimited_laps, 0);
        atomic_store(&g_practice_reset, 1);
        g_race_captured = 0;
        for (int i = 0; i < 5; i++) {
            uint32_t address = LD32(toc + records[i].toc_offset) + records[i].offset;
            if (!race_valid(address, records[i].size)) { race_restart_cancel(); return; }
            g_race_copy[i].address = address;
            g_race_copy[i].size = records[i].size;
            memcpy(g_race_copy[i].data, g_ram + address, records[i].size);
        }
        g_race_toc = toc;
        g_race_captured = 1;
        rt_log("enhanced: saved current race selections\n");
    }
    if (mode == race_mode && g_race_captured) {
        atomic_store(&g_race_time_trial, gti && (LD32(state + 0x10) & 0x400000) != 0);
        if (!race_time_trial()) atomic_store(&g_race_unlimited_laps, 0);
        atomic_store(&g_race_ready, 1);
        if (atomic_exchange(&g_race_restart, 0)) rt_log("enhanced: current race restarted\n");
    } else if (mode < load_mode || mode > race_mode) {
        race_restart_cancel();
    }
    g_race_previous_mode = mode;
}

/* GTI Club's remaining vblanks occupy bits 6..19 of the control word at +0xc.
 * At the native countdown store (JAB b45b4 / EAA b45d8), r8 is the old word and
 * r5 is the decremented counter. Keeping r5 unchanged leaves lap clocks and the
 * rest of the race update running normally. */
static int race_practice_active(PPCContext *c) {
    if (GAME_ENH_RACE_RESTART_STYLE != 1 ||
        (!race_unlimited_laps() && !atomic_load(&g_race_mission_practice)) || !race_time_trial()) return 0;
    uint32_t toc = c->r[2];
    if (toc != g_race_toc || !race_valid(toc, 0x558)) return 0;
    uint32_t state = LD32(toc + 0x54);
    if (!race_valid(state, 0x14) || ((LD32(state + 4) >> 23) & 15) != 11 ||
        !(LD32(state + 0x10) & 0x400000)) return 0;
    return 1;
}
static void race_countdown_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    c->r[5] = (c->r[8] >> 6) & 0x3fff;
}
/* Checkpoint extensions must not wrap the remaining fourteen-bit counter. */
static void race_time_bonus_hook(PPCContext *c) {
    if (race_practice_active(c)) c->r[3] = 0;
}
/* Only the HUD formatter sees a lap-relative clock; native timing stays cumulative. */
static void race_lap_clock_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    c->f[1] = fmax(0, c->f[1] - g_practice_lap_end);
    if (isfinite(c->f[1]) && c->f[1] < UINT32_MAX / 1000.0)
        atomic_store(&g_practice_current_ms, (unsigned)lround(c->f[1] * 1000));
}

/* The Time Attack course label/time calls pass their format string in r6.
 * Point at its terminating NUL so the native renderer emits no glyphs. */
static void race_practice_text_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    uint32_t text = c->r[6];
    for (unsigned i = 0; i < sizeof g_practice_courses / sizeof *g_practice_courses; i++) {
        size_t size = strlen(g_practice_courses[i]) + 1;
        if (race_valid(text, (uint32_t)size) && !memcmp(g_ram + text, g_practice_courses[i], size))
            atomic_store(&g_practice_course, (int)i);
    }
    if (race_valid(text, 64)) {
        uint8_t *end = memchr(g_ram + text, 0, 64);
        if (end) c->r[6] = (uint32_t)(end - g_ram);
    }
}

/* Called only when the native forward checkpoint gate accepts a crossing. */
static void race_practice_checkpoint_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    uint32_t car = LD32(c->r[2] + 0x488), timing = LD32(c->r[2] + 0x554);
    if (!race_valid(car, 0x514) || c->r[3] != car + 0x4dc || !race_valid(timing, 0xd4)) return;
    unsigned checkpoint = LD8(c->r[3] + 2);
    if (checkpoint <= 1 || checkpoint - 1 <= atomic_load(&g_practice_checkpoint)) return;
    double seconds = LDF32(timing + 0x24) - g_practice_lap_end;
    if (!isfinite(seconds) || seconds <= 0 || seconds >= UINT32_MAX / 1000.0) return;
    atomic_store(&g_practice_checkpoint_ms, (unsigned)lround(seconds * 1000));
    atomic_store(&g_practice_checkpoint, checkpoint - 1);
    g_practice_checkpoint_until = rt_now() + 5 * (uint64_t)CPU_HZ;
}

/* After local route tracking, before timing/checkpoints (JAB a9c30 / EAA a9c54).
 * Native route laps are bytes, lap clocks have ten slots and split history uses
 * byte/six-bit indices. Reuse lap 1 at each forward crossing, before any native
 * finish test or history write. Keep the native elapsed clock running in slot 1;
 * host counters provide the practice lap number and last/best lap durations.
 * The route's geometry, along-course distance and car position remain intact. */
static void race_laps_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    uint32_t toc = c->r[2], route = c->r[3];
    uint32_t car = LD32(toc + 0x488), course = LD32(toc + 0x400);
    uint32_t timing = LD32(toc + 0x554), state = LD32(toc + 0x54);
    if (!race_valid(car, 0x514) || route != car + 0x4dc || !race_valid(course, 0x30) ||
        !race_valid(timing, 0xd4)) return;
    double length = LDF32(course + 0x2c);
    if (!isfinite(length) || length <= 0) return;
    unsigned lap = LD8(route + 1);
    int reset = atomic_load(&g_practice_reset);
    if (reset) {
        double elapsed = 0, previous_laps = 0;
        unsigned slots = LD8(timing + 0x10);
        unsigned current = LD8(timing + 0x21);
        if (slots > 10) slots = 10;
        for (unsigned i = 0; i < slots; i++) {
            double seconds = LDF32(timing + 0x24 + i * 4);
            elapsed += seconds;
            if (i + 1 < current) previous_laps += seconds;
        }
        for (unsigned i = 1; i < 10; i++) STF32(timing + 0x24 + i * 4, 0);
        STF32(timing + 0x24, elapsed);
        ST8(timing + 0x21, 1);
        atomic_store(&g_practice_lap, 1);
        atomic_store(&g_practice_last_ms, 0);
        atomic_store(&g_practice_best_ms, 0);
        atomic_store(&g_practice_current_ms, 0);
        atomic_store(&g_practice_checkpoint_ms, 0);
        atomic_store(&g_practice_checkpoint, 0);
        g_practice_lap_end = previous_laps;
        atomic_store(&g_practice_reset, 0); /* publish visibility after clearing old HUD data */
    }
    if (lap <= 1) return; /* lap 0 is the native reverse-crossing state */
    /* The route origin can wrap before the finish line. Native lap timing waits
     * for checkpoint 1 as well as a new route lap (a9ff8..aa014). */
    if (!reset && LD8(route + 2) != 1) return;
    if (!reset) {
        double elapsed = LDF32(timing + 0x24);
        double duration = elapsed - g_practice_lap_end;
        if (isfinite(duration) && duration > 0 && duration < UINT32_MAX / 1000.0) {
            unsigned ms = (unsigned)lround(duration * 1000);
            unsigned best = atomic_load(&g_practice_best_ms);
            atomic_store(&g_practice_last_ms, ms);
            if (!best || ms < best) atomic_store(&g_practice_best_ms, ms);
            g_practice_lap_end = elapsed;
        }
        atomic_fetch_add(&g_practice_lap, 1);
        atomic_store(&g_practice_checkpoint_ms, 0);
        atomic_store(&g_practice_checkpoint, 0);
    }
    double distance = (lap - 1) * length;
    ST8(route + 1, 1);
    STF32(route + 8, LDF32(route + 8) - distance);
    STF32(route + 0xc, LDF32(route + 0xc) - distance);
    STF32(route + 0x14, LDF32(route + 0x14) - distance);
    ST8(timing + 0x11, 1);
    ST8(timing + 0x12, 0);
    ST8(timing + 0x14, 0);
    ST8(timing + 0x15, 1);
    STF32(timing + 0x1c, 0);
    STF32(timing + 0xc8, 0);
    ST32(state + 0xc, LD32(state + 0xc) & ~0x3fu);
    if (getenv("RT_ENH_LOG")) rt_log("enhanced: practice lap %u, last %.3f s\n",
        atomic_load(&g_practice_lap), atomic_load(&g_practice_last_ms) / 1000.0);
}

/* A zero sprite colour makes the native queue omit the countdown glyphs.
 * Restore it after this draw so every other HUD element keeps its own colour. */
static uint32_t g_practice_hud_state, g_practice_hud_colour;
static void race_practice_hud_hook(PPCContext *c) {
    if (!race_practice_active(c)) return;
    uint32_t sprites = LD32(c->r[2] + 0xf4);
    if (!race_valid(sprites, 0x166c)) return;
    g_practice_hud_state = sprites;
    g_practice_hud_colour = LD32(sprites + 0x1668);
    ST32(sprites + 0x1668, 0);
}
static void race_practice_hud_end_hook(PPCContext *c) {
    (void)c;
    if (g_practice_hud_state) {
        ST32(g_practice_hud_state + 0x1668, g_practice_hud_colour);
        g_practice_hud_state = 0;
    }
}
