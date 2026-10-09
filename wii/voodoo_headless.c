/* Headless C Voodoo device. FIFO/register/fastfill behavior follows the native
 * C backend and its runtime/voodoo reference. Only triangle raster pixels and
 * physical scanout are omitted. Guest VRAM always retains little-endian bytes,
 * independently of host endianness. Unsupported device operations stop. */
#include "hot_layout.h"
#include "runtime.h"
#include "voodoo_headless.h"
#ifdef VIPER_WII_SNAP_VERTICES
/* Vertex X/Y as the Voodoo's setup sees them: 12.4 fixed point, rounded to
 * nearest 1/16 pixel (add/subtract 1.5*2^23 at 16x scale; exact for any
 * screen coordinate). A car's paint and shine passes are transformed
 * separately and differ by ~1e-4 pixel; snapped they are identical, so the
 * depth-EQUAL shine covers the paint (no twinkling dots on showroom and
 * RESULT cars, which the cabinet does not show). */
static inline float SNAP16(float f){float t=f*16.f+12582912.f;return (t-12582912.f)*(1.f/16.f);}   /* -frounding-math keeps the add and subtract */
#else
#define SNAP16(f) (f)
#endif
#include <string.h>
#include <stdlib.h>
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
#include "texture_layout_cache.h"
static void (*layout_invalidate)(unsigned);
static void layouts_changed(void){if(layout_invalidate){layout_invalidate(0);layout_invalidate(1);}}
void wii_voodoo_set_layout_invalidate(void (*callback)(unsigned)){
    layout_invalidate=callback;layouts_changed();
}
#endif
#if defined(VIPER_WII) && defined(VIPER_WII_GX_PLANE_PROFILE)
#include <ogc/lwp_watchdog.h>
static uint64_t renderer_ticks[3];
#define RENDER_START() gettime()
#define RENDER_END(kind,start) (renderer_ticks[kind]+=gettime()-(start))
#else
#define RENDER_START() 0
#define RENDER_END(kind,start) ((void)(start))
#endif
static uint8_t *vram;
static void (*present_callback)(void);
static WiiVoodooRenderer renderer;
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
#include "gx_material_plan.h"
static void (*material_invalidate)(void);
static void material_changed(void){if(material_invalidate)material_invalidate();}
void wii_voodoo_set_material_invalidate(void (*callback)(void)){
    material_invalidate=callback;material_changed();
}
#ifdef VIPER_WII_PLAN_KEEP
/* Base-address-only TMU changes (texBaseAddr words 3-6 except word 3 bit 0,
 * which the plan reads): plans stay valid; only texture-identity reuse ends. */
static void (*material_texture_invalidate)(void);
void wii_voodoo_set_material_texture_invalidate(void (*callback)(void)){material_texture_invalidate=callback;}
#endif
#endif
static void *renderer_user;
static WiiVoodooStats counters;
static uint32_t io[64],agp[128],regs[256],clut[512];
static uint8_t vga[256];
static uint32_t blit_regs[64],tmu_regs[2][64];
static uint32_t texture_palette[2][256],palette_epoch[2];
static uint64_t frames;
#ifdef VIPER_WII_DIRECT_STATE
static uint64_t direct_state_packets;
#endif
static unsigned swap_wait,vblanks_since_swap;
static int swap_pending,geometry_started;
static uint32_t vram_versions[2048];
static uint8_t fifo_present[0x200000/8];
static int fifo_hdr_valid;
static unsigned fifo_hdr_pc,fifo_hdr_count,fifo_hdr_checked;
static uint32_t fifo_hdr_cmd;
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
static uint64_t format_packets[512],format_vertices[512];
static uint64_t triangle_codes[3],triangle_fans;
static uint64_t fifo_type_packets[8],fifo_type_words[8],fifo_reg_writes[0x1000];
#endif
typedef WiiVoodooVertex NativeVertex;
#ifdef VIPER_WII_PACKET_CARRY
static NativeVertex strip_buf[3+15];
#define strip strip_buf
#else
static NativeVertex strip[3];
#endif
/* Field copies: GCC lowers whole 56-byte struct copies to memcpy calls on
 * every strip shuffle. lfs/stfs round-trip every single bit pattern. */
static inline void vertex_copy(NativeVertex *d,const NativeVertex *s){
    d->x=s->x;d->y=s->y;d->r=s->r;d->g=s->g;d->b=s->b;d->a=s->a;d->wb=s->wb;
    d->s=s->s;d->t=s->t;d->z=s->z;d->w0=s->w0;d->w1=s->w1;d->s1=s->s1;d->t1=s->t1;
}
static unsigned strip_count;
static inline void fifo_run(void);
static void triangle_packet(unsigned pc,uint32_t cmd);
static void fifo_register(unsigned r, uint32_t v);
static void fastfill(void);
static void fifo_hdr_invalidate(void);
#ifdef VIPER_WII_VRAM_WORD
/* VRAM holds little-endian words at 4-byte aligned offsets: one byte-reversed
 * word access is the same four bytes (lwbrx/stwbrx on Broadway). */
typedef uint32_t __attribute__((may_alias)) vram_word_t;
static inline uint32_t vram_read(unsigned word) {
    return __builtin_bswap32(*(const vram_word_t *)(const void *)(vram+((word&0x1fffff)*4)));
}
#else
static uint32_t vram_read(unsigned word) {
    const uint8_t *p=vram+((word&0x1fffff)*4);
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
#endif
/* Bumped by every VRAM writer that can reach outside the active command
 * FIFO (pixel stores, non-FIFO LFB words, uploads) and by palette changes:
 * anything a texture can read. The renderer skips its bound-texture
 * re-check while this is unchanged. FIFO-only writers do not bump it. */
uint32_t wii_voodoo_texture_epoch;
/* Bumped whenever any register a renderer callback can read (FBI, TMU, I/O,
 * palette, CLUT) changes value, and at reset. Equal epochs mean the view
 * passed to the renderer holds the same state words. */
uint32_t wii_voodoo_state_epoch;
#ifdef VIPER_WII_EPOCH_TRACE
/* Diagnostic: state-epoch bumps per register (FBI word index, then 512 + TMU*64 + word). */
static unsigned long long epoch_cause[640];
#endif
#ifdef VIPER_WII_INLINE_VRAM_WRITE
static inline __attribute__((always_inline))
#else
static
#endif
void vram_write(unsigned word,uint32_t value) {
    word&=0x1fffff;
    if(fifo_hdr_valid&&word==(fifo_hdr_pc>>2))fifo_hdr_invalidate();
    vram_versions[word/1024]++;
#ifdef VIPER_WII_VRAM_WORD
    *(vram_word_t *)(void *)(vram+word*4)=__builtin_bswap32(value);
#else
    uint8_t *p=vram+word*4;
    p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24;
#endif
}
static WiiVoodooView device_view(void) {
    return (WiiVoodooView){regs,io,clut,tmu_regs,texture_palette,palette_epoch,vram_versions,vram,strip_count
    };
}
static void present_frame(unsigned base) {
    counters.presents++;
    if(renderer.present){WiiVoodooView view=device_view();uint64_t start=RENDER_START();renderer.present(renderer_user,&view,base);RENDER_END(2,start);}
    if(geometry_started&&present_callback)present_callback();
}
static void draw_triangle(uint32_t cmd) {
    counters.triangles++;
    if(renderer.triangle){WiiVoodooView view=device_view();uint64_t start=RENDER_START();renderer.triangle(renderer_user,&view,strip,cmd);RENDER_END(0,start);}
}
void wii_voodoo_set_renderer(const WiiVoodooRenderer *backend,void *user) {
    renderer=backend?*backend:(WiiVoodooRenderer){0};renderer_user=user;
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    layouts_changed();
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
    material_changed();
#endif
}
void wii_voodoo_set_vram(uint8_t *bytes,size_t size) {
    if(!bytes||size!=WII_VOODOO_VRAM_BYTES)rt_fatal("Wii Voodoo requires exactly 8 MiB VRAM");
    vram=bytes;
}
void wii_voodoo_set_present(void (*callback)(void)) {present_callback=callback;}
WiiVoodooStats wii_voodoo_stats(void) {
    WiiVoodooStats result=counters;
#if defined(VIPER_WII) && defined(VIPER_WII_GX_PLANE_PROFILE)
    for(unsigned i=0;i<3;i++)result.renderer_us[i]=ticks_to_microsecs(renderer_ticks[i]);
#endif
    return result;
}
static void fifo_presence_reset(void) {
    memset(fifo_present,0,sizeof fifo_present);
}
static void fifo_hdr_invalidate(void) { fifo_hdr_valid = 0; }
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
static unsigned fifo_cached_base, fifo_cached_end;
static void fifo_bounds_refresh(void) {
    fifo_cached_base=(agp[8]&0xffffffu)<<12;
    fifo_cached_end=fifo_cached_base+(((agp[9]&255u)+1u)<<12);
}
#endif

static int fifo_has(unsigned a) {
    if (a>=0x800000) return 0;
    return fifo_present[a>>5] & (1u<<((a>>2)&7));
}
static void fifo_mark(unsigned a, int present) {
    if (a>=0x800000) rt_fatal("FIFO presence address outside VRAM");
    unsigned bit=1u<<((a>>2)&7);
    uint8_t *page=fifo_present;
    unsigned byte=a>>5;
    if (present) page[byte]|=bit;
    else page[byte]&=~bit;
}
/* Packet bounds have already been checked by fifo_describe. Preserve bits
 * belonging to adjacent packets while clearing whole interior bytes at once. */
/* At most 19 whole bytes per packet: an inline loop beats a memset call.
 * The attribute stops GCC turning the loop back into memset. */
__attribute__((optimize("no-tree-loop-distribute-patterns")))
WII_HOT_fifo_consume static void fifo_consume(unsigned a,unsigned words) {
    unsigned first=a>>2,last=first+words;
    if(first&7){
        unsigned n=8-(first&7);
        if(n>words)n=words;
        fifo_present[first>>3]&=~(((1u<<n)-1)<<(first&7));
        first+=n;
    }
    unsigned bytes=(last-first)>>3;
    for(uint8_t *p=fifo_present+(first>>3),*end=p+bytes;p<end;p++)*p=0;
    first+=bytes*8;
    if(first<last)fifo_present[first>>3]&=~((1u<<(last-first))-1);
}
static void swap_buffers(void) {
    unsigned base = regs[0x250/4] & 0x7ffff0;
    if (io[0xfc/4] != base) wii_voodoo_state_epoch++;
    io[0xfc/4] = base;
    present_frame(base);
    swap_pending = 0;
    vblanks_since_swap = 0;
}
static void store_pixel(unsigned addr, unsigned value) {
    if (addr >= 0x800000 || (addr & 1)) rt_fatal("invalid framebuffer pixel address");
    unsigned shift = (addr & 2) * 8;
    vram_write(addr/4, (vram_read(addr/4) & ~(65535u << shift)) | (value << shift));
    wii_voodoo_texture_epoch++;
}
static void store_pixel_pair(unsigned addr, unsigned low, unsigned high) {
    unsigned addr2 = addr + 2;
    if (addr >= 0x800000 || addr2 >= 0x800000 || (addr & 3))
        rt_fatal("invalid framebuffer pixel address");
    uint32_t pair = (low & 65535u) | ((high & 65535u) << 16);
    unsigned word = addr / 4;
    vram_write(word, pair);
    vram_versions[word/1024]++;
    wii_voodoo_texture_epoch++;
}
static unsigned fill_color(unsigned color, unsigned mode, unsigned x, unsigned y) {
    unsigned r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    if (!(mode & 256)) return ((r>>3)<<11) | ((g>>2)<<5) | (b>>3);
    static const unsigned char matrix[2][16] = {
        {0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5},
        {8,10,8,10,11,9,11,9,8,10,8,10,11,9,11,9}
    };
    unsigned d = matrix[(mode >> 11) & 1][(y & 3)*4 + (x & 3)];
    r = ((r<<1) - (r>>4) + (r>>7) + d) >> 4;
    g = ((g<<2) - (g>>4) + (g>>6) + d) >> 4;
    b = ((b<<1) - (b>>4) + (b>>7) + d) >> 4;
    return (r<<11) | (g<<5) | b;
}
static void fastfill(void) {
    counters.clears++;
    unsigned mode = regs[0x110/4], color = regs[0x148/4];
    unsigned stride = regs[0x1f0/4];
    stride = (stride & 0x8000) ? ((stride & 127)*128) : (stride & 0x3fff);
    if ((mode & (1 << 10)) && regs[0x1f8/4] != regs[0x1f0/4])
        rt_fatal("native auxiliary stride differs from colour stride");
    unsigned left = (regs[0x118/4] >> 16) & 1023, right = regs[0x118/4] & 1023;
    unsigned top = (regs[0x11c/4] >> 16) & 1023, bottom = regs[0x11c/4] & 1023;
    if (right*2 > stride) rt_fatal("native fastfill stride too small");
    unsigned rgb_base = regs[0x1ec/4] & 0x7ffff0, aux_base = regs[0x1f4/4] & 0x7ffff0;
    unsigned distance = rgb_base > aux_base ? rgb_base-aux_base : aux_base-rgb_base;
    unsigned span = bottom > top ? (bottom-top)*stride : 0;
    int overlap = (mode & 0x600) == 0x600 && distance < span;
    unsigned pattern[4][4];
    if (mode & (1 << 9)) {
        for (unsigned py = 0; py < 4; py++)
            for (unsigned px = 0; px < 4; px++)
                pattern[py][px] = fill_color(color, mode, px, py);
    }
    unsigned aux_pixel = regs[0x130/4] & 65535;
    for (unsigned pass=0; pass < (overlap ? 1u : 2u); pass++) {
        unsigned rgb_on = (mode & (1<<9)) && (overlap || pass == 0);
        unsigned aux_on = (mode & (1<<10)) && (overlap || pass == 1);
        if (!rgb_on && !aux_on) continue;
        for (unsigned y = top; y < bottom; y++) {
            int row = (mode & (1 << 17)) ? (int)((io[0x10/4] >> 18) & 4095) - (int)y : (int)y;
            if (row < 0) rt_fatal("native fastfill negative row");
            if (overlap) {
                for (unsigned x = left; x < right; x++) {
                    unsigned off = (unsigned)row*stride + x*2;
                    if (rgb_on) store_pixel(rgb_base + off, pattern[y & 3][x & 3]);
                    if (aux_on) store_pixel(aux_base + off, aux_pixel);
                }
            } else {
                unsigned base = rgb_on ? rgb_base : aux_base;
                unsigned x = left;
#ifdef VIPER_WII_FAST_FILL
                /* Whole aligned words of the row at once: the same bytes as
                 * store_pixel_pair word by word, the same totals for the page
                 * versions (two per word) and the texture epoch (one per
                 * word), and the cached FIFO header dropped if any word hits
                 * it. Nothing observes the counters during a fill. Edge
                 * pixels keep the original single-pixel stores. */
                {
                    unsigned row_base = base + (unsigned)row*stride;
                    if ((row_base + x*2) & 2) {
                        store_pixel(row_base + x*2, rgb_on ? pattern[y & 3][x & 3] : aux_pixel);
                        x++;
                    }
                    unsigned pairs = (right - x) / 2;
                    unsigned first = (row_base + x*2) / 4;
                    if (pairs && (row_base + x*2) + pairs*4 <= 0x800000) {
                        uint32_t w[2];
                        for (unsigned k = 0; k < 2; k++) {
                            unsigned px = x + 2*k;
                            unsigned c0 = rgb_on ? pattern[y & 3][px & 3] : aux_pixel;
                            unsigned c1 = rgb_on ? pattern[y & 3][(px + 1) & 3] : aux_pixel;
                            /* vram_write stores little-endian bytes. */
                            uint32_t pair = (c0 & 65535u) | ((c1 & 65535u) << 16);
                            uint8_t bytes[4] = {(uint8_t)pair, (uint8_t)(pair >> 8), (uint8_t)(pair >> 16), (uint8_t)(pair >> 24)};
                            memcpy(&w[k], bytes, 4);
                        }
                        if (fifo_hdr_valid && (fifo_hdr_pc >> 2) - first < pairs) fifo_hdr_invalidate();
                        uint8_t *dst = vram + first*4;
                        for (unsigned i = 0; i < pairs; i++) memcpy(dst + i*4, &w[i & 1], 4);
                        for (unsigned wd = first, left_words = pairs; left_words;) {
                            unsigned n = 1024 - (wd & 1023); if (n > left_words) n = left_words;
                            vram_versions[wd / 1024] += 2*n; wd += n; left_words -= n;
                        }
                        wii_voodoo_texture_epoch += pairs;
                        x += 2*pairs;
                    }
                }
#endif
                while (x < right) {
                    unsigned addr = base + (unsigned)row*stride + x*2;
                    if ((addr & 3) == 0 && x + 1 < right) {
                        unsigned c0 = rgb_on ? pattern[y & 3][x & 3] : aux_pixel;
                        unsigned c1 = rgb_on ? pattern[y & 3][(x + 1) & 3] : aux_pixel;
                        store_pixel_pair(addr, c0, c1);
                        x += 2;
                    } else {
                        unsigned c = rgb_on ? pattern[y & 3][x & 3] : aux_pixel;
                        store_pixel(addr, c);
                        x++;
                    }
                }
            }
        }
    }
    if(renderer.clear){WiiVoodooView view=device_view();uint64_t start=RENDER_START();renderer.clear(renderer_user,&view);RENDER_END(1,start);}
}
static void vblank(void *unused) {
    (void)unused; frames++;
    rt_pace_vblank();   /* real-time pacing; a no-op in scripted benchmarks */
    if (vblanks_since_swap < 250) vblanks_since_swap++;
    if (swap_pending && vblanks_since_swap >= swap_wait) { swap_buffers(); fifo_run(); }
    epic_raise(EPIC_IRQ0);
    rt_sched_at(rt_now() + (uint64_t)(CPU_HZ / 57.5), vblank, NULL);
}
static uint32_t status(void) {
    rt_eat_cycles(1000);
    return 0x3f | ((frames & 1) ? 0x40 : 0) | (swap_pending ? 0x10000000 : 0);
}
uint32_t voodoo_io_read(uint32_t off) {
    unsigned r = (off >> 2) & 63;
    if (!r) return status();
    if (r == 0x54 / 4) return clut[io[0x50 / 4] & 511];
    return io[r];
}
void voodoo_io_write(uint32_t off, uint32_t v, uint32_t mask) {
    unsigned r = (off >> 2) & 63;
    wii_voodoo_state_epoch++;
#ifdef VIPER_WII_EPOCH_TRACE
    epoch_cause[448+r]++;   /* I/O register writes (always bump) */
#endif
    io[r] = (io[r] & ~mask) | (v & mask);
    if (r == 0x54 / 4) clut[io[0x50 / 4] & 511] = io[r];
    if (off >= 0xb0 && off <= 0xdf)
        for (unsigned i = 0; i < 4; i++) if (mask & (255u << (8*i))) vga[(off+i)&255] = v >> (8*i);
}
uint32_t voodoo_reg_read(uint32_t off) {
    if (off < 0x80000) return voodoo_io_read(off);
    if (off < 0x100000) return agp[(off >> 2) & 127];
    if (off >= 0x200000 && off < 0x600000) {
        unsigned r = (off >> 2) & 255;
        if (r >= 0xc0) return tmu_regs[((off >> 10) & 4) && !((off >> 10) & 2)][r-0xc0];
        return r ? regs[r] : status();
    }
    return 0xffffffff;
}
/* One TMU register store (non-palette) with its cache invalidations. Shared
 * by register writes and the native TMU state packet so the rules agree. */
static inline void tmu_store(unsigned t,unsigned i,uint32_t value){
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    int layout_changed=value!=tmu_regs[t][i]&&wii_texture_layout_word(i);
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
#ifdef VIPER_WII_PLAN_KEEP
    /* The TMU planner reads words 0-2, 8 and bit 0 of word 3 only. */
    uint32_t diff=value^tmu_regs[t][i];
    int changed=diff&&(i<=2||i==8||(i==3&&(diff&1u)));
    int texture_changed=diff&&!changed&&i>=3&&i<=6;
#else
    int changed=value!=tmu_regs[t][i]&&wii_material_tmu_word(i);
#endif
#endif
    if(value!=tmu_regs[t][i])wii_voodoo_state_epoch++;
#ifdef VIPER_WII_EPOCH_TRACE
    if(value!=tmu_regs[t][i])epoch_cause[512+t*64+(i&63)]++;
#endif
    tmu_regs[t][i]=value;
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    if(layout_changed&&layout_invalidate)layout_invalidate(t);
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
    if(changed)material_changed();
#ifdef VIPER_WII_PLAN_KEEP
    else if(texture_changed&&material_texture_invalidate)material_texture_invalidate();
#endif
#endif
}
WII_HOT_voodoo_reg_write void voodoo_reg_write(uint32_t off, uint32_t v, uint32_t mask) {
    if (off < 0x80000) { voodoo_io_write(off, v, mask); return; }
    if (off < 0x100000) {
        unsigned r = (off >> 2) & 127;
        uint32_t agp_before = agp[r];
        agp[r] = (agp[r] & ~mask) | (v & mask);
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
        if(r==8||r==9)fifo_bounds_refresh();
#endif
        if (r == 9 && !(agp[r] & 256)) {
            fifo_presence_reset();
            fifo_hdr_invalidate();
        }
        if (r == 11 && agp[r] != agp_before) fifo_hdr_invalidate();
        return;
    }
    if (off >= 0x200000 && off < 0x600000) {
        unsigned r = (off >> 2) & 255;
        if (r >= 0xc0) {
            unsigned chips = (off >> 10) & 15;
            if (!chips) chips = 15;
            for (unsigned t = 0; t < 2; t++) if (chips & (2u << t)) {
                uint32_t value=(tmu_regs[t][r-0xc0] & ~mask) | (v & mask);
                if (r>=0xcd && r<=0xd4 && (value&0x80000000u)) {
                    unsigned index=((value>>24)&127)*2+((r-0xc9)&1);
                    uint32_t color=value|0xff000000u;
                    if (texture_palette[t][index]!=color) {
                        texture_palette[t][index]=color; palette_epoch[t]++; wii_voodoo_texture_epoch++; wii_voodoo_state_epoch++;
                    }
                } else tmu_store(t,r-0xc0,value);
            }
            return;
        }
        uint32_t material_before=regs[r];
        regs[r] = (regs[r] & ~mask) | (v & mask);
        if (regs[r] != material_before) wii_voodoo_state_epoch++;
#ifdef VIPER_WII_EPOCH_TRACE
        if (regs[r] != material_before) epoch_cause[r&511]++;
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
        if(regs[r]!=material_before&&wii_material_fbi_word(r))material_changed();
#endif
        if (r == 0x80 / 4 || r == 0x100 / 4 ||
            r == 0x2a0 / 4 || r == 0x2a4 / 4)
            rt_fatal("native Voodoo drawing implementation pending");
        if (r == 0x124 / 4) fastfill();
        if (r == 0x128 / 4) {
            if (swap_pending) rt_fatal("overlapping native swaps");
            swap_wait = (v >> 1) & 255;
            swap_pending = 1;
            if (!(v & 1)) swap_buffers();
        }
        if (r == 0x13c / 4 && (regs[1] & 0x20)) {
            regs[1] = (regs[1] & ~0x800ff000u) | ((v << 10) & 0xff000) | 0x800;
            wii_voodoo_state_epoch++;
            epic_raise(EPIC_IRQ4);
        }
        return;
    }
    rt_log("voodoo unsupported BAR0 write %08lx\n", (unsigned long)off);
    rt_fatal("Voodoo BAR0 implementation pending");
}
uint32_t voodoo_lfb_read(uint32_t off) {
    return vram_read(off >> 2);
}
WII_HOT_voodoo_lfb_write void voodoo_lfb_write(uint32_t off, uint32_t v, uint32_t mask) {
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
    unsigned fifo_base=fifo_cached_base, fifo_end=fifo_cached_end;
#else
    unsigned fifo_base = (agp[8] & 0xffffff) << 12;
    unsigned fifo_end = fifo_base + (((agp[9] & 255) + 1) << 12);
#endif
    unsigned a = (off & 0x7fffff) >> 2;
    /* Full bus words replace every bit; reading the previous VRAM value is
     * only necessary for partial writes. Presence/version tracking is shared. */
    vram_write(a, mask==0xffffffffu?v:(vram_read(a)&~mask)|(v&mask));
    if ((agp[9] & 256) && off >= fifo_base && off < fifo_end) {
        if (mask != 0xffffffff) rt_fatal("partial FIFO word write");
        fifo_mark(off, 1);
        /* Resume a cached incomplete packet without entering the large
         * parser until its missing-word frontier reaches the packet end.
         * Header writes and read-pointer changes invalidate this cache. */
        if(!swap_pending&&fifo_hdr_valid&&fifo_hdr_pc==agp[11]){
#ifdef VIPER_WII_FIFO_FRONTIER_WRITE
            /* The cached frontier was missing before this write. Only a
             * write to that word can make the packet ready; fifo_mark above
             * already proves its presence without reading the bitmap again. */
            if(fifo_hdr_checked<fifo_hdr_count){
                if((off&~3u)!=fifo_hdr_pc+fifo_hdr_checked*4)return;
                fifo_hdr_checked++;
            }
#endif
            while(fifo_hdr_checked<fifo_hdr_count){
                if(!fifo_has(fifo_hdr_pc+fifo_hdr_checked*4))return;
                fifo_hdr_checked++;
            }
        }
        /* Most words land while the packet at the read pointer still lacks
         * its header: fifo_run would return at once, so skip the call. */
        if(swap_pending||!fifo_has(agp[11]))return;
        fifo_run();
    } else wii_voodoo_texture_epoch++;
}
#ifdef VIPER_WII_BULK_WRITER
#ifdef VIPER_WII_BULK_INCOMPLETE_HEADER
#if !defined(VIPER_WII_BULK_PUBLISH) || defined(VIPER_WII_DIRECT_CAPTURE59)
#error Incomplete-header batching requires bulk publication without direct capture
#endif
/* The final packet word remains absent outside this batch. No prefix store
 * can execute a packet; an incomplete-prefix scan restores the frontier. */
static int bulk_incomplete_header(unsigned off,unsigned count){
    unsigned pc=agp[11],base=(agp[8]&0xffffffu)<<12;
    unsigned end=base+(((agp[9]&255u)+1u)<<12);
    if(!count||count>150||(off&3)||!(agp[9]&256)||swap_pending||
       !fifo_hdr_valid||fifo_hdr_pc!=pc||(pc&3)||pc<base||pc>=0x800000u||
       end>0x800000u||off<=pc||off>=0x800000u||off<base||
       count>(0x800000u-off)/4||off+count*4>end||
       !fifo_hdr_count||fifo_hdr_count>(0x800000u-pc)/4||
       fifo_hdr_checked>=fifo_hdr_count||!fifo_has(pc))return 0;
    unsigned last=pc+(fifo_hdr_count-1)*4;
    return off+count*4<=last&&!fifo_has(last);
}
#endif
#ifdef VIPER_WII_FIFO_FRONTIER_WRITE
/* Words after a packet header already at the read pointer (header-first
 * producers). While the cached packet is waiting for exactly the next word,
 * each word is stored, marked and advances the frontier as one full-word
 * voodoo_lfb_write would; when the packet completes it runs, and the rest
 * is left to the caller's scalar stores. Returns the words consumed. */
unsigned wii_voodoo_fifo_append_be(uint32_t ea,const uint32_t *words,unsigned n){
    if(ea<0x84000000u||ea>=0x86000000u||(ea&3))return 0;
    unsigned off=ea-0x84000000u,k=0;
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
    unsigned base=fifo_cached_base,end=fifo_cached_end;
#else
    unsigned base=(agp[8]&0xffffff)<<12,end=base+(((agp[9]&255)+1)<<12);
#endif
    while(k<n){
        unsigned o=off+4*k;
        if(!(agp[9]&256)||o<base||o>=end||o>=0x800000u||swap_pending||!fifo_hdr_valid||
           fifo_hdr_pc!=agp[11]||fifo_hdr_checked>=fifo_hdr_count||o!=fifo_hdr_pc+fifo_hdr_checked*4)break;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        uint32_t storage=words[k];
#else
        uint32_t storage=__builtin_bswap32(words[k]);
#endif
        memcpy(vram+o,&storage,4);
        vram_versions[o>>12]++;
        fifo_present[o>>5]|=1u<<((o>>2)&7);
        k++;
        fifo_hdr_checked++;
        while(fifo_hdr_checked<fifo_hdr_count&&fifo_has(fifo_hdr_pc+fifo_hdr_checked*4))fifo_hdr_checked++;
        if(fifo_hdr_checked<fifo_hdr_count)continue;
        if(!swap_pending&&fifo_has(agp[11]))fifo_run();
        break;
    }
    return k;
}
#endif
int wii_voodoo_bulk_writer_ready(uint32_t ea,unsigned count){
    if(!count || count>15*10 || (ea&3) || ea<0x84000000u || ea>=0x86000000u)return 0;
    unsigned off=ea-0x84000000u,pc=agp[11];
#ifdef VIPER_WII_BULK_INCOMPLETE_HEADER
    if(fifo_hdr_valid&&bulk_incomplete_header(off,count))return 1;
#endif
    unsigned base=(agp[8]&0xffffffu)<<12;
    unsigned end=base+(((agp[9]&255u)+1u)<<12);
    if(!(agp[9]&256) || swap_pending || fifo_hdr_valid || (pc&3) ||
       pc<base || pc>=0x800000 || end>0x800000 ||
       off<=pc || off<base || off>=0x800000 ||
       count>(0x800000-off)/4 || off+count*4>end || fifo_has(pc))return 0;
    return 1;
}
void wii_voodoo_bulk_writer_be(uint32_t ea,const uint32_t *words,unsigned count){
    unsigned off=ea-0x84000000u;
#if defined(VIPER_WII_BULK_PUBLISH)
    /* No callback or packet execution occurs in this helper. Ready guards
     * already establish that; retain a local bounds/header check for callers
     * outside the specialized writer. Capture invalidation below matches
     * the scalar writes before the caller records new native values. */
    if(count&&count<=150&&!(off&3)&&off<0x800000u&&
       count<=(0x800000u-off)/4&&(!fifo_hdr_valid||off>fifo_hdr_pc)){
        for(unsigned i=0;i<count;i++){
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
            uint32_t storage=words[i];
#else
            uint32_t storage=__builtin_bswap32(words[i]);
#endif
            memcpy(vram+off+i*4,&storage,4);
        }
        unsigned word=off/4,left=count;
        while(left){
            unsigned n=1024-(word&1023);if(n>left)n=left;
            vram_versions[word/1024]+=n;word+=n;left-=n;
        }
        word=off/4;left=count;
        while(left){
            unsigned n=8-(word&7);if(n>left)n=left;
            fifo_present[word>>3]|=((1u<<n)-1)<<(word&7);
            word+=n;left-=n;
        }
#ifdef VIPER_WII_BULK_INCOMPLETE_HEADER
        if(fifo_hdr_valid&&bulk_incomplete_header(off,count)){
            /* Equivalent to fifo_run's incomplete-prefix scan: the guarded
             * absent final word prevents header invalidation or execution. */
            while(fifo_hdr_checked<fifo_hdr_count&&
                  fifo_has(fifo_hdr_pc+fifo_hdr_checked*4))fifo_hdr_checked++;
        }
#endif
        return;
    }
#endif
    /* Every scalar call would return at the absent read-pointer header.
     * Preserve each byte, presence bit and version increment, including page
     * crossings and uint32 counter wrap. No packet can execute here. */
    for(unsigned i=0;i<count;i++){
        uint32_t value=__builtin_bswap32(words[i]);
        vram_write(off/4+i,value);fifo_mark(off+i*4,1);
    }
}
int wii_voodoo_bulk_tail_ready(uint32_t ea,unsigned count){
    if(!count || count>150 || (ea&3) || ea<0x84000000u || ea>=0x84800000u)return 0;
    unsigned off=ea-0x84000000u,pc=agp[11];
    unsigned base=(agp[8]&0xffffffu)<<12;
    unsigned end=base+(((agp[9]&255u)+1u)<<12);
    if(!(agp[9]&256) || swap_pending || !fifo_hdr_valid || fifo_hdr_pc!=pc ||
       (pc&3) || pc<base || end>0x800000u || off<=pc ||
       off<base || count>(0x800000u-off)/4 || off+count*4>end ||
       fifo_hdr_count>(0x800000u-pc)/4 ||
       pc+fifo_hdr_count*4!=off+count*4 ||
       fifo_hdr_checked>=fifo_hdr_count ||
       pc+fifo_hdr_checked*4<off || !fifo_has(pc) ||
       fifo_has(off+(count-1)*4))return 0;
    return 1;
}
static void bulk_tail_frontier(void){
    /* The final scalar LFB write advances the cached frontier before drawing.
     * Preserve that retained metadata even after the header becomes invalid. */
    if(!swap_pending&&fifo_hdr_valid&&fifo_hdr_pc==agp[11])
        while(fifo_hdr_checked<fifo_hdr_count&&fifo_has(fifo_hdr_pc+fifo_hdr_checked*4))fifo_hdr_checked++;
}
void wii_voodoo_bulk_tail_be(uint32_t ea,const uint32_t *words,unsigned count){
    /* The last word was absent: no scalar prefix could consume this packet.
     * Publish all words, then resume at exactly the last scalar-store boundary. */
    wii_voodoo_bulk_writer_be(ea,words,count);
    bulk_tail_frontier();
    fifo_run();
}
#endif
static void fifo_register(unsigned r, uint32_t v) {
    if (r & 0x800) {
        r &= 63; blit_regs[r] = v;
        if (r >= 32 && (blit_regs[0x70/4] & 15)) rt_fatal("native Voodoo 2D blit pending");
        return;
    }
    voodoo_reg_write(0x200000 + r * 4, v, 0xffffffff);
}
#ifdef VIPER_WII_DIRECT_STATE
#ifndef VIPER_WII_BULK_WRITER
#error VIPER_WII_DIRECT_STATE requires VIPER_WII_BULK_WRITER
#endif
int wii_gx_tmu_state_ready(uint32_t ea){
    return wii_voodoo_bulk_tail_ready(ea,7)&&
        fifo_hdr_cmd==0x00078601u&&ea-0x84000000u==agp[11]+4;
}
uint64_t wii_gx_native_state_packets(void){return direct_state_packets;}
void wii_gx_tmu_state_le(uint32_t ea,const uint32_t values[7]){
    if(!wii_gx_tmu_state_ready(ea))rt_fatal("native TMU state eligibility changed");
    unsigned pc=agp[11];
    uint32_t words[7];
    for(unsigned i=0;i<7;i++)words[i]=__builtin_bswap32(values[i]);
    /* Retain guest-visible bytes/versions/presence. All payload is published
     * before any invalidation callback, exactly as last-word FIFO execution. */
    wii_voodoo_bulk_writer_be(ea,words,7);
    /* The final scalar write advances the cached frontier through the packet
     * before execution. Keep even invalidated cache metadata exact. */
    fifo_hdr_checked=8;
    fifo_hdr_invalidate();
    /* The source command broadcasts textureMode/tLOD/tDetail/base addresses
     * to both TMUs. No BAR address, chip-mask, palette or register decoding
     * is needed for this fixed native state block. Preserve invalidations
     * in the reference order: register first, then TMU 0 and TMU 1. */
    for(unsigned i=0;i<7;i++)for(unsigned t=0;t<2;t++)tmu_store(t,i,values[i]);
    fifo_consume(pc,8);
    agp[11]=pc+32;
    direct_state_packets++;
    fifo_run();
}
#endif
static float fifo_float(unsigned word) {
    uint32_t raw = vram_read(word); float f; memcpy(&f, &raw, 4); return f;
}
#ifdef VIPER_WII_PACKET_VERTICES
__attribute__((unused))
#else
#define triangle_packet_copying triangle_packet
#endif
#ifdef VIPER_WII_FIFO_SPLIT_TRIANGLE
/* Keep the large decoder's register requirements out of parser entry/exit.
 * This is a code-layout experiment; packet readiness and callbacks are unchanged. */
__attribute__((noinline))
#endif
static void triangle_packet_copying(unsigned pc, uint32_t cmd) {
    counters.packets++;
    unsigned format = (cmd>>10)&255;
    unsigned code = (cmd>>3)&7, vertices = (cmd>>6)&15, word = pc/4+1;
    if (code > 2) rt_fatal("invalid triangle packet command");
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
    unsigned key=format+((cmd&(1u<<28))?256:0);
    format_packets[key]++;format_vertices[key]+=vertices;
    triangle_codes[code]++;triangle_fans+=!!(cmd&(1u<<22));
#endif
    geometry_started=1;
    for (unsigned i = 0; i < vertices; i++) {
        NativeVertex v;
#ifdef VIPER_WII_FIFO_FORMAT59
        if(format==59&&!(cmd&(1u<<28))){
            /* Every field but z is assigned, so skip the zero fill. */
            v.z=0;
            v.x=SNAP16(fifo_float(word++));v.y=SNAP16(fifo_float(word++));
            v.r=fifo_float(word++);v.g=fifo_float(word++);v.b=fifo_float(word++);
            v.a=fifo_float(word++);v.wb=fifo_float(word++);
            v.w0=v.w1=fifo_float(word++);
            v.s=v.s1=fifo_float(word++);v.t=v.t1=fifo_float(word++);
        }else
#endif
        {
        v = (NativeVertex){0}; v.wb = v.w0 = v.w1 = 1;
        v.x=SNAP16(fifo_float(word++)); v.y=SNAP16(fifo_float(word++));
        if (cmd & (1u<<28)) {
            if (format & 3) {
                uint32_t argb = vram_read(word++);
                if (format & 1) { v.r=(argb>>16)&255; v.g=(argb>>8)&255; v.b=argb&255; }
                if (format & 2) v.a=argb>>24;
            }
        } else {
            if (format & 1) { v.r=fifo_float(word++); v.g=fifo_float(word++); v.b=fifo_float(word++); }
            if (format & 2) v.a=fifo_float(word++);
        }
        if (format & 4) v.z=fifo_float(word++);
        if (format & 8) v.wb=v.w0=v.w1=fifo_float(word++);
        if (format & 16) v.w0=v.w1=fifo_float(word++);
        if (format & 32) { v.s=v.s1=fifo_float(word++); v.t=v.t1=fifo_float(word++); }
        if (format & 64) v.w1=fifo_float(word++);
        if (format & 128) { v.s1=fifo_float(word++); v.t1=fifo_float(word++); }
        }
        if ((code == 1 && i == 0) || (code == 0 && i%3 == 0)) {
            vertex_copy(&strip[0],&v);vertex_copy(&strip[1],&v);vertex_copy(&strip[2],&v);
            strip_count=1;
        } else {
            if (!(cmd & (1u<<22))) vertex_copy(&strip[0],&strip[1]);
            vertex_copy(&strip[1],&strip[2]);vertex_copy(&strip[2],&v);
            if (++strip_count >= 3) draw_triangle(cmd);
        }
    }
}
#ifdef VIPER_WII_PACKET_VERTICES
/* Same vertex decode and strip/fan/list rules as triangle_packet_copying,
 * but each vertex is decoded once into a packet array and the setup slots
 * are pointers into it. A strip triangle's three vertices are consecutive
 * there, so the renderer gets them in place; only fans and the first
 * triangles of a continuation (which use vertices carried from the last
 * packet) are assembled by copying. The carried strip[] is rewritten at the
 * end of the packet, and the device view is built once per packet with its
 * strip count updated before each triangle. */
#ifdef VIPER_WII_FIFO_SPLIT_TRIANGLE
__attribute__((noinline))
#endif
WII_HOT_triangle_packet static void triangle_packet(unsigned pc, uint32_t cmd) {
    counters.packets++;
    unsigned format = (cmd>>10)&255;
    unsigned code = (cmd>>3)&7, vertices = (cmd>>6)&15, word = pc/4+1;
    if (code > 2) rt_fatal("invalid triangle packet command");
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
    unsigned key=format+((cmd&(1u<<28))?256:0);
    format_packets[key]++;format_vertices[key]+=vertices;
    triangle_codes[code]++;triangle_fans+=!!(cmd&(1u<<22));
#endif
    geometry_started=1;
    /* buf[3+i]: this packet's vertex i. The setup slots start at the carried
     * strip[] itself: nothing writes strip[] until the end of the packet. */
    NativeVertex buf[3+15];
    const NativeVertex *s0=&strip[0],*s1=&strip[1],*s2=&strip[2];
    int fan=!!(cmd&(1u<<22));
    WiiVoodooView view;
    if(renderer.triangle)view=device_view();
    for (unsigned i = 0; i < vertices; i++) {
        NativeVertex *pv=&buf[3+i];
#ifdef VIPER_WII_FIFO_FORMAT59
        if(format==59&&!(cmd&(1u<<28))){
            pv->z=0;
            pv->x=SNAP16(fifo_float(word++));pv->y=SNAP16(fifo_float(word++));
            pv->r=fifo_float(word++);pv->g=fifo_float(word++);pv->b=fifo_float(word++);
            pv->a=fifo_float(word++);pv->wb=fifo_float(word++);
            pv->w0=pv->w1=fifo_float(word++);
            pv->s=pv->s1=fifo_float(word++);pv->t=pv->t1=fifo_float(word++);
        }else
#endif
        {
        NativeVertex v = {0}; v.wb = v.w0 = v.w1 = 1;
        v.x=SNAP16(fifo_float(word++)); v.y=SNAP16(fifo_float(word++));
        if (cmd & (1u<<28)) {
            if (format & 3) {
                uint32_t argb = vram_read(word++);
                if (format & 1) { v.r=(argb>>16)&255; v.g=(argb>>8)&255; v.b=argb&255; }
                if (format & 2) v.a=argb>>24;
            }
        } else {
            if (format & 1) { v.r=fifo_float(word++); v.g=fifo_float(word++); v.b=fifo_float(word++); }
            if (format & 2) v.a=fifo_float(word++);
        }
        if (format & 4) v.z=fifo_float(word++);
        if (format & 8) v.wb=v.w0=v.w1=fifo_float(word++);
        if (format & 16) v.w0=v.w1=fifo_float(word++);
        if (format & 32) { v.s=v.s1=fifo_float(word++); v.t=v.t1=fifo_float(word++); }
        if (format & 64) v.w1=fifo_float(word++);
        if (format & 128) { v.s1=fifo_float(word++); v.t1=fifo_float(word++); }
        vertex_copy(pv,&v);
        }
        if ((code == 1 && i == 0) || (code == 0 && i%3 == 0)) {
            s0=s1=s2=pv;
            strip_count=1;
        } else {
            if (!fan) s0=s1;
            s1=s2;s2=pv;
            if (++strip_count >= 3) {
                counters.triangles++;
                if(renderer.triangle){
                    NativeVertex tri[3];const NativeVertex *t=s0;
                    if(s1!=s0+1||s2!=s0+2){vertex_copy(&tri[0],s0);vertex_copy(&tri[1],s1);vertex_copy(&tri[2],s2);t=tri;}
                    view.strip_count=strip_count;
                    uint64_t start=RENDER_START();renderer.triangle(renderer_user,&view,t,cmd);RENDER_END(0,start);
                }
            }
        }
    }
    /* A slot k points into buf or at strip[j] with j >= k (slots only shift
     * down or take new vertices), so copying in order reads every source
     * before it is overwritten. */
    if(s0!=&strip[0])vertex_copy(&strip[0],s0);
    if(s1!=&strip[1])vertex_copy(&strip[1],s1);
    if(s2!=&strip[2])vertex_copy(&strip[2],s2);
}
#endif
#if defined(VIPER_WII_DIRECT_PACKET59) && defined(VIPER_WII_PACKET_VERTICES) && defined(VIPER_WII_FIFO_FORMAT59)
/* triangle_packet for a direct unpacked format-59 packet, decoding each
 * vertex from the producer's words (VRAM holds exactly these bytes, so
 * fifo_float(word) is the float with bits bswap32(words[k])), with the
 * format tests gone: fewer conditions live across the renderer call. Same
 * vertices, strip/fan/list rules, counters and calls as triangle_packet. */
static inline float word_float(uint32_t w){uint32_t v=__builtin_bswap32(w);float f;memcpy(&f,&v,4);return f;}
#ifdef VIPER_WII_NATIVE_PACKET
static int direct_native;   /* the packet's words are host-order (see wii_voodoo_direct_triangles_native) */
static inline float packet_float(uint32_t w){
    if(!direct_native)return word_float(w);
    float f;memcpy(&f,&w,4);return f;
}
#else
#define packet_float word_float
#endif
__attribute__((noinline)) static void triangle_packet59(uint32_t cmd,const uint32_t *words) {
    counters.packets++;
    unsigned code = (cmd>>3)&7, vertices = (cmd>>6)&15;
    if (code > 2) rt_fatal("invalid triangle packet command");
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
    format_packets[59]++;format_vertices[59]+=vertices;
    triangle_codes[code]++;triangle_fans+=!!(cmd&(1u<<22));
#endif
    geometry_started=1;
#ifdef VIPER_WII_PACKET_CARRY
    /* The carried strip vertices are the buffer's first three slots, so a
     * strip continuing from the last packet is contiguous (no per-triangle
     * copies) and only the end-of-packet carry copies remain. */
    NativeVertex *const buf=strip_buf;
#else
    NativeVertex buf[3+15];
#endif
    const NativeVertex *s0=&strip[0],*s1=&strip[1],*s2=&strip[2];
    const int fan=!!(cmd&(1u<<22));
    WiiVoodooView view;
    if(renderer.triangle){
        view=device_view();
        static uint32_t packet_serial;
        view.packet_lo=&buf[3];view.packet_hi=&buf[3+vertices];view.packet_serial=++packet_serial;
    }
    for (unsigned i = 0; i < vertices; i++, words += 10) {
        NativeVertex *pv=&buf[3+i];
        pv->z=0;
        pv->x=SNAP16(packet_float(words[0]));pv->y=SNAP16(packet_float(words[1]));
        pv->r=packet_float(words[2]);pv->g=packet_float(words[3]);pv->b=packet_float(words[4]);
        pv->a=packet_float(words[5]);pv->wb=packet_float(words[6]);
        pv->w0=pv->w1=packet_float(words[7]);
        pv->s=pv->s1=packet_float(words[8]);pv->t=pv->t1=packet_float(words[9]);
        if ((code == 1 && i == 0) || (code == 0 && i%3 == 0)) {
            s0=s1=s2=pv;
            strip_count=1;
        } else {
            if (!fan) s0=s1;
            s1=s2;s2=pv;
            if (++strip_count >= 3) {
                counters.triangles++;
                if(renderer.triangle){
                    NativeVertex tri[3];const NativeVertex *t=s0;
                    const void *lo=view.packet_lo,*hi=view.packet_hi;
                    if(s1!=s0+1||s2!=s0+2){
                        vertex_copy(&tri[0],s0);vertex_copy(&tri[1],s1);vertex_copy(&tri[2],s2);t=tri;
                        view.packet_lo=view.packet_hi=NULL;   /* copies: not cacheable */
                    }
                    view.strip_count=strip_count;
                    uint64_t start=RENDER_START();renderer.triangle(renderer_user,&view,t,cmd);RENDER_END(0,start);
                    view.packet_lo=lo;view.packet_hi=hi;
                }
            }
        }
    }
    if(s0!=&strip[0])vertex_copy(&strip[0],s0);
    if(s1!=&strip[1])vertex_copy(&strip[1],s1);
    if(s2!=&strip[2])vertex_copy(&strip[2],s2);
}
#endif
static void upload_words(unsigned target, unsigned source, unsigned count) {
    wii_voodoo_texture_epoch++;
    if (target < source+count && source < target+count) {
        for (unsigned i=0; i<count; i++) vram_write(target+i, vram_read(source+i));
    } else {
        uint32_t block[128];
        while (count) {
            unsigned n = count < 128 ? count : 128;
            for (unsigned i=0; i<n; i++) block[i]=vram_read(source+i);
            for (unsigned i=0; i<n; i++) vram_write(target+i, block[i]);
            target+=n; source+=n; count-=n;
        }
    }
}
WII_HOT_fifo_describe static void fifo_describe(unsigned pc, uint32_t cmd, unsigned *count_out) {
    unsigned type = cmd & 7, count;
    switch (type) {
    case 0: count = 1; break;
    case 1: count = 1 + (cmd >> 16); break;
    case 2: count = 1 + __builtin_popcount(cmd>>3); break;
    case 3: {
        unsigned n = 2;
        n += (cmd & (1u<<28)) ? !!(cmd & (3u<<10)) : 3*((cmd>>10)&1) + ((cmd>>11)&1);
        for (unsigned b = 12; b <= 17; b++) n += ((cmd>>b)&1) * ((b == 15 || b == 17) ? 2 : 1);
        count = 1 + n*((cmd>>6)&15) + (cmd>>29);
        break;
    }
    case 5: count = 2 + ((cmd>>3)&0x7ffff); break;
    case 4: count = 1 + __builtin_popcount((cmd >> 15) & 0x3fff) + (cmd >> 29); break;
    default:
        rt_log("FIFO packet pc=%08x type=%u cmd=%08lx\n", pc, type, (unsigned long)cmd);
        rt_fatal("native Voodoo FIFO packet implementation pending");
    }
    if (count > (0x800000 - pc) / 4) rt_fatal("FIFO packet crosses VRAM end");
    *count_out = count;
}
/* The loop's own first checks, inline: most calls (after every direct
 * packet and frontier word) find nothing to run and return here. */
__attribute__((noinline)) static void fifo_run_loop(void);
static inline void fifo_run(void) {
    if (swap_pending) return;
    unsigned pc = agp[11];
    if ((pc & 3) || pc >= 0x800000) rt_fatal("invalid FIFO read pointer");
    if (!fifo_has(pc)) return;
    fifo_run_loop();
}
WII_HOT_fifo_run_loop static void fifo_run_loop(void) {
    while (!swap_pending) {
        unsigned pc = agp[11];
        if ((pc & 3) || pc >= 0x800000) rt_fatal("invalid FIFO read pointer");
        if (!fifo_has(pc)) return;
        uint32_t cmd;
        unsigned count,checked=1;
        if (fifo_hdr_valid && fifo_hdr_pc == pc) {
            cmd = fifo_hdr_cmd;
            count = fifo_hdr_count;
            checked = fifo_hdr_checked;
        } else {
            cmd = vram_read(pc / 4);
            fifo_describe(pc, cmd, &count);
        }
        /* Presence bits only clear on packet consumption or FIFO reset, both
         * of which invalidate the cached header. Resume at the first missing
         * word instead of rescanning the already-present prefix per write. */
        for (unsigned i = checked; i < count; i++) if (!fifo_has(pc + i*4)) {
            fifo_hdr_valid = 1;
            fifo_hdr_pc = pc;
            fifo_hdr_cmd = cmd;
            fifo_hdr_count = count;
            fifo_hdr_checked = i;
            return;
        }
        fifo_hdr_invalidate();
        unsigned type = cmd & 7;
        unsigned next = pc + count*4;
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
        fifo_type_packets[type]++;fifo_type_words[type]+=count;
        if(type==1){unsigned r=(cmd>>3)&0xfff;for(unsigned i=1;i<count;i++,r+=(cmd>>15)&1)fifo_reg_writes[r&0xfff]++;}
        if(type==2){for(unsigned b=3;b<32;b++)if(cmd&(1u<<b))fifo_reg_writes[0x800+b-1]++;}
        if(type==4){unsigned r=(cmd>>3)&0xfff;for(unsigned b=0;b<14;b++)if(cmd&(1u<<(15+b)))fifo_reg_writes[(r+b)&0xfff]++;}
#endif
        if (type == 0) {
            unsigned fn = (cmd >> 3) & 7;
            if (fn == 3) next = ((cmd >> 6) & 0x7fffff) << 2;
            else if (fn) rt_fatal("native FIFO call/AGP implementation pending");
        } else if (type == 1) {
            unsigned r = (cmd >> 3) & 0xfff;
            for (unsigned i = 1; i < count; i++, r += (cmd >> 15) & 1)
                fifo_register(r, vram_read(pc/4 + i));
        } else if (type == 2) {
            unsigned i = 1;
            for (unsigned b = 3; b < 32; b++)
                if (cmd & (1u<<b)) fifo_register(0x800+b-1, vram_read(pc/4+i++));
        } else if (type == 3) {
            triangle_packet(pc, cmd);
        } else if (type == 5) {
            if ((cmd>>22) != 0) rt_fatal("native FIFO masked/texture upload pending");
            unsigned target = vram_read(pc/4+1);
            if ((target & 3) || target > 0x800000 || (count-2)*4 > 0x800000-target)
                rt_fatal("native FIFO upload address outside VRAM");
            upload_words(target/4, pc/4+2, count-2);
        } else {
            unsigned r = (cmd >> 3) & 0xfff, i = 1;
            for (unsigned b = 0; b < 14; b++)
                if (cmd & (1u << (15+b))) fifo_register(r+b, vram_read(pc/4 + i++));
        }
        fifo_consume(pc,count);
        agp[11] = next;
    }
}
#ifdef VIPER_WII_DIRECT_TRIANGLES
/* A native gl producer hands over one complete format-59 triangle packet
 * at the moment its last guest word would have completed it. When this
 * packet is next at the FIFO read pointer, nothing of it is present yet and
 * no swap is pending, the effect is exactly that of the guest's word stores:
 * the same VRAM bytes and page versions, then execution, consumption and the
 * read-pointer advance, then any later packets. Otherwise nothing changes
 * and the caller stores the words itself. words are the guest ST32 operands
 * of the vertex words; cmd is the header's ST32LE operand. */
static unsigned long long direct_triangle_reject[6],direct_triangle_packets;
#ifdef VIPER_WII_NATIVE_PACKET
/* Host-order words (VIPER_WII_NATIVE_PACKET): the producer stored each word
 * byte-swapped back from the FIFO's little-endian order, so a float word is
 * its float's own bits; the decode skips the swap. Consumed directly only
 * (DIRECT_NO_VRAM): a declined packet goes back to the producer, which
 * restores the FIFO order before any guest-visible store. */
#ifndef VIPER_WII_DIRECT_NO_VRAM
#error VIPER_WII_NATIVE_PACKET needs VIPER_WII_DIRECT_NO_VRAM
#endif
int wii_voodoo_direct_triangles_native(uint32_t header_ea,uint32_t cmd,const uint32_t *words,unsigned nwords){
    direct_native=1;
    int r=wii_voodoo_direct_triangles(header_ea,cmd,words,nwords);
    direct_native=0;
    return r;
}
#endif
WII_HOT_wii_voodoo_direct_triangles int wii_voodoo_direct_triangles(uint32_t header_ea,uint32_t cmd,const uint32_t *words,unsigned nwords){
    if(header_ea<0x84000000u||header_ea>=0x86000000u||(header_ea&3)){direct_triangle_reject[0]++;return 0;}
    unsigned off=header_ea-0x84000000u;
    if(!(agp[9]&256u)||swap_pending){direct_triangle_reject[1]++;return 0;}
    if(agp[11]!=off){direct_triangle_reject[2]++;return 0;}
    if(fifo_hdr_valid){direct_triangle_reject[3]++;return 0;}
    if((cmd&7u)!=3u||((cmd>>10)&255u)!=59u||(cmd&(1u<<28))){direct_triangle_reject[4]++;return 0;}
    unsigned count=1+nwords;
    if(off+count*4u>0x800000u){direct_triangle_reject[5]++;return 0;}
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
    unsigned base=fifo_cached_base,end=fifo_cached_end;
#else
    unsigned base=(agp[8]&0xffffffu)<<12,end=base+(((agp[9]&255u)+1u)<<12);
#endif
    if(off<base||off+count*4u>end){direct_triangle_reject[5]++;return 0;}
#ifdef VIPER_WII_DIRECT_LEAN
    /* fifo_describe for an unpacked format-59 triangle packet: ten words a
     * vertex (x,y 2, rgb 3, a 1, wb 1, w0 1, st 2), plus the trailing count in the
     * top bits; its VRAM-end check cannot fire after the bound above. */
    if(1u+10u*((cmd>>6)&15u)+(cmd>>29)!=count){direct_triangle_reject[5]++;return 0;}
#else
    unsigned described;fifo_describe(off,cmd,&described);
    if(described!=count){direct_triangle_reject[5]++;return 0;}
#endif
    unsigned word=off/4;
#ifdef VIPER_WII_PACKET_VERTICES
    /* off+count*4 <= 0x800000 was checked, so the presence bits are in range. */
    /* Any presence bit for words word..word+count-1: whole bitmap bytes,
     * the first and last masked to the range (bit i of byte b is word 8b+i). */
    unsigned last=word+count-1,fb=word>>3,lb=last>>3;
    unsigned lo=0xffu<<(word&7),hi=0xffu>>(7-(last&7)),present;
    if(fb==lb)present=fifo_present[fb]&lo&hi;
    else{
        present=(fifo_present[fb]&lo)|(fifo_present[lb]&hi);
        for(unsigned b=fb+1;b<lb;b++)present|=fifo_present[b];
    }
    if(present){direct_triangle_reject[5]++;return 0;}
    /* VRAM words are little-endian: storing value v writes bswap(v) natively.
     * The header is cmd and each payload word is bswap(words[i]), so the
     * native stores are bswap(cmd) and words[i]. Page versions get the same
     * per-word increments, summed per page. */
#ifndef VIPER_WII_DIRECT_NO_VRAM
    uint32_t *dst=(uint32_t*)(void*)(vram+word*4);
#if defined(VIPER_WII_CACHE_HINTS) && defined(__PPC__)
    {
        /* Every byte of the packet's whole cache lines is written below, so
         * establish them zeroed (dcbz) instead of reading them from memory. */
        uintptr_t a=((uintptr_t)dst+31)&~(uintptr_t)31,e=((uintptr_t)(dst+count))&~(uintptr_t)31;
        for(;a<e;a+=32)__asm__ volatile("dcbz 0,%0"::"r"(a):"memory");
    }
#endif
    dst[0]=__builtin_bswap32(cmd);
    {
        /* Bits only: FPR loads/stores of 8 bytes move them unchanged, two
         * words per instruction, when both sides are 8-byte aligned. */
        typedef double __attribute__((may_alias)) raw64;
        uint32_t *d=dst+1;const uint32_t *w=words;unsigned n=nwords;
        if(!(((uintptr_t)d|(uintptr_t)w)&7)){
            raw64 *dd=(raw64*)(void*)d;const raw64 *ww=(const raw64*)(const void*)w;
            for(unsigned i=0;i<n/2;i++)dd[i]=ww[i];
            d+=n&~1u;w+=n&~1u;n&=1u;
        }
        for(unsigned i=0;i<n;i++)d[i]=w[i];
    }
    for(unsigned w=word,left=count;left;){
        unsigned n=1024-(w&1023);if(n>left)n=left;
        vram_versions[w/1024]+=n;w+=n;left-=n;
    }
#else
    /* Consumed straight from the producer: the words never need to exist in
     * VRAM. Nothing reads a consumed FIFO packet back, and no texture lives
     * in the FIFO region (a texture there would change the picture: the
     * EFB oracle guards it), so its bytes and page versions are left as
     * they were. Guest-visible FIFO state (read pointer) still advances. */
    (void)word;
#endif
#else
    for(unsigned i=0;i<count;i++)if(fifo_has(off+i*4u)){direct_triangle_reject[5]++;return 0;}
    for(unsigned i=0;i<count;i++,word++){
        uint32_t value=i?__builtin_bswap32(words[i-1]):cmd;
        uint8_t *p=vram+word*4;
        p[0]=value;p[1]=value>>8;p[2]=value>>16;p[3]=value>>24;
        vram_versions[word/1024]++;
    }
    word=off/4;
#endif
#if defined(VIPER_WII_DIRECT_PACKET59) && defined(VIPER_WII_PACKET_VERTICES) && defined(VIPER_WII_FIFO_FORMAT59)
    triangle_packet59(cmd,words);
#else
    triangle_packet(off,cmd);
#endif
#if !defined(VIPER_WII_DIRECT_LEAN) || !defined(VIPER_WII_PACKET_VERTICES)
    fifo_consume(off,count);   /* With the presence test above, every bit here is already clear. */
#endif
    agp[11]=off+count*4u;
    fifo_run();
    direct_triangle_packets++;
    return 1;
}
#endif
void voodoo_init(void) {
#ifdef VIPER_WII_DIRECT_STATE
    direct_state_packets=0;
#endif
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
    layouts_changed();
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
    material_changed();
#endif
    if(!vram) {
        vram=calloc(1,WII_VOODOO_VRAM_BYTES);
        if(!vram)rt_fatal("Wii Voodoo VRAM allocation failed");
    }
    rt_sched_cancel(vblank,NULL);
    memset(vram,0,WII_VOODOO_VRAM_BYTES);
    memset(io,0,sizeof io);memset(agp,0,sizeof agp);memset(regs,0,sizeof regs);
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
    fifo_bounds_refresh();
#endif
    memset(vga,0,sizeof vga);memset(blit_regs,0,sizeof blit_regs);
    memset(tmu_regs,0,sizeof tmu_regs);memset(texture_palette,0,sizeof texture_palette);
    memset(palette_epoch,0,sizeof palette_epoch);memset(vram_versions,0,sizeof vram_versions);
    wii_voodoo_state_epoch++;
    memset(strip,0,sizeof strip);memset(&counters,0,sizeof counters);
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
    memset(format_packets,0,sizeof format_packets);memset(format_vertices,0,sizeof format_vertices);
    memset(triangle_codes,0,sizeof triangle_codes);triangle_fans=0;
#endif
    frames=swap_wait=vblanks_since_swap=swap_pending=geometry_started=strip_count=0;
    fifo_presence_reset();fifo_hdr_invalidate();
    io[0x04/4]=0x01800040;io[0x08/4]=0x40000000;io[0x0c/4]=0x000a2200;
    io[0x18/4]=0x08579d29;io[0x1c/4]=0x00f02200;io[0x24/4]=0x00000bfb;
    for(unsigned i=0;i<512;i++)clut[i]=(i&255)*0x010101;
    rt_sched_at(rt_now()+(uint64_t)(CPU_HZ/57.5),vblank,NULL);
}
uint64_t voodoo_get_frame(uint32_t *dst,int max_pixels,int *w,int *h) {
    (void)dst;(void)max_pixels;if(w)*w=0;if(h)*h=0;return frames;
}
void voodoo_stats(void) {
#ifdef VIPER_WII_DIRECT_TRIANGLES
    rt_log("VIPER WII DIRECT TRIANGLES packets=%llu reject ea=%llu fifo/swap=%llu readptr=%llu hdrcache=%llu format=%llu other=%llu\n",
        direct_triangle_packets,direct_triangle_reject[0],direct_triangle_reject[1],direct_triangle_reject[2],
        direct_triangle_reject[3],direct_triangle_reject[4],direct_triangle_reject[5]);
#endif
#ifdef VIPER_WII_DIRECT_STATE
    rt_log("VIPER WII DIRECT GX STATE packets=%llu\n",(unsigned long long)direct_state_packets);
#endif
#ifdef VIPER_WII_FIFO_FORMAT_PROFILE
    for(unsigned key=0;key<512;key++)if(format_packets[key])
        rt_log("VIPER WII FIFO FORMAT key=%u packets=%llu vertices=%llu\n",key,
            (unsigned long long)format_packets[key],(unsigned long long)format_vertices[key]);
    for(unsigned t=0;t<8;t++)if(fifo_type_packets[t])rt_log("VIPER WII FIFO TYPE %u packets=%llu words=%llu\n",t,
        (unsigned long long)fifo_type_packets[t],(unsigned long long)fifo_type_words[t]);
    for(unsigned r=0;r<0x1000;r++)if(fifo_reg_writes[r]>20000)rt_log("VIPER WII FIFO REG 0x%03x writes=%llu\n",r,(unsigned long long)fifo_reg_writes[r]);
    rt_log("VIPER WII FIFO CODES independent=%llu start=%llu continue=%llu fans=%llu\n",
        (unsigned long long)triangle_codes[0],(unsigned long long)triangle_codes[1],
        (unsigned long long)triangle_codes[2],(unsigned long long)triangle_fans);
#endif
#ifdef VIPER_WII_EPOCH_TRACE
    for(unsigned i=0;i<640;i++)if(epoch_cause[i]>1000)
        rt_log("VIPER WII EPOCH CAUSE %s word=%u (0x%03x) bumps=%llu\n",i<512?"fbi":(i<576?"tmu0":"tmu1"),
            i<512?i:(i-512)%64,i<512?i*4:((i-512)%64)*4,epoch_cause[i]);
#endif
    {
        /* Device-state oracle: VRAM bytes, page versions and epochs. */
        uint32_t h=2166136261u;
        for(size_t i=0;i<WII_VOODOO_VRAM_BYTES;i++)h=(h^vram[i])*16777619u;
        uint32_t hv=2166136261u;
        for(unsigned i=0;i<2048;i++)hv=(hv^vram_versions[i])*16777619u;
        rt_log("VIPER WII VOODOO STATE vram_fnv32=%08lx versions_fnv32=%08lx texture_epoch=%lu state_epoch=%lu\n",
            (unsigned long)h,(unsigned long)hv,(unsigned long)wii_voodoo_texture_epoch,(unsigned long)wii_voodoo_state_epoch);
    }
    rt_log("VIPER WII VOODOO frames=%llu presents=%llu packets=%llu triangles=%llu clears=%llu raw_vram=8388608\n",
      (unsigned long long)frames,(unsigned long long)counters.presents,
      (unsigned long long)counters.packets,(unsigned long long)counters.triangles,
      (unsigned long long)counters.clears);
}
