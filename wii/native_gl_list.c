/* Native replacement for gl f_gl_00029d28 (VIPER_WII_NATIVE_GL_LIST): the
 * indexed-vertex list draw. After reserving FIFO space it writes one type-3
 * header, then for each of n indexed vertices (0x24-byte records at 0x2ed0)
 * forms 1/w by fdivs and writes ten FIFO words: projected x, y, three colour
 * words, the 0x2780 word, then s/w-style scaled terms and 1/w.
 *
 * Line-for-line transcription of the generated C: every value is computed by
 * the same expression and rounding macro, every guest RAM access is the same
 * accessor in the same order (the stack round trips stfs/lwz read back the
 * bits just stored), and the budget is charged at the same points. Guest
 * registers live in locals and are written back before each firing
 * checkpoint and at return.
 *
 * The header and vertex words are held instead of stored: no device access,
 * call or firing checkpoint lies between the header store and the last
 * vertex word, so the device cannot observe the difference. The completed
 * packet goes to wii_voodoo_direct_triangles; if it declines, or before a
 * checkpoint fires, the held words are stored in the generated order. */
#include "ppc_rt.h"
#include "voodoo_headless.h"
#include "native_ram.h"

void f_gl_000211d4(PPCContext *c);
void f_gl_00029d28_generated(PPCContext *c);

#if !defined(VIPER_WII_BULK_WRITER) || !defined(VIPER_WII_DIRECT_TRIANGLES)
#error native gl list needs VIPER_WII_BULK_WRITER and VIPER_WII_DIRECT_TRIANGLES
#endif

static inline void list_store_words(uint32_t ea, const uint32_t *w, unsigned n) {
    if (n && rt_wii_bulk_lfb_allowed() && wii_voodoo_bulk_writer_ready(ea, n)) {
        wii_voodoo_bulk_writer_be(ea, w, n);
        return;
    }
    for (unsigned i = 0; i < n; i++) ST32(ea + 4 * i, w[i]);
}

/* stfs to the stack then lwz of the same word: the bits stored. */
#define stf32_bits(ea, d) native_stf32_bits(native_ram_base, (ea), (d))
static inline uint32_t native_stf32_bits(uint8_t *ram, uint32_t ea, double d) {
    float f = (float)d; uint32_t b; memcpy(&b, &f, 4);
    native_st32(ram, ea, b);
    return b;
}

static uint32_t list_words[150] __attribute__((aligned(32))), list_hdr_ea, list_cmd;
static unsigned list_n;
static int list_held;
static unsigned long long list_direct, list_stored, list_unheld;

/* Store what the generated code would have stored by now. */
static void list_materialize(void) {
    if (!list_held) return;
    list_held = 0;
    ST32LE(list_hdr_ea, list_cmd);
    list_store_words(list_hdr_ea + 4, list_words, list_n);
    list_stored++;
}

void wii_native_gl_list_stats(unsigned long long *direct, unsigned long long *stored,
                              unsigned long long *unheld) {
    *direct = list_direct; *stored = list_stored; *unheld = list_unheld;
}

/* Held words stay exact only while every access in between is plain RAM:
 * the record at base+0x2ed0..0x2eef, the stack frame and the index list. */
#define LIST_RAM_GUARD(base) do { if (UNLIKELY(list_held && ((base) >= RAM_LIMIT - 0x3000u || \
    r1 >= RAM_LIMIT - 0x100u || r8 >= RAM_LIMIT - 8u))) list_materialize(); } while (0)

/* One vertex's ten ST32LE operands, at ea (held or stored). */
static inline void list_vertex(uint32_t ea, const uint32_t v[10]) {
    if (list_held) {
        for (unsigned i = 0; i < 10; i++) list_words[list_n + i] = bswap32(v[i]);
        list_n += 10;
        return;
    }
    for (unsigned i = 0; i < 10; i++) ST32LE(ea + 4 * i, v[i]);
}

WII_HOT_wii_native_gl_00029d28 void wii_native_gl_00029d28(PPCContext *c) {
    /* The stack round trips below are plain RAM only for an in-RAM frame
     * (always so for the game; random test states need the generated body). */
    if (UNLIKELY(c->r[1] - 0x100u >= RAM_LIMIT - 0x200u)) { f_gl_00029d28_generated(c); return; }
    CHK(c, 0x29d28u, 0);
    NATIVE_RAM_BEGIN();
    c->budget -= 32;
    /* Prologue on the context: the reservation call sees it. */
    c->r[0] = c->lr;
#ifdef VIPER_WII_GPR_MULTIPLE
    wii_gpr_stmw(c, c->r[1] + 0xfffffff4u, 29);
#else
    { uint32_t ea = c->r[1] + 0xfffffff4u; for (int k = 29; k < 32; k++, ea += 4) NST32(ea, c->r[k]); }
#endif
    c->r[29] = c->r[3];
    c->r[30] = c->r[4];
    NST32(c->r[1] + 0x8u, c->r[0]);
    c->r[31] = c->r[5];
    { uint32_t ea = c->r[1] + 0xffffff80u; NST32(ea, c->r[1]); c->r[1] = ea; }
    c->r[3] = 0x25cu;
    c->lr = 0x29d4cu;
    f_gl_000211d4(c); if (c->unwind) return;

    uint32_t r1 = c->r[1], r29 = c->r[29], r30 = c->r[30], r31 = c->r[31];
    uint32_t r3, r4, r5, r6, r8, r9, r10, r11, r12, ctr = c->ctr;
    uint8_t cr0, xer_ca;
    double f0, f1, f2, f3, f4, f5, f6, f7, f8, f9;
    uint32_t v[10];

    r3 = ROTL(r31, 24) & 0xff000000u;
    r4 = NLD32(0x3360u);
    r3 = r3 | 0x400000u;
    r9 = NLD32(0x2358u);
    r3 = r3 | 0xbu;
    r3 = r3 ^ 0xbu;
    r3 = r4 ^ r3;
    r8 = ROTL(r29, 6) & 0xffffffc0u;
    r4 = r9;
    r3 = r3 | r8;
    { uint64_t t = (uint64_t)r9 + 0x4u; xer_ca = (uint8_t)(t >> 32); r9 = (uint32_t)t; }
    /* Header: held when the vertices that complete it follow. */
    /* FIFO words bound for guest RAM (never so in the game) may land on the
     * frame slots reloaded below: those vertices keep the generated order of
     * stores and reloads instead of forwarding. */
    int ram_target = r4 < RAM_LIMIT || r4 > 0xfffff000u;
    /* Held words may pass guest RAM accesses only while bound for the device. */
    if ((int32_t)r29 > 0 && r29 <= 15 && !ram_target) {
        list_held = 1; list_hdr_ea = r4; list_cmd = r3; list_n = 0;
    } else {
        list_unheld++;
        NST32LE(r4, r3);
    }
    cr0 = CMPS((int32_t)r29, (int32_t)0) | c->xer_so;
    f9 = NLDF32(0x2024u);
    { uint64_t t = (uint64_t)r30 + 0xfffffffcu; xer_ca = (uint8_t)(t >> 32); r8 = (uint32_t)t; }
    f6 = NLDF32(0x2940u);
    f4 = NLDF32(0x2944u);
    f2 = NLDF32(0x2948u);
    f0 = NLDF32(0x294cu);
    f1 = NLDF32(0x2950u);
    f3 = NLDF32(0x2954u);
    r10 = NLD32(0x2780u);
    /* Registers the generated code leaves untouched when n <= 0. */
    r5 = c->r[5]; r6 = c->r[6]; r11 = c->r[11]; r12 = c->r[12];
    f5 = c->f[5]; f7 = c->f[7]; f8 = c->f[8];
    if (!(cr0 & 4)) goto done;
    c->budget -= 3;
    LIST_RAM_GUARD(0u);
    r8 = r8 + 0x4u; r3 = NLD32(r8);
    ctr = r29;
    ctr--;
    if (ctr == 0) goto last;
vertex: /* 00029db4 */
    c->budget -= 72;
    r4 = ROTL(r3, 3) & 0xfffffff8u;
    { uint64_t t = (uint64_t)r3 + r4; xer_ca = (uint8_t)(t >> 32); r3 = (uint32_t)t; }
    r11 = ROTL(r3, 2) & 0xfffffffcu;
    LIST_RAM_GUARD(r11);
    r4 = r9;
    f5 = NLDF32(r11 + 0x2eecu);
    { uint64_t t = (uint64_t)r9 + 0x4u; xer_ca = (uint8_t)(t >> 32); r6 = (uint32_t)t; }
    f7 = NLDF32(r11 + 0x2ee4u);
    f5 = ROUND_S(f9 / f5);
    f8 = ROUND_S(f7 * f5);
    f8 = ROUND_S(fma(f8, f6, f4));
    f7 = ROUND_S(fma(f5, f1, f3));
    r3 = stf32_bits(r1 + 0x40u, f8);
    f8 = NLDF32(r11 + 0x2ee8u);
    v[6] = stf32_bits(r1 + 0x58u, f7);
    v[7] = stf32_bits(r1 + 0x5cu, f5);
    f7 = ROUND_S(f5 * f8);
    f7 = ROUND_S(fma(f7, f2, f0));
    r5 = stf32_bits(r1 + 0x44u, f7);
    f7 = NLDF32(r11 + 0x2edcu);
    f7 = ROUND_S(f5 * f7);
    v[8] = stf32_bits(r1 + 0x60u, f7);
    f7 = NLDF32(r11 + 0x2ee0u);
    f5 = ROUND_S(f5 * f7);
    v[9] = stf32_bits(r1 + 0x64u, f5);
    r31 = NLD32(r11 + 0x2ed0u);
    r12 = NLD32(r11 + 0x2ed8u);
    r11 = NLD32(r11 + 0x2ed4u);
    v[0] = r3; v[1] = r5; v[2] = r31; v[3] = r11; v[4] = r12; v[5] = r10;
    if (UNLIKELY(ram_target)) {
        for (unsigned i = 0; i < 5; i++) NST32LE(r4 + 4 * i, v[i]);
        v[6] = NLD32(r1 + 0x58u); v[7] = NLD32(r1 + 0x5cu); v[8] = NLD32(r1 + 0x60u); v[9] = NLD32(r1 + 0x64u);
        for (unsigned i = 5; i < 10; i++) NST32LE(r4 + 4 * i, v[i]);
    } else list_vertex(r4, v);
    /* Registers as the store sequence leaves them (stack words reloaded). */
    r5 = v[6]; r6 = v[7]; r31 = v[8]; r11 = v[9];
    r4 = r9 + 0x24u;
    r12 = r9 + 0x24u;
    { uint64_t t = (uint64_t)r9 + 0x28u; xer_ca = (uint8_t)(t >> 32); r9 = (uint32_t)t; }
    r8 = r8 + 0x4u; r3 = NLD32(r8);
    ctr--;
    if (ctr != 0) {
        if (UNLIKELY(c->budget <= 0)) {
            list_materialize();
            c->r[3] = r3; c->r[4] = r4; c->r[5] = r5; c->r[6] = r6; c->r[8] = r8; c->r[9] = r9;
            c->r[10] = r10; c->r[11] = r11; c->r[12] = r12; c->r[29] = r29; c->r[30] = r30;
            c->r[31] = r31; c->r[1] = r1;
            c->f[0] = f0; c->f[1] = f1; c->f[2] = f2; c->f[3] = f3; c->f[4] = f4; c->f[5] = f5;
            c->f[6] = f6; c->f[7] = f7; c->f[8] = f8; c->f[9] = f9;
            c->cr[0] = cr0; c->xer_ca = xer_ca; c->ctr = ctr;
            CHK(c, 0x29db4u, 0);
            r3 = c->r[3]; r4 = c->r[4]; r5 = c->r[5]; r6 = c->r[6]; r8 = c->r[8]; r9 = c->r[9];
            r10 = c->r[10]; r11 = c->r[11]; r12 = c->r[12]; r29 = c->r[29]; r30 = c->r[30];
            r31 = c->r[31]; r1 = c->r[1];
            f0 = c->f[0]; f1 = c->f[1]; f2 = c->f[2]; f3 = c->f[3]; f4 = c->f[4]; f5 = c->f[5];
            f6 = c->f[6]; f7 = c->f[7]; f8 = c->f[8]; f9 = c->f[9];
            cr0 = c->cr[0]; xer_ca = c->xer_ca; ctr = c->ctr;
        }
        goto vertex;
    }
last: /* 00029ed4 */
    c->budget -= 70;
    r4 = ROTL(r3, 3) & 0xfffffff8u;
    { uint64_t t = (uint64_t)r3 + r4; xer_ca = (uint8_t)(t >> 32); r3 = (uint32_t)t; }
    r8 = ROTL(r3, 2) & 0xfffffffcu;
    LIST_RAM_GUARD(r8);
    r4 = r9;
    f5 = NLDF32(r8 + 0x2eecu);
    { uint64_t t = (uint64_t)r9 + 0x4u; xer_ca = (uint8_t)(t >> 32); r31 = (uint32_t)t; }
    f7 = NLDF32(r8 + 0x2ee4u);
    f5 = ROUND_S(f9 / f5);
    f7 = ROUND_S(f7 * f5);
    f1 = ROUND_S(fma(f5, f1, f3));
    f3 = ROUND_S(fma(f7, f6, f4));
    r3 = stf32_bits(r1 + 0x40u, f3);
    f3 = NLDF32(r8 + 0x2ee8u);
    v[6] = stf32_bits(r1 + 0x58u, f1);
    v[7] = stf32_bits(r1 + 0x5cu, f5);
    f1 = ROUND_S(f5 * f3);
    f0 = ROUND_S(fma(f1, f2, f0));
    r12 = stf32_bits(r1 + 0x44u, f0);
    f0 = NLDF32(r8 + 0x2edcu);
    f0 = ROUND_S(f5 * f0);
    v[8] = stf32_bits(r1 + 0x60u, f0);
    f0 = NLDF32(r8 + 0x2ee0u);
    f0 = ROUND_S(f5 * f0);
    v[9] = stf32_bits(r1 + 0x64u, f0);
    r11 = NLD32(r8 + 0x2ed0u);
    r5 = NLD32(r8 + 0x2ed8u);
    r8 = NLD32(r8 + 0x2ed4u);
    v[0] = r3; v[1] = r12; v[2] = r11; v[3] = r8; v[4] = r5; v[5] = r10;
    {
        uint32_t base = r9;
        if (UNLIKELY(ram_target)) {
            for (unsigned i = 0; i < 5; i++) NST32LE(base + 4 * i, v[i]);
            v[6] = NLD32(r1 + 0x58u); v[7] = NLD32(r1 + 0x5cu); v[8] = NLD32(r1 + 0x60u); v[9] = NLD32(r1 + 0x64u);
            for (unsigned i = 5; i < 10; i++) NST32LE(base + 4 * i, v[i]);
        }
        r11 = v[6]; r5 = v[7]; r10 = v[8]; r12 = v[9];
        r3 = r12;
        r4 = r9 + 0x24u;
        r8 = r9 + 0x24u;
        { uint64_t t = (uint64_t)r9 + 0x28u; xer_ca = (uint8_t)(t >> 32); r9 = (uint32_t)t; }
        if (!list_held) {
            if (!ram_target) for (unsigned i = 0; i < 10; i++) NST32LE(base + 4 * i, v[i]);
            goto done;
        }
    }
    /* The last word completes the held packet. */
    for (unsigned i = 0; i < 10; i++) list_words[list_n + i] = bswap32(v[i]);
    list_n += 10;
    list_held = 0;
    if (wii_voodoo_direct_triangles(list_hdr_ea, list_cmd, list_words, list_n)) list_direct++;
    else {
        list_stored++;
        NST32LE(list_hdr_ea, list_cmd);
        list_store_words(list_hdr_ea + 4, list_words, list_n);
    }
done: /* 00029fec */
    list_materialize();
    c->budget -= 6;
    NST32(0x2358u, r9);
    r12 = NLD32(r1 + 0x88u);
    { uint64_t t = (uint64_t)r1 + 0x80u; xer_ca = (uint8_t)(t >> 32); r1 = (uint32_t)t; }
    c->r[1] = r1; c->r[3] = r3; c->r[4] = r4; c->r[5] = r5; c->r[6] = r6; c->r[8] = r8;
    c->r[9] = r9; c->r[10] = r10; c->r[11] = r11; c->r[12] = r12;
    c->f[0] = f0; c->f[1] = f1; c->f[2] = f2; c->f[3] = f3; c->f[4] = f4; c->f[5] = f5;
    c->f[6] = f6; c->f[7] = f7; c->f[8] = f8; c->f[9] = f9;
    c->cr[0] = cr0; c->xer_ca = xer_ca; c->ctr = ctr;
#ifdef VIPER_WII_GPR_MULTIPLE
    wii_gpr_lmw(c, r1 + 0xfffffff4u, 29);
#else
    { uint32_t ea = r1 + 0xfffffff4u; for (int k = 29; k < 32; k++, ea += 4) c->r[k] = NLD32(ea); }
#endif
    c->lr = r12;
    RETURN(c);
}
