"""Exact original-source versus native TMU state frontend comparison."""
from pathlib import Path
import subprocess
import tempfile
from specialize_direct_state import regions
root=Path(__file__).resolve().parent.parent
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
_,_,original,replacement=regions((root/'generated/gticlub2/gl_000.c').read_text())
code=r'''
#include "ppc_rt.h"
#include <fenv.h>
#include <errno.h>
uint8_t *g_ram;
int rt_wii_bulk_lfb_allowed(void){return 1;}
void rt_mmio_w32(uint32_t ea,uint32_t v){voodoo_lfb_write(ea-0x84000000u,__builtin_bswap32(v),~0u);}
uint32_t rt_mmio_r32(uint32_t ea){(void)ea;assert(0);return 0;}
uint32_t rt_mmio_r8(uint32_t ea){(void)ea;assert(0);return 0;}
void rt_mmio_w8(uint32_t ea,uint32_t v){(void)ea;(void)v;assert(0);}
'''+'\nvoid reference(PPCContext *c){\n'+original+'}\nvoid candidate(PPCContext *c){\n'+replacement+'}\n'+r'''
static unsigned char *saved;
static unsigned char state_observed[65536];
static size_t state_observed_used;
static void observe(unsigned kind){
 assert(state_observed_used+sizeof tmu_regs+sizeof agp+64<sizeof state_observed);
 memcpy(state_observed+state_observed_used,&kind,4);state_observed_used+=4;
 memcpy(state_observed+state_observed_used,tmu_regs,sizeof tmu_regs);state_observed_used+=sizeof tmu_regs;
 memcpy(state_observed+state_observed_used,agp,sizeof agp);state_observed_used+=sizeof agp;
 memcpy(state_observed+state_observed_used,&fifo_hdr_valid,4);state_observed_used+=4;
 memcpy(state_observed+state_observed_used,&fifo_hdr_checked,4);state_observed_used+=4;
 memcpy(state_observed+state_observed_used,vram+agp[11],32);state_observed_used+=32;
}
#ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
static void layout_observe(unsigned t){observe(t);}
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
static void material_observe(void){observe(2);}
#endif
static size_t used;
static void check(const void *p,size_t n,int compare){
 if(compare){if(memcmp(saved+used,p,n)){fprintf(stderr,"state mismatch offset=%zu size=%zu\n",used,n);if(n==4){unsigned expected,actual;memcpy(&expected,saved+used,4);memcpy(&actual,p,4);fprintf(stderr," expected=%u actual=%u\n",expected,actual);}abort();}}else memcpy(saved+used,p,n);
 used+=n;
}
static void state(PPCContext *c,int compare){
 used=0;
 int round=fegetround(),flags=fetestexcept(FE_ALL_EXCEPT),err=errno;
 check(&round,sizeof round,compare);check(&flags,sizeof flags,compare);check(&err,sizeof err,compare);
 check(c,sizeof(*c),compare);check(g_ram,RAM_SIZE,compare);
 check(vram,WII_VOODOO_VRAM_BYTES,compare);
 check(fifo_present,sizeof fifo_present,compare);check(vram_versions,sizeof vram_versions,compare);
 check(agp,sizeof agp,compare);check(regs,sizeof regs,compare);check(tmu_regs,sizeof tmu_regs,compare);
 check(texture_palette,sizeof texture_palette,compare);check(palette_epoch,sizeof palette_epoch,compare);
 check(&fifo_hdr_valid,sizeof fifo_hdr_valid,compare);check(&fifo_hdr_pc,sizeof fifo_hdr_pc,compare);
 check(&fifo_hdr_count,sizeof fifo_hdr_count,compare);check(&fifo_hdr_checked,sizeof fifo_hdr_checked,compare);
 check(&fifo_hdr_cmd,sizeof fifo_hdr_cmd,compare);check(&counters,sizeof counters,compare);
 check(strip,sizeof strip,compare);check(&strip_count,sizeof strip_count,compare);
 check(&swap_pending,sizeof swap_pending,compare);
 check(&state_observed_used,sizeof state_observed_used,compare);check(state_observed,state_observed_used,compare);
}
static void prepare(unsigned pc,unsigned mode){
 voodoo_init();agp[8]=1;agp[9]=256|7;agp[11]=pc;
 voodoo_lfb_write(pc,0x78601u,~0u);
 assert(fifo_hdr_valid&&fifo_hdr_checked==1);
 if(mode==1)swap_pending=1;
 if(mode==2)voodoo_lfb_write(pc,0x78609u,~0u);
 if(mode==3)agp[11]=0x7000;
 if(mode==5)for(unsigned i=1;i<=3;i++)voodoo_lfb_write(pc+i*4,0x12340000+i,~0u);
 if(mode==6)voodoo_lfb_write(pc+28,0x11112222u,~0u);
 if(mode==7)agp[9]&=~256u;
 if(mode==8)agp[9]=256;
 for(unsigned i=0;i<2048;i++)vram_versions[i]=0xfffffffcu;
 state_observed_used=0;
}
int main(void){
 device_contract_main();vram=calloc(1,WII_VOODOO_VRAM_BYTES);g_ram=calloc(1,RAM_SIZE);
 saved=malloc(RAM_SIZE+WII_VOODOO_VRAM_BYTES+sizeof fifo_present+131072);assert(saved&&vram&&g_ram);
 #ifdef VIPER_WII_TEXTURE_LAYOUT_CACHE
 layout_invalidate=layout_observe;
#endif
#ifdef VIPER_WII_MATERIAL_PLAN_CACHE
 material_invalidate=material_observe;
#endif
 unsigned cases=0,fast=0;
 for(unsigned seed=0;seed<128;seed++)for(unsigned page=0;page<2;page++)for(unsigned mode=0;mode<9;mode++){
  unsigned pc=page?0x1ff0:0x1800;
  PPCContext a={0};
  for(unsigned i=0;i<32;i++)a.r[i]=(seed*0x71382659u+i*0x123bc897u);
  a.r[6]=0x84000000u+pc-8;a.r[8]=0x84000000u+pc+4;a.r[4]=0x84000000u+pc;
  a.fpscr=seed*0x91384763u;
  for(unsigned i=0;i<32;i++){uint64_t bits=(uint64_t)(seed*0x71938463u+i)<<32 | (seed*0x12738465u+i);memcpy(&a.f[i],&bits,8);}
  a.xer_ca=seed&1;a.xer_so=(seed>>1)&1;a.budget=1234567;
  PPCContext b=a;
  prepare(pc,mode);
  if(mode==4){uint32_t values[7]={a.r[25]|0x10241000u,a.r[27],0,a.r[29],a.r[26],a.r[30],a.r[31]};for(unsigned t=0;t<2;t++)memcpy(tmu_regs[t],values,sizeof values);}
  fast+=wii_gx_tmu_state_ready(a.r[8]);assert(!fesetround((int[]){FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD}[seed&3]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  reference(&a);state(&a,0);
  prepare(pc,mode);
  if(mode==4){uint32_t values[7]={b.r[25]|0x10241000u,b.r[27],0,b.r[29],b.r[26],b.r[30],b.r[31]};for(unsigned t=0;t<2;t++)memcpy(tmu_regs[t],values,sizeof values);}
  assert(!fesetround((int[]){FE_TONEAREST,FE_TOWARDZERO,FE_UPWARD,FE_DOWNWARD}[seed&3]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  candidate(&b);state(&b,1);cases++;
 }
 printf("Native TMU state original-source differential PASS cases=%u fast=%u\n",cases,fast);
 free(saved);free(g_ram);free(vram);return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    path=Path(temp);(path/'probe.c').write_text(fixture+code)
    subprocess.run(['clang','-O2','-std=c11','-frounding-math','-ffp-contract=off','-fsanitize=address,undefined','-DVIPER_WII_BULK_WRITER','-DVIPER_WII_BULK_PUBLISH','-DVIPER_WII_DIRECT_STATE','-DVIPER_WII_TEXTURE_LAYOUT_CACHE','-DVIPER_WII_MATERIAL_PLAN_CACHE','-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures'),str(path/'probe.c'),'-o',str(path/'probe')],check=True)
    subprocess.run([str(path/'probe')],check=True)
