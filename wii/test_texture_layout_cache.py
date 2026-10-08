#!/usr/bin/env python3
"""Prove cached descriptors against the real masked TMU write/reset paths."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
harness=r'''
static WiiTextureLayouts layouts;
static unsigned notifications[2];
static void invalidate(unsigned unit){assert(unit<2);notifications[unit]++;wii_texture_layout_invalidate(&layouts,unit);}
static uint32_t rng=0x798172ab;
static uint32_t next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void compare(void){
 for(unsigned unit=0;unit<2;unit++){
  NativeMip original[9];native_texture_layout(tmu_regs[unit],original);
  const NativeMip *actual=wii_texture_layout_get(&layouts,unit,tmu_regs[unit]);
  assert(!memcmp(original,actual,sizeof original));
  assert(actual==wii_texture_layout_get(&layouts,unit,tmu_regs[unit]));
 }
}
int main(void){
 device_contract_main();vram=calloc(1,WII_VOODOO_VRAM_BYTES);assert(vram);
 wii_voodoo_set_layout_invalidate(invalidate);compare();
 unsigned checks=0;
 for(unsigned n=0;n<200000;n++){
  unsigned word=next()%64,chips=(next()%4)<<1;
  uint32_t value=next(),mask=next();
  if(n%7==0)mask=0;
  if(n%11==0)mask=~0u;
  uint32_t before[2]={tmu_regs[0][word],tmu_regs[1][word]};
  unsigned calls[2]={notifications[0],notifications[1]};
  voodoo_reg_write(0x200000u|((0xc0u+word)<<2)|(chips<<10),value,mask);
  for(unsigned unit=0;unit<2;unit++){
   unsigned changed=before[unit]!=tmu_regs[unit][word]&&wii_texture_layout_word(word);
   assert(notifications[unit]==calls[unit]+changed);
   if(!changed)assert(layouts.valid[unit]);else assert(!layouts.valid[unit]);
  }
  compare();checks++;
  if(n%127==0){voodoo_init();assert(!layouts.valid[0]&&!layouts.valid[1]);compare();}
  if(n%251==0){wii_voodoo_set_renderer(NULL,NULL);assert(!layouts.valid[0]&&!layouts.valid[1]);compare();}
 }
 /* FIFO type1 must go through the same TMU notifier. */
 voodoo_init();compare();
 voodoo_reg_write(0x80000+8*4,1,~0u);voodoo_reg_write(0x80000+9*4,256,~0u);
 voodoo_reg_write(0x80000+11*4,0x1000,~0u);
 uint32_t header=1u|(0xc0u<<3)|(1u<<16)|(2u<<11);
 voodoo_lfb_write(0x1000,header,~0u);voodoo_lfb_write(0x1004,0x00000800u,~0u);
 assert(tmu_regs[0][0]==0x00000800u);assert(!layouts.valid[0]);compare();
 wii_voodoo_set_layout_invalidate(NULL);
 voodoo_reg_write(0x200000u|(0xc4u<<2)|(2u<<10),0x00321000u,~0u);
 wii_voodoo_set_layout_invalidate(invalidate);
 assert(!layouts.valid[0]&&!layouts.valid[1]);compare();
 puts("Texture layout cache PASS: 200000 masked device writes, both units, resets/backend/FIFO and exact nine-level descriptors");
 free(vram);vram=NULL;
}
'''
with tempfile.TemporaryDirectory(prefix='viper-layout-cache-') as d:
 p=Path(d);(p/'test.c').write_text('#define VIPER_WII_TEXTURE_LAYOUT_CACHE\n'+fixture+harness)
 flags=['clang','-O2','-std=c11','-fsanitize=address,undefined','-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures')]
 subprocess.run(flags+[str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
