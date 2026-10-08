"""Differentially execute the real hot PPC block and its bulk specialization."""
from pathlib import Path
import argparse
import subprocess
import tempfile
import re
from specialize_submission import regions

root = Path(__file__).resolve().parent.parent
source = (root / 'generated/gticlub2/gl_000.c').read_text()
_, _, original, replacement = regions(source)
fixture = (root / 'wii/voodoo_headless_test.c').read_text().replace(
    'int main(void)', 'void device_contract_main(void)')
harness = r'''
#include <fenv.h>
#include <errno.h>
uint8_t *g_ram;
static unsigned allowed=1;
int rt_wii_bulk_lfb_allowed(void){return allowed;}
void rt_mmio_w32(uint32_t ea,uint32_t value){
 assert(ea>=0x84000000u&&ea<0x86000000u);
 voodoo_lfb_write(ea-0x84000000u,__builtin_bswap32(value),~0u);
}
void rt_mmio_w8(uint32_t ea,uint32_t value){(void)ea;(void)value;assert(0);}
uint32_t rt_mmio_r32(uint32_t ea){(void)ea;return 0;}
uint32_t rt_mmio_r8(uint32_t ea){(void)ea;return 0;}
static unsigned char *saved_vram,*saved_ram;
static uint32_t saved_versions[2048],saved_agp[128];
static uint8_t saved_presence[sizeof fifo_present];
static unsigned saved_valid,saved_pc,saved_count,saved_checked;
static uint32_t saved_cmd;
static void reset(unsigned mode){
 voodoo_init();memset(g_ram,0,RAM_SIZE);
 agp[8]=1;agp[9]=256|3;agp[11]=0x1800;
 if(mode==2)swap_pending=1;
 if(mode==3){
  vram_write(0x1800/4,3|(1<<3)|(15<<6)|(59<<10));fifo_mark(0x1800,1);
 }
 if(mode==4)agp[9]&=~256u;
 if(mode==6)for(unsigned i=0;i<2048;i++)vram_versions[i]=0xfffffff8u;
 if(mode==7)allowed=0;else allowed=1;
 for(unsigned i=0;i<64;i++)ST32(0x3000+i*4,0x3f000000u+i*157);
 for(unsigned i=0;i<20;i++)ST32(0x3398+i*4,0x3e800000u+i*971);
}
static void save(void){
 memcpy(saved_vram,vram,WII_VOODOO_VRAM_BYTES);memcpy(saved_ram,g_ram,RAM_SIZE);
 memcpy(saved_versions,vram_versions,sizeof saved_versions);memcpy(saved_agp,agp,sizeof agp);
 memcpy(saved_presence,fifo_present,sizeof saved_presence);
 saved_valid=fifo_hdr_valid;saved_pc=fifo_hdr_pc;saved_count=fifo_hdr_count;
 saved_checked=fifo_hdr_checked;saved_cmd=fifo_hdr_cmd;
}
static void compare(void){
 assert(!memcmp(saved_vram,vram,WII_VOODOO_VRAM_BYTES));
 assert(!memcmp(saved_ram,g_ram,RAM_SIZE));
 assert(!memcmp(saved_versions,vram_versions,sizeof saved_versions));
 assert(!memcmp(saved_agp,agp,sizeof agp));
 assert(!memcmp(saved_presence,fifo_present,sizeof saved_presence));
 assert(saved_valid==(unsigned)fifo_hdr_valid&&saved_pc==fifo_hdr_pc&&saved_count==fifo_hdr_count);
 assert(saved_checked==fifo_hdr_checked&&saved_cmd==fifo_hdr_cmd);
}
int main(void){
 device_contract_main();vram=calloc(1,WII_VOODOO_VRAM_BYTES);
 g_ram=calloc(1,RAM_SIZE);saved_vram=malloc(WII_VOODOO_VRAM_BYTES);saved_ram=malloc(RAM_SIZE);
 assert(vram&&g_ram&&saved_vram&&saved_ram);
 unsigned rounds[]={FE_TONEAREST,FE_DOWNWARD,FE_UPWARD,FE_TOWARDZERO};
 unsigned comparisons=0,fast=0;
 for(unsigned round=0;round<4;round++)for(unsigned mode=0;mode<9;mode++)for(unsigned values=0;values<4;values++){
  PPCContext a={0};a.r[15]=0x3398;a.r[14]=0x33ac;a.r[19]=4;a.r[18]=8;a.r[17]=12;a.r[16]=16;
  a.r[25]=mode==0?0x84001804u:mode==1?0x33c0u:0x84001ff0u;
  a.r[31]=mode==5?0x80000000u:0x3000u;
  a.r[12]=0x01020304;a.r[11]=0xabcdef89;a.r[10]=0x0000803f;
  a.r[9]=0x00000080;a.r[8]=0xffffffff;a.r[13]=0xa5a5a5a5;
  for(unsigned i=0;i<32;i++)a.f[i]=(double)(i+1)*0.03125;
  if(values==1)a.f[19]=INFINITY;
  if(values==2)a.f[22]=NAN;
  if(values==3){a.f[2]=-0.;a.f[20]=-INFINITY;}
  PPCContext b=a;reset(mode);
  int eligible=allowed&&a.r[31]<=RAM_SIZE-0x20u&&wii_voodoo_bulk_writer_ready(a.r[25],10);
  fast+=!!eligible;
  assert(!fesetround(rounds[round]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  reference(&a);int flags=fetestexcept(FE_ALL_EXCEPT),err=errno;save();
  reset(mode);assert(!fesetround(rounds[round]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  candidate(&b);assert(flags==fetestexcept(FE_ALL_EXCEPT)&&err==errno);
  assert(!memcmp(&a,&b,sizeof a));compare();comparisons++;
 }
 reset(0);
 assert(!wii_voodoo_bulk_writer_ready(0x84001801,10));
 assert(!wii_voodoo_bulk_writer_ready(0x84001800,10));
 assert(!wii_voodoo_bulk_writer_ready(0x840017fc,10));
 assert(!wii_voodoo_bulk_writer_ready(0x84004ffc,10));
 assert(!wii_voodoo_bulk_writer_ready(0x84800000,10));
 assert(!wii_voodoo_bulk_writer_ready(0x84001804,0));
 assert(!wii_voodoo_bulk_writer_ready(0x84001804,151));
 assert(fast>0&&fast<comparisons);
 free(vram);free(g_ram);free(saved_vram);free(saved_ram);
 printf("Actual hot-block bulk differential PASS: %u cases (%u fast), RAM/VRAM/presence/versions/header/CPU/FP flags/errno\n",comparisons,fast);
 return 0;
}
'''
code_text = ('#include "bulk_ram.h"\n' + fixture + '\nvoid reference(PPCContext *c){\n' + original + '}\n'
             + 'void candidate(PPCContext *c){\n' + replacement + '}\n' + harness)
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--emit-native', type=Path)
parser.add_argument('--bulk-publish', action='store_true')
parser.add_argument('--bulk-ram', action='store_true')
parser.add_argument('--bulk-noalias', action='store_true')
parser.add_argument('--staging-guard-or', action='store_true')
parser.add_argument('--bulk-admission-diagnostic', action='store_true')
parser.add_argument('--incomplete-header', action='store_true')
args = parser.parse_args()
if args.bulk_admission_diagnostic:
    code_text = '#define VIPER_WII_BULK_ADMISSION_DIAGNOSTIC\n' + code_text
    code_text = code_text.replace('void candidate(PPCContext *c){',
        'void wii_bulk_admission_observe(unsigned site,unsigned reason){if(site>=2||reason>=6)__builtin_trap();}\nvoid candidate(PPCContext *c){',1)
if args.staging_guard_or:
    code_text = '#define VIPER_WII_STAGING_GUARD_OR\n' + code_text
if args.bulk_noalias and not args.bulk_ram:
    parser.error('--bulk-noalias requires --bulk-ram')
if args.bulk_ram:
    code_text = '#define VIPER_WII_BULK_RAM\n' + code_text
    code_text=code_text.replace('static unsigned allowed=1;', 'static unsigned allowed=1,ram_pattern;')
    code_text=code_text.replace('for(unsigned i=0;i<20;i++)ST32(0x3398+i*4,0x3e800000u+i*971);',
      'for(unsigned i=0;i<20;i++)ST32(0x3398+i*4,0x3e800000u+i*971);\n'
      ' if(ram_pattern>=4){const uint32_t edge[]={0,0x80000000,1,0x807fffff,0x00800000,0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,0x7fc00001,0x7fa00001};\n'
      ' for(unsigned i=0x3000;i<0x3500;i+=4)ST32(i,edge[(i/4+ram_pattern)%11]);}')
    code_text=code_text.replace('mode<9','mode<12').replace('values<4','values<8')
    code_text=code_text.replace('PPCContext b=a;reset(mode);',
      'if(mode==9)a.r[31]=0x3001; if(mode==10)a.r[31]=0x3398; if(mode==11)a.r[31]=0x33b8;\n'
      ' ram_pattern=values; PPCContext b=a;reset(mode);')
if args.incomplete_header:
    if not args.bulk_ram or not args.bulk_publish:
        parser.error('--incomplete-header requires --bulk-ram and --bulk-publish')
    code_text = '#define VIPER_WII_BULK_INCOMPLETE_HEADER\n' + code_text
    code_text=code_text.replace('mode<12','mode<18')
    code_text=code_text.replace('static uint32_t saved_cmd;',
        'static uint32_t saved_cmd,saved_regs[256];static WiiVoodooStats saved_stats;')
    code_text=code_text.replace('saved_valid=fifo_hdr_valid;',
        'memcpy(saved_regs,regs,sizeof regs);saved_stats=counters;\n saved_valid=fifo_hdr_valid;')
    code_text=code_text.replace('assert(saved_valid==',
        'assert(!memcmp(saved_regs,regs,sizeof regs));assert(!memcmp(&saved_stats,&counters,sizeof counters));\n assert(saved_valid==')
    code_text=code_text.replace('for(unsigned i=0;i<64;i++)ST32',
        'if(mode>=12){\n'
        ' unsigned payload=mode==15?10:20;uint32_t header=1|(65<<3)|(payload<<16);\n'
        ' vram_write(0x1800/4,header);fifo_mark(0x1800,1);fifo_run();\n'
        ' if(mode==13){vram_write(0x1804/4,0x12345678);fifo_mark(0x1804,1);fifo_run();}\n'
        ' if(mode==14){vram_write((0x1800+payload*4)/4,0x87654321);fifo_mark(0x1800+payload*4,1);}\n'
        ' if(mode==16)swap_pending=1;\n'
        ' if(mode==17)fifo_hdr_pc+=4;\n'
        '}\n for(unsigned i=0;i<64;i++)ST32',1)
    code_text=code_text.replace('ram_pattern=values; PPCContext b=a;',
        'if(mode>=12)a.r[25]=0x84001804u+(mode==13?4:0);\n ram_pattern=values; PPCContext b=a;')
if args.bulk_publish:
    code_text = '#define VIPER_WII_BULK_PUBLISH\n' + code_text
if args.bulk_noalias:
    # Instrument the actual generated restrict admission, never a replacement
    # oracle. Emission fails until the candidate generator exists and has one
    # unambiguous scoped admission. Ordinary probes are unchanged.
    pattern=r'(PPCContext\s*\*\s*restrict\s+\w+\s*=\s*c\s*;)'
    code_text,hits=re.subn(pattern,r'\1 noalias_hits++;',code_text)
    if hits!=1:raise ValueError('Expected one actual scoped restrict admission')
    code_text='#define VIPER_WII_BULK_NOALIAS\n'+code_text
    code_text=code_text.replace('void candidate(PPCContext *c){',
        'static unsigned noalias_hits;\nvoid candidate(PPCContext *c){')
    alias_proof=r'''
 unsigned aliases=0;
 for(unsigned rn=0;rn<4;rn++)for(unsigned location=0;location<3;location++)
 for(unsigned value=0;value<2;value++){
  unsigned offset=location==0?0x3000u:location==1?0x3398u:
      (RAM_SIZE-sizeof(PPCContext))&~7u;
  PPCContext initial={0},expected_context;
  initial.r[15]=0x3398;initial.r[14]=0x33ac;
  initial.r[19]=4;initial.r[18]=8;initial.r[17]=12;initial.r[16]=16;
  initial.r[25]=0x84001804u;initial.r[31]=0x3000u;
  for(unsigned i=0;i<32;i++)initial.f[i]=(double)(i+1)*0.03125;
  if(value){uint64_t bits=0x7ff0000000000001ull;memcpy(&initial.f[19],&bits,8);}
  reset(0);PPCContext *context=(PPCContext *)(void *)(g_ram+offset);
  memcpy(context,&initial,sizeof initial);
  assert(wii_voodoo_bulk_writer_ready(context->r[25],10));
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_NATIVE_BULK_NOALIAS_PROBE
  uint32_t incoming=0x82000000u|(uint32_t[]){0,3,2,1}[rn];
  probe_set_fpscr(incoming);
#endif
  reference(context);
#ifdef VIPER_NATIVE_BULK_NOALIAS_PROBE
  uint32_t full=probe_fpscr();
#endif
  int flags=fetestexcept(FE_ALL_EXCEPT),err=errno;
  memcpy(&expected_context,context,sizeof expected_context);save();
  reset(0);memcpy(context,&initial,sizeof initial);
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_NATIVE_BULK_NOALIAS_PROBE
  probe_set_fpscr(incoming);
#endif
  unsigned before_hits=noalias_hits;candidate(context);
#ifdef VIPER_NATIVE_BULK_NOALIAS_PROBE
  uint32_t current=probe_fpscr();
  if(full!=current){printf("Alias FPSCR mismatch RN=%u location=%u value=%u %08lx/%08lx\n",rn,location,value,(unsigned long)full,(unsigned long)current);assert(0);}
#endif
  assert(noalias_hits==before_hits);
  assert(flags==fetestexcept(FE_ALL_EXCEPT)&&err==errno);
  assert(!memcmp(&expected_context,context,sizeof expected_context));compare();aliases++;
 }
 assert(aliases==24&&noalias_hits>0);
 printf("Bulk noalias context-in-RAM fallback PASS cases=%u admissions=%u\n",aliases,noalias_hits);
'''
    code_text=code_text.replace(' reset(0);\n assert(!wii_voodoo_bulk_writer_ready',
        alias_proof+' reset(0);\n assert(!wii_voodoo_bulk_writer_ready',1)
if args.emit_native:
    prefix = r'''
#include <gccore.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
static void *probe_malloc(size_t size){
 uintptr_t p=((uintptr_t)SYS_GetArena2Lo()+31)&~(uintptr_t)31;
 size=(size+31)&~(size_t)31;
 if(size>(uintptr_t)SYS_GetArena2Hi()-p)return NULL;
 SYS_SetArena2Lo((void *)(p+size));return (void *)p;
}
static void *probe_calloc(size_t n,size_t size){
 if(size&&n>SIZE_MAX/size)return NULL;
 void *p=probe_malloc(n*size);if(p)memset(p,0,n*size);return p;
}
static void probe_free(void *p){(void)p;}
static uint32_t probe_fpscr(void){
 double f;uint64_t bits;
 __asm__ volatile("mffs %0":"=f"(f));memcpy(&bits,&f,8);return (uint32_t)bits;
}
static void probe_set_fpscr(uint32_t value){
 uint64_t bits=value;double f;memcpy(&f,&bits,8);
 __asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");
}
#define malloc probe_malloc
#define calloc probe_calloc
#define free probe_free
'''
    native = code_text.replace('int main(void){', 'int test_main(void){')
    native = native.replace('reference(&a);int flags=',
                            'uint32_t incoming=(values?0x82000000u:0u)|(uint32_t[]){0,3,2,1}[round];probe_set_fpscr(incoming);\n'
                            '  reference(&a);uint32_t full=probe_fpscr();int flags=')
    native = native.replace('candidate(&b);assert(flags==',
                            'probe_set_fpscr(incoming);candidate(&b);uint32_t current=probe_fpscr();\n'
                            '  if(full!=current){printf("FPSCR mismatch round=%u mode=%u values=%u ref=%08lx cand=%08lx\\n",round,mode,values,(unsigned long)full,(unsigned long)current);assert(0);}\n'
                            '  assert(flags==')
    # The audited block's RAM reads/writes are below0x3500 (or the explicit
    # MMIO fallback). Keep a complete low64KiB snapshot for this standalone
    # probe so its oracle fits alongside full8MiB VRAM in Wii MEM2. The game
    # run separately compares the complete16MiB RAM checkpoint.
    if args.bulk_noalias:
        # The guard covers the entire real RAM allocation. Reclaim only our
        # last owned allocation if the arena hasn't moved since it was made.
        # Never roll back an SDK allocation or shrink RAM behind a 16MiB proof.
        prefix='#define VIPER_NATIVE_BULK_NOALIAS_PROBE\n'+prefix
        prefix=prefix.replace('static void *probe_malloc(size_t size){',
            'static uintptr_t probe_last_begin,probe_last_end;\nstatic void *probe_malloc(size_t size){')
        prefix=prefix.replace('SYS_SetArena2Lo((void *)(p+size));return (void *)p;',
            'probe_last_begin=p;probe_last_end=p+size;\n SYS_SetArena2Lo((void *)(p+size));return (void *)p;')
        prefix=prefix.replace('static void probe_free(void *p){(void)p;}',
            'static void probe_free(void *p){\n'
            ' if((uintptr_t)p==probe_last_begin&&(uintptr_t)SYS_GetArena2Lo()==probe_last_end){\n'
            ' SYS_SetArena2Lo(p);probe_last_begin=probe_last_end=0;}}\n')
        prefix+=r'''
static void *probe_framebuffer;
static void probe_failed(unsigned line,const char *condition){
 printf("VIPER BULK NATIVE FAIL line=%u condition=%s\n",line,condition);fflush(stdout);
 VIDEO_SetNextFramebuffer(probe_framebuffer);VIDEO_SetBlack(FALSE);VIDEO_Flush();
 for(;;)VIDEO_WaitVSync();
}
#define probe_require(condition) do{if(!(condition))probe_failed(__LINE__,#condition);}while(0)
'''
        native=re.sub(r'\bassert\(', 'probe_require(',native)
    else:
        native = native.replace('calloc(1,RAM_SIZE)', 'calloc(1,65536)')
        native = native.replace('malloc(RAM_SIZE)', 'malloc(65536)')
        native = native.replace('g_ram,0,RAM_SIZE', 'g_ram,0,65536')
        native = native.replace('saved_ram,g_ram,RAM_SIZE', 'saved_ram,g_ram,65536')
    native += r'''
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);
 VIDEO_Flush();VIDEO_WaitVSync();if(mode->viTVMode&VI_NON_INTERLACE)VIDEO_WaitVSync();
 printf("VIPER WII BULK WRITER NATIVE PROBE\n");
 test_main();printf("VIPER WII BULK WRITER NATIVE FULL FPSCR PASS\n");
 for(;;)VIDEO_WaitVSync();
}
'''
    if args.bulk_noalias:
        native=native.replace('console_init(fb,20,20,mode->fbWidth',
            'probe_framebuffer=fb;console_init(fb,20,20,mode->fbWidth',1)
    args.emit_native.parent.mkdir(parents=True, exist_ok=True)
    args.emit_native.write_text(prefix + native)
    raise SystemExit(0)

with tempfile.TemporaryDirectory() as temp:
    directory = Path(temp)
    code = directory / 'test.c'
    code.write_text(code_text)
    binary = directory / 'test'
    alias_flags=['-fno-strict-aliasing'] if args.bulk_noalias else []
    subprocess.run(['clang', '-O2', '-std=c11', '-frounding-math', '-ffp-contract=off']+alias_flags+[
                    '-DVIPER_WII_BULK_WRITER', '-fsanitize=address,undefined',
                    '-I' + str(root / 'runtime'), '-I' + str(root / 'wii'),
                    '-I' + str(root / 'tests/fixtures'), str(code), '-lm', '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
