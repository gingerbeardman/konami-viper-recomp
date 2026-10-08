/* Wii Remote steering recorder. Hold the remote however feels natural for
 * steering. A starts recording a movement; then A saves it as STEERING
 * (a move that should steer) or B saves it as NOT STEERING (a move that
 * must not steer). Each recording's gravity vector and wiiuse orientation,
 * one line per frame, is appended to sd:/viper/tilt.log for fitting the
 * game's steering mapping. Live values are shown throughout. Home exits to
 * the Homebrew Channel.
 * Calibration: MINUS = this pose is centre, 1 = this pose is full LEFT lock,
 * 2 = this pose is full RIGHT lock. The arcade wheel's full lock is +/-200
 * (the game's calibrated ADC range); the screen shows the value the current
 * mapping (tilt of the long axis, scaled between the calibrated poses,
 * default 45 degrees) would send, with a bar. Poses are logged as CAL lines. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

enum { MAX_FRAMES = 60 * 20 };   /* 20 seconds per recording */
typedef struct { float gx, gy, gz, roll, pitch, yaw; } Sample;
static Sample frames[MAX_FRAMES];
static void *xfb;
static GXRModeObj *mode;

static int save(FILE *log, int id, const char *label, unsigned n) {
    if (!log) return 0;
    fprintf(log, "MOVE %d label=%s frames=%u\n", id, label, n);
    for (unsigned i = 0; i < n; i++)
        fprintf(log, "%u %.4f %.4f %.4f %.2f %.2f %.2f\n", i, frames[i].gx, frames[i].gy, frames[i].gz,
                frames[i].roll, frames[i].pitch, frames[i].yaw);
    fprintf(log, "END %d\n", id);
    fflush(log);
    return 1;
}

int main(void) {
    VIDEO_Init();
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC);
    mode = VIDEO_GetPreferredMode(NULL);
    xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(FALSE);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    FILE *log = NULL;
    if (fatInitDefault()) log = fopen("sd:/viper/tilt.log", "a");
    if (log) { fprintf(log, "SESSION start\n"); fflush(log); }
    int recording = 0, steer_moves = 0, other_moves = 0, id = 0;
    unsigned n = 0;
    const char *last = "";
    float centre = 0, left = -45, right = 45;   /* tilt angles, degrees */
    int have_centre = 0;
    for (;;) {
        WPAD_ScanPads();
        WPADData *d = WPAD_Data(WPAD_CHAN_0);
        u32 type;
        int connected = WPAD_Probe(WPAD_CHAN_0, &type) == WPAD_ERR_NONE;
        u32 down = connected ? d->btns_d : 0;
        if (down & WPAD_BUTTON_HOME) break;
        Sample s = {0};
        if (connected) s = (Sample){d->gforce.x, d->gforce.y, d->gforce.z, d->orient.roll, d->orient.pitch, d->orient.yaw};
        float tilt = atan2f(s.gy, sqrtf(s.gx * s.gx + s.gz * s.gz)) * (180.0f / (float)M_PI);
        if (connected && (!have_centre || (down & WPAD_BUTTON_MINUS))) {
            centre = tilt; have_centre = 1;
            if (log && (down & WPAD_BUTTON_MINUS)) { fprintf(log, "CAL centre %.4f %.4f %.4f %.2f %.2f tilt=%.2f\n", s.gx, s.gy, s.gz, s.roll, s.pitch, tilt); fflush(log); }
        }
        if (connected && (down & (WPAD_BUTTON_1 | WPAD_BUTTON_2))) {
            int is_left = !!(down & WPAD_BUTTON_1);
            if (is_left) left = tilt; else right = tilt;
            if (log) { fprintf(log, "CAL %s %.4f %.4f %.4f %.2f %.2f tilt=%.2f\n", is_left ? "full_left" : "full_right", s.gx, s.gy, s.gz, s.roll, s.pitch, tilt); fflush(log); }
        }
        float delta = tilt - centre, l = left - centre, r = right - centre;
        float frac = 0;
        if (delta < 0 && l < -1) frac = delta / -l;          /* left lock maps to -1 */
        else if (delta < 0 && l > 1) frac = -delta / l;
        else if (delta > 0 && r > 1) frac = delta / r;        /* right lock maps to +1 */
        else if (delta > 0 && r < -1) frac = -delta / -r;
        if (frac < -1) frac = -1;
        if (frac > 1) frac = 1;
        int arcade = (int)lroundf(frac * 200);
        if (!recording && (down & WPAD_BUTTON_A)) { recording = 1; n = 0; }
        else if (recording && (down & (WPAD_BUTTON_A | WPAD_BUTTON_B))) {
            int steer = !!(down & WPAD_BUTTON_A);
            id++;
            int ok = save(log, id, steer ? "steer" : "not_steer", n);
            if (steer) steer_moves++; else other_moves++;
            last = ok ? (steer ? "saved as STEERING" : "saved as NOT STEERING") : "NOT SAVED (no SD log)";
            recording = 0;
        } else if (recording && n < MAX_FRAMES) frames[n++] = s;
        printf("\x1b[2;0H");
        printf("Viper steering recorder                    HOME exits\n\n");
        if (recording) {
            printf("RECORDING %5.1f s: do the movement now.            \n", n / 60.0);
            printf("  then A = it should STEER, B = it must NOT steer   \n\n");
        } else {
            printf("Press A, then do one movement.                      \n");
            printf("  last: %-40s\n\n", last);
        }
        printf("moves saved: steering %d, not steering %d   %s\n\n", steer_moves, other_moves,
               log ? "(sd:/viper/tilt.log)" : "(SD log unavailable!)");
        if (!connected) printf("Wii Remote not connected...                          \n");
        else {
            printf("gravity x=%+6.2f y=%+6.2f z=%+6.2f            \n", s.gx, s.gy, s.gz);
            printf("wiiuse roll=%+7.1f pitch=%+7.1f yaw=%+7.1f    \n\n", s.roll, s.pitch, s.yaw);
            printf("tilt %+6.1f deg  centre %+6.1f  left lock %+6.1f  right lock %+6.1f   \n", tilt, centre, left, right);
            printf("MINUS = centre here, 1 = full LEFT here, 2 = full RIGHT here   \n\n");
            printf("arcade steering %+4d of +/-200 (left negative)                  \n", arcade);
            char bar[42];
            for (int i = 0; i < 41; i++) bar[i] = i == 20 ? '|' : '-';
            bar[41] = 0;
            int pos = 20 + arcade * 20 / 200;
            bar[pos < 0 ? 0 : pos > 40 ? 40 : pos] = '#';
            printf("L [%s] R\n", bar);
        }
        VIDEO_WaitVSync();
    }
    if (log) { fprintf(log, "SESSION end\n"); fclose(log); }
    WPAD_Shutdown();
    exit(0);
}
