/* Register-preserving calls for cold paths (VIPER_WII_PRESERVE_SLOW): the
 * call goes through wii_preserve_trampoline (wii/ram_access_cold.c), which
 * saves and restores every volatile register but r3, r4, r12, f1 and LR. To
 * GCC the call site is an asm that changes only those, so values stay in
 * f0-f13/r5-r11 across it. The callee and its arguments are unchanged, so
 * results are the same as a direct call. */
#ifndef WII_PRESERVE_CALL_H
#define WII_PRESERVE_CALL_H
#include <stdint.h>

#define WII_PRESERVE_ASM(fn_) \
    register void *_pc12 __asm__("r12") = (void *)(fn_); \
    __asm__ volatile("bl wii_preserve_trampoline" \
                     : "+r"(_pc3), "+r"(_pc4), "+f"(_pcf1), "+r"(_pc12) : : "lr", "memory")

static inline __attribute__((always_inline)) uint32_t wii_pcall_u_u(uint32_t (*fn)(uint32_t), uint32_t a) {
    register uint32_t _pc3 __asm__("r3") = a;
    register uint32_t _pc4 __asm__("r4");
    register double _pcf1 __asm__("fr1");
    __asm__("" : "=r"(_pc4), "=f"(_pcf1));
    WII_PRESERVE_ASM(fn);
    return _pc3;
}
static inline __attribute__((always_inline)) uint32_t wii_pcall_u_uu(int (*fn)(uint32_t, uint32_t), uint32_t a, uint32_t b) {
    register uint32_t _pc3 __asm__("r3") = a;
    register uint32_t _pc4 __asm__("r4") = b;
    register double _pcf1 __asm__("fr1");
    __asm__("" : "=f"(_pcf1));
    WII_PRESERVE_ASM(fn);
    return _pc3;
}
static inline __attribute__((always_inline)) void wii_pcall_v_uu(void (*fn)(uint32_t, uint32_t), uint32_t a, uint32_t b) {
    register uint32_t _pc3 __asm__("r3") = a;
    register uint32_t _pc4 __asm__("r4") = b;
    register double _pcf1 __asm__("fr1");
    __asm__("" : "=f"(_pcf1));
    WII_PRESERVE_ASM(fn);
}
static inline __attribute__((always_inline)) double wii_pcall_d_u(double (*fn)(uint32_t), uint32_t a) {
    register uint32_t _pc3 __asm__("r3") = a;
    register uint32_t _pc4 __asm__("r4");
    register double _pcf1 __asm__("fr1");
    __asm__("" : "=r"(_pc4), "=f"(_pcf1));
    WII_PRESERVE_ASM(fn);
    return _pcf1;
}
static inline __attribute__((always_inline)) void wii_pcall_v_ud(void (*fn)(uint32_t, double), uint32_t a, double d) {
    register uint32_t _pc3 __asm__("r3") = a;
    register uint32_t _pc4 __asm__("r4");
    register double _pcf1 __asm__("fr1") = d;
    __asm__("" : "=r"(_pc4));
    WII_PRESERVE_ASM(fn);
}
static inline __attribute__((always_inline)) void wii_pcall_v_v(void (*fn)(void)) {
    register uint32_t _pc3 __asm__("r3");
    register uint32_t _pc4 __asm__("r4");
    register double _pcf1 __asm__("fr1");
    __asm__("" : "=r"(_pc3), "=r"(_pc4), "=f"(_pcf1));
    WII_PRESERVE_ASM(fn);
}
static inline __attribute__((always_inline)) double wii_pcall_d_d(double (*fn)(double), double d) {
    register uint32_t _pc3 __asm__("r3");
    register uint32_t _pc4 __asm__("r4");
    register double _pcf1 __asm__("fr1") = d;
    __asm__("" : "=r"(_pc3), "=r"(_pc4));
    WII_PRESERVE_ASM(fn);
    return _pcf1;
}
#endif
