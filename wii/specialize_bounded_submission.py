"""Remove general RAM dispatch only in the audited vertex entry block."""
from pathlib import Path
import hashlib
import re

ENTRY_SHA = '501da9034cc2854f900cd4d1199a8cf843706bbad7ee609af1417b983b31077b'


def regions(source):
    begin = source.index('    /* 0002adac:')
    end = source.index('  L_0002af08:', begin)
    original = source[begin:end]
    if hashlib.sha256(original.encode()).hexdigest() != ENTRY_SHA:
        raise ValueError('Vertex entry changed: re-audit bounds and scheduling')
    names = {'LD8': 'wii_submission_ram8', 'LD32': 'wii_submission_ram32',
             'LD32LE': 'wii_submission_ram32le', 'LDF32': 'wii_submission_ramf32'}
    candidate = re.sub(r'\b(LD8|LD32|LD32LE|LDF32)\(',
                       lambda m: names[m[1]] + '(', original)
    if sum(candidate.count(name + '(') for name in names.values()) != 39:
        raise ValueError('Expected 39 audited RAM loads')
    replacement = '''    {
#if defined(VIPER_WII_BOUNDED_SUBMISSION) && !defined(VIPER_MEMORY_AUDIT) && !defined(RT_TRACE)
    if(RAM_SIZE>=0x5400u && !((uintptr_t)g_ram&3u)){
''' + candidate + '''    }else
#endif
    {
''' + original + '''    }
    }
'''
    return begin, end, original, replacement


def specialize(source):
    begin, end, _, replacement = regions(source)
    return '#include "bounded_submission.h"\n' + source[:begin] + replacement + source[end:]
