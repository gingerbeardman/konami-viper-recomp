/* Differential against the installed SDK constructors and load, including
 * pristine/loaded object bytes, complete SDK shadow and hardware FPSCR. */
#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include "gx_texture_resource.h"
extern uint32_t __gxregs[0x598/4];
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static unsigned char images[4][64*64*4] ATTRIBUTE_ALIGN(32);
static uint32_t initial[0x598/4],expected[0x598/4];
static WiiGXTextureResource resources[4];
static uint32_t fpscr(void){double f;uint64_t b;__asm__ volatile("mffs %0":"=f"(f));memcpy(&b,&f,8);return (uint32_t)b;}
static void set_fpscr(uint32_t value){uint64_t b=value;double f;memcpy(&f,&b,8);__asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");}
int main(void){
 VIDEO_Init();GXRModeObj *m=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
 VIDEO_Configure(m);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_DrawDone();
 unsigned cases=0,failures=0,object_failures=0,shadow_failures=0,fpscr_failures=0;
 uint32_t first_expected=0,first_actual=0;
 for(unsigned n=0;n<65536;n++){
  unsigned slot=(n/8)%4,generation=n/128;
  /* Independently perturb every descriptor field, then also cross them. */
  unsigned variant=(generation/7)%4;
  void *image=images[0];unsigned w=16,h=16,ws=GX_CLAMP,wt=GX_CLAMP,filter=GX_NEAR;
  switch(generation%7){
   case 0:image=images[variant];break;
   case 1:w=16u<<(variant%3);break;
   case 2:h=16u<<(variant%3);break;
   case 3:ws=variant&1?GX_REPEAT:GX_CLAMP;break;
   case 4:wt=variant&1?GX_REPEAT:GX_CLAMP;break;
   case 5:filter=variant&1?GX_LINEAR:GX_NEAR;break;
   default:image=images[variant];w=16u<<(variant%3);h=16u<<((variant+1)%3);
    ws=variant&1?GX_REPEAT:GX_CLAMP;wt=variant&2?GX_REPEAT:GX_CLAMP;
    filter=variant&1?GX_LINEAR:GX_NEAR;break;
  }
  if(!(n%509))wii_gx_texture_resource_invalidate(&resources[slot]);
  uint32_t incoming=(n&1?0x82000000u:0u)|(((n>>2)&31u)<<12)|
      (((n>>7)&3u)<<17)|(n&3); /* sticky, FPRF, FR/FI and all RN modes */
  GXTexObj a,b;
  memcpy(initial,__gxregs,sizeof initial);
  set_fpscr(incoming);
  GX_InitTexObj(&a,image,w,h,GX_TF_RGBA8,ws,wt,GX_FALSE);
  GX_InitTexObjLOD(&a,filter,filter,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
  GXTexObj pristine=a;
  GX_LoadTexObj(&a,slot);uint32_t flags=fpscr();memcpy(expected,__gxregs,sizeof expected);
  memcpy(__gxregs,initial,sizeof initial);set_fpscr(incoming);
  b=wii_gx_texture_resource_acquire(&resources[slot],image,w,h,ws,wt,filter);
  int od=memcmp(&pristine,&b,sizeof b)!=0;
  GX_LoadTexObj(&b,slot);uint32_t actual_flags=fpscr();
  od|=memcmp(&a,&b,sizeof b)!=0;
  int sd=memcmp(expected,__gxregs,sizeof expected)!=0,fd=flags!=actual_flags;
  if(fd&&!fpscr_failures){first_expected=flags;first_actual=actual_flags;}
  object_failures+=od;shadow_failures+=sd;fpscr_failures+=fd;
  failures+=!!(od||sd||fd);cases++;
  if(!(n&127))GX_DrawDone();
 }
 GX_DrawDone();console_init(fb,20,20,m->fbWidth,m->xfbHeight,m->fbWidth*2);
 uint64_t hits=0,misses=0;for(unsigned i=0;i<4;i++){hits+=resources[i].hits;misses+=resources[i].misses;}
 printf("VIPER WII NATIVE TEXTURE RESOURCE %s\n",!failures&&hits>1000&&misses>1000?"PASS":"FAIL");
 printf("cases=%u failures=%u hits=%llu misses=%llu\n",cases,failures,(unsigned long long)hits,(unsigned long long)misses);
 printf("object=%u shadow=%u FPSCR=%u\n",object_failures,shadow_failures,fpscr_failures);
 if(fpscr_failures)printf("first FPSCR expected=%08lx actual=%08lx\n",(unsigned long)first_expected,(unsigned long)first_actual);
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
