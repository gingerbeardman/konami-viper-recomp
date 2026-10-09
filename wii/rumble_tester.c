/* Wii Remote rumble tester, for tuning the port's rumble on real hardware.
 * The remote's motor is on/off only (one bit in every output report), so
 * strength has to come from timing: software PWM, the motor switched on for
 * ON frames out of every PERIOD frames (60 per second).
 * Hold the remote upright. UP/DOWN pick a row, LEFT/RIGHT change it, A starts
 * or stops the pattern, B fires a bump, HOME returns to the Homebrew Channel.
 * Patterns:
 *   CONTINUOUS   motor on
 *   PWM          ON of every PERIOD frames
 *   COBBLES OLD  the game's cobble shake (motor commands 0xa0 and 0x89
 *                alternating every 4 frames) through the port's current rule:
 *                on at torque 6+, at most one change per 6 frames
 *   COBBLES PWM  the same commands, played as PWM while they keep coming
 *   BUMP         B: one burst of BUMP frames
 * The screen counts rumble commands per second: each is a Bluetooth packet. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <stdio.h>

enum { CONTINUOUS, PWM, COBBLES_OLD, COBBLES_PWM, BUMP_ONLY, N_MODES };
static const char *const k_modes[N_MODES] = { "CONTINUOUS", "PWM", "COBBLES OLD", "COBBLES PWM", "BUMP ONLY" };
enum { ROW_MODE, ROW_PERIOD, ROW_ON, ROW_BUMP, N_ROWS };

static int motor;              /* what the remote was last told */
static unsigned sends, sends_shown;

static void set_motor(int on) {
    if (on == motor) return;
    WPAD_Rumble(WPAD_CHAN_0, on);
    motor = on;
    sends++;
}

int main(void) {
    VIDEO_Init();
    WPAD_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    int row = 0, mode_i = COBBLES_PWM, period = 6, on = 1, bump = 6, running = 0;
    unsigned frame = 0, bump_left = 0, old_hold = 0;
    int old_on = 0;
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
            if (row == ROW_MODE) mode_i = (mode_i + N_MODES + step) % N_MODES;
            if (row == ROW_PERIOD) { period += step; if (period < 1) period = 1; if (period > 60) period = 60; }
            if (row == ROW_ON) on += step;
            if (row == ROW_BUMP) { bump += step; if (bump < 1) bump = 1; if (bump > 60) bump = 60; }
        }
        if (on < 0) on = 0;
        if (on > period) on = period;
        if (down & WPAD_BUTTON_A) running = !running;
        if (down & WPAD_BUTTON_B) bump_left = (unsigned)bump;

        /* the game's cobble commands: torque 0 (shake bit) and torque 9, every 4 frames */
        int cmd_torque = (frame / 4) & 1 ? 9 : 0;
        int want = 0;
        if (running) switch (mode_i) {
        case CONTINUOUS: want = 1; break;
        case PWM: want = (int)(frame % (unsigned)period) < on; break;
        case COBBLES_OLD:
            if (old_hold) old_hold--;
            else if ((cmd_torque >= 6) != old_on) { old_on = cmd_torque >= 6; old_hold = 6; }
            want = old_on;
            break;
        case COBBLES_PWM: want = (int)(frame % (unsigned)period) < on; break;
        default: break;
        }
        if (bump_left) { want = 1; bump_left--; }
        if (connected) set_motor(want);
        if (frame % 60 == 0) { sends_shown = sends; sends = 0; }

        printf("\x1b[2;0H");
        printf("  WII REMOTE RUMBLE TESTER        HOME: back to HBC\n\n");
        printf("  UP/DOWN row   LEFT/RIGHT change   A start/stop   B bump\n\n");
        printf("  %c MODE    %-14s\n", row == ROW_MODE ? '>' : ' ', k_modes[mode_i]);
        printf("  %c PERIOD  %2d frames (%5.1f Hz)   \n", row == ROW_PERIOD ? '>' : ' ', period, 60.0 / period);
        printf("  %c ON      %2d frames (%3d%% duty)   \n", row == ROW_ON ? '>' : ' ', on, period ? on * 100 / period : 0);
        printf("  %c BUMP    %2d frames (%4d ms)   \n\n", row == ROW_BUMP ? '>' : ' ', bump, bump * 1000 / 60);
        printf("  %-8s  motor %-3s  %3u commands/s  remote %-12s\n", running ? "RUNNING" : "stopped",
               motor ? "ON" : "off", sends_shown, connected ? "connected" : "not found");
        printf("\n  PERIOD and ON apply to PWM and COBBLES PWM.\n");
        printf("  COBBLES OLD is the port's current rule, for comparison.\n");
        VIDEO_WaitVSync();
    }
    set_motor(0);
    WPAD_Shutdown();
    return 0;
}
