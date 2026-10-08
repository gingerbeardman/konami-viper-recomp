#include "texture.h"
#include <string.h>
size_t wii_texture_rgba8_size(unsigned w,unsigned h){
    if(!w||!h||w>1024||h>1024)return 0;
    return (size_t)((w+3)/4)*((h+3)/4)*64;
}
static unsigned e5(unsigned v){return (v<<3)|(v>>2);}
static unsigned e6(unsigned v){return (v<<2)|(v>>4);}
static unsigned e3(unsigned v){return (v<<5)|(v<<2)|(v>>1);}
static uint32_t texel(unsigned value,unsigned fmt,const uint32_t *palette){
    unsigned a=255,r=0,g=0,b=0;
    switch(fmt){
    case 0:case 8:r=e3((value>>5)&7);g=e3((value>>2)&7);b=(value&3)*85;break;
    case 2:a=r=g=b=value;break;
    case 3:case 13:r=g=b=value&255;break;
    case 4:a=(value>>4)*17;r=g=b=(value&15)*17;break;
    case 5:case 14:return 0xff000000u|(palette[value&255]&0xffffffu);
    case 10:r=e5((value>>11)&31);g=e6((value>>5)&63);b=e5(value&31);break;
    case 11:a=(value>>15)*255;r=e5((value>>10)&31);g=e5((value>>5)&31);b=e5(value&31);break;
    case 12:a=(value>>12)*17;r=((value>>8)&15)*17;g=((value>>4)&15)*17;b=(value&15)*17;break;
    }
    if(fmt==8||fmt==13)a=value>>8;
    return (a<<24)|(r<<16)|(g<<8)|b;
}
int wii_texture_rgba8(uint8_t *dst,size_t bytes,const uint8_t *src,size_t src_bytes,
                     size_t base,unsigned w,unsigned h,unsigned fmt,const uint32_t *pal){
    const unsigned byte_xor=0;
    size_t needed=wii_texture_rgba8_size(w,h);
    if(!dst||!src||!needed||bytes<needed||!src_bytes||(src_bytes&(src_bytes-1)))return 0;
    if(byte_xor!=0&&byte_xor!=3)return 0;
    if(byte_xor&&src_bytes<4)return 0;
    if(!wii_texture_format_supported(fmt))return 0;
    if((fmt==5||fmt==14)&&!pal)return 0;
    unsigned bpp=fmt>=8?2:1;
    memset(dst,0,needed);
    for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
        size_t addr=(base+((size_t)y*w+x)*bpp)&(src_bytes-1);
        unsigned value=src[addr^byte_xor];
        if(bpp==2)value|=(unsigned)src[((addr+1)&(src_bytes-1))^byte_xor]<<8;
        uint32_t rgba=texel(value,fmt,pal);
        if(fmt==14)rgba=(rgba&0xffffffu)|((value&0xff00u)<<16);
        size_t off=((size_t)(y/4)*((w+3)/4)+x/4)*64+((y%4)*4+x%4)*2;
        dst[off]=rgba>>24;dst[off+1]=rgba>>16;dst[off+32]=rgba>>8;dst[off+33]=rgba;
    }
    return 1;
}
