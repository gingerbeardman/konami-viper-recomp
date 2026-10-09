/* Wii Remote rumble tester, for tuning the port's rumble on real hardware.
 * The remote's motor is on/off only (one bit in every output report), so
 * strength has to come from timing: software PWM, the motor switched on for
 * ON frames out of every PERIOD frames (60 per second).
 * Each kind of rumble the game asks for has its own PERIOD and ON, and plays
 * in the rhythm the game uses for it (logged from the cabinet motor):
 *   COBBLES      rough surfaces: continuous while on them
 *   CAR RUB      contact with another car: 1 s episodes
 *   WALL SCRAPE  grinding along a wall: 0.3 s bursts
 *   BIG BUMP     kerbs, hard edges, impacts: 0.25 s bursts
 *   STEER PULL   the wheel's steady pull through corners: continuous
 *   FULL ON      the motor on, for reference
 * Hold the remote upright. UP/DOWN pick a row, LEFT/RIGHT change it, A plays
 * or stops the selected kind, B plays all kinds in turn (2 s each), 2 maps the
 * game's own torque straight to power (on-time = torque / 15 of the period,
 * averaged over the kind's command pattern) as the game build's RUMBLE FULL,
 * 1 sets its RUMBLE MILD (half the on-time, same rhythm), HOME
 * saves the settings to sd:/viper/rumble_settings.txt and returns to the
 * Homebrew Channel. The screen counts rumble commands per second (each is a
 * Bluetooth packet). */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <stdio.h>

enum { COBBLES, CAR_RUB, WALL_SCRAPE, BIG_BUMP, STEER_PULL, FULL_ON, N_KINDS };
static const char *const k_kinds[N_KINDS] = { "COBBLES", "CAR RUB", "WALL SCRAPE", "BIG BUMP", "STEER PULL", "FULL ON" };
/* the game's rhythm: active for ACTIVE frames out of every CYCLE (0 = continuous) */
static const unsigned k_active[N_KINDS] = { 0, 60, 18, 15, 0, 0 };
static const unsigned k_cycle[N_KINDS] = { 0, 120, 90, 120, 0, 0 };
/* the game's torque mapped directly: each kind's motor commands (torque 0-15) averaged over its
 * pattern, as on-time = period * torque / 15, with the period matching the command rhythm.
 * cobbles 0xa0/0x89 every 4 frames: 0 and 9; car rub 0x8a 0x80 0x9a 0x80: 10, 0, 10, 0;
 * wall scrape 0xa0 0x88 0x89: 0, 8, 9; big bump and steering pull are not one fixed pattern,
 * so they take the heavy shake threshold (10) and the steady-pull threshold (6) */
static const int k_torque_pattern[N_KINDS][4] = { { 0, 9 }, { 10, 0, 10, 0 }, { 0, 8, 9 }, { 10 }, { 6 }, { 15 } };
static const int k_torque_len[N_KINDS] = { 2, 4, 3, 1, 1, 1 };
static const int k_torque_period[N_KINDS] = { 8, 6, 8, 6, 5, 1 };   /* as wii/input_platform.c */
static int period[N_KINDS], on[N_KINDS];

static void set_game_torque(int mild) {
    for (int k = 0; k < N_KINDS; k++) {
        int sum = 0;
        for (int i = 0; i < k_torque_len[k]; i++) sum += k_torque_pattern[k][i];
        period[k] = k_torque_period[k];
        on[k] = (2 * period[k] * sum + 15 * k_torque_len[k]) / (30 * k_torque_len[k]);   /* rounded */
        if (mild && on[k] > 1 && k != FULL_ON) on[k] /= 2;
    }
}
enum { ROW_KIND, ROW_PERIOD, ROW_ON, N_ROWS };

static int motor;
static unsigned sends, sends_shown;

static void set_motor(int value) {
    if (value == motor) return;
    WPAD_Rumble(WPAD_CHAN_0, value);
    motor = value;
    sends++;
}

static void save(void) {
    if (!fatInitDefault()) return;
    FILE *f = fopen("sd:/viper/rumble_settings.txt", "w");
    if (!f) return;
    fprintf(f, "# kind period on (60 Hz frames: motor on for ON of every PERIOD)\n");
    for (int k = 0; k < N_KINDS; k++) fprintf(f, "%s %d %d\n", k_kinds[k], period[k], on[k]);
    fclose(f);
    fatUnmount("sd:");
}

int main(void) {
    VIDEO_Init();
    WPAD_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_ClearFrameBuffer(mode, xfb, COLOR_BLACK);
    console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    set_game_torque(0);
    int row = 0, kind = COBBLES, playing = 0, tour = 0;
    unsigned frame = 0, started = 0;
    for (;; frame++) {
        WPAD_ScanPads();
        u32 type;
        int connected = WPAD_Probe(WPAD_CHAN_0, &type) == WPAD_ERR_NONE;
        u32 down = connected ? WPAD_ButtonsDown(WPAD_CHAN_0) : 0;
        if (down & WPAD_BUTTON_HOME) break;
        if (down & WPAD_BUTTON_UP) row = (row + N_ROWS - 1) % N_ROWS;
        if (down & WPAD_BUTTON_DOWN) row = (row + 1) % N_ROWS;
        int step = (down & WPAD_BUTTON_RIGHT) ? 1 : (down & WPAD_BUTTON_LEFT) ? -1 : 0;
        if (step) {
            if (row == ROW_KIND) { kind = (kind + N_KINDS + step) % N_KINDS; started = frame; }
            if (row == ROW_PERIOD) { period[kind] += step; if (period[kind] < 1) period[kind] = 1; if (period[kind] > 30) period[kind] = 30; }
            if (row == ROW_ON) on[kind] += step;
        }
        if (on[kind] < 0) on[kind] = 0;
        if (on[kind] > period[kind]) on[kind] = period[kind];
        if (down & WPAD_BUTTON_A) { playing = !playing; tour = 0; started = frame; }
        if (down & WPAD_BUTTON_B) { playing = tour = 1; kind = 0; started = frame; }
        if (down & WPAD_BUTTON_1) set_game_torque(1);
        if (down & WPAD_BUTTON_2) set_game_torque(0);
        if (tour && frame - started >= 120) {
            kind++;
            started = frame;
            if (kind >= N_KINDS) { kind = 0; playing = tour = 0; }
        }

        unsigned t = frame - started;
        int active = playing && (!k_cycle[kind] || t % k_cycle[kind] < k_active[kind]);
        int want = active && (int)(t % (unsigned)period[kind]) < on[kind];
        if (connected) set_motor(want);
        if (frame % 60 == 0) { sends_shown = sends; sends = 0; }

        printf("\x1b[2;0H");
        printf("  WII REMOTE RUMBLE POWER                HOME: save and quit\n\n");
        printf("  UP/DOWN row  LEFT/RIGHT change  A play/stop  B play all\n");
        printf("  1 game mild  2 game full (torque direct)\n\n");
        printf("  %c KIND    %-12s\n", row == ROW_KIND ? '>' : ' ', k_kinds[kind]);
        printf("  %c PERIOD  %2d frames (%5.1f Hz)   \n", row == ROW_PERIOD ? '>' : ' ', period[kind], 60.0 / period[kind]);
        printf("  %c ON      %2d frames (%3d%% power)   \n\n", row == ROW_ON ? '>' : ' ', on[kind], on[kind] * 100 / period[kind]);
        printf("  %-12s motor %-3s  %3u commands/s  remote %-10s\n\n", playing ? (tour ? "PLAYING ALL" : "PLAYING") : "stopped",
               motor ? "ON" : "off", sends_shown, connected ? "connected" : "not found");
        for (int k = 0; k < N_KINDS; k++)
            printf("  %c %-12s %2d of %2d  (%3d%%)   \n", k == kind ? '*' : ' ', k_kinds[k], on[k], period[k], on[k] * 100 / period[k]);
        VIDEO_WaitVSync();
    }
    set_motor(0);
    WPAD_Shutdown();
    printf("\n  saving settings...\n");
    save();
    /* Blank before handing back: the loader's framebuffer is not cleared, so
     * the old buffer would flash as static while it starts. */
    VIDEO_SetBlack(TRUE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    VIDEO_WaitVSync();
    return 0;
}
