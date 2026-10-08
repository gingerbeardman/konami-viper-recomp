/* Platform bring-up only: this is not a game-speed benchmark. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ppc_rt.h"
uint8_t *g_ram;
/* The platform probe never intentionally accesses MMIO. */
uint32_t rt_mmio_r32(uint32_t ea) { (void)ea;abort(); }
uint32_t rt_mmio_r16(uint32_t ea) { (void)ea;abort(); }
uint32_t rt_mmio_r8(uint32_t ea) { (void)ea;abort(); }
void rt_mmio_w32(uint32_t ea,uint32_t v) { (void)ea;(void)v;abort(); }
void rt_mmio_w16(uint32_t ea,uint32_t v) { (void)ea;(void)v;abort(); }
void rt_mmio_w8(uint32_t ea,uint32_t v) { (void)ea;(void)v;abort(); }
static int memory_check(void) {
    ST32(3,0x12345678);
    if(g_ram[3]!=0x12||g_ram[4]!=0x34||g_ram[5]!=0x56||g_ram[6]!=0x78||
       LD32(3)!=0x12345678||LD16(4)!=0x3456||LD32(RAM_SIZE+3)!=0x12345678)return 0;
    ST32LE(8,0x12345678);
    if(g_ram[8]!=0x78||g_ram[11]!=0x12||LD32LE(8)!=0x12345678)return 0;
    STF64(16,-123.5);if(LDF64(16)!=-123.5)return 0;
    STF32(24,1.25);if(LDF32(24)!=1.25)return 0;
    ST32(RAM_SIZE-2,0xabcdef12);
    if(g_ram[RAM_SIZE-2]!=0xab||g_ram[RAM_SIZE-1]!=0xcd||g_ram[0]!=0xef||g_ram[1]!=0x12||LD32(RAM_SIZE-2)!=0xabcdef12)return 0;
    ST16(RAM_SIZE-1,0x4567);if(LD16(RAM_SIZE-1)!=0x4567||g_ram[0]!=0x67)return 0;
    return 1;
}
int main(void) {
    VIDEO_Init();
    GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
    VIDEO_Configure(mode); VIDEO_SetNextFramebuffer(fb);
    VIDEO_SetBlack(FALSE); VIDEO_Flush(); VIDEO_WaitVSync();
    if(mode->viTVMode&VI_NON_INTERLACE)VIDEO_WaitVSync();
    printf("VIPER WII PLATFORM PROBE\n");
    const size_t bytes=32u*1024u*1024u;
    unsigned char *ram=malloc(bytes);
    if(!ram)printf("FAIL 32 MiB guest RAM allocation\n");
    else {
        memset(ram,0x5a,bytes);
        printf("32 MiB allocation %p: %s\n",(void *)ram,
               ram[0]==0x5a&&ram[bytes-1]==0x5a?"OK":"FAIL");
        g_ram=ram;
        printf("Guest BE / peripheral LE / float access: %s\n",memory_check()?"OK":"FAIL");
        free(ram);
    }
    printf("Timebase sample: %llu\n",(unsigned long long)gettime());
    printf("Platform probe complete; game runtime not started.\n");
    for(;;)VIDEO_WaitVSync();
}
