#include <gccore.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static uint32_t get_fpscr(void){double f;uint64_t b;__asm__ volatile("mffs %0":"=f"(f));memcpy(&b,&f,8);return b;}
static void set_fpscr(uint32_t v){uint64_t b=v;double f;memcpy(&f,&b,8);__asm__ volatile("mtfsf 255,%0"::"f"(f):"memory");}
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_DrawDone();
 const uint32_t bits[]={0,0x80000000u,1,0x80000001u,0x007fffffu,0x807fffffu,0x00800000u,0x80800000u,0x3f800000u,0xbf800000u,0x7f7fffffu,0xff7fffffu};
 const uint32_t sticky[]={0,0x82000000u,0xffffffffu&~0xf8u,0x12345600u&~0xf8u};
 uint32_t rng=0x51784293u;
 unsigned cases=0,failures=0;uint32_t expected=0,actual=0;
 for(unsigned n=0;n<1024;n++)for(unsigned rn=0;rn<4;rn++)for(unsigned st=0;st<4;st++){
  Mtx matrix;for(unsigned i=0;i<12;i++){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;uint32_t b=n%3? rng:bits[(n+i)%12];if((b&0x7f800000u)==0x7f800000u)b&=~0x00800000u;memcpy(&matrix[i/4][i%4],&b,4);}
  GX_LoadTexMtxImm(matrix,GX_TEXMTX1,GX_MTX3x4);
  uint32_t incoming=((st==3?rng&~0xf8u:sticky[st])&~3u)|rn;
  set_fpscr(incoming);GX_LoadTexMtxImm(matrix,GX_TEXMTX1,GX_MTX3x4);expected=get_fpscr();
  set_fpscr(incoming);__asm__ volatile("":::"memory");actual=get_fpscr();
  cases++;if(expected!=actual){failures++;goto done;}
  if((cases&63)==0)GX_DrawDone();
 }
 done: GX_DrawDone();set_fpscr(0);
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII LOOKUP BIND FULL FPSCR %s\n",failures?"FAIL":"PASS");
 printf("cases=%u failures=%u expected=%08lx actual=%08lx\n",cases,failures,(unsigned long)expected,(unsigned long)actual);
 printf("Repeated MTX1 upload vs retained identical matrix; GPU output separate\n");
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
