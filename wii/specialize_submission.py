"""Guarded bulk lowering of the audited hot writer, preserving input source."""
import argparse
import hashlib
from pathlib import Path
import re

LOCALIZED_GL = ('f_gl_00028fc4', 'f_gl_000248b0', 'f_gl_00029d28', 'f_gl_000281ac',
                'f_gl_00021940', 'f_gl_00021ed0', 'f_gl_00029b28', 'f_gl_00022314', 'f_gl_000211d4')
# Localized gl functions whose FIFO stores are gathered into bulk writes
# (wii/localize_function.py gather=True) under VIPER_WII_GATHER_GL.
# Further hot gl functions (physical-Wii profile P), under VIPER_WII_LOCALIZE_GL_MORE.
LOCALIZED_GL_MORE = ('f_gl_00024d28', 'f_gl_0002a004', 'f_gl_0002132c', 'f_gl_00022340',
                     'f_gl_00022e18', 'f_gl_00027668', 'f_gl_00028158')
GATHER_GL = ('f_gl_00029d28', 'f_gl_00028fc4', 'f_gl_00021ed0')
# Header-last producers whose FIFO words are gathered and completed as direct
# triangle packets (gather='direct') under VIPER_WII_DIRECT_GL.
DIRECT_GL = ('f_gl_00028fc4',)
REGION_SHA = '7dcc19acfe4af507936feb9dd9fa4a9719a4026e597c864365ee2791ce7d36ad'


STAGING_CHECK = ('c->r[15]==0x3398u && c->r[14]==0x33acu &&\n'
                 '        c->r[19]==4 && c->r[18]==8 && c->r[17]==12 && c->r[16]==16 &&')
STAGING_OR = ('(((c->r[15]^0x3398u)|(c->r[14]^0x33acu)|\n'
              '          (c->r[19]^4u)|(c->r[18]^8u)|(c->r[17]^12u)|(c->r[16]^16u))==0) &&')

def staging_guard(replacement, site):
    if replacement.count(STAGING_CHECK)!=1:
        raise ValueError('Expected one exact staging guard')
    replacement = replacement.replace(STAGING_CHECK,
        '#ifdef VIPER_WII_STAGING_GUARD_OR\n        '+STAGING_OR+
        '\n#else\n        '+STAGING_CHECK+'\n#endif',1)
    reason = ('!rt_wii_bulk_lfb_allowed()?1:!'+STAGING_OR.removesuffix(' &&')+'?2:'
              + ('c->r[31]>RAM_SIZE-0x20u?3:' if site==0 else '')+'4')
    diagnostic = ('#ifdef VIPER_WII_BULK_ADMISSION_DIAGNOSTIC\n'
        '    extern void wii_bulk_admission_observe(unsigned,unsigned);\n'
        '#if defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)\n'
        f'    wii_bulk_admission_observe({site},5);\n'
        '#else\n'
        f'    wii_bulk_admission_observe({site},bulk_ok?0:({reason}));\n'
        '#endif\n#endif\n')
    return replacement.replace('    if(bulk_ok){',diagnostic+'    if(bulk_ok){',1)


def regions(source):
    begin = source.index('    /* 0002af88:')
    end = source.index('    /* 0002b04c:', begin)
    original = source[begin:end]
    if hashlib.sha256(original.encode()).hexdigest() != REGION_SHA:
        raise ValueError('Hot writer changed; re-audit scheduling, memory and instruction order')
    stores = list(re.finditer(r'ST32\((.+), c->r\[(\d+)\]\);', original))
    if len(stores) != 10:
        raise ValueError('Expected ten word stores')
    expected_regs = [12, 11, 10, 9, 8, 13, 11, 10, 9, 8]
    candidate = original
    for i, match in reversed(list(enumerate(stores))):
        expected_ea = 'c->r[25]' if i == 0 else f'(c->r[25] + 0x{i*4:x}u)'
        if match[1] != expected_ea or int(match[2]) != expected_regs[i]:
            raise ValueError('Store layout changed')
        candidate = candidate[:match.start()] + f'bulk_words[{i}]=c->r[{match[2]}];' + candidate[match.end():]
    ram_names={'LD32LE':'wii_bulk_ram32le','LDF32':'wii_bulk_ramf32',
               'STF32':'wii_bulk_ramstf32'}
    ram_candidate=re.sub(r'\b(LD32LE|LDF32|STF32)\(',
                         lambda m: ram_names[m[1]]+'(',candidate)
    if sum(ram_candidate.count(n+'(') for n in ram_names.values()) != 13:
        raise ValueError('Expected thirteen guarded bulk RAM accesses')
    candidate='#if defined(VIPER_WII_BULK_RAM) && !defined(VIPER_PAGED_MEMORY)\n'+ram_candidate+'#else\n'+candidate+'#endif\n'
    # Fixed staging pointers and RAM-only source prove that no intervening load
    # observes deferred graphics-aperture stores. The hashed region has no
    # guest call, checkpoint or control-flow edge. The helper's absent-header
    # guard proves that scalar stores cannot execute a packet in this interval.
    # VIPER_WII_BULK_NOALIAS is a separate fast path. Difference checks prove
    # the whole PPCContext and the whole RAM_SIZE backing are disjoint and do
    # not wrap. Failure runs the original hashed fragment: context may alias
    # RAM, so the deferred bulk candidate must not run. restrict covers only
    # the pure hashed body; the device publisher stays outside that scope.
    noalias_body=ram_candidate.replace('c->','bulk_context->')
    if noalias_body.replace('bulk_context->','c->')!=ram_candidate:
        raise ValueError('Restrict rewrite changed more than context accesses')
    scope='PPCContext *restrict bulk_context=c;\n'+noalias_body
    if re.findall(r'(?<![A-Za-z0-9_])c(?![A-Za-z0-9_])', scope)!=['c']:
        raise ValueError('Restrict scope keeps a c reference other than bulk_context=c')
    if (noalias_body.count('fma(')!=ram_candidate.count('fma(') or
            noalias_body.count('rt_frsqrte(')!=ram_candidate.count('rt_frsqrte(')):
        raise ValueError('Restrict rewrite touched fma or rt_frsqrte')
    publisher='''        wii_voodoo_bulk_writer_be(c->r[25],bulk_words,10);
'''
    legacy_fast=candidate + publisher
    replacement = '''    {
    #if defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)
    int bulk_ok=0;
    #else
    int bulk_ok=rt_wii_bulk_lfb_allowed() &&
        c->r[15]==0x3398u && c->r[14]==0x33acu &&
        c->r[19]==4 && c->r[18]==8 && c->r[17]==12 && c->r[16]==16 &&
        c->r[31]<=RAM_SIZE-0x20u &&
        wii_voodoo_bulk_writer_ready(c->r[25],10);
    #endif
    if(bulk_ok){
        uint32_t bulk_words[10];
#if defined(VIPER_WII_BULK_NOALIAS)
#if !defined(VIPER_WII_BULK_RAM) || defined(VIPER_PAGED_MEMORY) || defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)
#error VIPER_WII_BULK_NOALIAS requires VIPER_WII_BULK_RAM without VIPER_PAGED_MEMORY, VIPER_MEMORY_AUDIT, or RT_TRACE
#else
        uintptr_t bulk_ctx_addr=(uintptr_t)c;
        uintptr_t bulk_ram_addr=(uintptr_t)g_ram;
        int bulk_spans_disjoint=0;
        if((uintptr_t)sizeof(PPCContext)<=(UINTPTR_MAX-bulk_ctx_addr) &&
           (uintptr_t)RAM_SIZE<=(UINTPTR_MAX-bulk_ram_addr)){
            if(bulk_ctx_addr<=bulk_ram_addr)
                bulk_spans_disjoint=(uintptr_t)sizeof(PPCContext)<=(bulk_ram_addr-bulk_ctx_addr);
            else
                bulk_spans_disjoint=(uintptr_t)RAM_SIZE<=(bulk_ctx_addr-bulk_ram_addr);
        }
        if(bulk_spans_disjoint){
            {
            PPCContext *restrict bulk_context=c;
''' + noalias_body + '''            }
''' + publisher + '''        }else{
''' + original + '''        }
#endif
#else
''' + legacy_fast + '''#endif
    }else{
''' + original + '''    }
    }
'''
    return begin, end, original, staging_guard(replacement,0)


def specialize(source, dead_flags=False):
    from specialize_direct_state import specialize as state_specialize
    source = state_specialize(source)
    from specialize_driving_writer import specialize as driving_specialize
    source = driving_specialize(source)
    from specialize_driving_tail import specialize as driving_tail_specialize
    source = driving_tail_specialize(source)
    from specialize_bounded_submission import specialize as bounded_specialize
    begin, end, _, replacement = regions(source)
    source = '#include "voodoo_headless.h"\n#include "bulk_ram.h"\n' + source[:begin] + replacement + source[end:]
    from specialize_gpr_multiple import specialize as gpr_multiple_specialize
    result = bounded_specialize(source)
    # Native draw routine (wii/native_gl_draw.c); the generated body stays
    # compiled under another name as the reference it was transcribed from.
    head = 'void f_gl_0002adac(PPCContext *c) {'
    if result.count(head) != 1:
        raise ValueError('Expected one f_gl_0002adac definition')
    result = result.replace(head, '''#ifdef VIPER_WII_NATIVE_GL_DRAW
void wii_native_gl_0002adac(PPCContext *c);
void f_gl_0002adac(PPCContext *c) { wii_native_gl_0002adac(c); }
void f_gl_0002adac_generated(PPCContext *c);
void f_gl_0002adac_generated(PPCContext *c) {
#else
void f_gl_0002adac(PPCContext *c) {
#endif''')
    # The gl interpreter 0x210a8 repeats the same 80-case dispatch on LR after
    # each of its 51 handler calls: share one copy (same switch, same targets,
    # same default return), shrinking 220 KB of code that thrashes the caches.
    m = re.search(r'\nvoid f_gl_000210a8\(PPCContext \*c\) \{.*?\n\}\n', result, re.S)
    if not m:
        raise ValueError('Expected f_gl_000210a8')
    body = m.group(0)
    copies = set(re.findall(r'switch \(c->lr\) \{ (?:case 0x[0-9a-f]+u: goto L_[0-9a-f]+; )+default: return; \}', body))
    if len(copies) != 1:
        raise ValueError('Expected one repeated LR dispatch in f_gl_000210a8')
    dispatch = copies.pop()
    shared = body.replace(dispatch, 'goto L_lr_dispatch;')
    assert shared.endswith('\n}\n')
    shared = shared[:-2] + '  L_lr_dispatch:\n    ' + dispatch + '\n}\n'
    result = result.replace(body, shared, 1)
    # Vertex prefetch at the 0x28fc4 strip loops (32-byte records at r31; a
    # cache hint only, VIPER_WII_CACHE_HINTS), before localization.
    # The interpreter's command dispatch walks the display list at r31.
    label = '  L_00021080:\n'
    if result.count(label) != 1:
        raise ValueError('Expected one L_00021080')
    result = result.replace(label, label + '    RAM_PREFETCH(c->r[31] + 0x40u);\n')
    for label in ('  L_000290d0:\n', '  L_000294d0:\n'):
        if result.count(label) != 1:
            raise ValueError('Expected one ' + label.strip())
        result = result.replace(label, label + '    RAM_PREFETCH(c->r[31] + VIPER_WII_STRIP_PREFETCH);\n')
    # Localized guest registers (wii/localize_function.py) for the other hot
    # gl functions; the generated text stays as the #else branch.
    from localize_function import localize
    for name in LOCALIZED_GL:
        m = re.search(r'\nvoid ' + name + r'\(PPCContext \*c\) \{.*?\n\}\n', result, re.S)
        if not m:
            raise ValueError('Expected one ' + name + ' definition')
        text = m.group(0).strip('\n')
        local = localize(text)
        if name in DIRECT_GL:
            local = ('#ifdef VIPER_WII_DIRECT_GL\n' + localize(text, gather='direct') +
                     '#else\n' + local + '#endif\n')
        if name in GATHER_GL:
            local = ('#ifdef VIPER_WII_GATHER_GL\n' + localize(text, gather=True) +
                     '#else\n' + local + '#endif\n')
        result = result.replace(text, '#ifdef VIPER_WII_LOCALIZE_GL\n' + local +
                                '#else\n' + text + '\n#endif', 1)
    for name in LOCALIZED_GL_MORE:
        m = re.search(r'\nvoid ' + name + r'\(PPCContext \*c\) \{.*?\n\}\n', result, re.S)
        if not m:
            raise ValueError('Expected one ' + name + ' definition')
        text = m.group(0).strip('\n')
        try:
            local = localize(text)
        except ValueError as e:
            print('localize-more skipped', name, e)
            continue
        result = result.replace(text, '#ifdef VIPER_WII_LOCALIZE_GL_MORE\n' + local +
                                '#else\n' + text + '\n#endif', 1)
    # Native list draw (wii/native_gl_list.c): every variant of the generated
    # body stays compiled under another name, used for out-of-RAM frames.
    head = 'void f_gl_00029d28(PPCContext *c) {'
    if head not in result:
        raise ValueError('Expected f_gl_00029d28 definitions')
    result = (result.replace(head, '#ifdef VIPER_WII_NATIVE_GL_LIST\n'
                             'void f_gl_00029d28_generated(PPCContext *c);\n'
                             'void f_gl_00029d28_generated(PPCContext *c) {\n#else\n' +
                             head + '\n#endif') +
              '\n#ifdef VIPER_WII_NATIVE_GL_LIST\n'
              'void wii_native_gl_00029d28(PPCContext *c);\n'
              'void f_gl_00029d28(PPCContext *c) { wii_native_gl_00029d28(c); }\n'
              '#endif\n')
    if dead_flags:
        # After every exact-text specialization: dead guest flag writes in the
        # remaining context-register code (interpreter and other gl functions).
        from specialize_gpr_multiple import dead_carry
        result = dead_carry(result)[0]
    return gpr_multiple_specialize(result)[0]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--dead-carry', action='store_true')
    parser.add_argument('--restrict-ctx', action='store_true')
    parser.add_argument('--budget-local', action='store_true')
    parser.add_argument('--dense-switch', action='store_true')
    parser.add_argument('--dense-lr', action='store_true')
    args = parser.parse_args()
    result = specialize(args.source.read_text(), args.dead_carry)
    if args.dense_lr:
        # The interpreter's shared LR dispatch (76 return points 32 bytes
        # apart): switch on rotr(lr - base, 5) so GCC emits a jump table
        # instead of a compare tree; any other LR (unaligned or below base)
        # rotates above every case and still takes the default return.
        m = re.search(r'switch \(c->lr\) \{ ((?:case 0x[0-9a-f]+u: goto L_[0-9a-f]+; )+)default: return; \}', result)
        if not m or result.count(m.group(0)) != 1:
            raise ValueError('Expected one shared LR dispatch')
        cases = [(int(v, 16), l) for v, l in re.findall(r'case (0x[0-9a-f]+)u: goto (L_[0-9a-f]+);', m.group(1))]
        base = min(v for v, _ in cases)
        if any((v - base) % 32 for v, _ in cases):
            raise ValueError('LR dispatch is not 32-byte strided')
        k = f'(c->lr - 0x{base:x}u)'
        table = ' '.join(f'case {(v - base) >> 5}u: goto {l};' for v, l in cases)
        result = result.replace(m.group(0), f'switch (({k} >> 5) | ({k} << 27)) {{ {table} default: return; }}')
        print('dense LR dispatch', len(cases))
    if args.dense_switch:
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from specialize_gpr_multiple import dense_switches
        result, n = dense_switches(result)
        print('dense switches', n)
    if args.budget_local:
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from specialize_gpr_multiple import budget_local
        result, charges, flushes = budget_local(result)
        print('budget local charges', charges, 'flushes', flushes)
    if args.restrict_ctx:
        import sys
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from specialize_gpr_multiple import restrict_context
        result, n = restrict_context(result)
        print('restrict context', n)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(result)
    print('Guarded hot-writer bulk specialization: ten scalar stores, source proof hash verified')
