#include "texture.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint32_t pixel(const uint8_t *p,unsigned w,unsigned x,unsigned y){
    size_t off=((size_t)(y/4)*((w+3)/4)+x/4)*64+2*((y%4)*4+x%4);
    return ((uint32_t)p[off]<<24)|((uint32_t)p[off+1]<<16)|((uint32_t)p[off+32]<<8)|p[off+33];
}
static unsigned expand(unsigned v,unsigned bits){
    /* Reference repeated MSBs, independent of converter's specialized shifts. */
    unsigned result=0,n=0;
    while(n<8){unsigned take=bits<8-n?bits:8-n;result=(result<<take)|(v>>(bits-take));n+=take;}
    return result;
}
int main(void){
    uint8_t *src=malloc(131072),*out=malloc(262144);assert(src&&out);
    for(unsigned v=0;v<65536;v++){src[v*2]=v;src[v*2+1]=v>>8;}
    for(unsigned f=10;f<=12;f++){
        assert(wii_texture_rgba8(out,262144,src,131072,0,256,256,f,NULL));
        for(unsigned v=0;v<65536;v++){
            unsigned a=255,r,g,b;
            if(f==10){r=expand(v>>11,5);g=expand((v>>5)&63,6);b=expand(v&31,5);}
            else if(f==11){a=(v>>15)*255;r=expand((v>>10)&31,5);g=expand((v>>5)&31,5);b=expand(v&31,5);}
            else{a=expand(v>>12,4);r=expand((v>>8)&15,4);g=expand((v>>4)&15,4);b=expand(v&15,4);}
            assert(pixel(out,256,v%256,v/256)==((a<<24)|(r<<16)|(g<<8)|b));
        }
    }
    uint32_t palette[256];for(unsigned i=0;i<256;i++)palette[i]=0x12345600u|i;
    uint8_t ring[4]={0x00,0x80,0x00,0xff},small[128];
    assert(wii_texture_rgba8(small,sizeof small,ring,4,3,2,1,11,NULL));
    assert(pixel(small,2,0,0)==0x000039ffu); /* wrapped LE00ff */
    assert(pixel(small,2,1,0)==0x00002100u); /* wrapped LE0080 */
    ring[0]=7;assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,5,palette));
    assert(pixel(small,1,0,0)==0xff345607u);assert(pixel(small,1,3,3)==0);
    ring[0]=0x5a;assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,4,NULL));assert(pixel(small,1,0,0)==0x55aaaaaau);
    ring[0]=0x80;assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,2,NULL));assert(pixel(small,1,0,0)==0x80808080u);
    for(unsigned v=0;v<256;v++){
        ring[0]=v;ring[1]=0xa7;
        uint32_t rgb=(expand(v>>5,3)<<16)|(expand((v>>2)&7,3)<<8)|expand(v&3,2);
        assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,0,NULL));assert(pixel(small,1,0,0)==(0xff000000u|rgb));
        assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,8,NULL));assert(pixel(small,1,0,0)==(0xa7000000u|rgb));
        assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,3,NULL));assert(pixel(small,1,0,0)==(0xff000000u|v*0x10101u));
        assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,13,NULL));assert(pixel(small,1,0,0)==(0xa7000000u|v*0x10101u));
        assert(wii_texture_rgba8(small,sizeof small,ring,4,0,1,1,14,palette));assert(pixel(small,1,0,0)==(0xa7345600u|v));
    }
    assert(!wii_texture_rgba8(small,63,ring,4,0,1,1,10,NULL));
    assert(!wii_texture_rgba8(small,128,ring,3,0,1,1,10,NULL));
    assert(!wii_texture_rgba8(small,128,ring,4,0,1,1,1,NULL));
    assert(!wii_texture_rgba8(small,128,ring,4,0,1,1,5,NULL));
    free(src);free(out);puts("PASS GX texture expansion, tiles, alpha, palette, wrap, padding and bounds");
}
