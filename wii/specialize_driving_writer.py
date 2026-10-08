"""Experimental lowering of the audited second vertex writer; not enabled by builds."""
import hashlib
import re

REGION_SHA = 'fbcf797b4d25da6b35f63c5205dfec2c6086b8e697ae8e04d20be7d5cf783c5a'


def regions(source):
    begin = source.index('    /* 00029404:')
    end = source.index('    /* 0002943c:', begin)
    original = source[begin:end]
    if hashlib.sha256(original.encode()).hexdigest() != REGION_SHA:
        raise ValueError('Second writer changed; re-audit memory and packet boundaries')
    stores = list(re.finditer(r'ST32\((.+), c->r\[(\d+)\]\);', original))
    expected = [7, 0, 5, 4, 3, 23, 7, 5, 4, 3]
    if len(stores) != len(expected):
        raise ValueError('Expected ten second-writer stores')
    candidate = original
    for i, match in reversed(list(enumerate(stores))):
        address = 'c->r[9]' if i == 0 else f'(c->r[9] + 0x{i*4:x}u)'
        if match[1] != address or int(match[2]) != expected[i]:
            raise ValueError('Second-writer store layout changed')
        candidate = candidate[:match.start()] + f'driving_words[{i}]=c->r[{match[2]}];' + candidate[match.end():]
    # The four intervening loads stay in original order through original
    # helpers. RAM-only source and an absent FIFO header make deferred device
    # writes unobservable. No CPU/context alias or arithmetic assumption.
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
    return source[:begin] + '#ifdef VIPER_WII_DRIVING_BULK\n' + replacement + '#else\n' + original + '#endif\n' + source[end:]
