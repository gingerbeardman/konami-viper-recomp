#include <gccore.h>
#include <stdint.h>
#include <stdio.h>
#ifndef DEST
#define DEST 0
#endif
#ifndef SUBTRACT
#define SUBTRACT 0
#endif
extern uint32_t arithmetic_probe(uint32_t x, uint32_t carry);
int main(void) {
 VIDEO_Init(); GXRModeObj *m=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(m));
 console_init(fb,20,20,m->fbWidth,m->xfbHeight,m->fbWidth*2);
 VIDEO_Configure(m);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 printf("PPC %s destination r%d\n",SUBTRACT?"subfme":"addme",DEST);
 const uint32_t values[]={0,1,7,0xffffffffu,0x80000000u};
 unsigned failures=0;
 for(unsigned i=0;i<5;i++)for(unsigned ca=0;ca<2;ca++) {
  uint32_t x=values[i],expected=(SUBTRACT?~x:x)-1+ca;
  uint32_t actual=arithmetic_probe(x,ca<<29);
  printf("x=%08lx CA=%u got=%08lx expected=%08lx %s\n",(unsigned long)x,ca,(unsigned long)actual,(unsigned long)expected,actual==expected?"OK":"FAIL");
  failures+=actual!=expected;
 }
 printf("END %s failures=%u\n",failures?"FAIL":"PASS",failures);
 for(;;)VIDEO_WaitVSync();
}
