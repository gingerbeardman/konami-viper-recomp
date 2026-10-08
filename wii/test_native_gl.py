"""Differential proof for the native gl transcriptions (wii/native_gl_*.c):
each native function against its generated original, from identical random
contexts and RAM, linked with the complete generated gl module and a fake
runtime. Compared after every case: the full PPCContext, all 16 MiB of RAM,
and the ordered log of MMIO accesses, rt_call/rt_hook/rt_mftb calls and
firing checkpoints. The stand-in wii_voodoo_direct_triangles logs a packet
exactly as the scalar stores it replaces (header ST32LE, then the words) and
declines at random, so held packets and their fallback are both proven to
reach the device in generated order.
Run: python3 wii/test_native_gl.py [cases]"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root / 'wii'))
args = sys.argv[1:]
cases = int(args.pop(0)) if args and args[0].isdigit() else 3000
# function: (source, words stored before the header: 0 = none, 1 = all but the
# last vertex's ten, which complete the packet)
NATIVE = {'f_gl_00029d28': ('wii/native_gl_list.c', 0), 'f_gl_0002adac': ('wii/native_gl_draw.c', 1)}
functions = args or list(NATIVE)

runtime = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
FILE *trace;
static unsigned checks;
extern const RtFunc rt_funcs_gl[];
extern const unsigned rt_nfuncs_gl;
int rt_wii_bulk_lfb_allowed(void){return 0;}
int wii_voodoo_bulk_writer_ready(uint32_t ea,unsigned n){(void)ea;(void)n;return 0;}
void wii_voodoo_bulk_writer_be(uint32_t ea,const uint32_t *w,unsigned n){(void)ea;(void)w;(void)n;abort();}
#ifdef VIPER_MEMORY_AUDIT
void rt_memory_access(uint32_t ea,unsigned n,int w){if(ea>=0x3300&&ea<0x3500)fprintf(trace,"MEM%c %08x %u\n",w?87:82,ea,n);}
void rt_memory_sample(void){}
void rt_memory_report(void){}
#endif
int header_last;
unsigned long long direct_offered,direct_accepted;
static uint64_t drng=0x8badf00d;
int wii_voodoo_direct_triangles(uint32_t ea,uint32_t cmd,const uint32_t *w,unsigned n){
    drng^=drng<<13;drng^=drng>>7;drng^=drng<<17;
    direct_offered++;
    if(drng&1)return 0;
    direct_accepted++;
    unsigned before=header_last?n-10:0;
    for(unsigned i=0;i<before;i++)ST32(ea+4+4*i,w[i]);
    ST32LE(ea,cmd);
    for(unsigned i=before;i<n;i++)ST32(ea+4+4*i,w[i]);
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
    /* Like an interrupt handler, write guest RAM (including the 0x3398
     * staging words the native draw forwards across vertices). */
    g_ram[0x3398u+(checks*7u)%40u]^=0x5au;
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
decls = '\n'.join(f'void {n}(PPCContext *c); void wii_native_gl_{n[5:]}(PPCContext *c);' for n in functions)
table = ',\n'.join(f'    {{"{n}", {n}, wii_native_gl_{n[5:]}, {NATIVE[n][1]}}}' for n in functions)
driver = r'''
#include "ppc_rt.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uint8_t *g_ram; extern FILE *trace;
''' + decls + r'''
static const struct { const char *name; RtFn reference, native; int header_last; } tests[] = {
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
/* Arguments that reach the vertex loops most of the time. */
static void prepare(const char *name,PPCContext *c,uint8_t *ram){
    if(!strcmp(name,"f_gl_00029d28")&&(next64()&7)){
        c->r[3]=(uint32_t)(next64()%17);
        c->r[4]=0x100000u+(uint32_t)(next64()%0x10000)*4u;
        for(int k=0;k<16;k++)wr32(ram,c->r[4]+4*k,(uint32_t)(next64()%300));
        c->r[1]=0x200000u+(uint32_t)(next64()%0x1000)*16u;
        wr32(ram,0x2358,0x84600000u+(uint32_t)(next64()%0x1000)*4u);
        wr32(ram,0x235c,0x84700000u);
        /* Some FIFO pointers into guest RAM near the records and the frame. */
        if(!(next64()&3))wr32(ram,0x2358,(next64()&1?0x2e00u:c->r[1]-0x100u)+(uint32_t)(next64()%96)*4u);
    }
    if(!strcmp(name,"f_gl_0002adac")&&(next64()&7)){
        c->r[30]=1+(uint32_t)(next64()%40);
        c->r[31]=0x100000u+(uint32_t)(next64()%0x10000)*4u;
        wr32(ram,0x2358,0x84600000u+(uint32_t)(next64()%0x1000)*4u);
        wr32(ram,0x235c,0x84700000u);
    }
    /* Some FIFO pointers on or near the 0x3398 staging words. */
    if(!strcmp(name,"f_gl_0002adac")&&!(next64()&3)){
        c->r[30]=1+(uint32_t)(next64()%40);
        c->r[31]=0x100000u+(uint32_t)(next64()%0x10000)*4u;
        wr32(ram,0x2358,0x3300u+(uint32_t)(next64()%64)*4u);
        wr32(ram,0x235c,0x4000u);
    }
}
int main(int argc,char **argv){
    int cases=atoi(argv[1]);
    uint8_t *ra=malloc(RAM_SIZE),*rb=malloc(RAM_SIZE);
    static PPCContext ca,cb;
    extern int header_last;
    for(unsigned t=0;t<sizeof tests/sizeof *tests;t++){
        header_last=tests[t].header_last;
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
            trace=open_memstream(&tb,&lb);g_ram=rb;tests[t].native(&cb);fclose(trace);
            test_checks_reset();
            if(ta)for(char *q=ta;(q=strstr(q,"W32 84"));q++)fifo_words++;
            int ctx=memcmp(&ca,&cb,sizeof ca),ram=memcmp(ra,rb,RAM_SIZE),log=!getenv("AUDIT")&&(la!=lb||memcmp(ta,tb,la));
            if(ctx||ram||log){
                printf("MISMATCH %s case %d: context=%d ram=%d log=%d\n",tests[t].name,n,!!ctx,!!ram,log);
                for(int i=0;i<32;i++)if(ca.r[i]!=cb.r[i])printf(" r%d %08x %08x\n",i,ca.r[i],cb.r[i]);
                for(int i=0;i<32;i++)if(memcmp(&ca.f[i],&cb.f[i],8))printf(" f%d %a %a\n",i,ca.f[i],cb.f[i]);
                for(int i=0;i<8;i++)if(ca.cr[i]!=cb.cr[i])printf(" cr%d %x %x\n",i,ca.cr[i],cb.cr[i]);
                printf(" ctr %08x %08x lr %08x %08x budget %lld %lld unwind %d %d xer %d%d%d %d%d%d\n",ca.ctr,cb.ctr,ca.lr,cb.lr,
                    (long long)ca.budget,(long long)cb.budget,ca.unwind,cb.unwind,ca.xer_so,ca.xer_ov,ca.xer_ca,cb.xer_so,cb.xer_ov,cb.xer_ca);
                for(size_t i=0;i<RAM_SIZE;i++)if(ra[i]!=rb[i]){printf(" ram %06zx %02x %02x\n",i,ra[i],rb[i]);break;}
                if(getenv("RAM_DIFFS"))for(size_t i=0;i<RAM_SIZE;i++)if(ra[i]!=rb[i])printf(" diff %06zx %02x %02x\n",i,ra[i],rb[i]);
                if((log||getenv("AUDIT"))&&getenv("DUMP_LOGS")){FILE*f=fopen("/tmp/ref.log","w");fwrite(ta,1,la,f);fclose(f);f=fopen("/tmp/nat.log","w");fwrite(tb,1,lb,f);fclose(f);}
                if(log){size_t i=0,line=0;while(i<la&&i<lb&&ta[i]==tb[i]){if(ta[i]=='\n')line++;i++;}
                    size_t j=i;while(j>0&&ta[j-1]!='\n')j--;
                    size_t k=j;for(int q=0;q<4&&k>0;q++){k--;while(k>0&&ta[k-1]!='\n')k--;}
                    printf(" log differs at line %zu:\n--ref--\n%.300s\n--nat--\n%.300s\n",line,ta+k,tb+k);}
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
    flags = ['clang', '-O1', '-std=gnu11', '-frounding-math', '-ffp-contract=off', '-fno-strict-aliasing',
             '-Wno-unused-label', '-Wno-unused-variable', '-Wno-unused-but-set-variable',
             '-Wno-parentheses-equality', '-I' + str(root / 'runtime'), '-I' + str(root / 'generated/gticlub2'), '-I' + str(root / 'wii'),
             '-DVIPER_WII_BULK_WRITER', '-DVIPER_WII_DIRECT_TRIANGLES'] + \
            [f'-D{n}_generated={n}' for n in functions] + (['-DVIPER_MEMORY_AUDIT'] if os.environ.get('AUDIT') else []) + os.environ.get('NATIVE_GL_FLAGS', '').split()
    objs = []
    for src in (root / 'generated/gticlub2/gl_000.c', root / 'generated/gticlub2/gl_table.c',
                tmp / 'runtime.c', tmp / 'driver.c') + tuple(root / NATIVE[n][0] for n in functions):
        obj = tmp / (src.stem + '.o')
        subprocess.run(flags + ['-c', str(src), '-o', str(obj)], check=True)
        objs.append(str(obj))
    subprocess.run(['clang', '-o', str(tmp / 'test')] + objs + ['-lm'], check=True)
    subprocess.run([str(tmp / 'test'), str(cases)], check=True)
