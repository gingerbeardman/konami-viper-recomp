/* Final BP words for the audited installed libogc input+operation setters.
 * Colour preserves register ID; alpha also preserves both swap selectors. */
#ifndef VIPER_WII_GX_TEV_WORDS_H
#define VIPER_WII_GX_TEV_WORDS_H
#include <stdint.h>
static inline uint32_t wii_tev_control(unsigned op,unsigned bias,unsigned scale,unsigned clamp,unsigned out){
    return ((op&1u)<<18)|((clamp&1u)<<19)|((out&3u)<<22)|
        (op<=1?((bias&3u)<<16)|((scale&3u)<<20):0x30000u|(((op>>1)&3u)<<20));
}
static inline uint32_t wii_tev_color_payload(unsigned a,unsigned b,unsigned c,unsigned d,
    unsigned op,unsigned bias,unsigned scale,unsigned clamp,unsigned out){
    return ((a&15u)<<12)|((b&15u)<<8)|((c&15u)<<4)|(d&15u)|
        wii_tev_control(op,bias,scale,clamp,out);
}
static inline uint32_t wii_tev_alpha_payload(unsigned a,unsigned b,unsigned c,unsigned d,
    unsigned op,unsigned bias,unsigned scale,unsigned clamp,unsigned out){
    return ((a&7u)<<13)|((b&7u)<<10)|((c&7u)<<7)|((d&7u)<<4)|
        wii_tev_control(op,bias,scale,clamp,out);
}
static inline uint32_t wii_tev_color_merge(uint32_t old,uint32_t payload){return (old&0xff000000u)|(payload&0xffffffu);}
static inline uint32_t wii_tev_alpha_merge(uint32_t old,uint32_t payload){return (old&0xff00000fu)|(payload&0xfffff0u);}
#endif
