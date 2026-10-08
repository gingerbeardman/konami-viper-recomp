/* Hor+ widescreen for the Wii MULTI build, as the desktop enhanced mode
 * (runtime/enhanced.c): the gl library keeps two projection slots (matrix
 * row 0 and frustum per slot) and the viewport at fixed guest addresses;
 * hooks right after each write widen them by k = (512 + 2M) / 512 around the
 * centre, so the game draws its picture plus M pixels each side (x from -M
 * to 512 + M) and the GX renderer maps that range onto the output box. The
 * HUD stays in the 4:3 centre. Off (k = 1), nothing is written: the other
 * display modes stay exact. */
#include "ppc_rt.h"
#include "game_config.h"
#include "widescreen.h"

#if defined(VIPER_WII_DISPLAY_MULTI) && defined(GAME_ENH_WIDE_VIEWPORT)
static double wide_k = 1.0;             /* wanted: picture width / 512 */
static double slot_k[2], vp_k;          /* applied (0: not seen yet) */
int wii_wide_margin;                     /* renderer: M in game pixels */

static void widen_slot(int s, double f) {
    uint32_t m = GAME_ENH_WIDE_PROJ_MATRIX + 24 * s, fr = GAME_ENH_WIDE_PROJ_FRUSTUM + 24 * s;
    double l = LDF32(fr), r = LDF32(fr + 4), mid = (l + r) / 2, half = (r - l) / 2;
    STF32(m, LDF32(m) / f);
    STF32(m + 4, LDF32(m + 4) / f);
    STF32(fr, mid - half * f);
    STF32(fr + 4, mid + half * f);
}

void wii_wide_set(int margin) {         /* guest thread, at a frame boundary */
    wii_wide_margin = margin;
    wide_k = (512.0 + 2 * margin) / 512.0;
    for (int s = 0; s < 2; s++)
        if (slot_k[s] && slot_k[s] != wide_k) { widen_slot(s, wide_k / slot_k[s]); slot_k[s] = wide_k; }
    if (vp_k && vp_k != wide_k) {
        STF32(GAME_ENH_WIDE_VIEWPORT, LDF32(GAME_ENH_WIDE_VIEWPORT) * wide_k / vp_k);
        vp_k = wide_k;
    }
}

int wii_wide_hook(uint32_t pc) {
    static const struct { uint32_t addr; const char *name; } hooks[] = GAME_ENH_HOOKS;
    for (unsigned i = 0; hooks[i].name; i++) {
        if (hooks[i].addr != pc) continue;
        if (hooks[i].name[0] == 'p' && hooks[i].name[1] == 'r') {      /* "projection" */
            uint32_t s = LD8(GAME_ENH_WIDE_PROJ_SLOT);
            if (s > 1) return 1;
            slot_k[s] = 1.0;
            if (wide_k != 1.0) { widen_slot((int)s, wide_k); slot_k[s] = wide_k; }
            return 1;
        }
        if (hooks[i].name[0] == 'v' && hooks[i].name[1] == 'i') {      /* "viewport" */
            vp_k = wide_k;
            if (wide_k != 1.0) STF32(GAME_ENH_WIDE_VIEWPORT, LDF32(GAME_ENH_WIDE_VIEWPORT) * wide_k);
            return 1;
        }
    }
    return 0;
}
#else
int wii_wide_margin;
void wii_wide_set(int margin) { (void)margin; }
int wii_wide_hook(uint32_t pc) { (void)pc; return 0; }
#endif
