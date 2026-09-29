"""PowerPC 603e (32-bit, big-endian) instruction decoder for static recompilation.

Only the user + supervisor subset that a 603e implements is decoded; anything else
decodes as None (treated as data / invalid by the analyser).
"""
import struct


def sext16(v):
    return v - 0x10000 if v & 0x8000 else v


def sext(v, bits):
    m = 1 << (bits - 1)
    return (v ^ m) - m


# 10-bit extended opcodes of primary 31 (X / XO forms) that we accept
OP31 = {
    0: 'cmp', 4: 'tw', 8: 'subfc', 10: 'addc', 11: 'mulhwu', 19: 'mfcr', 20: 'lwarx',
    23: 'lwzx', 24: 'slw', 26: 'cntlzw', 28: 'and', 32: 'cmpl', 40: 'subf', 54: 'dcbst',
    55: 'lwzux', 60: 'andc', 75: 'mulhw', 83: 'mfmsr', 86: 'dcbf', 87: 'lbzx', 104: 'neg',
    119: 'lbzux', 124: 'nor', 136: 'subfe', 138: 'adde', 144: 'mtcrf', 146: 'mtmsr',
    150: 'stwcx.', 151: 'stwx', 183: 'stwux', 200: 'subfze', 202: 'addze', 210: 'mtsr',
    215: 'stbx', 232: 'subfme', 234: 'addme', 235: 'mullw', 242: 'mtsrin', 246: 'dcbtst',
    247: 'stbux', 266: 'add', 278: 'dcbt', 279: 'lhzx', 284: 'eqv', 306: 'tlbie',
    310: 'eciwx', 311: 'lhzux', 316: 'xor', 339: 'mfspr', 343: 'lhax', 370: 'tlbia',
    371: 'mftb', 375: 'lhaux', 407: 'sthx', 412: 'orc', 438: 'ecowx', 439: 'sthux',
    444: 'or', 459: 'divwu', 467: 'mtspr', 470: 'dcbi', 476: 'nand', 491: 'divw',
    512: 'mcrxr', 533: 'lswx', 534: 'lwbrx', 535: 'lfsx', 536: 'srw', 566: 'tlbsync',
    567: 'lfsux', 595: 'mfsr', 597: 'lswi', 598: 'sync', 599: 'lfdx', 631: 'lfdux',
    659: 'mfsrin', 661: 'stswx', 662: 'stwbrx', 663: 'stfsx', 695: 'stfsux', 725: 'stswi',
    727: 'stfdx', 759: 'stfdux', 790: 'lhbrx', 792: 'sraw', 824: 'srawi', 854: 'eieio',
    918: 'sthbrx', 922: 'extsh', 954: 'extsb', 978: 'tlbld', 982: 'icbi', 983: 'stfiwx',
    1010: 'tlbli', 1014: 'dcbz',
}
# XO-form ops (9-bit xo, OE at bit 10)
XO9 = {8, 10, 40, 104, 136, 138, 200, 202, 232, 234, 235, 266, 459, 491, 11, 75}

OP19 = {0: 'mcrf', 16: 'bclr', 33: 'crnor', 50: 'rfi', 129: 'crandc', 150: 'isync',
        193: 'crxor', 225: 'crnand', 257: 'crand', 289: 'creqv', 417: 'crorc', 449: 'cror',
        528: 'bcctr'}

OP59 = {18: 'fdivs', 20: 'fsubs', 21: 'fadds', 22: 'fsqrts', 24: 'fres', 25: 'fmuls',
        28: 'fmsubs', 29: 'fmadds', 30: 'fnmsubs', 31: 'fnmadds'}
OP63A = {18: 'fdiv', 20: 'fsub', 21: 'fadd', 22: 'fsqrt', 23: 'fsel', 25: 'fmul',
         26: 'frsqrte', 28: 'fmsub', 29: 'fmadd', 30: 'fnmsub', 31: 'fnmadd'}
OP63X = {0: 'fcmpu', 12: 'frsp', 14: 'fctiw', 15: 'fctiwz', 32: 'fcmpo', 38: 'mtfsb1',
         40: 'fneg', 64: 'mcrfs', 70: 'mtfsb0', 72: 'fmr', 134: 'mtfsfi', 136: 'fnabs',
         264: 'fabs', 583: 'mffs', 711: 'mtfsf'}

PRIMARY = {
    3: 'twi', 7: 'mulli', 8: 'subfic', 10: 'cmpli', 11: 'cmpi', 12: 'addic', 13: 'addic.',
    14: 'addi', 15: 'addis', 16: 'bc', 17: 'sc', 18: 'b', 20: 'rlwimi', 21: 'rlwinm',
    23: 'rlwnm', 24: 'ori', 25: 'oris', 26: 'xori', 27: 'xoris', 28: 'andi.', 29: 'andis.',
    32: 'lwz', 33: 'lwzu', 34: 'lbz', 35: 'lbzu', 36: 'stw', 37: 'stwu', 38: 'stb',
    39: 'stbu', 40: 'lhz', 41: 'lhzu', 42: 'lha', 43: 'lhau', 44: 'sth', 45: 'sthu',
    46: 'lmw', 47: 'stmw', 48: 'lfs', 49: 'lfsu', 50: 'lfd', 51: 'lfdu', 52: 'stfs',
    53: 'stfsu', 54: 'stfd', 55: 'stfdu',
}


class Insn:
    __slots__ = ('addr', 'word', 'op', 'name', 'rd', 'ra', 'rb', 'rc', 'imm', 'uimm',
                 'xo', 'oe', 'lk', 'aa', 'bo', 'bi', 'target', 'sh', 'mb', 'me', 'spr',
                 'crm', 'frc', 'fm')

    def __init__(self, addr, word):
        self.addr = addr
        self.word = word
        self.op = word >> 26
        self.rd = (word >> 21) & 31
        self.ra = (word >> 16) & 31
        self.rb = (word >> 11) & 31
        self.frc = (word >> 6) & 31
        self.rc = word & 1
        self.imm = sext16(word & 0xffff)
        self.uimm = word & 0xffff
        self.xo = (word >> 1) & 0x3ff
        self.oe = (word >> 10) & 1
        self.lk = word & 1
        self.aa = (word >> 1) & 1
        self.bo = self.rd
        self.bi = self.ra
        self.sh = self.rb
        self.mb = (word >> 6) & 31
        self.me = (word >> 1) & 31
        self.spr = ((word >> 16) & 31) | (((word >> 11) & 31) << 5)
        self.crm = (word >> 12) & 0xff
        self.fm = (word >> 17) & 0xff
        self.target = None
        self.name = None

    def __repr__(self):
        return f"<{self.addr:08x} {self.word:08x} {self.name}>"

    # --- control-flow classification -------------------------------------------------
    @property
    def is_branch(self):
        return self.name in ('b', 'bc', 'bclr', 'bcctr')

    @property
    def unconditional(self):
        """For bc/bclr/bcctr: BO says 'branch always'."""
        if self.name == 'b':
            return True
        return (self.bo & 0x14) == 0x14


def decode(addr, word):
    i = Insn(addr, word)
    op = i.op
    name = None
    if op in PRIMARY:
        name = PRIMARY[op]
        if op == 18:
            li = sext(word & 0x03fffffc, 26)
            i.target = (li if i.aa else addr + li) & 0xffffffff
        elif op == 16:
            bd = sext(word & 0xfffc, 16)
            i.target = (bd if i.aa else addr + bd) & 0xffffffff
        elif op == 17:
            if (word & 0x03ffffff) != 0x00000002:
                return None
        elif op in (10, 11):
            if word & 0x00600000:  # L bit must be 0 on 32-bit, bit 22 reserved
                return None
    elif op == 19:
        name = OP19.get(i.xo)
        if name in ('bclr', 'bcctr'):
            if name == 'bcctr' and not (i.bo & 4):
                return None  # bcctr with decrementing CTR is invalid
        elif name == 'rfi' and word != 0x4c000064:
            return None
        elif name == 'isync' and word != 0x4c00012c:
            return None
    elif op == 31:
        xo = i.xo
        x9 = xo & 0x1ff
        # XO-form: OE lives in bit 10; mulhw/mulhwu have no OE (bit must be 0)
        if x9 in XO9 and not (x9 in (11, 75) and xo & 0x200):
            name = OP31.get(x9)
            i.xo = x9
        else:
            name = OP31.get(xo)
            i.oe = 0
        if name is None:
            return None
        if name in ('cmp', 'cmpl') and word & 0x00600001:
            return None
    elif op == 59:
        name = OP59.get((word >> 1) & 31)
    elif op == 63:
        a = (word >> 1) & 31
        if a in OP63A and a >= 18:
            name = OP63A[a]
        else:
            name = OP63X.get(i.xo)
    if name is None:
        return None
    i.name = name
    return i


def decode_buf(buf, base, addr):
    off = addr - base
    if off < 0 or off + 4 > len(buf):
        return None
    return decode(addr, struct.unpack_from('>I', buf, off)[0])
