#ifndef VIPER_WII_VOODOO_COLOR_PLAN_H
#define VIPER_WII_VOODOO_COLOR_PLAN_H
#include <stdint.h>
/* Field layout from runtime/voodoo/voodoo_regs.h, reg_fbz_colorpath.
 * RGB and alpha have separate equations. Rejection uses OTHER before the
 * equation, so it must not be inferred from the final blended alpha. */
typedef struct {
    uint8_t other_rgb,other_alpha,local_rgb,local_alpha,local_override;
    uint8_t rgb_zero,rgb_sub,rgb_mul,rgb_reverse,rgb_add,rgb_invert;
    uint8_t alpha_zero,alpha_sub,alpha_mul,alpha_reverse,alpha_add,alpha_invert;
    uint8_t subpixel,texture,clamp,antialias;
} WiiVoodooColorPlan;
static inline WiiVoodooColorPlan wii_voodoo_color_plan(uint32_t v){
    return (WiiVoodooColorPlan){
        (uint8_t)(v&3),
        (uint8_t)((v>>2)&3),
        (uint8_t)((v>>4)&1),
        (uint8_t)((v>>5)&3),
        (uint8_t)((v>>7)&1),
        (uint8_t)((v>>8)&1),
        (uint8_t)((v>>9)&1),
        (uint8_t)((v>>10)&7),
        (uint8_t)((v>>13)&1),
        (uint8_t)((v>>14)&3),
        (uint8_t)((v>>16)&1),
        (uint8_t)((v>>17)&1),
        (uint8_t)((v>>18)&1),
        (uint8_t)((v>>19)&7),
        (uint8_t)((v>>22)&1),
        (uint8_t)((v>>23)&3),
        (uint8_t)((v>>25)&1),
        (uint8_t)((v>>26)&1),
        (uint8_t)((v>>27)&1),
        (uint8_t)((v>>28)&1),
        (uint8_t)((v>>29)&1)};
}
#endif
