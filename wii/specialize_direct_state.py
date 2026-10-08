"""Native TMU state frontend for the audited, call-free GL state writer."""
import hashlib
import re

REGION_SHA = '7d16db51e2df086509c16a29b08bc1d89a0691d6bf3c8be2a28582b08dbb3c61'

def regions(source):
    begin = source.index('    /* 000221ac:')
    end = source.index('    /* 00022220:', begin)
    original = source[begin:end]
    if hashlib.sha256(original.encode()).hexdigest() != REGION_SHA:
        raise ValueError('TMU state writer changed; re-audit before lowering')
    stores = list(re.finditer(r'ST32LE\(\(0 \+ c->r\[4\]\), c->r\[3\]\);', original))
    if len(stores) != 7 or re.search(r'\b(?:LD\w*|CHK|TRACE)\(', original):
        raise ValueError('TMU state interval is no longer seven call-free stores')
    candidate = original
    for i, match in reversed(list(enumerate(stores))):
        candidate = candidate[:match.start()] + f'state_values[{i}]=c->r[3];' + candidate[match.end():]
    replacement = """    {
#if defined(VIPER_MEMORY_AUDIT) || defined(RT_TRACE)
        int state_ok=0;
#else
        int state_ok=rt_wii_bulk_lfb_allowed() &&
            c->r[8]==c->r[6]+12u && c->r[4]==c->r[6]+8u &&
            wii_gx_tmu_state_ready(c->r[8]);
#endif
        if(state_ok){
            uint32_t state_ea=c->r[8],state_values[7];
""" + candidate + """            wii_gx_tmu_state_le(state_ea,state_values);
        }else{
""" + original + """        }
    }
"""
    return begin,end,original,replacement

def specialize(source):
    begin,end,original,replacement=regions(source)
    return source[:begin]+'#ifdef VIPER_WII_DIRECT_STATE\n'+replacement+'#else\n'+original+'#endif\n'+source[end:]
