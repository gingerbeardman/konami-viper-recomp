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
