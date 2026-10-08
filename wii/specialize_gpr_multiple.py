"""Lower exact generated lmw/stmw loops to guarded native multiword transfers.

Also opens every generated function body with RAM_BEGIN() and spells its
guest RAM accessors R* (wii/native_ram.h): with VIPER_WII_RAM_BASE_LOCAL
they use a local copy of g_ram and one bounds compare, otherwise they are
the original accessors.

With --localize (VIPER_WII_LOCALIZE_ALL) every generated function that
wii/localize_function.py accepts first gets its guest registers in C locals;
--localize-list FILE (VIPER_WII_LOCALIZE_HOT, wii/localize_hot.txt) limits
that to the named functions.
Functions that mention the media-check entry 0xde28 keep their text."""
import argparse
from pathlib import Path
import re

PATTERN = re.compile(r'\{ uint32_t ea = (.+?); for \(int k = (\d+); k < 32; k\+\+, ea \+= 4\) (ST32\(ea, c->r\[k\]\);|c->r\[k\] = LD32\(ea\);) \}')


LANC_LOOP = """  L_0003e070:
    TRACE(c, 0x3e070u);
"""
LANC_BULK = """#ifdef VIPER_WII_LANC_COPY
    /* k whole iterations of this byte copy (LAN RAM -> guest RAM) at once,
     * with k <= ctr and the budget still positive after each of them, so no
     * checkpoint can fire inside; then the loop's own back-edge test. */
    if (c->budget > 5) {
        int wii_lanc_copy_to_ram(uint32_t dst, uint32_t src, uint32_t n);
        int64_t room = (c->budget - 1) / 5;
        uint32_t k = (int64_t)c->ctr < room ? c->ctr : (uint32_t)room;
        int last = k ? wii_lanc_copy_to_ram(c->r[0] + 0x1u, c->r[10] + 0x1u, k) : -1;
        if (last >= 0) {
            c->budget -= 5 * (int64_t)k;
            c->r[4] = c->r[0] + k - 1u;
            c->r[3] = (uint32_t)last;
            c->r[10] = c->r[10] + k;
            c->xer_ca = (uint8_t)(c->r[4] == 0xffffffffu);
            c->r[0] = c->r[0] + k;
            c->ctr = c->ctr - k;
            if ((c->ctr != 0)) { CHK(c, 0x3e070u, 0); goto L_0003e070; }
            goto L_0003e070_done;
        }
    }
#endif
"""
LANC_TAIL = """    if ((c->ctr != 0)) { CHK(c, 0x3e070u, 0); goto L_0003e070; }
"""


def lanc_copy(source):
    """Bulk path for the link code's LAN RAM byte copy loop (game_000 only)."""
    if LANC_LOOP not in source:
        return source
    assert source.count(LANC_LOOP) == 1 and source.count(LANC_TAIL) == 1
    source = source.replace(LANC_TAIL, LANC_TAIL + '#ifdef VIPER_WII_LANC_COPY\n  L_0003e070_done:\n#endif\n')
    return source.replace(LANC_LOOP, LANC_LOOP + LANC_BULK)


CARRY_BARRIER = re.compile(r'^\s*L_\w+:|\bgoto\b|\bif\b|\bswitch\b|\breturn\b|\bRETURN\b|\bCHK\b|\bTRACE\b|\b\w+\(c\s*[,)]|\bcase\b|\bdefault\b')


FLAG_FIELDS = ['xer_ca'] + ['cr\\[%d\\]' % n for n in range(8)]


def dead_carry(source, fields=FLAG_FIELDS):
    """Drop guest flag writes (carry, and each CR field separately) that the
    next write of the same flag overwrites in straight-line code with no read
    of that flag and no label, branch, return, checkpoint, trace or
    context-taking call between. Exact: the dropped value is never observable."""
    writes_re = {f: re.compile(r'c->' + f + r' = [^;]*;') for f in fields}
    reads_re = {f: re.compile(r'c->' + f + r'(?! = )') for f in fields}
    lines = source.split('\n')
    pending = {}
    inside = False
    removed = 0
    for i, line in enumerate(lines):
        if not inside:
            if HEAD.match(line):
                inside = True
                pending = {}
            continue
        if line == '}' or line.lstrip().startswith('#'):
            inside = line != '}'
            pending = {}
            continue
        body = line.split('/*')[0]
        barrier = bool(CARRY_BARRIER.search(body))
        if barrier:
            pending = {}
        for f in fields:
            if 'c->' not in body:
                break
            reads = reads_re[f].findall(body)
            writes = writes_re[f].findall(body)
            if reads:
                pending.pop(f, None)
            if writes:
                if f in pending:
                    j = pending.pop(f)
                    lines[j] = writes_re[f].sub('', lines[j], count=1)
                    removed += 1
                # A write on a line that also branches, returns or calls may be
                # live wherever control goes next: never a candidate.
                if len(writes) == 1 and not reads and not barrier:
                    pending[f] = i
    return '\n'.join(lines), removed


def display_list_prefetch(source):
    """Store-intent cache touch one record ahead of the display-list write
    pointer (kept at 0x2328) in every emitter; a hint only, compiled only
    with VIPER_WII_CACHE_HINTS (wii/native_ram.h RAM_PREFETCH_W)."""
    return re.sub(r'(\n(\s*)c->r\[(\d+)\] = LD32\(\(0 \+ 0x2328u\)\);)',
                  r'\1\n\2RAM_PREFETCH_W(c->r[\3] + 0x40u);', source)


def localize_all(source, only=None):
    """Localize every multi-line generated function localize() accepts
    (only those named in `only`, when given)."""
    from localize_function import localize
    done = skipped = 0
    def replace(m):
        nonlocal done, skipped
        text = m.group(0).strip('\n')
        if only is not None and re.match(r'void (\w+)', text).group(1) not in only:
            return m.group(0)
        if '0xde28u' in text:
            skipped += 1
            return m.group(0)
        try:
            local = localize(text)
        except ValueError:
            skipped += 1
            return m.group(0)
        done += 1
        return '\n' + local
    out = re.sub(r'\nvoid f_\w+\(PPCContext \*c\) \{\n.*?\n\}\n', replace, source, flags=re.S)
    return out, done, skipped


def specialize(source):
    counts = {'lmw': 0, 'stmw': 0}
    def replace(m):
        first = int(m[2])
        if not 0<=first<32:raise ValueError('Invalid first GPR')
        if not re.fullmatch(r'\(c->r\[\d+\] \+ 0x[0-9a-f]+u\)', m[1]):
            raise ValueError('Unreviewed multiword address expression')
        kind = 'stmw' if m[3].startswith('ST32') else 'lmw'
        counts[kind] += 1
        return ('\n#ifdef VIPER_WII_GPR_MULTIPLE\n'
                f'    wii_gpr_{kind}(c,{m[1]},{first});\n'
                '#else\n    '+m[0]+'\n#endif\n')
    out = PATTERN.sub(replace, source)
    # Reject any matching loop family whose generated syntax has drifted.
    if len(re.findall(r'for \(int k = \d+; k < 32; k\+\+, ea \+= 4\)',source)) != sum(counts.values()):
        raise ValueError('Unparsed generated multiword loop')
    if sum(counts.values()):out='#ifdef VIPER_WII_GPR_MULTIPLE\n#include "gpr_multiple.h"\n#endif\n'+out
    return ram_base(out), counts


HEAD = re.compile(r'void f_\w+\(PPCContext \*c\) \{$')
ACCESSOR = re.compile(r'(?<![A-Za-z0-9_])(LD8|LD16|LD32|LD32LE|LDF32|LDF64|ST8|ST16|ST32|ST32LE|STF32|STF64)(?=\s*[(,)])')


RESTRICT_DEF=re.compile(r'^((?:static )?(?:inline )?void \w+)\(PPCContext \*c\)(\s*\{)',re.M)
def restrict_context(source):
    """Definitions take PPCContext *restrict c: within a generated function the
    context is reached only through c (and callees given c), and guest RAM is
    separate memory, so guest stores need not reload registers, flags or the
    budget. Calls still clobber the context (it escapes), so device handlers
    reached through calls see every pending write. Prototypes are unchanged
    (a parameter qualifier is not part of the function type)."""
    return RESTRICT_DEF.subn(r'\1(PPCContext *restrict c)\2',source)


# Calls that cannot observe the cycle budget: pure arithmetic, CR/XER
# packing, and the R* RAM accessors (whose device path writes it back itself).
BUDGET_PURE = {
    'TRACE', 'RLD8', 'RLD16', 'RLD32', 'RLD32LE', 'RLDF32', 'RLDF64', 'RST8', 'RST16',
    'RST32', 'RST32LE', 'RSTF32', 'RSTF64', 'CMPS', 'CMPU', 'CMPF', 'ROUND_S', 'ROTL',
    'fma', 'fabs', 'CLZ32', 'FPR_BITS', 'BITS_FPR', 'if', 'UNLIKELY', 'LIKELY',
    'RAM_PREFETCH', 'RAM_PREFETCH_W', 'RAM_BEGIN', 'rt_cr_unpack', 'rt_cr_pack',
    'rt_xer_unpack', 'rt_xer_pack', 'rt_divwu', 'rt_divw', 'rt_sraw', 'rt_fctiw',
    'rt_frsqrte', 'defined', 'sizeof', 'while'}
CALL_NAME = re.compile(r'\b([A-Za-z_]\w*)\s*\(')
CHARGE = re.compile(r'^(\s*)c->budget -= (\d+);\s*$')


def budget_local(source):
    """Block charges accumulate in a local; every statement that can observe
    c->budget (anything but BUDGET_PURE calls, a return, or a mention of the
    budget) is preceded by BUDGET_FLUSH(). A statement spread over several
    lines gets the flush at its first line, so every observer sees the same
    value in the same order: exact by construction."""
    out, inside, cont, charges, flushes = [], False, False, 0, 0
    start, flushed, complete = None, False, True
    def flush_at(k):
        line = out[k]
        stripped = line.lstrip()
        out[k] = f'{line[:len(line) - len(stripped)]}BUDGET_FLUSH(); {stripped}'
    for line in source.split('\n'):
        if not inside:
            out.append(line)
            if HEAD.match(line):
                inside = True
                out.append('    int32_t budget_pending = 0;')
                start, flushed, complete = None, False, True
            continue
        if line == '}':
            out.append('    BUDGET_FLUSH();')
            out.append(line)
            inside = False
            continue
        stripped = line.lstrip()
        if cont or stripped.startswith('#') or stripped.startswith('/*') or stripped.startswith('//') or not stripped:
            cont = line.endswith('\\') and (cont or stripped.startswith('#'))
            out.append(line)
            continue
        code = re.sub(r'/\*.*?\*/', '', line).rstrip()
        if complete:
            m = CHARGE.match(line)
            if m:
                out.append(f'{m.group(1)}budget_pending += {m.group(2)};')
                charges += 1
                continue
            start, flushed = len(out), False
        out.append(line)
        names = set(CALL_NAME.findall(code))
        if not flushed and (names - BUDGET_PURE or re.search(r'\breturn\b', code) or 'budget' in code):
            flush_at(start)
            flushed = True
            flushes += 1
        complete = code.endswith((';', '{', '}', ':')) or not code
        if code.endswith(('{', '}')):
            flushed = False
    if inside:
        raise ValueError('unterminated function body')
    return '\n'.join(out), charges, flushes


SWITCH_GOTO = re.compile(r'switch \((c->(?:lr|ctr|r\[\d+\]))\) \{\n((?:[ \t]*case 0x[0-9a-f]+u: goto L_\w+;\n)+)')


def dense_switches(source):
    """A switch whose cases are all goto on values sharing a power-of-two
    stride is switched on rotr(x - base, log2 stride): aligned values map to
    0, 1, 2, ... and every other value (unaligned, or below base) rotates to
    at least 2^(32-shift), above every case, so it matches nothing, as before.
    Dense indices let GCC use a jump table instead of a compare tree."""
    count = 0
    def fix(m):
        nonlocal count
        vals = [int(v, 16) for v in re.findall(r'case (0x[0-9a-f]+)u', m.group(2))]
        if len(vals) < 4:
            return m.group(0)
        base = min(vals)
        g = 0
        for v in vals:
            g |= v - base
        shift = (g & -g).bit_length() - 1 if g else 0
        if shift == 0 or (max(vals) - base) >> shift >= 1 << (32 - shift):
            return m.group(0)
        count += 1
        body = re.sub(r'case (0x[0-9a-f]+)u:',
                      lambda c: f'case {(int(c.group(1), 16) - base) >> shift}u:', m.group(2))
        k = f'({m.group(1)} - 0x{base:x}u)'
        return f'switch (({k} >> {shift}) | ({k} << {32 - shift})) {{\n{body}'
    return SWITCH_GOTO.sub(fix, source), count


def ram_base(source):
    """RAM_BEGIN() and R* accessors inside every multi-line function body."""
    out, inside, functions = [], False, 0
    for line in source.split('\n'):
        if inside:
            if line == '}':
                inside = False
            elif not line.lstrip().startswith('#'):
                line = ACCESSOR.sub(r'R\1', line)
            out.append(line)
            continue
        out.append(line)
        if HEAD.match(line):
            inside = True
            functions += 1
            out.append('    RAM_BEGIN();')
    if inside:
        raise ValueError('unterminated function body')
    return '#include "native_ram.h"\n' + '\n'.join(out)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--localize',action='store_true');p.add_argument('--localize-list',type=Path,action='append');p.add_argument('--lanc-copy',action='store_true');p.add_argument('--dead-carry',action='store_true');p.add_argument('--restrict-ctx',action='store_true');p.add_argument('--budget-local',action='store_true');p.add_argument('--dense-switch',action='store_true');p.add_argument('source',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    text=display_list_prefetch(a.source.read_text())
    if a.lanc_copy:
        text=lanc_copy(text)
    if a.dead_carry:
        text,removed=dead_carry(text);print('dead flag writes removed',removed)
    if a.localize or a.localize_list:
        import sys;sys.path.insert(0,str(Path(__file__).resolve().parent))
        only=None
        if a.localize_list:
            only={l.strip() for f in a.localize_list for l in f.read_text().splitlines() if l.strip() and not l.startswith('#')}
        text,done,skipped=localize_all(text,only);print('localized',done,'kept',skipped)
    out,counts=specialize(text)
    if a.dense_switch:
        out,n=dense_switches(out);print('dense switches',n)
    if a.budget_local:
        out,ch,fl=budget_local(out);print('budget local charges',ch,'flushes',fl)
    if a.restrict_ctx:
        out,n=restrict_context(out);print('restrict context',n)
    a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(out);print(counts)
