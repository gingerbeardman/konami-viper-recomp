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
static inline void gather_flush(void) {
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
    } else { if (_gea >= RAM_LIMIT) gather_flush(); ST32LE(_gea, _gv); } })
#define GATHER_FLUSHED(f) (gather_flush(), f)
#define GATHER_LOAD(fn, ea_) ({ uint32_t _gla = (ea_); if (UNLIKELY(_gla >= RAM_LIMIT)) gather_flush(); fn(_gla); })
#define GATHER_STORE(fn, ea_, v_) ({ uint32_t _gsa = (ea_); if (UNLIKELY(_gsa >= RAM_LIMIT)) gather_flush(); fn(_gsa, (v_)); })
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
static inline void direct_flush(void) {
    if (direct_n) direct_flush_words();
}
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
#define DIRECT_ST32(ea_, v_) ({ uint32_t _gea = (ea_), _gv = (v_); \
    if (_gea - 0x84000000u < 0x02000000u) { \
        if (direct_n && (_gea != direct_ea + 4 * direct_n || direct_n == 150)) direct_flush(); \
        if (!direct_n) direct_ea = _gea; \
        direct_words[direct_n++] = _gv; \
    } else { if (_gea >= RAM_LIMIT) direct_flush(); ST32(_gea, _gv); } })
#define DIRECT_ST32LE(ea_, v_) ({ uint32_t _gea = (ea_), _gv = (v_); \
    if (direct_n && _gea + 4 == direct_ea && \
        wii_voodoo_direct_triangles(_gea, _gv, direct_words, direct_n)) direct_n = 0; \
    else { if (_gea >= RAM_LIMIT) direct_flush(); ST32LE(_gea, _gv); } })
#define DIRECT_FLUSHED(f) (direct_flush(), f)
/* One range test on the common path: below RAM_SIZE - 8 the accessor's own
 * in-RAM test is known true; the flush still happens exactly when ea is in
 * device space (>= RAM_LIMIT). */
#define DIRECT_LOAD(fn, ea_) ({ uint32_t _gla = (ea_); __typeof__(fn(0)) _glv; \
    if (LIKELY(_gla <= RAM_SIZE - 8u)) _glv = fn(_gla); \
    else { if (_gla >= RAM_LIMIT) direct_flush(); _glv = fn(_gla); } _glv; })
#define DIRECT_STORE(fn, ea_, v_) ({ uint32_t _gsa = (ea_); \
    if (LIKELY(_gsa <= RAM_SIZE - 8u)) fn(_gsa, (v_)); \
    else { if (_gsa >= RAM_LIMIT) direct_flush(); fn(_gsa, (v_)); } })
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
    return body


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
    flush = 'direct_flush(); ' if gather == 'direct' else 'gather_flush(); ' if gather else ''
    prelude = ((DIRECT_PRELUDE if gather == 'direct' else GATHER_PRELUDE if gather else '') +
               f'#define SYNC_OUT() do {{ {flush}{out_} }} while (0)\n'
               f'#define SYNC_IN() do {{ {in_} }} while (0)\n'
               '#define NATIVE_CHK(pc, n) do { if (UNLIKELY((c->budget -= (n)) <= 0)) { '
               'SYNC_OUT(); rt_check(c, (pc)); if (c->unwind) return; SYNC_IN(); } } while (0)\n')
    text = (prelude + head + '{\n    CHK(c, ' + entry.group(1) + ', 0);\n    ' +
            '\n    '.join(decls) + '\n' + local + '}\n'
            '#undef SYNC_OUT\n#undef SYNC_IN\n#undef NATIVE_CHK\n')
    return text


if __name__ == '__main__':
    source = open(sys.argv[1]).read()
    m = re.search(r'\nvoid ' + sys.argv[2] + r'\(PPCContext \*c\) \{.*?\n\}\n', source, re.S)
    print(localize(m.group(0).strip('\n')))
