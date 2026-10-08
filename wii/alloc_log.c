/* Diagnostic (VIPER_WII_ALLOC_LOG, linked with --wrap=malloc,memalign,calloc):
 * records every allocation of 16 KB or more (size, address, caller) so the
 * MEM1/MEM2 placement of big buffers can be inspected; wii_alloc_log_dump()
 * writes them to the log with the arena bounds. MEM1 is 0x80000000-0x817fffff
 * (fast 1T-SRAM), MEM2 0x90000000-0x93ffffff (slower GDDR3). */
#include <gccore.h>
#include <stddef.h>
#include <stdint.h>
#include "runtime.h"

void *__real_malloc(size_t n);
void *__real_memalign(size_t a, size_t n);
void *__real_calloc(size_t n, size_t size);

typedef struct { void *p; size_t n; void *caller; } AllocRecord;
static AllocRecord rec[96];
static unsigned count;

static void note(void *p, size_t n, void *caller) {
    if (n >= 16384 && count < sizeof rec / sizeof rec[0]) rec[count++] = (AllocRecord){p, n, caller};
}
void *__wrap_malloc(size_t n) { void *p = __real_malloc(n); note(p, n, __builtin_return_address(0)); return p; }
void *__wrap_memalign(size_t a, size_t n) { void *p = __real_memalign(a, n); note(p, n, __builtin_return_address(0)); return p; }
void *__wrap_calloc(size_t n, size_t size) { void *p = __real_calloc(n, size); note(p, n * size, __builtin_return_address(0)); return p; }

void wii_alloc_log_dump(void) {
    rt_log("VIPER WII ARENA mem1 %08lx-%08lx mem2 %08lx-%08lx\n",
           (unsigned long)(uintptr_t)SYS_GetArena1Lo(), (unsigned long)(uintptr_t)SYS_GetArena1Hi(),
           (unsigned long)(uintptr_t)SYS_GetArena2Lo(), (unsigned long)(uintptr_t)SYS_GetArena2Hi());
    for (unsigned i = 0; i < count; i++)
        rt_log("VIPER WII ALLOC %08lx size=%lu caller=%08lx %s\n", (unsigned long)(uintptr_t)rec[i].p,
               (unsigned long)rec[i].n, (unsigned long)(uintptr_t)rec[i].caller,
               (uintptr_t)rec[i].p >= 0x90000000u ? "MEM2" : "MEM1");
}
