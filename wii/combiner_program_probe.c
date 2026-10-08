/* Real installed SDK full-shadow proof; symbol exposure is probe-only. */
#include <gccore.h>
#include <stdio.h>
#include <string.h>
extern uint32_t __gxregs[0x598/4];
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static uint32_t initial[0x598/4],reference[0x598/4];
static unsigned first_word;static uint32_t first_expected,first_actual;
static void prefix_snapshot_initial(void){memcpy(initial,__gxregs,sizeof initial);}
static void prefix_snapshot_reference(void){memcpy(reference,__gxregs,sizeof reference);}
static void prefix_restore_initial(void){memcpy(__gxregs,initial,sizeof initial);}
static int prefix_shadow_equal(void){
 for(unsigned i=0;i<sizeof reference/sizeof reference[0];i++)if(__gxregs[i]!=reference[i]){
  first_word=i*4;first_expected=reference[i];first_actual=__gxregs[i];return 0;
 }
 return 1;
}
static unsigned char textures[2][64*64*4] ATTRIBUTE_ALIGN(32);
static void prefix_backend_prepare(unsigned i,unsigned draw){
 for(unsigned unit=0;unit<2;unit++){
  unsigned w=16u<<((i+draw+unit)%3),h=16u<<((i+2*draw+unit)%3);
  GXTexObj tex;GX_InitTexObj(&tex,textures[unit],w,h,GX_TF_RGBA8,
   (i+draw)&1?GX_REPEAT:GX_CLAMP,(i+draw)&2?GX_REPEAT:GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,(i+draw)&1?GX_NEAR:GX_LINEAR,(i+draw)&1?GX_NEAR:GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
  GX_LoadTexObj(&tex,unit?GX_TEXMAP3:GX_TEXMAP0);
  Mtx matrix={{1.0f/w,0,0,(float)(i&7)},{0,1.0f/h,0,(float)(draw&3)},{0,0,0,1}};
  GX_LoadTexMtxImm(matrix,unit?GX_TEXMTX2:GX_TEXMTX0,GX_MTX3x4);
  GX_SetTexCoordGen(unit?GX_TEXCOORD2:GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,unit?GX_TEXMTX2:GX_TEXMTX0);
 }
 GX_SetNumTexGens(3);
}
static void prefix_backend_flush(void){GX_DrawDone();}
#include "gx_tmu_prefix_cache.h"
#include "tmu_prefix_reference.h"
#define PREFIX_REFERENCE_ORIGINAL prefix_source_original
#include "combiner_program_probe_common.h"
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_DrawDone();
 unsigned failures,dual;uint64_t hits=0,misses=0;
 unsigned cases=prefix_proof(16384,&failures,&dual,&hits,&misses);
 GX_DrawDone();
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 int pass=!failures&&cases>10000&&dual>1000&&hits>1000&&misses>1000;
 printf("VIPER WII COMBINER PROGRAM SDK SHADOW %s\n",pass?"PASS":"FAIL");
 printf("cases=%u failures=%u dual=%u hits=%llu misses=%llu\n",cases,failures,dual,(unsigned long long)hits,(unsigned long long)misses);
 if(failures)printf("shadow offset=%03x expected=%08x actual=%08x\n",first_word,first_expected,first_actual);
 printf("Complete0x598-byte SDK shadow; GPU output proof separate\n");
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
