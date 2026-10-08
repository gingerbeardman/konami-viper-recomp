#!/usr/bin/env python3
"""Compare actual bulk publication with each original byte/version/bit update."""
from pathlib import Path
import argparse
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
parser=argparse.ArgumentParser();parser.add_argument('--emit-native',type=Path);args=parser.parse_args()
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
proof=r'''
static uint8_t saved_bytes[1024],saved_presence[sizeof fifo_present];
static uint32_t saved_versions[2048];
static unsigned proof(void){
 device_contract_main();vram=calloc(1,WII_VOODOO_VRAM_BYTES);assert(vram);
 unsigned cases=0;
 for(unsigned start=0;start<32;start++)for(unsigned length=1;length<=150;length++)
 for(unsigned mode=0;mode<4;mode++){
  unsigned off=mode==3?0x800000-4*length:0x1000-64+4*start;
  unsigned base=off&~31u;uint32_t words[150];
  for(unsigned i=0;i<length;i++)words[i]=0xa19b0000u+i*0x10213u+start;
  memset(vram+base,0xa5,off+4*length-base+32<=0x800000-base?off+4*length-base+32:0x800000-base);
  memset(fifo_present,0x5a,sizeof fifo_present);
  for(unsigned i=0;i<2048;i++)vram_versions[i]=0xfffffff0u+i;
  fifo_hdr_valid=mode!=0;fifo_hdr_pc=mode==1?off-4:mode==2?off+4:0;
  unsigned hdr=fifo_hdr_pc,valid=fifo_hdr_valid;
  for(unsigned i=0;i<length;i++){vram_write(off/4+i,__builtin_bswap32(words[i]));fifo_mark(off+4*i,1);}
  unsigned expected_valid=fifo_hdr_valid;
  unsigned bytes=off+4*length-base+32;if(bytes>0x800000-base)bytes=0x800000-base;
  memcpy(saved_bytes,vram+base,bytes);memcpy(saved_presence,fifo_present,sizeof fifo_present);
  memcpy(saved_versions,vram_versions,sizeof vram_versions);
  memset(vram+base,0xa5,bytes);memset(fifo_present,0x5a,sizeof fifo_present);
  for(unsigned i=0;i<2048;i++)vram_versions[i]=0xfffffff0u+i;
  fifo_hdr_valid=valid;fifo_hdr_pc=hdr;
  wii_voodoo_bulk_writer_be(0x84000000u+off,words,length);
  assert(fifo_hdr_valid==(int)expected_valid&&fifo_hdr_pc==hdr);
  assert(!memcmp(saved_bytes,vram+base,bytes));
  assert(!memcmp(saved_presence,fifo_present,sizeof fifo_present));
  assert(!memcmp(saved_versions,vram_versions,sizeof vram_versions));cases++;
 }
 free(vram);vram=NULL;return cases;
}
'''
prefix='#define VIPER_WII_BULK_WRITER\n#define VIPER_WII_BULK_PUBLISH\n'
if args.emit_native:
    main=r'''
#include <gccore.h>
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();if(mode->viTVMode&VI_NON_INTERLACE)VIDEO_WaitVSync();
 printf("VIPER WII BULK PUBLISH DIFFERENTIAL\n");unsigned n=proof();printf("VIPER WII BULK PUBLISH %s cases=%u\n",n==19200?"PASS":"FAIL",n);for(;;)VIDEO_WaitVSync();
}
'''
    args.emit_native.parent.mkdir(parents=True,exist_ok=True);args.emit_native.write_text(prefix+fixture+proof+main)
else:
    with tempfile.TemporaryDirectory(prefix='viper-bulk-publish-') as d:
        p=Path(d);(p/'test.c').write_text(prefix+fixture+proof+'int main(void){unsigned n=proof();assert(n==19200);printf("Bulk publish PASS %u byte/version/presence/header cases\\n",n);}\n')
        flags=['clang','-O2','-std=c11','-fsanitize=address,undefined',
               '-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures')]
        subprocess.run(flags+[str(p/'test.c'),'-lm','-o',str(p/'test')],check=True)
        subprocess.run([str(p/'test')],check=True)
