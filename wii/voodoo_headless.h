#ifndef VIPER_WII_VOODOO_HEADLESS_H
#define VIPER_WII_VOODOO_HEADLESS_H
#include <stdint.h>
#include <stddef.h>
#define WII_VOODOO_VRAM_BYTES 0x800000u
typedef struct {
    uint64_t packets, triangles, presents, clears;
    uint64_t renderer_us[3]; /* triangle, clear, present; optional profiling */
} WiiVoodooStats;
typedef struct {float x,y,r,g,b,a,wb,s,t,z,w0,w1,s1,t1;} WiiVoodooVertex;
/* Read-only device view valid only during the synchronous callback. A backend
 * queuing commands must copy the state and source bytes that it needs. */
typedef struct {
    const uint32_t *regs, *io, *clut;
    const uint32_t (*tmu)[64], (*palette)[256];
    const uint32_t *palette_epoch, *vram_versions;
    const uint8_t *vram;
    unsigned strip_count; /* setup vertex count, including packet continuations */
    /* Vertices in [packet_lo, packet_hi) are this packet's own decoded
     * vertices: stable until the packet ends, and identified across its
     * triangles by packet_serial (renderers may cache per-vertex work).
     * Null when the triangle's vertices are not from such an array. */
    const void *packet_lo,*packet_hi;
    uint32_t packet_serial;
} WiiVoodooView;
typedef struct {
    void (*triangle)(void *user,const WiiVoodooView *view,
                     const WiiVoodooVertex vertices[3],uint32_t command);
    void (*clear)(void *user,const WiiVoodooView *view);
    void (*present)(void *user,const WiiVoodooView *view,unsigned base);
} WiiVoodooRenderer;
/* Copies the callback table. NULL selects the unchanged headless device. */
void wii_voodoo_set_renderer(const WiiVoodooRenderer *renderer,void *user);
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
void wii_voodoo_set_layout_invalidate(void (*callback)(unsigned));
#endif
/* Caller owns an 8 MiB byte buffer for the complete device lifetime. */
void wii_voodoo_set_vram(uint8_t *bytes,size_t size);
/* Callback runs synchronously on the guest fiber, only after first geometry. */
void wii_voodoo_set_present(void (*callback)(void));
WiiVoodooStats wii_voodoo_stats(void);
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
/* Invalidation runs after any changed planner-input write and device reset.
 * Callback must not reenter or mutate the device. Installation invalidates. */
void wii_voodoo_set_material_invalidate(void (*callback)(void));
void wii_voodoo_set_material_texture_invalidate(void (*callback)(void));
#endif
#ifdef VIPER_WII_BULK_WRITER
#ifdef VIPER_WII_DIRECT_STATE
/* Audited GL 0x221ac..0x22220: seven TMU state words, command 0x78601.
 * Values are ST32LE operands; the call-free writer must establish ready. */
int wii_gx_tmu_state_ready(uint32_t ea);
void wii_gx_tmu_state_le(uint32_t ea,const uint32_t values[7]);
uint64_t wii_gx_native_state_packets(void);
#endif
int rt_wii_bulk_lfb_allowed(void);
int wii_voodoo_bulk_writer_ready(uint32_t ea,unsigned count);
int wii_voodoo_bulk_tail_ready(uint32_t ea,unsigned count);
void wii_voodoo_bulk_tail_be(uint32_t ea,const uint32_t *words,unsigned count);
/* Caller must establish ready and defer only through a call-free/checkpoint-
 * free interval containing RAM accesses and unchanged arithmetic. Values are
 * the original guest big-endian ST32 operands; VRAM remains little endian. */
void wii_voodoo_bulk_writer_be(uint32_t ea,const uint32_t *words,unsigned count);
#endif
#ifdef VIPER_WII_DIRECT_TRIANGLES
int wii_voodoo_direct_triangles(uint32_t header_ea,uint32_t cmd,const uint32_t *words,unsigned nwords);
int wii_voodoo_direct_triangles_native(uint32_t header_ea,uint32_t cmd,const uint32_t *words,unsigned nwords);
#endif
extern uint32_t wii_voodoo_texture_epoch;
extern uint32_t wii_voodoo_state_epoch;
unsigned wii_voodoo_fifo_append_be(uint32_t ea,const uint32_t *words,unsigned n);
#endif
