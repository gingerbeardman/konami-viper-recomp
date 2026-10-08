"""Differential proof for wii/localize_function.py: each localized gl function
against its generated original, from identical random contexts and RAM.

Both versions are linked with the complete generated gl module (callees and
the rt_call dispatch table are the real generated code) and a small fake
runtime. Compared after every case: the full PPCContext, all 16 MiB of RAM,
and the ordered log of MMIO accesses, rt_call/rt_hook/rt_mftb calls and
firing checkpoints. A checkpoint limit sets unwind, so runaway random states
end the same way in both versions.
With --gather the functions are localized with FIFO store gathering
(scalar flush path), so the ordered MMIO log proves buffered words reach the
device in generated order before anything else can observe it.
With --direct (header-last FIFO gathering, gather='direct') a stand-in
wii_voodoo_direct_triangles logs each packet as the scalar stores it
replaces (the words, then the header) and declines at random, so both
paths are compared.
With --ram-base the localized functions also get the generated-code RAM
base pass (wii/specialize_gpr_multiple.py ram_base) and are compiled with
VIPER_WII_RAM_BASE_LOCAL; random registers put many accesses at RAM ends,
mirrors and device space.
Run: python3 wii/test_localize.py [--gather|--direct] [--ram-base] [cases] [function ...]"""
from pathlib import Path
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'wii'))
from localize_function import localize

args = sys.argv[1:]
gather = 'direct' if '--direct' in args else '--gather' in args
ram_base_pass = '--ram-base' in args
args = [a for a in args if a not in ('--gather', '--direct', '--ram-base')]
cases = int(args.pop(0)) if args and args[0].isdigit() else 3000
functions = args or ['f_gl_0002adac', 'f_gl_00028fc4', 'f_gl_00029d28', 'f_gl_00021ed0',
                     'f_gl_00021940', 'f_gl_000281ac', 'f_gl_000248b0', 'f_gl_000210a8']
gl = (root / 'generated/gticlub2/gl_000.c').read_text()
localized = []
for name in functions:
    m = re.search(r'\nvoid ' + name + r'\(PPCContext \*c\) \{.*?\n\}\n', gl, re.S)
    text = localize(m.group(0).strip('\n'), gather=gather)
    if ram_base_pass:
        from specialize_gpr_multiple import ram_base
        text = ram_base(text)
        if 'RAM_BEGIN();' not in text:
            raise ValueError('RAM base pass did not apply to ' + name)
    localized.append(text.replace(f'void {name}(PPCContext *c)', f'void localized_{name}(PPCContext *c)', 1))

runtime = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
FILE *trace;
static unsigned checks;
extern const RtFunc rt_funcs_gl[];
extern const unsigned rt_nfuncs_gl;
static uint64_t drng=0x8badf00d;
unsigned long long direct_offered,direct_accepted;
int wii_voodoo_direct_triangles(uint32_t ea,uint32_t cmd,const uint32_t *w,unsigned n){
    drng^=drng<<13;drng^=drng>>7;drng^=drng<<17;
    direct_offered++;
    if(drng&1)return 0;
    direct_accepted++;
    for(unsigned i=0;i<n;i++)ST32(ea+4+4*i,w[i]);ST32LE(ea,cmd);
    return 1;
}
uint32_t rt_mmio_r8(uint32_t a){fprintf(trace,"R8 %08x\n",a);return (a*29+5)&255;}
uint32_t rt_mmio_r16(uint32_t a){fprintf(trace,"R16 %08x\n",a);return (a*29+5)&0xffff;}
uint32_t rt_mmio_r32(uint32_t a){fprintf(trace,"R32 %08x\n",a);return a*2654435761u;}
void rt_mmio_w8(uint32_t a,uint32_t v){fprintf(trace,"W8 %08x %02x\n",a,v);}
void rt_mmio_w16(uint32_t a,uint32_t v){fprintf(trace,"W16 %08x %04x\n",a,v);}
void rt_mmio_w32(uint32_t a,uint32_t v){fprintf(trace,"W32 %08x %08x\n",a,v);}
void rt_check(PPCContext *c,uint32_t pc){
    fprintf(trace,"CHK %08x %lld\n",pc,(long long)c->budget);
    c->budget+=700;
    if(++checks>40)c->unwind=1;
}
void rt_hook(PPCContext *c,uint32_t pc){fprintf(trace,"HOOK %08x r3=%08x\n",pc,c->r[3]);c->r[3]^=pc;}
uint32_t rt_mftb(PPCContext *c,int tbr){fprintf(trace,"MFTB %d\n",tbr);return (uint32_t)c->budget*7u+(uint32_t)tbr;}
void rt_call(PPCContext *c,uint32_t t){
    fprintf(trace,"CALL %08x\n",t);
    for(unsigned i=0;i<rt_nfuncs_gl;i++)if(rt_funcs_gl[i].addr==t){rt_funcs_gl[i].fn(c);return;}
    c->r[3]=t^0x5a5a5a5au;   /* outside gl: deterministic stand-in */
}
/* Same semantics as runtime/cpu.c. */
uint32_t rt_cr_pack(PPCContext *c){uint32_t v=0;for(int i=0;i<8;i++)v|=(uint32_t)(c->cr[i]&15)<<(28-4*i);return v;}
void rt_cr_unpack(PPCContext *c,uint32_t v,uint32_t crm){for(int i=0;i<8;i++)if(crm&(0x80u>>i))c->cr[i]=(v>>(28-4*i))&15;}
uint32_t rt_sraw(PPCContext *c,uint32_t v,uint32_t n){
    int32_t s=(int32_t)v;
    if(n>=32){c->xer_ca=s<0;return (uint32_t)(s>>31);}
    if(n==0){c->xer_ca=0;return v;}
    c->xer_ca=(s<0)&&(v&((1u<<n)-1));return (uint32_t)(s>>n);
}
uint32_t rt_divwu(PPCContext *c,uint32_t a,uint32_t b,int oe){if(oe){c->xer_ov=(b==0);c->xer_so|=c->xer_ov;}return b?a/b:0;}
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
'''
names = ', '.join(functions)
decls = '\n'.join(f'void {n}(PPCContext *c); void localized_{n}(PPCContext *c);' for n in functions)
table = ',\n'.join(f'    {{"{n}", {n}, localized_{n}}}' for n in functions)
driver = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint8_t *g_ram; extern FILE *trace;
''' + decls + r'''
static const struct { const char *name; RtFn reference, localized; } tests[] = {
''' + table + r'''
};
static uint64_t rng;
static uint64_t next64(void){rng^=rng<<13;rng^=rng>>7;rng^=rng<<17;return rng;}
static void setup(PPCContext *c,uint8_t *ram,uint64_t seed){
    rng=seed;
    memset(c,0,sizeof *c);
    for(size_t i=0;i<RAM_SIZE;i+=8){uint64_t v=next64();memcpy(ram+i,&v,8);}
    /* Mostly small, aligned in-RAM values so control flow goes somewhere. */
    /* Some point into the Voodoo LFB/FIFO window, so FIFO stores occur. */
    for(int i=0;i<32;i++){uint64_t v=next64();c->r[i]=(v&3)?((uint32_t)v&0x00fffffcu):
        (v&4)?(0x84000000u|((uint32_t)(v>>40)&0x0000fffcu)):(uint32_t)(v>>32);}
    for(int i=0;i<32;i++){float f=(float)((int64_t)next64()%200000)/1000.0f;c->f[i]=f;}
    for(int i=0;i<8;i++)c->cr[i]=next64()&15;
    c->xer_so=next64()&1;c->xer_ca=next64()&1;c->ctr=(uint32_t)next64()&63;c->lr=(uint32_t)next64();
    c->budget=200+(int)(next64()%3000);
}
static void wr32(uint8_t *ram,uint32_t ea,uint32_t v){ram[ea]=v>>24;ram[ea+1]=v>>16;ram[ea+2]=v>>8;ram[ea+3]=v;}
/* Most cases of the strip producer reach its unclipped FIFO path. */
static void prepare(const char *name,PPCContext *c,uint8_t *ram){
    if(!strcmp(name,"f_gl_00028fc4")&&(next64()&7)){
        if(next64()&3)memset(ram+0x2224,0,0x80);
        c->r[30]=1+(uint32_t)(next64()%40);
        c->r[31]=0x100000u+(uint32_t)(next64()%0x10000)*4u;
        wr32(ram,0x2358,0x84600000u+(uint32_t)(next64()%0x1000)*4u);
        wr32(ram,0x235c,0x84700000u);
    }
}
int main(int argc,char **argv){
    int cases=atoi(argv[1]);
    uint8_t *ra=malloc(RAM_SIZE),*rb=malloc(RAM_SIZE);
    static PPCContext ca,cb;
    for(unsigned t=0;t<sizeof tests/sizeof *tests;t++){
        unsigned long long checks=0,fifo_words=0;
        for(int n=0;n<cases;n++){
            uint64_t seed=0x9e3779b97f4a7c15ull*(n+1)+t*0x1234567ull;
            char *ta=NULL,*tb=NULL;size_t la=0,lb=0;
            setup(&ca,ra,seed);prepare(tests[t].name,&ca,ra);
            setup(&cb,rb,seed);prepare(tests[t].name,&cb,rb);
            extern unsigned test_checks_reset(void);
            test_checks_reset();
            trace=open_memstream(&ta,&la);g_ram=ra;tests[t].reference(&ca);fclose(trace);
            checks+=test_checks_reset();
            trace=open_memstream(&tb,&lb);g_ram=rb;tests[t].localized(&cb);fclose(trace);
            test_checks_reset();
            if(ta)for(char *q=ta;(q=strstr(q,"W32 84"));q++)fifo_words++;
            int ctx=memcmp(&ca,&cb,sizeof ca),ram=memcmp(ra,rb,RAM_SIZE),log=la!=lb||memcmp(ta,tb,la);
            if(ctx||ram||log){
                printf("MISMATCH %s case %d: context=%d ram=%d log=%d\n",tests[t].name,n,!!ctx,!!ram,log);
                for(int i=0;i<32;i++)if(ca.r[i]!=cb.r[i])printf(" r%d %08x %08x\n",i,ca.r[i],cb.r[i]);
                for(int i=0;i<32;i++)if(memcmp(&ca.f[i],&cb.f[i],8))printf(" f%d %a %a\n",i,ca.f[i],cb.f[i]);
                for(int i=0;i<8;i++)if(ca.cr[i]!=cb.cr[i])printf(" cr%d %x %x\n",i,ca.cr[i],cb.cr[i]);
                printf(" ctr %08x %08x lr %08x %08x budget %lld %lld unwind %d %d xer %d%d%d %d%d%d\n",ca.ctr,cb.ctr,ca.lr,cb.lr,
                    (long long)ca.budget,(long long)cb.budget,ca.unwind,cb.unwind,ca.xer_so,ca.xer_ov,ca.xer_ca,cb.xer_so,cb.xer_ov,cb.xer_ca);
                for(size_t i=0;i<RAM_SIZE;i++)if(ra[i]!=rb[i]){printf(" ram %06zx %02x %02x\n",i,ra[i],rb[i]);break;}
                return 1;
            }
            free(ta);free(tb);
        }
        extern unsigned long long direct_offered,direct_accepted;
        printf("%s: %d cases identical (%llu checkpoints fired, %llu FIFO-window words in reference, %llu/%llu direct packets accepted/offered)\n",
               tests[t].name,cases,checks,fifo_words,direct_accepted,direct_offered);
        direct_offered=direct_accepted=0;
    }
    return 0;
}
'''
runtime += 'unsigned test_checks_reset(void){unsigned n=checks;checks=0;return n;}\n'

with tempfile.TemporaryDirectory() as tmp:
    tmp = Path(tmp)
    (tmp / 'runtime.c').write_text(runtime)
    (tmp / 'driver.c').write_text(driver)
    (tmp / 'localized.c').write_text('#include "mod_gl.h"\n' + '\n'.join(localized))
    if ram_base_pass:
        (tmp / 'localized.c').write_text('#define VIPER_WII_RAM_BASE_LOCAL 1\n' + (tmp / 'localized.c').read_text())
    flags = ['clang', '-O1', '-std=gnu11', '-frounding-math', '-ffp-contract=off', '-fno-strict-aliasing',
             '-Wno-unused-label', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
             '-Wno-parentheses-equality', '-I' + str(root / 'runtime'), '-I' + str(root / 'generated/gticlub2'), '-I' + str(root / 'wii')]
    objs = []
    for src in (root / 'generated/gticlub2/gl_000.c', root / 'generated/gticlub2/gl_table.c',
                tmp / 'runtime.c', tmp / 'driver.c', tmp / 'localized.c'):
        obj = tmp / (src.stem + '.o')
        subprocess.run(flags + ['-c', str(src), '-o', str(obj)], check=True)
        objs.append(str(obj))
    subprocess.run(['clang', '-o', str(tmp / 'test')] + objs + ['-lm'], check=True)
    subprocess.run([str(tmp / 'test'), str(cases)], check=True)
