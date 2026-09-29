/* Internal runtime interfaces (not used by generated code). */
#pragma once
#include "ppc_rt.h"
#include <stdio.h>

extern PPCContext g_ctx;

/* logging / errors */
void rt_log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void rt_fatal(const char *why) __attribute__((noreturn));
int rt_verbose(void);
void rt_dump_state(void);

/* virtual time: CPU cycles of the 203.2128 MHz core */
#define CPU_HZ 203212800.0
#define US_TO_CYC(us) ((uint64_t)((us) * (CPU_HZ / 1e6)))
uint64_t rt_now(void);
uint64_t rt_timebase(void);
void rt_shorten_slice(uint64_t until);

typedef void (*SchedCb)(void *arg);
void rt_sched_at(uint64_t cycle, SchedCb cb, void *arg);
void rt_sched_cancel(SchedCb cb, void *arg);
uint64_t rt_sched_next(void);
void rt_sched_run(uint64_t now);

/* dispatch / fibers */
void rt_register_module(const RtModuleInfo *m);
RtFn rt_lookup(uint32_t addr);
void rt_start(uint32_t pc);

/* interrupt controller */
enum {
    EPIC_IRQ0 = 0, EPIC_IRQ1, EPIC_IRQ2, EPIC_IRQ3, EPIC_IRQ4,
    EPIC_I2C = 16, EPIC_DMA0, EPIC_DMA1, EPIC_MSG,
    EPIC_GT0 = 20, EPIC_GT1, EPIC_GT2, EPIC_GT3, EPIC_NUM
};
void epic_raise(int irq);
int rt_irq_line(void);

/* devices */
typedef struct HwConfig {
    const char *cf_image;       /* raw CF card image (chdman extracthd output) */
    const char *nvram_path;     /* M48T58 contents (8 KiB) */
    const char *ds2430_path;    /* 40-byte DS2430A dump */
    const char *bios_path;      /* 941b01.u25 (only mapped for reads) */
    const char *nvram_save;     /* where the NVRAM is persisted (loaded instead of nvram_path if present) */
} HwConfig;

void hw_init(const HwConfig *cfg);
void hw_shutdown(void);
void nvram_save(void);
void rt_pace_vblank(void);
void audio_frontend_push(const uint8_t *blk);
int frontend_run(int scale);
uint32_t hw_read(uint32_t ea, int size);
void hw_write(uint32_t ea, int size, uint32_t v);

/* voodoo 3 (MAME core, runtime/voodoo/) - offsets in bytes, LE register values */
void voodoo_init(void);
uint32_t voodoo_reg_read(uint32_t off);
void voodoo_reg_write(uint32_t off, uint32_t v, uint32_t mask);
uint32_t voodoo_lfb_read(uint32_t off);
void voodoo_lfb_write(uint32_t off, uint32_t v, uint32_t mask);
uint32_t voodoo_io_read(uint32_t off);
void voodoo_io_write(uint32_t off, uint32_t v, uint32_t mask);
uint64_t voodoo_get_frame(uint32_t *dst, int max_pixels, int *w, int *h);
void voodoo_stats(void);
void rt_eat_cycles(uint32_t n);

/* helpers for little-endian peripherals on the big-endian bus */
static inline uint32_t le_bus_read(uint32_t reg, int k, int size) {
    if (size == 4) return bswap32(reg);
    if (size == 2) return (((reg >> (8 * k)) & 0xff) << 8) | ((reg >> (8 * (k + 1))) & 0xff);
    return (reg >> (8 * k)) & 0xff;
}
static inline uint32_t le_bus_write(uint32_t old, int k, int size, uint32_t v) {
    if (size == 4) return bswap32(v);
    if (size == 2) {
        old &= ~((0xffu << (8 * k)) | (0xffu << (8 * (k + 1))));
        return old | (((v >> 8) & 0xff) << (8 * k)) | ((v & 0xff) << (8 * (k + 1)));
    }
    old &= ~(0xffu << (8 * k));
    return old | ((v & 0xff) << (8 * k));
}
