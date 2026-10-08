#include "ppc_rt.h"
/* Same byte access order as the original wrappers. MEMORY_ACCESS already
 * ran once in the wrapper; reloading g_ram retains callback side effects. */
uint32_t wii_ram_cross_r32(uint32_t ea){
 uint32_t v=0;
 for(unsigned i=0;i<4;i++){uint32_t a=ea+i;v=(v<<8)|(a<RAM_LIMIT?g_ram[a&RAM_MASK]:rt_mmio_r8(a));}
 return v;
}
uint32_t wii_ram_cross_r16(uint32_t ea){
 uint32_t a=ea+1;
 return ((uint32_t)g_ram[RAM_MASK]<<8)|(a<RAM_LIMIT?g_ram[a&RAM_MASK]:rt_mmio_r8(a));
}
void wii_ram_cross_w32(uint32_t ea,uint32_t v){
 for(unsigned i=0;i<4;i++){uint32_t a=ea+i,b=(v>>(24-8*i))&255;if(a<RAM_LIMIT)g_ram[a&RAM_MASK]=b;else rt_mmio_w8(a,b);}
}
void wii_ram_cross_w16(uint32_t ea,uint32_t v){
 g_ram[RAM_MASK]=(uint8_t)(v>>8);uint32_t a=ea+1;
 if(a<RAM_LIMIT)g_ram[a&RAM_MASK]=(uint8_t)v;else rt_mmio_w8(a,v&255);
}
/* Slow paths of wii/native_ram.h: the original accessors, out of line. */
uint32_t wii_ram_slow_ld8(uint32_t ea){return LD8(ea);}
uint32_t wii_ram_slow_ld16(uint32_t ea){return LD16(ea);}
uint32_t wii_ram_slow_ld32(uint32_t ea){return LD32(ea);}
void wii_ram_slow_st8(uint32_t ea,uint32_t v){ST8(ea,v);}
void wii_ram_slow_st16(uint32_t ea,uint32_t v){ST16(ea,v);}
void wii_ram_slow_st32(uint32_t ea,uint32_t v){ST32(ea,v);}
double wii_ram_slow_ldf32(uint32_t ea){return LDF32(ea);}
void wii_ram_slow_stf32(uint32_t ea,double d){STF32(ea,d);}
double wii_ram_slow_ldf64(uint32_t ea){return LDF64(ea);}
void wii_ram_slow_stf64(uint32_t ea,double d){STF64(ea,d);}

#if defined(VIPER_WII_PRESERVE_SLOW) || defined(VIPER_WII_PRESERVE_SLOW_ALL) || defined(VIPER_WII_PRESERVE_RSQRT) || defined(VIPER_WII_PRESERVE_DTRI)
/* wii/preserve_call.h: call the function at r12 with r3/r4/f1 as arguments
 * and r3/f1 as results, preserving every other volatile register (r0, r5-r11,
 * f0, f2-f13, CR, CTR, XER). Cold paths of the localized functions call
 * through this, so GCC can keep guest values in the volatile registers
 * across them instead of spilling to the stack. */
__asm__(
"    .section .text.wii_preserve_trampoline,\"ax\",@progbits\n"
"    .align 2\n"
"    .globl wii_preserve_trampoline\n"
"    .type wii_preserve_trampoline,@function\n"
"wii_preserve_trampoline:\n"
"    stwu 1,-176(1)\n"
"    stw 0,12(1)\n"
"    mflr 0\n"
"    stw 0,180(1)\n"
"    stw 5,16(1)\n    stw 6,20(1)\n    stw 7,24(1)\n    stw 8,28(1)\n"
"    stw 9,32(1)\n    stw 10,36(1)\n    stw 11,40(1)\n"
"    mfcr 0\n    stw 0,44(1)\n"
"    mfctr 0\n    stw 0,48(1)\n"
"    mfxer 0\n    stw 0,52(1)\n"
"    stfd 0,56(1)\n    stfd 2,64(1)\n    stfd 3,72(1)\n    stfd 4,80(1)\n"
"    stfd 5,88(1)\n    stfd 6,96(1)\n    stfd 7,104(1)\n    stfd 8,112(1)\n"
"    stfd 9,120(1)\n    stfd 10,128(1)\n    stfd 11,136(1)\n    stfd 12,144(1)\n"
"    stfd 13,152(1)\n"
"    mtctr 12\n"
"    bctrl\n"
"    lfd 0,56(1)\n    lfd 2,64(1)\n    lfd 3,72(1)\n    lfd 4,80(1)\n"
"    lfd 5,88(1)\n    lfd 6,96(1)\n    lfd 7,104(1)\n    lfd 8,112(1)\n"
"    lfd 9,120(1)\n    lfd 10,128(1)\n    lfd 11,136(1)\n    lfd 12,144(1)\n"
"    lfd 13,152(1)\n"
"    lwz 0,52(1)\n    mtxer 0\n"
"    lwz 0,48(1)\n    mtctr 0\n"
"    lwz 0,44(1)\n    mtcrf 255,0\n"
"    lwz 5,16(1)\n    lwz 6,20(1)\n    lwz 7,24(1)\n    lwz 8,28(1)\n"
"    lwz 9,32(1)\n    lwz 10,36(1)\n    lwz 11,40(1)\n"
"    lwz 0,180(1)\n    mtlr 0\n"
"    lwz 0,12(1)\n"
"    addi 1,1,176\n"
"    blr\n"
"    .size wii_preserve_trampoline,.-wii_preserve_trampoline\n"
"    .previous\n");
#endif
