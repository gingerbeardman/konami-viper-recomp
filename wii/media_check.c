/* Bypass only the resident kernel's diagnostic media scan. The filesystem
 * worker still receives/completes its request, and normal file reads,
 * decompression and per-file checksum verification remain guest code. */
#include "runtime.h"

void rt_dc_media_check_ok(PPCContext *c) {
    /* 941B01 kernel scanner prologue, before it saves any caller state. Refuse
     * a different kernel rather than interpreting the wrong function ABI. */
    static const uint32_t prologue[] = {
        0x7d800026u, 0xbe01ffc0u, 0x7c0802a6u, 0x91810004u,
        0x3aa30000u, 0x82620068u
    };
    for (unsigned i = 0; i < sizeof prologue / sizeof prologue[0]; i++)
        if (LD32(0xde28u + 4*i) != prologue[i]) {
            rt_fatal("media-check bypass requires the supported kernel scanner");
            return;
        }
    if (c->r[3] >= RAM_LIMIT || (c->r[3] & 3u)) {
        rt_fatal("invalid media-check result pointer");
        return;
    }
    /* de28..e06c: this kernel runs pass 0; bits 8..9 are its result
     * (1 = OK). Bit 30 means finished, bit 31 means aborted, low byte
     * is percentage. Match the original successful result exactly. */
    ST32(c->r[3], 0x40000164u);
    rt_log("VIPER MEDIA CHECK skipped status=%08lx result=%08lx\n",
           (unsigned long)0x40000164u, (unsigned long)c->r[3]);
}
