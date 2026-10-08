"""Experimental lowering of the audited driving tail writer; not enabled by builds."""
import hashlib
import re

REGION_SHA = '72228e32145a58bbb42e09b5167b17d38219b56a97038abd10034f1ccc69039d'

# Guest 0x2945c inclusive .. 0x294a8 exclusive. Ten aperture stores are
# r9+0,4,8,12,16,20,24,28,32,36. The only source loads are four lwbrx from
# r12 at 0, r20, r19, r18. r17 is not a source in this window and r9 is not
# written (r11 = r9+0x28 is a read). Proved offsets 0/4/8/12 need 16 bytes.
_SOURCE_LOADS = (
    'LD32LE((0 + c->r[12]));',
    'LD32LE((c->r[20] + c->r[12]));',
    'LD32LE((c->r[19] + c->r[12]));',
    'LD32LE((c->r[18] + c->r[12]));',
)


def regions(source):
    begin = source.index('    /* 0002945c:')
    end = source.index('    /* 000294a8:', begin)
    original = source[begin:end]
    if hashlib.sha256(original.encode()).hexdigest() != REGION_SHA:
        raise ValueError('Driving tail changed; re-audit memory and packet boundaries')
    if re.search(r'c->r\[9\]\s*=', original):
        raise ValueError('Driving tail mutates r9')
    loads = re.findall(r'LD\w*\([^;]*\);', original)
    if loads != list(_SOURCE_LOADS):
        raise ValueError('Driving tail source loads changed')
    if re.search(r'\b(LDF|STF)\w*\(', original):
        raise ValueError('Driving tail contains a float memory op')
    stores = list(re.finditer(r'ST32\((.+), c->r\[(\d+)\]\);', original))
    expected = [7, 6, 5, 4, 3, 23, 7, 6, 5, 4]
    if len(stores) != len(expected):
        raise ValueError('Expected ten driving-tail stores')
    candidate = original
    for i, match in reversed(list(enumerate(stores))):
        address = 'c->r[9]' if i == 0 else f'(c->r[9] + 0x{i*4:x}u)'
        if match[1] != address or int(match[2]) != expected[i]:
            raise ValueError('Driving-tail store layout changed')
        candidate = candidate[:match.start()] + f'driving_words[{i}]=c->r[{match[2]}];' + candidate[match.end():]
    # Interleaved addic./CR/XER, addi, and the four lwbrx stay in order.
    # RAM-only sources (r20=4, r19=8, r18=12, r12+16<=RAM_SIZE) and a ready
    # bulk writer make the ten deferred stores unobservable. No alias or
    # arithmetic assumption beyond those proved offsets.
    replacement = '''    {
#if defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)
        int driving_ok=0;
#else
        int driving_ok=rt_wii_bulk_lfb_allowed() &&
            c->r[20]==4 && c->r[19]==8 && c->r[18]==12 &&
            c->r[12]<=RAM_SIZE-16u &&
            wii_voodoo_bulk_writer_ready(c->r[9],10);
#endif
        if(driving_ok){
            uint32_t driving_words[10];
''' + candidate + '''            wii_voodoo_bulk_writer_be(c->r[9],driving_words,10);
        }else{
''' + original + '''        }
    }
'''
    return begin, end, original, replacement


def specialize(source):
    begin, end, original, replacement = regions(source)
    return source[:begin] + '#ifdef VIPER_WII_DRIVING_TAIL\n' + replacement + '#else\n' + original + '#endif\n' + source[end:]
