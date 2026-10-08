#!/usr/bin/env python3
"""Compare original and split memory helpers including native full FPSCR."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--emit-native', type=Path)
parser.add_argument('--f64-ram', action='store_true')
args = parser.parse_args()
header = (root / 'runtime/ppc_rt.h').read_text()
source = header[header.index('#else\n#ifdef VIPER_WII_SPLIT_RAM_HELPERS'):]
names = ['LD32', 'LD16', 'ST32', 'ST16', 'LDF32', 'STF32', 'LDF64', 'STF64']
reference = ''
for name in names:
    match = re.search(r'static inline [^\n]+ ' + name + r'\(', source)
    assert match
    end = source.index('{', match.start()) + 1
    depth = 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    body = source[match.start():end]
    body = re.sub(r'#ifdef VIPER_WII_SPLIT_RAM_HELPERS\n(.*?)#else\n(.*?)#endif',
                  lambda m: m[2], body, flags=re.S)
    original_lines = []
    skipped = 0
    for line in body.splitlines():
        if line.startswith('#if defined(VIPER_WII_F64_RAM)'):
            assert not skipped
            skipped = 1
        elif skipped:
            if line.startswith('#if'):
                skipped += 1
            elif line.startswith('#endif'):
                skipped -= 1
        else:
            original_lines.append(line)
    assert not skipped
    body = '\n'.join(original_lines)
    for item in names:
        body = re.sub(r'\b' + item + r'\b', 'ref_' + item, body)
    reference += body + '\n'

code = r'''
#include "ppc_rt.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
uint8_t *g_ram;
struct Event {uint32_t ea,value,width,write;};
static struct Event events[16],saved[16];
static unsigned used,seed;
static void record(uint32_t ea,uint32_t value,unsigned width,unsigned write){
 assert(used<16);events[used++]=(struct Event){ea,value,width,write};
}
static uint32_t bus(uint32_t ea,unsigned width){
 uint32_t v=ea*0x517cc1b7u+seed+used;record(ea,v,width,0);
 return v&(width==1?255:width==2?65535:~0u);
}
uint32_t rt_mmio_r8(uint32_t ea){return bus(ea,1);}
uint32_t rt_mmio_r16(uint32_t ea){return bus(ea,2);}
uint32_t rt_mmio_r32(uint32_t ea){return bus(ea,4);}
void rt_mmio_w8(uint32_t ea,uint32_t v){record(ea,v,1,1);}
void rt_mmio_w16(uint32_t ea,uint32_t v){record(ea,v,2,1);}
void rt_mmio_w32(uint32_t ea,uint32_t v){record(ea,v,4,1);}
void rt_memory_access(uint32_t ea,unsigned width,int write){record(ea,0,width,write+2);}
''' + reference + r'''
static uint32_t rng=0x5216589u;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
__attribute__((noinline)) static uint64_t reference_op(unsigned op,uint32_t ea,uint64_t v){
 if(op==0)return FPR_BITS(ref_LDF32(ea));
 if(op==1)return FPR_BITS(ref_LDF64(ea));
 if(op==2)ref_STF32(ea,BITS_FPR(v));else ref_STF64(ea,BITS_FPR(v));
 return 0;
}
__attribute__((noinline)) static uint64_t candidate_op(unsigned op,uint32_t ea,uint64_t v){
 if(op==0)return FPR_BITS(LDF32(ea));
 if(op==1)return FPR_BITS(LDF64(ea));
 if(op==2)STF32(ea,BITS_FPR(v));else STF64(ea,BITS_FPR(v));
 return 0;
}
static void check(void){
 uint8_t *a=malloc(RAM_SIZE),*b=malloc(RAM_SIZE);assert(a&&b);
 for(unsigned i=0;i<RAM_SIZE;i++)a[i]=b[i]=(uint8_t)(i*13+(i>>9));
 const uint32_t addresses[]={0,1,3,7,RAM_SIZE-8,RAM_SIZE-7,RAM_SIZE-4,
  RAM_SIZE-3,RAM_SIZE-2,RAM_SIZE-1,RAM_SIZE,RAM_LIMIT-8,RAM_LIMIT-7,
  RAM_LIMIT-4,RAM_LIMIT-3,RAM_LIMIT-2,RAM_LIMIT-1,RAM_LIMIT,0x80000001,0xfffffffc,0xffffffff};
 const uint32_t floats[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,
  0xff7fffff,0x7f800000,0xff800000,0x7fc00001,0x7fa00001};
 const uint64_t doubles[]={0,0x8000000000000000ULL,1,0x000fffffffffffffULL,
  0x0010000000000000ULL,0x7fefffffffffffffULL,0xffefffffffffffffULL,
  0x7ff0000000000000ULL,0xfff0000000000000ULL,0x7ff8000000000001ULL,
  0x7ff0000000000001ULL,0x3ff0000010000000ULL};
 unsigned count=0,fast=0;
 for(unsigned n=0;n<1024;n++)for(unsigned rn=0;rn<4;rn++)for(unsigned op=0;op<4;op++){
  uint32_t ea=n<sizeof addresses/sizeof addresses[0]?addresses[n]:next()&(n%3?0x01ffffff:~0u);
  uint64_t v=n<sizeof doubles/sizeof doubles[0]?doubles[n]:((uint64_t)next()<<32)|next();
  if(op<2&&ea<RAM_LIMIT){
   uint64_t bits=op==0?floats[n%(sizeof floats/sizeof floats[0])]:doubles[n%(sizeof doubles/sizeof doubles[0])];
   unsigned width=op==0?4:8;
   for(unsigned i=0;i<width;i++){uint32_t addr=ea+i;if(addr<RAM_LIMIT)a[addr&RAM_MASK]=b[addr&RAM_MASK]=(uint8_t)(bits>>(8*(width-1-i)));}
  }
  seed=n;used=0;g_ram=a;SET_FLAGS(rn,n);
  uint64_t r=reference_op(op,ea,v);uint32_t flags=GET_FLAGS();
  unsigned size=used;memcpy(saved,events,sizeof events);
  used=0;g_ram=b;SET_FLAGS(rn,n);
  uint64_t c=candidate_op(op,ea,v);uint32_t current=GET_FLAGS();
#ifdef VIPER_WII_F64_RAM
  if((op==1||op==3)&&ea<RAM_LIMIT&&(ea&RAM_MASK)<=RAM_MASK-7&&
     !((uintptr_t)(b+(ea&RAM_MASK))&7))fast++;
#endif
  if(r!=c||flags!=current||used!=size||memcmp(saved,events,used*sizeof events[0])){
   printf("SPLIT FP mismatch n=%u rn=%u op=%u ea=%08lx flags=%08lx/%08lx\n",n,rn,op,(unsigned long)ea,(unsigned long)flags,(unsigned long)current);assert(0);
  }
  for(unsigned i=0;i<8;i++){uint32_t addr=ea+i;if(addr<RAM_LIMIT)assert(a[addr&RAM_MASK]==b[addr&RAM_MASK]);}
  if(!(n&63))assert(!memcmp(a,b,RAM_SIZE));count++;
 }
 assert(!memcmp(a,b,RAM_SIZE));free(a);free(b);
 printf("VIPER WII SPLIT FLOAT MEMORY PASS cases=%u; values/full RAM/audit/MMIO/FP flags\n",count);
#ifdef VIPER_WII_F64_RAM
 assert(fast>0&&fast<count);printf("F64 aligned RAM admitted=%u; native flags/MMIO unchanged\n",fast);
#endif
}
int main(void){check();return 0;}
'''
if args.emit_native:
    prefix = r'''
#include <gccore.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
static void *probe_malloc(size_t size){
 uintptr_t p=((uintptr_t)SYS_GetArena2Lo()+31)&~(uintptr_t)31;
 size=(size+31)&~(size_t)31;
 if(p>(uintptr_t)SYS_GetArena2Hi()||size>(uintptr_t)SYS_GetArena2Hi()-p)return NULL;
 SYS_SetArena2Lo((void *)(p+size));return (void *)p;
}
static void probe_free(void *p){(void)p;}
static uint32_t get_flags(void){double f;uint64_t bits;__asm__ volatile("mffs %0":"=f"(f));memcpy(&bits,&f,8);return (uint32_t)bits;}
static void set_flags(unsigned rn,unsigned n){uint64_t bits=(n&1?0x82000000u:0u)|rn;double f;memcpy(&f,&bits,8);__asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");}
#define malloc probe_malloc
#define free probe_free
#define SET_FLAGS(rn,n) set_flags(rn,n)
#define GET_FLAGS() get_flags()
'''
    code = code.replace('int main(void){check();return 0;}', r'''
int main(void){VIDEO_Init();GXRModeObj *m=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));console_init(fb,20,20,m->fbWidth,m->xfbHeight,m->fbWidth*2);
 VIDEO_Configure(m);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 check();printf("NATIVE FULL FPSCR PASS\n");for(;;)VIDEO_WaitVSync();}
''')
    args.emit_native.parent.mkdir(parents=True, exist_ok=True)
    if args.f64_ram:
        prefix = '#define VIPER_WII_F64_RAM\n#undef VIPER_MEMORY_AUDIT\n' + prefix
    args.emit_native.write_text(prefix + code)
else:
    prefix = '''#include <fenv.h>
static void set_flags(unsigned rn,unsigned n){(void)n;int rounds[]={FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD};fesetround(rounds[rn]);feclearexcept(FE_ALL_EXCEPT);}
#define SET_FLAGS(rn,n) set_flags(rn,n)
#define GET_FLAGS() ((unsigned)fetestexcept(FE_ALL_EXCEPT))
'''
    with tempfile.TemporaryDirectory(prefix='ram-float-') as directory:
        p = Path(directory)
        (p / 'test.c').write_text(prefix + code)
        flags = ['-DVIPER_WII_F64_RAM'] if args.f64_ram else ['-DVIPER_MEMORY_AUDIT']
        subprocess.run(['clang', '-O2', '-std=c11', '-DVIPER_WII_SPLIT_RAM_HELPERS',
                        *flags, '-frounding-math', '-ffp-contract=off',
                        '-fsanitize=address,undefined', '-I' + str(root / 'runtime'),
                        str(p / 'test.c'), str(root / 'wii/ram_access_cold.c'),
                        '-o', str(p / 'test')], check=True)
        subprocess.run([str(p / 'test')], check=True)
