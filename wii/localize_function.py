"""Mechanically localize one recompiled guest function: guest registers move
from the PPCContext into C locals, with the context made current wherever
anything else can observe it.

Rules (the ones proven by hand on f_gl_0002adac, wii/native_gl_draw.c):
- Every c->r[n], c->f[n], c->cr[n], c->ctr, c->lr and c->xer_* becomes a
  local, loaded once after the entry checkpoint.
- Any call that takes the context (guest functions, rt_call, rt_hook,
  rt_lswi, rt_stswi) is preceded by a full write-back and followed by a
  reload; an unwind return after it returns without further writes.
- CHK writes back and reloads only when it fires (the budget stays in the
  context); RETURN and every return write back first.
- rt_cr_unpack/rt_cr_pack, rt_sraw and rt_divwu become the same operations
  on the locals. rt_frsqrte, rt_fctiw and rt_mftb do not touch registers.
The function keeps its name, labels, budget charges and memory accesses in
order, so it is exact by construction; wii/test_localize.py checks it.

With gather=True, ST32LE stores into the Voodoo LFB window (the command
FIFO) are also buffered while their addresses stay consecutive and written
as one run through the bulk FIFO writer when it accepts them, otherwise as
the same scalar stores in the same order. The device never reads guest RAM,
so RAM accesses may pass buffered words; every access at or above RAM_LIMIT
(device space) and every SYNC_OUT (calls, firing checkpoints, returns)
flushes first, so whatever can observe the device sees the generated order.

With gather='direct' (header-last producers such as f_gl_00028fc4), the
DRIVING_BULK/DRIVING_TAIL specializations are first resolved to their scalar
text; ST32 stores into the LFB window are buffered the same way, and a
ST32LE that stores a packet header just before the buffered words hands the
whole packet to wii_voodoo_direct_triangles. When the device declines, the
words and then the header are stored as before."""
import re
import sys

REG = re.compile(r'c->(r|f|cr)\[(\d+)\]|c->(ctr|lr|xer_ca|xer_so|xer_ov|xer_bc)\b')
CONTEXT_CALL = re.compile(r'\b(f_[a-z]+_[0-9a-f]+|rt_call|rt_hook|rt_lswi|rt_stswi)\(c\b')


def _local(m):
    if m.group(1):
        return f'{m.group(1)}_{m.group(2)}'
    return f'{m.group(3)}_'


def _balanced(text, start):
    """Index just past the parenthesised group starting at text[start] == '('."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == '(':
            depth += 1
        elif text[i] == ')':
            depth -= 1
            if depth == 0:
                return i + 1
    raise ValueError('unbalanced parentheses')


def _args(text, open_paren):
    end = _balanced(text, open_paren)
    inner = text[open_paren + 1:end - 1]
    parts, depth, cur = [], 0, ''
    for ch in inner:
        if ch == ',' and depth == 0:
            parts.append(cur.strip()); cur = ''
            continue
        depth += ch in '([{'
        depth -= ch in ')]}'
        cur += ch
    parts.append(cur.strip())
    return parts, end


def _rewrite_helpers(body):
    out, i = [], 0
    pat = re.compile(r'\b(rt_cr_unpack|rt_cr_pack|rt_sraw|rt_divwu)\(')
    while True:
        m = pat.search(body, i)
        if not m:
            out.append(body[i:])
            return ''.join(out)
        out.append(body[i:m.start()])
        args, end = _args(body, m.end() - 1)
        name = m.group(1)
        if args[0] != 'c':
            raise ValueError(f'{name} without context argument')
        if name == 'rt_cr_unpack':
            v, crm = args[1], args[2]
            fields = ''.join(f'if(_m&0x{0x80 >> k:02x}u)c->cr[{k}]=(_v>>{28 - 4 * k})&15;' for k in range(8))
            out.append(f'(void)({{ uint32_t _v=({v}),_m=({crm});{fields} }})')
        elif name == 'rt_cr_pack':
            out.append('(' + '|'.join(f'((uint32_t)(c->cr[{k}]&15)<<{28 - 4 * k})' for k in range(8)) + ')')
        elif name == 'rt_sraw':
            v, n = args[1], args[2]
            out.append(f'({{ uint32_t _v=({v}),_n=({n}),_r; int32_t _s=(int32_t)_v; '
                       f'if(_n>=32){{c->xer_ca=_s<0;_r=(uint32_t)(_s>>31);}} '
                       f'else if(!_n){{c->xer_ca=0;_r=_v;}} '
                       f'else{{c->xer_ca=(_s<0)&&(_v&((1u<<_n)-1));_r=(uint32_t)(_s>>_n);}} _r; }})')
        else:
            a, b, oe = args[1], args[2], args[3]
            out.append(f'({{ uint32_t _a=({a}),_b=({b}); if({oe}){{c->xer_ov=(_b==0);c->xer_so|=c->xer_ov;}} '
                       f'_b?_a/_b:0; }})')
        i = end


def _wrap_context_calls(body):
    """Write back before and reload after each context call statement."""
    out, i = [], 0
    while True:
        m = CONTEXT_CALL.search(body, i)
        if not m:
            out.append(body[i:])
            return ''.join(out)
        # The statement containing the call: from the previous ';', '{' or '}'
        # (or line start) to the ';' that ends the call.
        start = max(body.rfind(';', 0, m.start()), body.rfind('{', 0, m.start()),
                    body.rfind('}', 0, m.start()), body.rfind(':\n', 0, m.start()) + 1) + 1
        _, end = _args(body, m.start() + len(m.group(1)))
        semi = body.index(';', end)
        stmt = body[start:semi + 1]
        lead = stmt[:len(stmt) - len(stmt.lstrip())]
        core = stmt.strip()
        assign = re.match(r'^(c->(?:r|f)\[\d+\])\s*=\s*(.+);$', core, re.S)
        out.append(body[i:start])
        if assign:
            out.append(f'{lead}{{ SYNC_OUT(); __typeof__({assign.group(1)}) _t = {assign.group(2)}; '
                       f'SYNC_IN(); {assign.group(1)} = _t; }}')
        elif core.endswith(';') and core.startswith(m.group(1)):
            out.append(f'{lead}SYNC_OUT(); {core} SYNC_IN();')
        else:
            raise ValueError('unsupported context call statement: ' + core[:120])
        i = semi + 1


REVIEWED_CONTEXT_HELPERS = {'rt_call', 'rt_hook', 'rt_lswi', 'rt_stswi', 'rt_cr_pack', 'rt_cr_unpack',
                            'rt_divwu', 'rt_sraw', 'rt_fctiw', 'rt_mftb', 'CHK', 'RETURN', 'TRACE'}
RAM_ACCESSORS = ('LD8', 'LD16', 'LD32', 'LD32LE', 'LDF32', 'LDF64',
                 'ST8', 'ST16', 'ST32', 'ST16LE', 'STF32', 'STF64')
GATHER_PRELUDE = r'''#ifndef LOCALIZE_GATHER_DEFINED
#define LOCALIZE_GATHER_DEFINED
static uint32_t gather_words[150], gather_ea;
static unsigned gather_n;
#if defined(VIPER_WII_GATHER_PRESERVE) && defined(VIPER_WII_RAM_BASE_LOCAL)
#include "preserve_call.h"
__attribute__((noinline)) static void gather_flush_words(void);
static inline void gather_flush(void) {
    if (UNLIKELY(gather_n)) wii_pcall_v_v(gather_flush_words);
}
__attribute__((noinline)) static void gather_flush_words(void) {
#else
static inline void gather_flush(void) {
#endif
    if (!gather_n) return;
    unsigned n = gather_n;
    gather_n = 0;
    unsigned i = 0;
#ifdef VIPER_WII_BULK_WRITER
    if (rt_wii_bulk_lfb_allowed()) {
        if (wii_voodoo_bulk_writer_ready(gather_ea, n)) {
            wii_voodoo_bulk_writer_be(gather_ea, gather_words, n);
            return;
        }
        /* Header-first packets: words up to the one that completes it. */
        i = wii_voodoo_fifo_append_be(gather_ea, gather_words, n);
    }
#endif
    for (; i < n; i++) ST32(gather_ea + 4 * i, gather_words[i]);
}
#define GATHER_ST32LE(ea_, v_) ({ uint32_t _gea = (ea_), _gv = (v_); \
    if (_gea - 0x84000000u < 0x02000000u) { \
        if (gather_n && (_gea != gather_ea + 4 * gather_n || gather_n == 150)) gather_flush(); \
        if (!gather_n) gather_ea = _gea; \
        gather_words[gather_n++] = bswap32(_gv); \
    } else GATHER_ST32LE_OTHER(_gea, _gv); })
#if defined(VIPER_WII_GATHER_PRESERVE) && defined(VIPER_WII_RAM_BASE_LOCAL)
#define GATHER_ST32LE_OTHER(ea_, v_) do { \
    if (LIKELY(NATIVE_IN_RAM(ea_, 4))) RST32(ea_, bswap32(v_)); \
    else { if (ea_ >= RAM_LIMIT) gather_flush(); wii_pcall_v_uu(wii_ram_slow_st32, ea_, bswap32(v_)); } } while (0)
#else
#define GATHER_ST32LE_OTHER(ea_, v_) do { if (ea_ >= RAM_LIMIT) gather_flush(); ST32LE(ea_, v_); } while (0)
#endif
#define GATHER_FLUSHED(f) (gather_flush(), f)
#if defined(VIPER_WII_GATHER_PRESERVE) && defined(VIPER_WII_RAM_BASE_LOCAL)
/* As the direct prelude's VIPER_WII_PRESERVE_SLOW: the fast path is the
 * accessor's own in-RAM-and-aligned test (GCC folds the accessor's copy),
 * and everything else, exactly the original accessor, is a register-
 * preserving call (wii/preserve_call.h). Device space flushes first. */
#include "preserve_call.h"
#define GATHER_SIZE_RLD8 1u
#define GATHER_SIZE_RLD16 2u
#define GATHER_SIZE_RLD32 4u
#define GATHER_SIZE_RLD32LE 4u
#define GATHER_SIZE_RLDF32 4u
#define GATHER_SIZE_RLDF64 8u
#define GATHER_SIZE_RST8 1u
#define GATHER_SIZE_RST16 2u
#define GATHER_SIZE_RST32 4u
#define GATHER_SIZE_ST16LE 2u
#define GATHER_SIZE_RSTF32 4u
#define GATHER_SIZE_RSTF64 8u
#define GATHER_SLOW_RLD8(a) wii_pcall_u_u(wii_ram_slow_ld8, (a))
#define GATHER_SLOW_RLD16(a) wii_pcall_u_u(wii_ram_slow_ld16, (a))
#define GATHER_SLOW_RLD32(a) wii_pcall_u_u(wii_ram_slow_ld32, (a))
#define GATHER_SLOW_RLD32LE(a) bswap32(wii_pcall_u_u(wii_ram_slow_ld32, (a)))
#define GATHER_SLOW_RLDF32(a) wii_pcall_d_u(wii_ram_slow_ldf32, (a))
#define GATHER_SLOW_RLDF64(a) wii_pcall_d_u(wii_ram_slow_ldf64, (a))
#define GATHER_SLOW_RST8(a, v) wii_pcall_v_uu(wii_ram_slow_st8, (a), (v))
#define GATHER_SLOW_RST16(a, v) wii_pcall_v_uu(wii_ram_slow_st16, (a), (v))
#define GATHER_SLOW_RST32(a, v) wii_pcall_v_uu(wii_ram_slow_st32, (a), (v))
#define GATHER_SLOW_ST16LE(a, v) wii_pcall_v_uu(wii_ram_slow_st16, (a), bswap16((uint16_t)(v)))
#define GATHER_SLOW_RSTF32(a, d) wii_pcall_v_ud(wii_ram_slow_stf32, (a), (d))
#define GATHER_SLOW_RSTF64(a, d) wii_pcall_v_ud(wii_ram_slow_stf64, (a), (d))
#define GATHER_IN_RAM(fn, a) (!((a) & (0xff000000u | (GATHER_SIZE_##fn - 1u))))
#define GATHER_LOAD(fn, ea_) ({ uint32_t _gla = (ea_); __typeof__(fn(0)) _glv; \
    if (LIKELY(GATHER_IN_RAM(fn, _gla))) _glv = fn(_gla); \
    else { if (_gla >= RAM_LIMIT) gather_flush(); _glv = GATHER_SLOW_##fn(_gla); } _glv; })
#define GATHER_STORE(fn, ea_, v_) ({ uint32_t _gsa = (ea_); \
    if (LIKELY(GATHER_IN_RAM(fn, _gsa))) fn(_gsa, (v_)); \
    else { if (_gsa >= RAM_LIMIT) gather_flush(); GATHER_SLOW_##fn(_gsa, (v_)); } })
#else
#define GATHER_LOAD(fn, ea_) ({ uint32_t _gla = (ea_); if (UNLIKELY(_gla >= RAM_LIMIT)) gather_flush(); fn(_gla); })
#define GATHER_STORE(fn, ea_, v_) ({ uint32_t _gsa = (ea_); if (UNLIKELY(_gsa >= RAM_LIMIT)) gather_flush(); fn(_gsa, (v_)); })
#endif
#endif
'''

DIRECT_PRELUDE = r'''#ifndef LOCALIZE_DIRECT_DEFINED
#define LOCALIZE_DIRECT_DEFINED
int wii_voodoo_direct_triangles(uint32_t header_ea, uint32_t cmd, const uint32_t *words, unsigned nwords);
static uint32_t direct_words[150] __attribute__((aligned(32))), direct_ea;
static unsigned direct_n;
/* The store itself out of line: the inline test is all that is left at the
 * ~100 call sites (device-space guards, SYNC_OUT) of a gathering function. */
__attribute__((noinline)) static void direct_flush_words(void);
/* VIPER_WII_DIRECT_LOCAL: the buffer's count and address live in locals of
 * the function (DIRECT_LOCALS), so GCC keeps them in registers instead of
 * reloading the statics after every guest RAM store (those go through
 * may_alias pointers). Every write-back flushes, so the count is 0 whenever
 * the function returns or calls out, and starts at 0. The out-of-line
 * writer still takes them from the statics, set just before it runs. */
#if defined(VIPER_WII_DIRECT_LOCAL)
#if defined(VIPER_WII_PRESERVE_DTRI)
#error VIPER_WII_DIRECT_LOCAL and VIPER_WII_PRESERVE_DTRI are exclusive
#endif
#define DN dl_n
#define DEA dl_ea
#if defined(VIPER_WII_DIRECT_GROUP)
/* A run of k stores to consecutive words from one base (wii/
 * localize_function.py _direct_groups) is checked once, at its first store:
 * in the FIFO window with room for all k words, contiguous with the buffer
 * (or starting it), and room in the buffer. Each store is then a plain
 * append; any flush in between clears the flag, so the stores after it
 * take the full path, as they would have. */
#define DIRECT_LOCALS() unsigned dl_n = 0; uint32_t dl_ea = 0; int dl_fast = 0
#define DIRECT_FAST_CLEAR() dl_fast = 0,
#define DIRECT_GROUP_BEGIN(ea_, k_) do { uint32_t _gb = (ea_); \
    if (LIKELY(_gb - 0x84000000u <= 0x02000000u - 4u * (k_) && dl_n + (k_) <= 150u && \
               (!dl_n || _gb == dl_ea + 4u * dl_n))) { if (!dl_n) dl_ea = _gb; dl_fast = 1; } \
    else dl_fast = 0; } while (0)
#define DIRECT_GST32(ea_, v_) do { if (LIKELY(dl_fast)) direct_words[dl_n++] = (v_); \
    else DIRECT_ST32(ea_, v_); } while (0)
#else
#define DIRECT_LOCALS() unsigned dl_n = 0; uint32_t dl_ea = 0
#define DIRECT_FAST_CLEAR()
#endif
#define DIRECT_FLUSH_WITH(call) (UNLIKELY(dl_n) ? (void)(DIRECT_FAST_CLEAR() direct_n = dl_n, direct_ea = dl_ea, dl_n = 0, \
    DBUD_OUT(), call, DBUD_IN()) : (void)0)
#else
#define DN direct_n
#define DEA direct_ea
#define DIRECT_LOCALS() do { } while (0)
#endif
#if defined(VIPER_WII_DIRECT_BUDGET)
#if !defined(VIPER_WII_DIRECT_LOCAL)
#error VIPER_WII_DIRECT_BUDGET needs VIPER_WII_DIRECT_LOCAL
#endif
#define DBUD dl_budget
#define DBUD_OUT() (void)(c->budget = dl_budget)
#define DBUD_IN() (void)(dl_budget = c->budget)
#define DIRECT_BUDGET_LOCAL() __typeof__(c->budget) dl_budget = c->budget
#define DIRECT_MFTB(...) (DBUD_OUT(), rt_mftb(c, __VA_ARGS__))
#else
#define DIRECT_MFTB(...) rt_mftb(c, __VA_ARGS__)
#define DBUD c->budget
#define DBUD_OUT() (void)0
#define DBUD_IN() (void)0
#define DIRECT_BUDGET_LOCAL() do { } while (0)
#endif
#if !defined(VIPER_WII_DIRECT_GROUP)
#define DIRECT_GROUP_BEGIN(ea_, k_) do { } while (0)
#define DIRECT_GST32(ea_, v_) DIRECT_ST32(ea_, v_)
#elif !defined(VIPER_WII_DIRECT_LOCAL)
#error VIPER_WII_DIRECT_GROUP needs VIPER_WII_DIRECT_LOCAL
#endif
#if defined(VIPER_WII_PRESERVE_SLOW) && defined(VIPER_WII_RAM_BASE_LOCAL)
/* Cold calls through wii/preserve_call.h: the localized values stay in the
 * volatile registers across them. The fast paths are the accessors' own
 * (in RAM and aligned); everything else is exactly ST32 (wii_ram_slow_st32). */
#include "preserve_call.h"
#if defined(VIPER_WII_DIRECT_LOCAL)
#define direct_flush() DIRECT_FLUSH_WITH(wii_pcall_v_v(direct_flush_words))
#else
static inline void direct_flush(void) {
    if (direct_n) wii_pcall_v_v(direct_flush_words);
}
#endif
#define DIRECT_ST32_OTHER(ea_, v_) do { \
    if (LIKELY(NATIVE_IN_RAM(ea_, 4))) RST32(ea_, v_); \
    else { DBUD_OUT(); if (ea_ >= RAM_LIMIT) direct_flush(); wii_pcall_v_uu(wii_ram_slow_st32, ea_, v_); DBUD_IN(); } } while (0)
/* Slow paths of the R* accessors: each is exactly the original accessor
 * (wii/ram_access_cold.c), as the R* accessors are for every address. */
#define DIRECT_SLOW_RLD8(a) wii_pcall_u_u(wii_ram_slow_ld8, (a))
#define DIRECT_SLOW_RLD16(a) wii_pcall_u_u(wii_ram_slow_ld16, (a))
#define DIRECT_SLOW_RLD32(a) wii_pcall_u_u(wii_ram_slow_ld32, (a))
#define DIRECT_SLOW_RLD32LE(a) bswap32(wii_pcall_u_u(wii_ram_slow_ld32, (a)))
#define DIRECT_SLOW_RLDF32(a) wii_pcall_d_u(wii_ram_slow_ldf32, (a))
#define DIRECT_SLOW_RLDF64(a) wii_pcall_d_u(wii_ram_slow_ldf64, (a))
#define DIRECT_SLOW_RST8(a, v) wii_pcall_v_uu(wii_ram_slow_st8, (a), (v))
#define DIRECT_SLOW_RST16(a, v) wii_pcall_v_uu(wii_ram_slow_st16, (a), (v))
#define DIRECT_SLOW_ST16LE(a, v) wii_pcall_v_uu(wii_ram_slow_st16, (a), bswap16((uint16_t)(v)))
#define DIRECT_SLOW_RSTF32(a, d) wii_pcall_v_ud(wii_ram_slow_stf32, (a), (d))
#define DIRECT_SLOW_RSTF64(a, d) wii_pcall_v_ud(wii_ram_slow_stf64, (a), (d))
#define DIRECT_SLOW_LOAD(fn, a) DIRECT_SLOW_##fn(a)
#define DIRECT_SLOW_STORE(fn, a, v) DIRECT_SLOW_##fn(a, v)
#else
#if defined(VIPER_WII_DIRECT_LOCAL)
#define direct_flush() DIRECT_FLUSH_WITH(direct_flush_words())
#else
static inline void direct_flush(void) {
    if (direct_n) direct_flush_words();
}
#endif
#define DIRECT_ST32_OTHER(ea_, v_) do { DBUD_OUT(); if (ea_ >= RAM_LIMIT) direct_flush(); ST32(ea_, v_); DBUD_IN(); } while (0)
#define DIRECT_SLOW_LOAD(fn, a) fn(a)
#define DIRECT_SLOW_STORE(fn, a, v) fn(a, v)
#endif
__attribute__((noinline)) static void direct_flush_words(void) {
    unsigned n = direct_n;
    direct_n = 0;
#ifdef VIPER_WII_BULK_WRITER
    if (rt_wii_bulk_lfb_allowed() && wii_voodoo_bulk_writer_ready(direct_ea, n)) {
        wii_voodoo_bulk_writer_be(direct_ea, direct_words, n);
        return;
    }
#endif
    for (unsigned i = 0; i < n; i++) ST32(direct_ea + 4 * i, direct_words[i]);
}
#if defined(VIPER_WII_PRESERVE_DTRI)
/* The packet hand-off through wii/preserve_call.h, words and count from the
 * statics (direct_words is passed as itself). */
#include "preserve_call.h"
static int direct_triangles_preserved(uint32_t header_ea, uint32_t cmd) {
    return wii_voodoo_direct_triangles(header_ea, cmd, direct_words, direct_n);
}
#define DIRECT_TRIANGLES(h_, c_, w_, n_) ((int)wii_pcall_u_uu(direct_triangles_preserved, (h_), (c_)))
#else
#define DIRECT_TRIANGLES wii_voodoo_direct_triangles
#endif
#define DIRECT_ST32(ea_, v_) ({ uint32_t _gea = (ea_), _gv = (v_); \
    if (_gea - 0x84000000u < 0x02000000u) { \
        if (UNLIKELY(DN && (_gea != DEA + 4 * DN || DN == 150))) direct_flush(); \
        if (UNLIKELY(!DN)) DEA = _gea; \
        direct_words[DN++] = _gv; \
    } else DIRECT_ST32_OTHER(_gea, _gv); })
#define DIRECT_ST32LE(ea_, v_) ({ uint32_t _gea = (ea_), _gv = (v_); int _gr; \
    if (DN && _gea + 4 == DEA && \
        (DBUD_OUT(), _gr = DIRECT_TRIANGLES(_gea, _gv, direct_words, DN), DBUD_IN(), _gr)) DN = 0; \
    else DIRECT_ST32_OTHER(_gea, bswap32(_gv)); })
#define DIRECT_FLUSHED(f) (direct_flush(), f)
/* One range test on the common path: below RAM_SIZE - 8 the accessor's own
 * in-RAM test is known true; the flush still happens exactly when ea is in
 * device space (>= RAM_LIMIT). With VIPER_WII_DIRECT_MASK the test is the
 * in-RAM-and-aligned mask of VIPER_WII_RAM_MASK_TEST instead, the same
 * expression as the accessor's own test, so GCC folds that one away. Any
 * address passing either test is below RAM_LIMIT, so both are exact. */
#if defined(VIPER_WII_DIRECT_MASK) || defined(VIPER_WII_PRESERVE_SLOW)
#define DIRECT_SIZE_LD8 1u
#define DIRECT_SIZE_LD16 2u
#define DIRECT_SIZE_LD32 4u
#define DIRECT_SIZE_LD32LE 4u
#define DIRECT_SIZE_LDF32 4u
#define DIRECT_SIZE_LDF64 8u
#define DIRECT_SIZE_ST8 1u
#define DIRECT_SIZE_ST16 2u
#define DIRECT_SIZE_ST16LE 2u
#define DIRECT_SIZE_STF32 4u
#define DIRECT_SIZE_STF64 8u
#define DIRECT_SIZE_RLD8 1u
#define DIRECT_SIZE_RLD16 2u
#define DIRECT_SIZE_RLD32 4u
#define DIRECT_SIZE_RLD32LE 4u
#define DIRECT_SIZE_RLDF32 4u
#define DIRECT_SIZE_RLDF64 8u
#define DIRECT_SIZE_RST8 1u
#define DIRECT_SIZE_RST16 2u
#define DIRECT_SIZE_RST16LE 2u
#define DIRECT_SIZE_RSTF32 4u
#define DIRECT_SIZE_RSTF64 8u
#define DIRECT_IN_RAM(fn, a) (!((a) & (0xff000000u | (DIRECT_SIZE_##fn - 1u))))
#else
#define DIRECT_IN_RAM(fn, a) ((a) <= RAM_SIZE - 8u)
#endif
/* VIPER_WII_DIRECT_VERSION: the accessors' in-RAM paths, for blocks whose
 * addresses were all checked once at the top (_direct_version). */
#if defined(VIPER_WII_DIRECT_VERSION) && defined(VIPER_WII_RAM_BASE_LOCAL)
#define DIRECT_VERSION_OK(c_) (c_)
typedef float __attribute__((may_alias)) direct_raw_f32;
typedef double __attribute__((may_alias)) direct_raw_f64;
typedef uint32_t __attribute__((may_alias)) direct_raw_u32;
typedef uint16_t __attribute__((may_alias)) direct_raw_u16;
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define DIRECT_RAW_RLD8(a) ((uint32_t)native_ram_base[a])
#define DIRECT_RAW_RLD16(a) ((uint32_t)*(const direct_raw_u16 *)(const void *)(native_ram_base + (a)))
#define DIRECT_RAW_RLD32(a) (*(const direct_raw_u32 *)(const void *)(native_ram_base + (a)))
#define DIRECT_RAW_RLD32LE(a) bswap32(*(const direct_raw_u32 *)(const void *)(native_ram_base + (a)))
#define DIRECT_RAW_RLDF32(a) ((double)*(const direct_raw_f32 *)(const void *)(native_ram_base + (a)))
#define DIRECT_RAW_RLDF64(a) (*(const direct_raw_f64 *)(const void *)(native_ram_base + (a)))
#define DIRECT_RAW_RST8(a, v) ((void)(native_ram_base[a] = (uint8_t)(v)))
#define DIRECT_RAW_RST16(a, v) ((void)(*(direct_raw_u16 *)(void *)(native_ram_base + (a)) = (uint16_t)(v)))
#define DIRECT_RAW_RSTF32(a, d) ((void)(*(direct_raw_f32 *)(void *)(native_ram_base + (a)) = (float)(d)))
#define DIRECT_RAW_RSTF64(a, d) ((void)(*(direct_raw_f64 *)(void *)(native_ram_base + (a)) = (d)))
#else
/* Little-endian hosts (the localize test): guest RAM is big-endian. */
static inline uint32_t direct_raw_ld32(const uint8_t *p) { uint32_t v; memcpy(&v, p, 4); return guest_be32(v); }
static inline double direct_raw_ldf32(const uint8_t *p) { uint32_t v = direct_raw_ld32(p); float f; memcpy(&f, &v, 4); return f; }
static inline double direct_raw_ldf64(const uint8_t *p) { uint64_t v = ((uint64_t)direct_raw_ld32(p) << 32) | direct_raw_ld32(p + 4); double d; memcpy(&d, &v, 8); return d; }
static inline void direct_raw_st32(uint8_t *p, uint32_t v) { v = guest_be32(v); memcpy(p, &v, 4); }
static inline void direct_raw_stf32(uint8_t *p, double d) { float f = (float)d; uint32_t v; memcpy(&v, &f, 4); direct_raw_st32(p, v); }
static inline void direct_raw_stf64(uint8_t *p, double d) { uint64_t v; memcpy(&v, &d, 8); direct_raw_st32(p, (uint32_t)(v >> 32)); direct_raw_st32(p + 4, (uint32_t)v); }
#define DIRECT_RAW_RLD8(a) ((uint32_t)native_ram_base[a])
#define DIRECT_RAW_RLD16(a) ((uint32_t)((native_ram_base[a] << 8) | native_ram_base[(a) + 1]))
#define DIRECT_RAW_RLD32(a) direct_raw_ld32(native_ram_base + (a))
#define DIRECT_RAW_RLD32LE(a) bswap32(direct_raw_ld32(native_ram_base + (a)))
#define DIRECT_RAW_RLDF32(a) direct_raw_ldf32(native_ram_base + (a))
#define DIRECT_RAW_RLDF64(a) direct_raw_ldf64(native_ram_base + (a))
#define DIRECT_RAW_RST8(a, v) ((void)(native_ram_base[a] = (uint8_t)(v)))
#define DIRECT_RAW_RST16(a, v) ((void)(native_ram_base[a] = (uint8_t)((v) >> 8), native_ram_base[(a) + 1] = (uint8_t)(v)))
#define DIRECT_RAW_RSTF32(a, d) direct_raw_stf32(native_ram_base + (a), (d))
#define DIRECT_RAW_RSTF64(a, d) direct_raw_stf64(native_ram_base + (a), (d))
#endif
#define DIRECT_RAW_LOAD(fn, ea_) DIRECT_RAW_##fn(ea_)
#define DIRECT_RAW_STORE(fn, ea_, v_) DIRECT_RAW_##fn(ea_, v_)
#else
#define DIRECT_VERSION_OK(c_) 0
#define DIRECT_RAW_LOAD DIRECT_LOAD
#define DIRECT_RAW_STORE DIRECT_STORE
#endif
#define DIRECT_LOAD(fn, ea_) ({ uint32_t _gla = (ea_); __typeof__(fn(0)) _glv; \
    if (LIKELY(DIRECT_IN_RAM(fn, _gla))) _glv = fn(_gla); \
    else { DBUD_OUT(); if (_gla >= RAM_LIMIT) direct_flush(); _glv = DIRECT_SLOW_LOAD(fn, _gla); DBUD_IN(); } _glv; })
#define DIRECT_STORE(fn, ea_, v_) ({ uint32_t _gsa = (ea_); \
    if (LIKELY(DIRECT_IN_RAM(fn, _gsa))) fn(_gsa, (v_)); \
    else { DBUD_OUT(); if (_gsa >= RAM_LIMIT) direct_flush(); DIRECT_SLOW_STORE(fn, _gsa, (v_)); DBUD_IN(); } })
#endif
'''
SCALAR_FALLBACK = ('VIPER_WII_DRIVING_BULK', 'VIPER_WII_DRIVING_TAIL')


def _resolve_undefined(body, names):
    """Resolve `#ifdef NAME` blocks for NAME in names as if NAME were not
    defined: keep only their #else text. Other conditionals pass through."""
    out, stack = [], []   # stack entries: None (pass-through) or [keeping]
    for line in body.split('\n'):
        t = line.strip()
        m = re.match(r'#ifdef\s+(\w+)$', t)
        if t.startswith('#if'):
            if m and m.group(1) in names and all(e is None or e[0] for e in stack):
                stack.append([False]); continue
            stack.append(None)
        elif t.startswith('#else') and stack and stack[-1] is not None:
            stack[-1][0] = True; continue
        elif t.startswith('#endif') and stack:
            if stack.pop() is not None: continue
        if all(e is None or e[0] for e in stack):
            out.append(line)
    return '\n'.join(out)


STORE_LINE = re.compile(r'^(\s*)DIRECT_ST32\((?:\((c->r\[\d+\]) \+ 0x([0-9a-f]+)u\)|(c->r\[\d+\]))(, .*)\);\s*$')
GROUP_BREAK = re.compile(r'\bL_\w+:|\bgoto\b|\breturn\b|SYNC_|\bCHK\(|DIRECT_ST32LE|DIRECT_FLUSHED|\bf_[a-z]+_[0-9a-f]+\(|rt_\w+\(c[,)]|^\s*#')


def _direct_groups(body):
    """Mark runs of DIRECT_ST32 to consecutive words from one base register
    (no label, jump, call, checkpoint, header store or reassignment of the
    base in between) for DIRECT_GROUP_BEGIN / DIRECT_GST32."""
    lines = body.split('\n')
    groups, cur = [], None
    def close():
        if cur and len(cur['lines']) >= 2:
            groups.append(cur)
    for i, l in enumerate(lines):
        m = STORE_LINE.match(l)
        if m:
            base = m.group(2) or m.group(4)
            off = int(m.group(3), 16) if m.group(3) else 0
            if cur and cur['base'] == base and off == cur['next']:
                cur['lines'].append(i); cur['next'] += 4
                continue
            close()
            cur = {'base': base, 'off': off, 'next': off + 4, 'lines': [i]}
            continue
        if cur and (GROUP_BREAK.search(l) or re.search(re.escape(cur['base']) + r'\s*(?:[-+|&^*/]|<<|>>)?=(?!=)', l)):
            close(); cur = None
    close()
    for g in groups:
        k = len(g['lines'])
        for j in g['lines']:
            lines[j] = lines[j].replace('DIRECT_ST32(', 'DIRECT_GST32(', 1)
        first = g['lines'][0]
        lead = re.match(r'\s*', lines[first]).group(0)
        ea = f"({g['base']} + 0x{g['off']:x}u)"
        lines[first] = f'{lead}DIRECT_GROUP_BEGIN({ea}, {k}u); ' + lines[first].lstrip()
    return '\n'.join(lines)


ACCESS_SIZE = {'LD8': 1, 'LD16': 2, 'LD32': 4, 'LD32LE': 4, 'LDF32': 4, 'LDF64': 8,
               'ST8': 1, 'ST16': 2, 'ST16LE': 2, 'STF32': 4, 'STF64': 8}
VERSION_ACCESS = re.compile(r'DIRECT_(LOAD|STORE)\((\w+), (?:\((?:0 \+ )?(c->r\[\d+\])(?: \+ 0x([0-9a-f]+)u)?\)|(c->r\[\d+\]))')


DIRECT_VERSION_MIN = int(__import__('os').environ.get('VIPER_DIRECT_VERSION_MIN', '8'))


def _direct_version(body, min_accesses=None):
    """VIPER_WII_DIRECT_VERSION: a guest block (label to label) with many
    register-based RAM accesses gets an unchecked copy, taken when one test
    at its top shows every base's whole offset range in RAM and aligned; the
    original block runs otherwise. Accesses after their base is reassigned
    in the block keep their checks. Exact: the unchecked accessors are the
    checked ones' in-RAM paths."""
    lines = body.split('\n')
    starts = [i for i, l in enumerate(lines) if re.match(r'\s*L_\w+:\s*$', l)] + [len(lines)]
    out, prev = [], 0
    versions = 0
    for a, b in zip(starts, starts[1:]):
        out.extend(lines[prev:a + 1])
        prev = a + 1
        block = lines[a + 1:b]
        if any(re.match(r'\s*#', l) for l in block):
            continue
        ranges, assigned, fast, count = {}, set(), [], 0
        for l in block:
            def repl(m):
                nonlocal count
                kind, fn, base = m.group(1), m.group(2), m.group(3) or m.group(5)
                off = int(m.group(4), 16) if m.group(4) else 0
                size = ACCESS_SIZE.get(fn)
                if base in assigned or size is None or off % size:
                    return m.group(0)
                lo, hi, al = ranges.get(base, (off, off + size, size))
                ranges[base] = (min(lo, off), max(hi, off + size), max(al, size))
                count += 1
                return 'DIRECT_RAW_' + kind + '(' + fn + ', ' + m.group(0)[len('DIRECT_' + kind + '(' + fn + ', '):]
            fast.append(VERSION_ACCESS.sub(repl, l))
            for r in re.findall(r'(c->r\[\d+\])\s*(?:[-+|&^*/]|<<|>>)?=(?!=)', l):
                assigned.add(r)
        if count < (min_accesses or DIRECT_VERSION_MIN):
            continue
        cond = ' && '.join(f'!(({base}) & {al - 1}u) && ({base}) <= {0x01000000 - hi}u'
                           for base, (lo, hi, al) in ranges.items())
        out.append(f'    if (LIKELY(DIRECT_VERSION_OK({cond}))) {{')
        out.extend(fast)
        out.append('    } else {')
        out.extend(block)
        out.append('    }')
        prev = b
        versions += 1
    out.extend(lines[prev:])
    return '\n'.join(out)


def _direct(body):
    body = _resolve_undefined(body, SCALAR_FALLBACK)
    if re.search(r'\bwii_voodoo_bulk', body):
        raise ValueError('bulk writer call left after resolving the driving specializations')
    body = re.sub(r'\b(rt_mmio_\w+|store_words|wii_(?!bulk_ram|rsqrt)\w+)\(',
                  r'DIRECT_FLUSHED(\1)(', body)
    body = re.sub(r'\bST32LE\(', 'DIRECT_ST32LE(', body)
    body = re.sub(r'\bST32\(', 'DIRECT_ST32(', body)
    for name in RAM_ACCESSORS:
        if name == 'ST32':
            continue
        kind = 'DIRECT_LOAD' if name.startswith('LD') else 'DIRECT_STORE'
        body = re.sub(r'\b' + name + r'\(', kind + '(' + name + ', ', body)
    return _direct_version(_direct_groups(body))


def _gather(body):
    # Helpers that can reach the device flush first: (gather_flush(), f)(...).
    # Only wii_bulk_ram* (guest RAM) and wii_rsqrt* (pure) are known not to.
    body = re.sub(r'\b(rt_mmio_\w+|store_words|wii_(?!bulk_ram|rsqrt)\w+)\(',
                  r'GATHER_FLUSHED(\1)(', body)
    body = re.sub(r'\bST32LE\(', 'GATHER_ST32LE(', body)
    for name in RAM_ACCESSORS:
        kind = 'GATHER_LOAD' if name.startswith('LD') else 'GATHER_STORE'
        body = re.sub(r'\b' + name + r'\(', kind + '(' + name + ', ', body)
    return body


LOCAL_NAME = r'(?:(?:r|f|cr)_\d+|ctr_|lr_|xer_(?:ca|so|ov|bc)_)'
ASSIGN = re.compile(r'\b(' + LOCAL_NAME + r')\s*(?:(?:[-+|&^*/]|<<|>>)?=(?!=)|\+\+|--)|(?:\+\+|--)\s*(' + LOCAL_NAME + r')\b')
TERMINAL = re.compile(r'^\s*(?:goto L_\w+;|\{ NATIVE_CHK\([^;]*\); goto L_\w+; \}|\{ SYNC_OUT\(\); return; \})\s*$')


def _dirty_sites(local):
    """Per write-back site (each SYNC_OUT() and NATIVE_CHK( in text order),
    the locals that may differ from the context there: a forward dataflow
    over the lines, gotos and fall-through (every line falls through unless
    it is only a goto or a return, so extra edges can only add locals). An
    assignment makes a local dirty; a SYNC_IN() that runs unconditionally
    makes all clean (the context and the locals are then equal); write-backs
    do not clean (they are on the firing or returning paths). A set is the
    union over all paths, so a local left out is equal to the context on
    every path: storing it again would change nothing."""
    lines = local.split('\n')
    labels = {}
    for i, l in enumerate(lines):
        m = re.match(r'\s*(L_\w+):\s*$', l)
        if m:
            labels[m.group(1)] = i
    # Braces opened under if/switch/else are conditional; a SYNC_IN inside
    # one (or after an unbraced if in the same statement) does not clean.
    events = []   # per line: (pos, kind, arg)
    stack = []
    opened = []   # line of each open brace
    skips = {}    # conditional block: its opening line -> its closing line
    site = 0
    for li, l in enumerate(lines):
        ev = []
        for m in re.finditer(r'[{}]|SYNC_OUT\(\)|SYNC_IN\(\)|NATIVE_CHK\(|goto (L_\w+)|' + ASSIGN.pattern, l):
            t = m.group(0)
            if t == '{':
                before = l[:m.start()].rstrip()
                stack.append(before.endswith(')') or before.endswith('else') or before.endswith(':'))
                opened.append(li)
            elif t == '}':
                if not stack:
                    return None
                if stack.pop() and opened[-1] != li:
                    skips.setdefault(opened[-1], []).append(li)
                opened.pop()
            elif t in ('SYNC_OUT()', 'NATIVE_CHK('):
                ev.append(('site', site)); site += 1
            elif t == 'SYNC_IN()':
                stmt = re.split(r'[;{}]', l[:m.start()])[-1]
                if not any(stack) and 'if' not in stmt:
                    ev.append(('clean', None))
            elif t.startswith('goto'):
                ev.append(('goto', m.group(1)))
            else:
                ev.append(('assign', m.group(2) or m.group(3)))
        events.append(ev)
    # Preprocessor conditionals: every branch (and the #endif, for none)
    # also starts from the state before the #if, not only from the end of
    # the branch above it.
    extra = {i: list(js) for i, js in skips.items()}
    cond = []
    for i, l in enumerate(lines):
        t = l.strip()
        if re.match(r'#\s*if', t):
            cond.append(i)
        elif re.match(r'#\s*(elif|else|endif)', t):
            if not cond:
                return None
            extra.setdefault(cond[-1], []).append(i)
            if re.match(r'#\s*endif', t):
                cond.pop()
    if cond:
        return None
    entry = [frozenset() if i == 0 else None for i in range(len(lines))]
    sites = [frozenset()] * site
    work = [0]
    while work:
        i = work.pop()
        cur = entry[i]
        for kind, arg in events[i]:
            if kind == 'assign':
                cur = cur | {arg}
            elif kind == 'clean':
                cur = frozenset()
            elif kind == 'site':
                sites[arg] = sites[arg] | cur
            elif kind == 'goto':
                if arg not in labels:
                    return None
                j = labels[arg]
                if entry[j] is None or not cur <= entry[j]:
                    entry[j] = cur if entry[j] is None else entry[j] | cur
                    work.append(j)
        for j in extra.get(i, ()):
            if entry[j] is None or not cur <= entry[j]:
                entry[j] = cur if entry[j] is None else entry[j] | cur
                work.append(j)
        if not TERMINAL.match(lines[i]) and i + 1 < len(lines):
            j = i + 1
            if entry[j] is None or not cur <= entry[j]:
                entry[j] = cur if entry[j] is None else entry[j] | cur
                work.append(j)
    # Safety net: a write-back or an assignment on a line the flow never
    # reached means the line model missed a path; keep full write-backs.
    # Dead code is fine: an unreached line that only follows a goto or a
    # return, through lines nothing else can enter (no label, case, brace
    # or preprocessor line), is unreachable in C too.
    def dead(i):
        for k in range(i, -1, -1):
            t = lines[k].strip()
            if k < i and entry[k] is not None:
                return bool(TERMINAL.match(lines[k]))
            if re.search(r'(^|\s)(L_\w+|case\b[^:]*|default):|[{}]|^#', t) and not TERMINAL.match(lines[k]):
                return False
            if k < i and TERMINAL.match(lines[k]):
                return True
        return False
    for i, ev in enumerate(events):
        if entry[i] is None and any(k in ('site', 'assign') for k, _ in ev) and not dead(i):
            return None
    return sites


LOCAL_TOKEN = re.compile(r'\b' + LOCAL_NAME + r'\b')


def _live_reloads(local):
    """Per SYNC_IN() in text order, the locals live after it (that may be read
    before being overwritten), for VIPER_WII_LAZY_RELOAD: only those are
    reloaded. Backward liveness over the same line graph as _dirty_sites,
    with the write-back lists (SYNC_OUT_D / NATIVE_CHK_D) counted as reads:
    a local left stale is clean, so no write-back stores it before it is
    assigned again, and the context keeps the callee's value. Only an
    unconditional plain assignment or SYNC_IN kills; None when the text has
    a shape the line model does not cover."""
    lines = local.split('\n')
    labels = {}
    for i, l in enumerate(lines):
        m = re.match(r'\s*(L_\w+):\s*$', l)
        if m:
            labels[m.group(1)] = i
    events, stack, opened, extra, cond = [], [], [], {}, []
    site = 0
    for li, l in enumerate(lines):
        t = l.strip()
        if re.match(r'#\s*if', t):
            cond.append(li)
        elif re.match(r'#\s*(elif|else|endif)', t):
            if not cond:
                return None
            extra.setdefault(cond[-1], []).append(li)
            if re.match(r'#\s*endif', t):
                cond.pop()
        ev = []
        for m in re.finditer(r'[{}]|SYNC_IN\(\)|goto (L_\w+)|' + LOCAL_TOKEN.pattern, l):
            tok = m.group(0)
            if tok == '{':
                before = l[:m.start()].rstrip()
                stack.append(before.endswith(')') or before.endswith('else') or before.endswith(':'))
                opened.append(li)
            elif tok == '}':
                if not stack:
                    return None
                if stack.pop() and opened[-1] != li:
                    extra.setdefault(opened[-1], []).append(li)
                opened.pop()
            elif tok == 'SYNC_IN()':
                stmt = re.split(r'[;{}]', l[:m.start()])[-1]
                ev.append((m.start(), 'reload', site, not any(stack) and 'if' not in stmt))
                site += 1
            elif tok.startswith('goto'):
                ev.append((m.start(), 'goto', m.group(1), None))
            else:
                stmt = re.split(r'[;{}]', l[:m.start()])[-1]
                end = l.find(';', m.end())
                body = l[m.end():end if end >= 0 else len(l)]
                if re.match(r'\s*=(?!=)', body) and not any(stack) and not stmt.strip() and '?' not in body:
                    ev.append((end if end >= 0 else len(l), 'def', tok, None))
                else:
                    ev.append((m.start(), 'use', tok, None))
        ev.sort(key=lambda e: e[0])
        events.append(ev)
    if cond or stack:
        return None
    for ev in events:
        for _, kind, arg, _ in ev:
            if kind == 'goto' and arg not in labels:
                return None
    n = len(lines)
    live_in = [frozenset()] * n
    sites = [frozenset()] * site
    changed = True
    while changed:
        changed = False
        for i in range(n - 1, -1, -1):
            cur = set()
            if not TERMINAL.match(lines[i]) and i + 1 < n:
                cur |= live_in[i + 1]
            for j in extra.get(i, ()):
                cur |= live_in[j]
            for _, kind, arg, flag in reversed(events[i]):
                if kind == 'use':
                    cur.add(arg)
                elif kind == 'def':
                    cur.discard(arg)
                elif kind == 'goto':
                    cur |= live_in[labels[arg]]
                elif kind == 'reload':
                    sites[arg] = sites[arg] | frozenset(cur)
                    if flag:
                        cur = set()
            f = frozenset(cur)
            if f != live_in[i]:
                live_in[i] = f
                changed = True
    return sites


def localize(func, gather=False):
    head, brace, body = func.partition('{')
    name = re.match(r'\s*void\s+(\w+)\(', head).group(1)
    body = body.rstrip()
    if not body.endswith('}'):
        raise ValueError('function text must end with its closing brace')
    body = body[:-1]
    body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
    # Only reviewed helpers may receive the context: guest calls and the
    # context calls are synced, the rewritten helpers operate on the locals,
    # and rt_fctiw/rt_mftb do not touch registers. Anything else (MSR, SPR,
    # system call, rfi, FPSCR, XER, cache helpers) keeps its generated text.
    for helper in set(re.findall(r'\b(\w+)\(c\s*[,)]', body)):
        if not (re.fullmatch(r'f_[a-z]+_[0-9a-f]+', helper) or helper in REVIEWED_CONTEXT_HELPERS):
            raise ValueError('unreviewed context helper ' + helper)
    # stmw/lmw loops index registers by a variable: unroll them to literals.
    body = re.sub(r'for \(int k = (\d+); k < 32; k\+\+, ea \+= 4\) c->r\[k\] = LD32\(ea\);',
                  lambda m: ' '.join(f'c->r[{k}] = LD32(ea); ea += 4;' for k in range(int(m.group(1)), 32)), body)
    body = re.sub(r'for \(int k = (\d+); k < 32; k\+\+, ea \+= 4\) ST32\(ea, c->r\[k\]\);',
                  lambda m: ' '.join(f'ST32(ea, c->r[{k}]); ea += 4;' for k in range(int(m.group(1)), 32)), body)
    if re.search(r'c->(?:r|f|cr)\[[^\d]', body):
        raise ValueError('register indexed by a variable')
    body = _rewrite_helpers(body)
    # Guest fsel as FSEL (runtime/ppc_rt.h): the same expression by default.
    body = re.sub(r'\(\((c->f\[\d+\]) >= 0\.0\) \? (c->f\[\d+\]) : (c->f\[\d+\])\)', r'FSEL(\1, \2, \3)', body)
    # Entry checkpoint first, before the locals are loaded.
    entry = re.match(r'\s*CHK\(c, (0x[0-9a-f]+u), 0\);\n', body)
    if not entry:
        raise ValueError('expected an entry checkpoint')
    body = body[entry.end():]
    body = _wrap_context_calls(body)
    if gather == 'direct':
        body = _direct(body)
    elif gather:
        body = _gather(body)
    body = re.sub(r'\bCHK\(c, (0x[0-9a-f]+u), (\d+)\);', r'NATIVE_CHK(\1, \2);', body)
    # Braced: the generated code also has unbraced `if (...) RETURN(c);`.
    body = re.sub(r'\breturn;', '{ SYNC_OUT(); return; }', body)
    body = re.sub(r'\bRETURN\(c\);', '{ SYNC_OUT(); return; }', body)
    regs = sorted({m.group(0) for m in REG.finditer(body)})
    gprs = sorted({int(m.group(2)) for m in REG.finditer(body) if m.group(1) == 'r'})
    fprs = sorted({int(m.group(2)) for m in REG.finditer(body) if m.group(1) == 'f'})
    crs = sorted({int(m.group(2)) for m in REG.finditer(body) if m.group(1) == 'cr'})
    specials = sorted({m.group(3) for m in REG.finditer(body) if m.group(3)})
    local = REG.sub(_local, body)
    decls = []
    decls += [f'uint32_t r_{n} = c->r[{n}];' for n in gprs]
    decls += [f'double f_{n} = c->f[{n}];' for n in fprs]
    decls += [f'uint8_t cr_{n} = c->cr[{n}];' for n in crs]
    types = {'ctr': 'uint32_t', 'lr': 'uint32_t', 'xer_ca': 'uint8_t', 'xer_so': 'uint8_t',
             'xer_ov': 'uint8_t', 'xer_bc': 'uint8_t'}
    decls += [f'{types[s]} {s}_ = c->{s};' for s in specials]
    if gather == 'direct':
        decls.append('DIRECT_LOCALS();')
    # Only locals the function assigns can differ from the context (reloads
    # after calls make the others equal again), so only they are written back.
    assigned = set(re.findall(r'\b((?:r|f|cr)_\d+|ctr_|lr_|xer_(?:ca|so|ov|bc)_)\s*(?:[-+|&^*/]|<<|>>)?=(?!=)', local))
    assigned |= set(re.findall(r'\b((?:r|f|cr)_\d+|ctr_|lr_|xer_(?:ca|so|ov|bc)_)\s*(?:\+\+|--)', local))
    assigned |= set(re.findall(r'(?:\+\+|--)\s*((?:r|f|cr)_\d+|ctr_|lr_|xer_(?:ca|so|ov|bc)_)\b', local))
    out_ = ' '.join([f'c->r[{n}]=r_{n};' for n in gprs if f'r_{n}' in assigned] +
                    [f'c->f[{n}]=f_{n};' for n in fprs if f'f_{n}' in assigned] +
                    [f'c->cr[{n}]=cr_{n};' for n in crs if f'cr_{n}' in assigned] +
                    [f'c->{s}={s}_;' for s in specials if f'{s}_' in assigned])
    in_ = ' '.join([f'r_{n}=c->r[{n}];' for n in gprs] + [f'f_{n}=c->f[{n}];' for n in fprs] +
                   [f'cr_{n}=c->cr[{n}];' for n in crs] + [f'{s}_=c->{s};' for s in specials])
    # VIPER_WII_DIRTY_SYNC: each write-back stores only the locals that may
    # differ from the context at that site (_dirty_sites).
    order = ([(f'r_{n}', f'c->r[{n}]=r_{n};') for n in gprs] + [(f'f_{n}', f'c->f[{n}]=f_{n};') for n in fprs] +
             [(f'cr_{n}', f'c->cr[{n}]=cr_{n};') for n in crs] + [(f'{s}_', f'c->{s}={s}_;') for s in specials])
    sites = _dirty_sites(local)
    if sites is not None:
        it = iter(sites)
        def site_text(m):
            d = next(it)
            st = ' '.join(t for name, t in order if name in d and name in assigned)
            return 'SYNC_OUT_D(' + st + ')' if m.group(0) == 'SYNC_OUT()' else 'NATIVE_CHK_D(' + st + ', '
        local = re.sub(r'SYNC_OUT\(\)|NATIVE_CHK\(', site_text, local)
        # VIPER_WII_LAZY_RELOAD: each reload after a call loads only the
        # locals live there (_live_reloads); needs the dirty write-backs.
        reloads = _live_reloads(local)
        if reloads is not None:
            load = ([(f'r_{n}', f'r_{n}=c->r[{n}];') for n in gprs] + [(f'f_{n}', f'f_{n}=c->f[{n}];') for n in fprs] +
                    [(f'cr_{n}', f'cr_{n}=c->cr[{n}];') for n in crs] + [(f'{s}_', f'{s}_=c->{s};') for s in specials])
            rit = iter(reloads)
            def reload_text(m):
                live = next(rit)
                return 'SYNC_IN_D(' + ' '.join(t for nm, t in load if nm in live) + ')'
            local = re.sub(r'SYNC_IN\(\)', reload_text, local)
    flush = 'direct_flush(); ' if gather == 'direct' else 'gather_flush(); ' if gather else ''
    # VIPER_WII_DIRECT_BUDGET: in the direct function the budget is a local
    # (DBUD), written back with the registers and wherever device code can
    # run (the direct prelude's slow paths, flush and packet hand-off).
    bud = 'c->budget'
    in_extra = ''
    if gather == 'direct':
        # Everything else that takes the context is a synced call.
        helpers = set(re.findall(r'\b(\w+)\(c[,)]', local))
        other = {h for h in helpers if not re.fullmatch(r'f_[a-z]+_[0-9a-f]+', h)} - {
            'TRACE', 'rt_call', 'rt_hook', 'rt_lswi', 'rt_stswi', 'rt_mftb'}
        if other:
            raise ValueError('direct function reads the budget through ' + ', '.join(sorted(other)))
        # rt_mftb reads the time (the budget) and nothing it changes.
        local = re.sub(r'\brt_mftb\(c, ', 'DIRECT_MFTB(', local)
        local = local.replace('c->budget', 'DBUD')
        bud = 'DBUD'
        flush += 'DBUD_OUT(); '
        in_ += ' DBUD_IN();'
        in_extra = 'DBUD_IN();'
        decls.append('DIRECT_BUDGET_LOCAL();')
    # VIPER_WII_DIRTY_SYNC_CHECK (diagnostic): after a reduced write-back,
    # any local that still differs from the context (bitwise) was wrongly
    # left out; log it, then store everything.
    def differs(name, ctx, typ):
        if typ == 'double':
            return f'memcmp(&{ctx}, &{name}, 8)'
        return f'{ctx} != {name}'
    check = ' || '.join([differs(f'r_{n}', f'c->r[{n}]', 'u') for n in gprs if f'r_{n}' in assigned] +
                        [differs(f'f_{n}', f'c->f[{n}]', 'double') for n in fprs if f'f_{n}' in assigned] +
                        [differs(f'cr_{n}', f'c->cr[{n}]', 'u') for n in crs if f'cr_{n}' in assigned] +
                        [differs(f'{s}_', f'c->{s}', 'u') for s in specials if f'{s}_' in assigned]) or '0'
    prelude = ((DIRECT_PRELUDE if gather == 'direct' else GATHER_PRELUDE if gather else '') +
               f'#define SYNC_OUT() do {{ {flush}{out_} }} while (0)\n'
               f'#define SYNC_IN() do {{ {in_} }} while (0)\n'
               f'#define NATIVE_CHK(pc, n) do {{ if (UNLIKELY(({bud} -= (n)) <= 0)) {{ '
               'SYNC_OUT(); rt_check(c, (pc)); if (c->unwind) return; SYNC_IN(); } } while (0)\n'
               '#if defined(VIPER_WII_DIRTY_SYNC_CHECK)\n'
               f'#define SYNC_DIRTY_CHECK() do {{ if ({check}) {{ void rt_log(const char *, ...); rt_log("VIPER WII DIRTY SYNC MISS {name} line %d\\n", __LINE__); }} }} while (0)\n'
               f'#define SYNC_OUT_D(...) do {{ {flush}__VA_ARGS__ SYNC_DIRTY_CHECK(); {out_} }} while (0)\n'
               f'#define NATIVE_CHK_D(st, pc, n) do {{ if (UNLIKELY(({bud} -= (n)) <= 0)) {{ '
               f'{flush}st SYNC_DIRTY_CHECK(); {out_} rt_check(c, (pc)); if (c->unwind) return; SYNC_IN(); }} }} while (0)\n'
               '#elif defined(VIPER_WII_DIRTY_SYNC)\n'
               f'#define SYNC_OUT_D(...) do {{ {flush}__VA_ARGS__ }} while (0)\n'
               f'#define NATIVE_CHK_D(st, pc, n) do {{ if (UNLIKELY(({bud} -= (n)) <= 0)) {{ '
               f'{flush}st rt_check(c, (pc)); if (c->unwind) return; SYNC_IN(); }} }} while (0)\n'
               '#else\n'
               '#define SYNC_OUT_D(...) SYNC_OUT()\n'
               '#define NATIVE_CHK_D(st, pc, n) NATIVE_CHK(pc, n)\n'
               '#endif\n'
               '#if defined(VIPER_WII_LAZY_RELOAD) && defined(VIPER_WII_DIRTY_SYNC) && !defined(VIPER_WII_DIRTY_SYNC_CHECK)\n'
               f'#define SYNC_IN_D(...) do {{ __VA_ARGS__ {in_extra} }} while (0)\n'
               '#else\n'
               '#define SYNC_IN_D(...) SYNC_IN()\n'
               '#endif\n')
    text = (prelude + head + '{\n    CHK(c, ' + entry.group(1) + ', 0);\n    ' +
            '\n    '.join(decls) + '\n' + local + '}\n'
            '#undef SYNC_OUT\n#undef SYNC_IN\n#undef NATIVE_CHK\n#undef SYNC_OUT_D\n#undef NATIVE_CHK_D\n#undef SYNC_IN_D\n')
    return text


if __name__ == '__main__':
    source = open(sys.argv[1]).read()
    m = re.search(r'\nvoid ' + sys.argv[2] + r'\(PPCContext \*c\) \{.*?\n\}\n', source, re.S)
    print(localize(m.group(0).strip('\n')))
