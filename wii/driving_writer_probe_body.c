
#include <fenv.h>
#include <errno.h>
uint8_t *g_ram;
static unsigned allowed=1,pattern;
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
 for(unsigned i=0;i<16;i++)g_ram[RAM_SIZE-16+i]=(uint8_t)(i*19+pattern*31);
 for(unsigned i=0x3000;i<0x3400;i++)g_ram[i]=(uint8_t)(i*13+pattern*37);
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
 for(unsigned round=0;round<4;round++)for(unsigned mode=0;mode<12;mode++)for(unsigned values=0;values<8;values++){
  PPCContext a={0};a.r[20]=4;a.r[19]=8;a.r[18]=12;
  a.r[9]=mode==1?0x33c0u:mode==0?0x84001804u:0x84001ff0u;
  a.r[12]=mode==5?0x80000000u:mode==9?0x3001u:mode==10?RAM_SIZE-16u:mode==11?0x3300u:0x3000u;
  a.r[7]=0x01020304;a.r[0]=0xabcdef89;a.r[5]=0x0000803f;
  a.r[4]=0x00000080;a.r[3]=0xffffffff;a.r[23]=0xa5a5a5a5;
  if(values==1)a.r[20]=5;
  if(values==2)a.r[19]=9;
  if(values==3)a.r[18]=13;
  pattern=values;
  PPCContext b=a;reset(mode);
  unsigned before=admissions;
  int eligible=allowed&&a.r[20]==4&&a.r[19]==8&&a.r[18]==12&&a.r[12]<=RAM_SIZE-16u&&wii_voodoo_bulk_writer_ready(a.r[9],10);
  fast+=!!eligible;
  assert(!fesetround(rounds[round]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  reference(&a);int flags=fetestexcept(FE_ALL_EXCEPT),err=errno;save();
  reset(mode);assert(!fesetround(rounds[round]));feclearexcept(FE_ALL_EXCEPT);errno=0;
  candidate(&b);assert(admissions-before==(unsigned)eligible);assert(flags==fetestexcept(FE_ALL_EXCEPT)&&err==errno);
  assert(!memcmp(&a,&b,sizeof a));compare();comparisons++;
 }

 unsigned aliases=0;
 for(unsigned rn=0;rn<4;rn++)for(unsigned location=0;location<3;location++){
  PPCContext initial={0},expected_context;
  unsigned offset=location==2?(RAM_SIZE-sizeof(PPCContext))&~7u:0x3000u;
  initial.r[20]=4;initial.r[19]=8;initial.r[18]=12;
  initial.r[9]=0x84001804u;initial.r[12]=offset+(location==0?0u:28u);
  initial.r[7]=0x12345678;initial.r[23]=0xabcdef01;
  PPCContext *context=(PPCContext *)(void *)(g_ram+offset);
  reset(0);memcpy(context,&initial,sizeof initial);
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_NATIVE_DRIVING_PROBE
  uint32_t incoming=0x82000000u|(uint32_t[]){0,3,2,1}[rn];
  probe_set_fpscr(incoming);
#endif
  reference(context);
#ifdef VIPER_NATIVE_DRIVING_PROBE
  uint32_t full=probe_fpscr();
#endif
  int flags=fetestexcept(FE_ALL_EXCEPT),err=errno;
  memcpy(&expected_context,context,sizeof expected_context);save();
  reset(0);memcpy(context,&initial,sizeof initial);
  assert(!fesetround(rounds[rn]));feclearexcept(FE_ALL_EXCEPT);errno=0;
#ifdef VIPER_NATIVE_DRIVING_PROBE
  probe_set_fpscr(incoming);
#endif
  unsigned before=admissions;candidate(context);
#ifdef VIPER_NATIVE_DRIVING_PROBE
  assert(full==probe_fpscr());
#endif
  assert(admissions==before+1);
  assert(flags==fetestexcept(FE_ALL_EXCEPT)&&err==errno);
  assert(!memcmp(&expected_context,context,sizeof expected_context));compare();aliases++;
 }
 printf("Second-writer context-in-RAM PASS cases=%u\n",aliases);
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
 printf("Second vertex-writer bulk differential PASS: %u cases (%u fast), RAM/VRAM/presence/versions/header/CPU/FP flags/errno\n",comparisons,fast);
 return 0;
}
