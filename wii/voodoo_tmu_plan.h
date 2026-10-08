/* SPDX-License-Identifier: BSD-3-Clause
 * Field definitions adapted from MAME's Voodoo renderer (copyright Aaron Giles). */
#ifndef VIPER_WII_VOODOO_TMU_PLAN_H
#define VIPER_WII_VOODOO_TMU_PLAN_H
#include <stdint.h>
typedef struct {
    uint8_t rgb_zero,rgb_sub,rgb_mul,rgb_reverse,rgb_add,rgb_invert;
    uint8_t alpha_zero,alpha_sub,alpha_mul,alpha_reverse,alpha_add,alpha_invert;
} WiiVoodooTMUPlan;
static inline WiiVoodooTMUPlan wii_voodoo_tmu_plan(uint32_t mode){
    return (WiiVoodooTMUPlan){
        (uint8_t)((mode>>12)&1),(uint8_t)((mode>>13)&1),(uint8_t)((mode>>14)&7),(uint8_t)((mode>>17)&1),(uint8_t)((mode>>18)&3),(uint8_t)((mode>>20)&1),
        (uint8_t)((mode>>21)&1),(uint8_t)((mode>>22)&1),(uint8_t)((mode>>23)&7),(uint8_t)((mode>>26)&1),(uint8_t)((mode>>27)&3),(uint8_t)((mode>>29)&1)};
}
#endif
