"""Whole-program differential proof for VIPER_WII_LOCALIZE_ALL with the RAM
base pass: a seeded random sample of generated functions from every module,
localized (wii/localize_function.py) and given the RAM base pass
(wii/specialize_gpr_multiple.py ram_base, compiled with
VIPER_WII_RAM_BASE_LOCAL) exactly as the live build does, against the
generated originals, from identical random contexts and RAM.

Every generated module is linked as the reference (callees and the rt_call
tables are the real generated code), with a fake runtime whose
privileged helpers (MSR, SPR, system call, rfi, FPSCR, XER, dcbz) are
deterministic and logged. Compared after every case: the full PPCContext,
all 16 MiB of RAM and the ordered log of MMIO accesses, helper calls,
rt_call/rt_hook targets and firing checkpoints. A checkpoint limit sets
unwind, so runaway random states end the same way on both sides.
Both sides are compiled at -O0 by default (TEST_OPT overrides): with
optimization the host compiler may fuse or not fuse a negation into an fma
differently in the two builds, which changes only the sign of a default NaN
(C leaves NaN signs unspecified; the generated build has the same freedom).
Run: python3 wii/test_localize_all.py [functions-per-module] [cases] [seed] [regex]"""
from pathlib import Path
import os
import random
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'wii'))
from localize_function import localize
from specialize_gpr_multiple import ram_base, lanc_copy
lanc = bool(os.environ.get('LANC'))

args = sys.argv[1:]
per_module = int(args[0]) if args else 40
cases = int(args[1]) if len(args) > 1 else 100
seed = int(args[2]) if len(args) > 2 else 1
only = args[3] if len(args) > 3 else None   # optional regex: sample only matching functions
gen = root / 'generated/gticlub2'
sources = re.findall(r'generated/gticlub2/(\w+)\.c', (gen / 'sources.mk').read_text())
bodies = [s for s in sources if not s.endswith('_table')]
rng = random.Random(seed)
chosen, refused = [], 0
for name in bodies:
    text = (gen / f'{name}.c').read_text()
    if lanc:
        text = lanc_copy(text)
    if os.environ.get('DEADCARRY'):
        from specialize_gpr_multiple import dead_carry
        text = dead_carry(text)[0]
    funcs = re.findall(r'\nvoid f_\w+\(PPCContext \*c\) \{\n.*?\n\}\n', text, re.S)
    ok = []
    for f in funcs:
        f = f.strip('\n')
        if '0xde28u' in f:
            continue
        if only and not re.search(only, f):
            continue
        try:
            ok.append((re.match(r'void (\w+)', f).group(1), ram_base(localize(f))))
        except ValueError:
            refused += 1
    chosen += rng.sample(ok, min(per_module, len(ok)))
localized = []
for fname, text in chosen:
    text = text.replace('#include "native_ram.h"\n', '', 1)
    localized.append(text.replace(f'void {fname}(PPCContext *c)', f'void localized_{fname}(PPCContext *c)', 1))

tables = [s[:-6] for s in sources if s.endswith('_table')]
runtime = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
uint8_t *g_ram;
FILE *trace;
static unsigned checks;
/* Everything a delivered exception could observe: GPRs, FPRs, CR, CTR, LR, XER. */
static uint32_t regs_hash(PPCContext *c){
    uint32_t h=2166136261u;const uint8_t *p=(const uint8_t *)c;
    for(size_t i=0;i<offsetof(PPCContext,msr);i++)h=(h^p[i])*16777619u;
    return h;
}
''' + ''.join(f'extern const RtFunc rt_funcs_{t}[]; extern const unsigned rt_nfuncs_{t};\n' for t in tables) + r'''
static const RtFunc *const all_funcs[] = {''' + ','.join(f'rt_funcs_{t}' for t in tables) + r'''};
static const unsigned *const all_n[] = {''' + ','.join(f'&rt_nfuncs_{t}' for t in tables) + r'''};
/* LAN RAM as the runtime's fast path sees it: plain bytes, no log. */
static uint8_t fake_lanc[0x2000];
static int lanc_init;
static void lanc_fill(void){if(!lanc_init){for(int i=0;i<0x2000;i++)fake_lanc[i]=(uint8_t)(i*37+11);lanc_init=1;}}
uint32_t rt_mmio_r8(uint32_t a){if(a-0xffe9a000u<0x2000u){lanc_fill();return fake_lanc[a&0x1fff];}fprintf(trace,"R8 %08x\n",a);return (a*29+5)&255;}
int wii_lanc_copy_to_ram(uint32_t dst,uint32_t src,uint32_t n){
    uint32_t off=src-0xffe9a000u;lanc_fill();
    if(!n||off>=0x2000u||n>0x2000u-off||dst>=RAM_SIZE||n>RAM_SIZE-dst)return -1;
    memcpy(g_ram+dst,fake_lanc+off,n);return fake_lanc[off+n-1];
}
uint32_t rt_mmio_r16(uint32_t a){fprintf(trace,"R16 %08x\n",a);return (a*29+5)&0xffff;}
uint32_t rt_mmio_r32(uint32_t a){fprintf(trace,"R32 %08x\n",a);return a*2654435761u;}
void rt_mmio_w8(uint32_t a,uint32_t v){fprintf(trace,"W8 %08x %02x\n",a,v);}
void rt_mmio_w16(uint32_t a,uint32_t v){fprintf(trace,"W16 %08x %04x\n",a,v);}
void rt_mmio_w32(uint32_t a,uint32_t v){fprintf(trace,"W32 %08x %08x\n",a,v);}
void rt_check(PPCContext *c,uint32_t pc){
    fprintf(trace,"CHK %08x %lld %08x\n",pc,(long long)c->budget,regs_hash(c));
    c->budget+=700;
    if(++checks>40)c->unwind=1;
}
void rt_hook(PPCContext *c,uint32_t pc){fprintf(trace,"HOOK %08x r3=%08x\n",pc,c->r[3]);c->r[3]^=pc;}
uint32_t rt_mftb(PPCContext *c,int tbr){fprintf(trace,"MFTB %d\n",tbr);return (uint32_t)c->budget*7u+(uint32_t)tbr;}
static unsigned depth;
void rt_call(PPCContext *c,uint32_t t){
    fprintf(trace,"CALL %08x %08x\n",t,regs_hash(c));
    if(depth>64){c->unwind=1;return;}
    for(unsigned m=0;m<sizeof all_funcs/sizeof *all_funcs;m++)
        for(unsigned i=0;i<*all_n[m];i++)if(all_funcs[m][i].addr==t){depth++;all_funcs[m][i].fn(c);depth--;return;}
    c->r[3]=t^0x5a5a5a5au;
}
uint32_t rt_cr_pack(PPCContext *c){uint32_t v=0;for(int i=0;i<8;i++)v|=(uint32_t)(c->cr[i]&15)<<(28-4*i);return v;}
void rt_cr_unpack(PPCContext *c,uint32_t v,uint32_t crm){for(int i=0;i<8;i++)if(crm&(0x80u>>i))c->cr[i]=(v>>(28-4*i))&15;}
uint32_t rt_sraw(PPCContext *c,uint32_t v,uint32_t n){
    int32_t s=(int32_t)v;
    if(n>=32){c->xer_ca=s<0;return (uint32_t)(s>>31);}
    if(n==0){c->xer_ca=0;return v;}
    c->xer_ca=(s<0)&&(v&((1u<<n)-1));return (uint32_t)(s>>n);
}
uint32_t rt_divwu(PPCContext *c,uint32_t a,uint32_t b,int oe){if(oe){c->xer_ov=(b==0);c->xer_so|=c->xer_ov;}return b?a/b:0;}
uint32_t rt_divw(PPCContext *c,uint32_t a,uint32_t b,int oe){
    int bad=b==0||(a==0x80000000u&&b==0xffffffffu);
    if(oe){c->xer_ov=bad;c->xer_so|=c->xer_ov;}
    return bad?0:(uint32_t)((int32_t)a/(int32_t)b);
}
void rt_lswi(PPCContext *c,uint32_t ea,int rd,int nb){
    int r=(rd-1)&31;
    for(int n=0;n<nb;n++){if((n&3)==0){r=(r+1)&31;c->r[r]=0;}c->r[r]|=LD8(ea+n)<<(24-8*(n&3));}
}
void rt_stswi(PPCContext *c,uint32_t ea,int rs,int nb){
    int r=(rs-1)&31;
    for(int n=0;n<nb;n++){if((n&3)==0)r=(r+1)&31;ST8(ea+n,(c->r[r]>>(24-8*(n&3)))&0xff);}
}
double rt_fctiw(PPCContext *c,double v,int trunc){
    int32_t r;
    if(v!=v)r=(int32_t)0x80000000;else if(v>=2147483647.0)r=0x7fffffff;
    else if(v<=-2147483648.0)r=(int32_t)0x80000000;else if(trunc)r=(int32_t)v;
    else r=(int32_t)nearbyint(v);
    (void)c;return BITS_FPR(0xfff8000000000000ull|(uint32_t)r);
}
/* Privileged helpers: deterministic functions of the context, logged; like
 * the real ones (which may deliver exceptions) they read and write guest
 * registers, so a missing context sync around them shows. */
int rt_sc(PPCContext *c,uint32_t next){fprintf(trace,"SC %08x %08x\n",next,regs_hash(c));c->r[3]=next^c->r[0];return 0;}
void rt_rfi(PPCContext *c){fprintf(trace,"RFI %08x\n",regs_hash(c));c->unwind=1;}
void rt_mtmsr(PPCContext *c,uint32_t v,uint32_t next){fprintf(trace,"MTMSR %08x %08x %08x\n",v,next,regs_hash(c));c->msr=v;c->r[3]^=v;c->lr^=next;}
uint32_t rt_mfspr(PPCContext *c,int spr){fprintf(trace,"MFSPR %d %08x\n",spr,regs_hash(c));return c->r[3]^(uint32_t)spr;}
void rt_mtspr(PPCContext *c,int spr,uint32_t v){fprintf(trace,"MTSPR %d %08x %08x\n",spr,v,regs_hash(c));c->r[4]^=v+(uint32_t)spr;}
uint32_t rt_xer_pack(PPCContext *c){return ((uint32_t)c->xer_so<<31)|((uint32_t)c->xer_ov<<30)|((uint32_t)c->xer_ca<<29)|c->xer_bc;}
void rt_xer_unpack(PPCContext *c,uint32_t v){c->xer_so=v>>31&1;c->xer_ov=v>>30&1;c->xer_ca=v>>29&1;c->xer_bc=v&127;}
void rt_dcbz(PPCContext *c,uint32_t ea){(void)c;fprintf(trace,"DCBZ %08x %08x\n",ea,regs_hash(c));for(int i=0;i<32;i++)ST8((ea&~31u)+i,0);}
void rt_mtfsf(PPCContext *c,uint32_t fm,uint32_t v){fprintf(trace,"MTFSF %02x %08x %08x\n",fm,v,regs_hash(c));c->fpscr=v;c->f[1]+=1.0;}
void rt_fpscr_changed(PPCContext *c){fprintf(trace,"FPSCR %08x %08x\n",c->fpscr,regs_hash(c));}
unsigned test_checks_reset(void){unsigned n=checks;checks=0;depth=0;return n;}
'''
names = [f for f, _ in chosen]
decls = '\n'.join(f'void {n}(PPCContext *c); void localized_{n}(PPCContext *c);' for n in names)
table = ',\n'.join(f'    {{"{n}", {n}, localized_{n}}}' for n in names)
driver = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint8_t *g_ram; extern FILE *trace;
unsigned test_checks_reset(void);
''' + decls + r'''
static const struct { const char *name; RtFn reference, localized; } tests[] = {
''' + table + r'''
};
static uint64_t rng;
static uint64_t next64(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static void setup(PPCContext *c,uint64_t s){
    rng=s;
    memset(c,0,sizeof *c);
    /* Mostly small aligned in-RAM values, some in device space, some raw;
     * a stack pointer near the top of RAM. */
    for(int i=0;i<32;i++){uint64_t v=next64();c->r[i]=(v&3)?((uint32_t)v&0x00fffffcu):
        (v&4)?(0x84000000u|((uint32_t)(v>>40)&0x0000fffcu)):(uint32_t)(v>>32);}
    c->r[1]=0x00f00000u+(uint32_t)(next64()%0x8000)*16u;
    for(int i=0;i<32;i++){float f=(float)((int64_t)next64()%200000)/1000.0f;c->f[i]=f;}
    for(int i=0;i<8;i++)c->cr[i]=next64()&15;
    c->xer_so=next64()&1;c->xer_ca=next64()&1;c->ctr=(uint32_t)next64()&63;c->lr=(uint32_t)next64();
    c->budget=200+(int)(next64()%3000);
#ifdef TEST_LANC
    /* The link copy's destination base (LD32(r2+0xc0)) in guest RAM, budgets
     * long enough to run whole 1 KB copies. */
    c->r[2]=0x10000u+(uint32_t)(next64()%0x1000)*16u;
    {uint32_t b=0x200000u+(uint32_t)(next64()%0x400)*0x1000u;extern uint8_t *g_ram;
     g_ram[c->r[2]+0xc0]=b>>24;g_ram[c->r[2]+0xc1]=b>>16;g_ram[c->r[2]+0xc2]=b>>8;g_ram[c->r[2]+0xc3]=b;}
    c->budget=200+(int)(next64()%20000);
#endif
}
int main(int argc,char **argv){
    int cases=atoi(argv[1]);
    uint8_t *tmpl=malloc(RAM_SIZE),*ra=malloc(RAM_SIZE),*rb=malloc(RAM_SIZE);
    static PPCContext ca,cb;
    unsigned long long total_checks=0;
    for(unsigned t=0;t<sizeof tests/sizeof *tests;t++){
        rng=0x9e3779b97f4a7c15ull*(t+1);
        for(size_t i=0;i<RAM_SIZE;i+=8){uint64_t v=next64();memcpy(tmpl+i,&v,8);}
        for(int n=0;n<cases;n++){
            uint64_t s=0x9e3779b97f4a7c15ull*(n+1)+t*0x1234567ull;
            char *ta=NULL,*tb=NULL;size_t la=0,lb=0;
            memcpy(ra,tmpl,RAM_SIZE);memcpy(rb,tmpl,RAM_SIZE);
            g_ram=ra;setup(&ca,s);g_ram=rb;setup(&cb,s);
            test_checks_reset();
            trace=open_memstream(&ta,&la);g_ram=ra;tests[t].reference(&ca);fclose(trace);
            total_checks+=test_checks_reset();
            trace=open_memstream(&tb,&lb);g_ram=rb;tests[t].localized(&cb);fclose(trace);
            test_checks_reset();
            int ctx=memcmp(&ca,&cb,sizeof ca),ram=memcmp(ra,rb,RAM_SIZE),log=la!=lb||memcmp(ta,tb,la);
            if(ctx||ram||log){
                printf("MISMATCH %s case %d: context=%d ram=%d log=%d\n",tests[t].name,n,!!ctx,!!ram,log);
                for(int i=0;i<32;i++)if(ca.r[i]!=cb.r[i])printf(" r%d %08x %08x\n",i,ca.r[i],cb.r[i]);
                for(int i=0;i<32;i++)if(memcmp(&ca.f[i],&cb.f[i],8))printf(" f%d %a %a bits %016llx %016llx\n",i,ca.f[i],cb.f[i],
                    (unsigned long long)FPR_BITS(ca.f[i]),(unsigned long long)FPR_BITS(cb.f[i]));
                for(size_t i=0;i<RAM_SIZE;i++)if(ra[i]!=rb[i]){printf(" ram %06zx %02x %02x\n",i,ra[i],rb[i]);break;}
                for(int i=0;i<8;i++)if(ca.cr[i]!=cb.cr[i])printf(" cr%d %x %x\n",i,ca.cr[i],cb.cr[i]);
                printf(" ctr %08x %08x lr %08x %08x budget %lld %lld unwind %d %d\n",ca.ctr,cb.ctr,ca.lr,cb.lr,
                    (long long)ca.budget,(long long)cb.budget,ca.unwind,cb.unwind);
                return 1;
            }
            free(ta);free(tb);
        }
    }
    printf("PASS: %u functions x %d cases identical (%llu checkpoints fired in reference)\n",
           (unsigned)(sizeof tests/sizeof *tests),cases,total_checks);
    return 0;
}
'''
print(f'{len(chosen)} functions sampled from {len(bodies)} modules ({refused} refused by the localizer)')
with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'runtime.c').write_text(runtime)
    (tmp / 'driver.c').write_text(driver)
    headers = ''.join(f'#include "mod_{t}.h"\n' for t in tables)
    (tmp / 'localized.c').write_text('#define VIPER_WII_RAM_BASE_LOCAL 1\n' +
                                     ('#define VIPER_WII_RAM_MASK_TEST 1\n' if os.environ.get('RAMMASK') else '') +
                                     '#include "ppc_rt.h"\n#include "native_ram.h"\n' +
                                     headers + '\n'.join(localized))
    flags = ['clang', os.environ.get('TEST_OPT', '-O0'), '-std=gnu11', '-frounding-math', '-ffp-contract=off', '-fno-strict-aliasing', '-w',
             '-I' + str(root / 'runtime'), '-I' + str(gen), '-I' + str(root / 'wii')]
    if lanc:
        flags += ['-DTEST_LANC', '-DVIPER_WII_LANC_COPY']
    srcs = [gen / f'{s}.c' for s in sources] + [tmp / 'runtime.c', tmp / 'driver.c', tmp / 'localized.c']
    procs, objs = [], []
    for src in srcs:
        obj = tmp / (src.stem + '.o')
        procs.append(subprocess.Popen(flags + ['-c', str(src), '-o', str(obj)]))
        objs.append(str(obj))
    if any(p.wait() for p in procs):
        sys.exit('compile failed')
    subprocess.run(['clang', '-o', str(tmp / 'test')] + objs + ['-lm'], check=True)
    subprocess.run([str(tmp / 'test'), str(cases)], check=True)
