#include "../runtime/runtime.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../runtime/race_restart.h"

uint8_t *g_ram;
static uint64_t now;
uint64_t rt_now(void) { return now; }
void rt_log(const char *format, ...) { (void)format; }
uint32_t rt_mmio_r32(uint32_t address) { (void)address; assert(0); return 0; }
void rt_mmio_w32(uint32_t address, uint32_t value) { (void)address; (void)value; assert(0); }
uint32_t rt_mmio_r8(uint32_t address) { (void)address; assert(0); return 0; }
void rt_mmio_w8(uint32_t address, uint32_t value) { (void)address; (void)value; assert(0); }

static void set_mode(uint32_t state, int mode) {
    if (GAME_ENH_RACE_RESTART_STYLE == 1) ST32(state + 4, (uint32_t)mode << 23);
    else ST32(state + 8, (uint32_t)mode);
}

int main(void) {
    const int gti = GAME_ENH_RACE_RESTART_STYLE == 1;
    const int load_mode = gti ? 9 : 21, race_mode = gti ? 11 : 27;
    g_ram = malloc(RAM_SIZE); assert(g_ram); memset(g_ram, 0xa5, RAM_SIZE);
    PPCContext c = {0}; c.r[2] = 0x100000;
    const uint32_t offsets[5] = { gti ? 0x54 : 0x60, gti ? 0xcc : 0x114,
        gti ? 0x124 : 0x178, gti ? 0x3c : 0x604, gti ? 0x754 : 0x648 };
    for (int i = 0; i < 5; i++) ST32(c.r[2] + offsets[i], 0x200000 + i * 0x10000);
    uint32_t state = LD32(c.r[2] + (gti ? 0x54 : 0x604));
    uint32_t players = LD32(c.r[2] + offsets[1]);
    ST32(players + 0x30, 3); /* selected car */
    ST32(players + 0x34, 1); /* transmission */
    set_mode(state, load_mode); race_dispatch_hook(&c);
    assert(g_race_captured && !race_restart_available());
    set_mode(state, race_mode); race_dispatch_hook(&c);
    assert(race_restart_available());
    assert(!race_time_trial()); race_unlimited_toggle(); assert(!race_unlimited_laps());
    if (gti) {
        ST32(state + 0x10, 0x400000); race_dispatch_hook(&c);
        assert(race_time_trial()); race_unlimited_toggle(); assert(race_unlimited_laps());
        assert(!race_practice_hud_visible()); /* timing has not initialized yet */
        c.r[8] = 0x80100005 | (3200 << 6); c.r[5] = 3198;
        double elapsed = c.f[1] = 12.34;
        race_countdown_hook(&c);
        assert(c.r[5] == 3200 && c.r[8] == (0x80100005u | (3200 << 6)) && c.f[1] == elapsed);
        const uint32_t car = 0x500000, route = car + 0x4dc, course = 0x510000, timing = 0x520000;
        ST32(c.r[2] + 0x488, car); ST32(c.r[2] + 0x400, course); ST32(c.r[2] + 0x554, timing);
        memset(g_ram + timing, 0, 0x100);
        STF32(course + 0x2c, 5000); ST8(route + 1, 1); ST8(timing + 0x10, 1);
        c.r[3] = 580; race_time_bonus_hook(&c); assert(c.r[3] == 0);
        /* Enabling on native lap 2 preserves elapsed time and that lap's start. */
        ST8(timing + 0x10, 3); ST8(timing + 0x21, 2);
        STF32(timing + 0x24, 70); STF32(timing + 0x28, 40);
        ST8(route + 1, 2); STF32(route + 8, 12000); STF32(route + 0xc, 10000);
        c.r[3] = route; race_laps_hook(&c);
        assert(LDF32(timing + 0x24) == 110 && LDF32(timing + 0x28) == 0 && g_practice_lap_end == 70);
        assert(LD8(route + 1) == 1 && atomic_load(&g_practice_lap) == 1);
        assert(race_practice_hud_visible());
        atomic_store(&g_practice_reset, 1); ST8(timing + 0x10, 1); STF32(timing + 0x24, 0);
        ST8(route + 1, 1); c.r[3] = route; race_laps_hook(&c);
        /* Route origin wraps while still in the final checkpoint: no timed lap yet. */
        ST8(route + 1, 2); ST8(route + 2, 10);
        STF32(timing + 0x24, 65); race_laps_hook(&c);
        assert(LD8(route + 1) == 2 && atomic_load(&g_practice_lap) == 1);
        assert(atomic_load(&g_practice_best_ms) == 0 && g_practice_lap_end == 0);
        for (unsigned lap = 1; lap <= 1000; lap++) {
            ST8(route + 1, 2); ST8(route + 2, 1);
            STF32(route + 8, 10010); STF32(route + 0xc, 10000); STF32(route + 0x14, 9990);
            STF32(route + 0x10, 10); STF32(route + 0x18, 0);
            STF32(timing + 0x24, lap * 70.0); ST8(timing + 0x11, 1); ST8(timing + 0x12, 10);
            ST8(timing + 0x15, 11); ST8(timing + 0x14, 10);
            ST32(state + 0xc, (3200 << 6) | 10);
            race_laps_hook(&c);
            assert(LD8(route + 1) == 1 && LDF32(route + 8) == 5010 && LDF32(route + 0xc) == 5000);
            assert(LDF32(route + 0x14) == 4990 && LDF32(route + 0x10) == 10 && LDF32(route + 0x18) == 0);
            assert(LD8(timing + 0x11) == 1 && LD8(timing + 0x12) == 0 && LD8(timing + 0x15) == 1);
            assert(LDF32(timing + 0x1c) == 0 && LD32(state + 0xc) == (3200 << 6));
            assert(LDF32(timing + 0x24) == lap * 70.0 && atomic_load(&g_practice_lap) == lap + 1);
            assert(atomic_load(&g_practice_best_ms) == 70000 && atomic_load(&g_practice_last_ms) == 70000);
        }
        c.f[1] = 70012.345; race_lap_clock_hook(&c);
        assert(fabs(c.f[1] - 12.345) < 0.00001 && LDF32(timing + 0x24) == 70000);
        assert(atomic_load(&g_practice_current_ms) == 12345);
        ST8(route + 1, 1); ST8(route + 2, 4); STF32(timing + 0x24, 70012.5);
        c.r[3] = route; race_practice_checkpoint_hook(&c);
        assert(atomic_load(&g_practice_checkpoint) == 3 && atomic_load(&g_practice_checkpoint_ms) == 12500);
        assert(race_practice_checkpoint_visible());
        now += 5 * (uint64_t)CPU_HZ - 1;
        assert(race_practice_checkpoint_visible());
        now++;
        assert(!race_practice_checkpoint_visible());
        /* Expiry hides the split without allowing repeats to refresh its timer. */
        /* Repeated or reverse checkpoint events cannot overwrite a forward split. */
        STF32(timing + 0x24, 70020); race_practice_checkpoint_hook(&c);
        ST8(route + 2, 3); race_practice_checkpoint_hook(&c);
        assert(atomic_load(&g_practice_checkpoint_ms) == 12500);
        assert(!race_practice_checkpoint_visible());
        ST8(route + 2, 5); STF32(route + 8, LDF32(route + 8) + 100); race_practice_checkpoint_hook(&c);
        assert(atomic_load(&g_practice_checkpoint) == 4 && race_practice_checkpoint_visible());
        now += 5 * (uint64_t)CPU_HZ;
        assert(!race_practice_checkpoint_visible());
        /* An accepted forward crossing can have a lower route checkpoint ID. */
        ST8(route + 2, 3); STF32(route + 8, LDF32(route + 8) + 100);
        race_practice_checkpoint_hook(&c);
        assert(atomic_load(&g_practice_checkpoint) == 2 && race_practice_checkpoint_visible());
        STF32(timing + 0x24, 70000);
        memcpy(g_ram + 0x540000, "COAST", 6);
        c.r[6] = 0x540000; c.r[5] = 0x00ff00; race_practice_text_hook(&c);
        assert(c.r[6] == 0x540005 && c.r[5] == 0x00ff00 && atomic_load(&g_practice_course) == 1);
        c.f[1] = 69999; race_lap_clock_hook(&c); assert(c.f[1] == 0);
        ST8(route + 1, 0); race_laps_hook(&c); assert(LD8(route + 1) == 0 && atomic_load(&g_practice_lap) == 1001);
        ST32(c.r[2] + 0xf4, 0x530000); ST32(0x531668, 0xffffffff);
        c.f[1] = 400; race_practice_hud_hook(&c); assert(LD32(0x531668) == 0 && c.f[1] == 400);
        race_practice_hud_end_hook(&c); assert(LD32(0x531668) == 0xffffffff);
        race_unlimited_toggle(); c.r[3] = 580; race_time_bonus_hook(&c); assert(c.r[3] == 580);
        c.r[6] = 0x540000; race_practice_text_hook(&c); assert(c.r[6] == 0x540000);
        c.f[1] = 70012.345; race_lap_clock_hook(&c); assert(c.f[1] == 70012.345);
        c.r[3] = route; ST8(route + 1, 2); race_laps_hook(&c); assert(LD8(route + 1) == 2);
        c.f[1] = 400; race_practice_hud_hook(&c); assert(c.f[1] == 400 && LD32(0x531668) == 0xffffffff);
        race_unlimited_toggle();
        assert(atomic_load(&g_practice_lap) == 1001 && atomic_load(&g_practice_best_ms) == 70000);
        c.r[3] = route + 0x100; race_laps_hook(&c); assert(LD8(route + 1) == 2); /* local car only */
        ST32(state + 0x10, 0); c.r[5] = 3198; race_countdown_hook(&c); assert(c.r[5] == 3198);
        race_dispatch_hook(&c); assert(!race_time_trial() && !race_unlimited_laps());
        ST32(state + 0x10, 0x400000);
        set_mode(state, load_mode); race_dispatch_hook(&c);
        set_mode(state, race_mode); race_dispatch_hook(&c); race_unlimited_toggle();
    }
    for (int attempt = 0; attempt < 3; attempt++) {
        for (int i = 0; i < 5; i++) memset(g_ram + g_race_copy[i].address, 0x17, g_race_copy[i].size);
        set_mode(state, race_mode);
        race_restart_request(); assert(race_restart_pending() == 1);
        assert(!race_practice_hud_visible());
        race_dispatch_hook(&c);
        assert(race_restart_pending() == 2 && !race_restart_available());
        if (gti) assert(race_unlimited_laps() && atomic_load(&g_practice_reset));
        for (int i = 0; i < 5; i++)
            assert(!memcmp(g_ram + g_race_copy[i].address, g_race_copy[i].data, g_race_copy[i].size));
        assert(LD32(players + 0x30) == 3 && LD32(players + 0x34) == 1);
        for (uint32_t i = 0; i < 0x38040; i++) assert(g_ram[i] == 0xa5);
        assert(g_ram[0xff1000] == 0xa5); /* live CPU stack remains intact */
        set_mode(state, load_mode + 1); race_dispatch_hook(&c);
        assert(race_restart_pending() == 2);
        set_mode(state, race_mode); race_dispatch_hook(&c);
        assert(race_restart_available() && !race_restart_pending());
        assert(!race_practice_hud_visible()); /* old labels stay hidden until fresh timing */
        if (gti) {
            uint32_t car = LD32(c.r[2] + 0x488), timing = LD32(c.r[2] + 0x554);
            ST8(car + 0x4dd, 1); ST8(timing + 0x21, 1); STF32(timing + 0x24, 0);
            c.r[3] = car + 0x4dc; race_laps_hook(&c);
            assert(race_practice_hud_visible() && atomic_load(&g_practice_lap) == 1);
            assert(!atomic_load(&g_practice_best_ms) && !atomic_load(&g_practice_current_ms));
            assert(!atomic_load(&g_practice_checkpoint) && !atomic_load(&g_practice_checkpoint_ms));
        }
    }
    set_mode(state, 0); race_dispatch_hook(&c);
    race_restart_request(); assert(!race_restart_pending() && !race_restart_available());
    assert(!race_time_trial() && !race_unlimited_laps());
    ST32(c.r[2] + offsets[1], RAM_SIZE - 1);
    set_mode(state, load_mode); race_dispatch_hook(&c);
    assert(!g_race_captured && !race_restart_available());
    c.r[2] = RAM_SIZE - 1; race_dispatch_hook(&c);
    assert(!race_restart_available());
    race_laps_hook(&c); race_practice_hud_hook(&c); /* unavailable/TD2 hooks are inert */
    free(g_ram);
    puts(gti ? "GTI Club race restart records: passed" : "Thrill Drive race restart records: passed");
}
