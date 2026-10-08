#include "render_family.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 assert(wii_iterated_chroma_family(0x15024100,0x2177b,0x4511f,0x40,0,0x10000000));
 assert(!wii_iterated_chroma_family(0x15024100,0x2177b|(1u<<13),0x4511f,0x40,0,0x10000000));
 assert(!wii_iterated_chroma_family(0x15024100,0x2177b&~2u,0x4511f,0x40,0,0x10000000));
 assert(wii_iterated_mask_family(0x15024100,0x217b,0x4511f,0x40,0,0));
 /* Both equation bases are zero: multiplier and reverse selections are
  * inactive, so admission must depend on semantics rather than raw tuples. */
 for(unsigned rgb=0;rgb<16;rgb++)for(unsigned a=0;a<16;a++){
  uint32_t equivalent=(0x15024100u&~((15u<<10)|(15u<<19)))|
      (rgb<<10)|(a<<19);
  assert(wii_iterated_mask_family(equivalent,0x217b,0x4511f,0x40,0,0));
 }
 for(unsigned bit=29;bit<32;bit++)
  assert(!wii_iterated_mask_family(0x15024100|(1u<<bit),0x217b,0x4511f,0x40,0,0));
 for(unsigned bit=5;bit<8;bit++)
  assert(!wii_iterated_mask_family(0x15024100,0x217b,0x4511f|(1u<<bit),0x40,0,0));
 const uint32_t mask_forbidden[]={1024,1u<<2,1u<<16,1u<<18,1u<<19,1u<<20,1u<<21};
 for(unsigned i=0;i<sizeof mask_forbidden/sizeof *mask_forbidden;i++)
  assert(!wii_iterated_mask_family(0x15024100,0x217b|mask_forbidden[i],0x4511f,0x40,0,0));
 assert(!wii_iterated_mask_family(0x15024100,0x217b&~(1u<<13),0x4511f,0x40,0,0));
 assert(!wii_iterated_mask_family(0x15024100,0x217b,0x4511f,0x40,1,0));
 assert(!wii_iterated_mask_family(0x15024100,0x217b,0x4511f,0x40,0,0x10000001));
 assert(wii_iterated_mask_family(0x15024100,0x217b,0x4511f,0x40,0xff000000,0x0fffffff));
 assert(wii_iterated_mask_family(0x15024100,0x217b,0x4511f,0x40,0xff000000,0x10000000));
 assert(!wii_iterated_mask_family(0x15024100,0x217b,0x4521f,0x40,0,0));
 assert(!wii_iterated_mask_family(0x15024101,0x217b,0x4511f,0x40,0,0));
 const uint32_t cp[]={0x1c482405,0x1c482405,0x1d022401,0x1d022401,0x1c484104};
 const unsigned fmt[]={11,12,5,10,2};
 const uint32_t alpha[]={0x0c045119,0x0c045109,0x4511f,0x4411f,0x4421f,0x4221f,0x4510f};
 unsigned checked=0;
 for(unsigned f=0;f<5;f++)for(unsigned packet=0;packet<2;packet++)
 for(unsigned pass=0;pass<2;pass++)for(unsigned compare=0;compare<8;compare++)
 for(unsigned writes=0;writes<2;writes++)for(unsigned key=0;key<2;key++)
 for(unsigned a=0;a<7;a++){
  unsigned mode=packet?7:6;
  uint32_t t1=0x10241000|fmt[f]<<8|mode,t0=pass?(fmt[f]<<8|mode):t1;
  uint32_t fbz=0x21309|16|compare<<5|writes<<10|key<<1;
  WiiRenderFamily p={99,99,99,99};
  int got=wii_render_family(cp[f],fbz,alpha[a],0x40,t0,t1,packet?0x3b:0x23,&p);
  /* ALWAYS alpha with chroma cannot preserve source-alpha blending and
   * zero-alpha survivor depth simultaneously through a single GX alpha. */
  int zero_noop=compare==2&&(a==2||a==3);
  int unsupported=key&&writes&&(a==2||a==3)&&!zero_noop;
  assert(got==!unsupported);
  if(got){assert(p.unit==pass&&p.format==fmt[f]);assert(p.binary_chroma==(key&&(a==4||a==5||a==6)));assert(p.reject_zero_alpha==(key&&(a==2||a==3)&&(!writes||compare==2)));}
  checked++;
 }
 WiiRenderFamily p;
 for(unsigned compare=0;compare<8;compare++)for(unsigned writes=0;writes<2;writes++)
 for(unsigned key=0;key<2;key++)for(unsigned a=0;a<7;a++){
  uint32_t fbz=0x21309|16|compare<<5|writes<<10|key<<1;
  int got=wii_render_family(0x1c482405,fbz,alpha[a],0x40,0x4c0,0x102414c0,0x23,&p);
  int zero_noop=compare==2&&(a==2||a==3);
  assert(got==!(key&&writes&&(a==2||a==3)&&!zero_noop));
  if(got)assert(p.unit==1&&p.format==4);
  checked++;
 }
 const uint32_t forbidden[]={1u<<2,1u<<13,1u<<16,1u<<18,1u<<19,1u<<20,1u<<21};
 for(unsigned i=0;i<sizeof forbidden/sizeof *forbidden;i++)
  assert(!wii_render_family(cp[1],0x21359|forbidden[i],0x4421f,0x40,0xcc7,0x10241cc7,0x3b,&p));
 assert(!wii_render_family(cp[1],0x21359,0x4431f,0x40,0xcc7,0x10241cc7,0x3b,&p));
 assert(!wii_render_family(cp[1],0x21359,0x4421f,0x42,0xcc7,0x10241cc7,0x3b,&p));
 assert(!wii_render_family(cp[1],0x21359,0x4421f,0x40,0xcc7,0x10241cc7,0x0b,&p));
 assert(!wii_render_family(cp[1],0x21359,0x4421f,0x40,0xcc7,0x10241cc7,0x3b,0));
 printf("render family PASS %u combinations and forbidden-state checks\n",checked);
}
