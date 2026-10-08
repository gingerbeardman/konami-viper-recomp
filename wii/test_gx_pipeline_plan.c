#include "gx_pipeline_plan.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 unsigned seed=1,checked=0;const unsigned formats[]={0,2,3,4,5,8,10,11,12,13,14};
 for(unsigned n=0;n<1000000;n++){
  seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
  uint32_t cp=(seed&0x03ffffffu)&~((1u<<6)|(1u<<7));
  cp|=(1u<<28)|(1u<<27);
  unsigned mode=n&1?7:6,format=formats[n%11],unit=(n>>1)&1;
  uint32_t t1=0x10241000|(format<<8)|mode,t0=unit?(format<<8)|mode:t1;
  uint32_t fbz=0x21329|((n&4)?2:0)|((n&8)?1u<<13:0);
  WiiGXPipelinePlan p=wii_gx_pipeline_plan(cp,fbz,0x4400f,0x40+(n&1),0,0x10000000,
      t0,t1,mode==7?0x3b:0x23,0);
  assert(p.reason==WII_PIPE_OK&&p.unit==unit&&p.format==format);
  assert(p.equation.other_rgb==(cp&3)&&p.equation.other_alpha==((cp>>2)&3));
  assert(p.equation_stages>=1&&p.equation_stages<=4&&p.total_stages<=12);
  assert(p.rejection_stages==(p.key||p.mask?1+5*p.mask:0));
  if(p.key||p.mask)assert(p.rejection==WII_REJECT_BINARY);
  uint32_t untextured=cp;
  if((untextured&3)==1)untextured=(untextured&~3u)|3;
  if(((untextured>>2)&3)==1)untextured=(untextured&~12u)|12;
  if(((untextured>>10)&7)==4||((untextured>>10)&7)==5)untextured=(untextured&~(7u<<10))|(6u<<10);
  if(((untextured>>19)&7)==4)untextured=(untextured&~(7u<<19))|(5u<<19);
  p=wii_gx_pipeline_plan(untextured,fbz,0x4400f,0x40+(n&1),0,0x10000000,0xffffffff,0xffffffff,0x0b,0);
  assert(p.reason==WII_PIPE_OK&&!p.textured);
  checked+=2;
 }
 WiiGXPipelinePlan p=wii_gx_pipeline_plan(0x1d022401,0x2177b,0x4511f,0x40,0,0x10000000,0xa07,0x10241a07,0x3b,0);
 assert(p.reason==WII_PIPE_OK&&p.rejection==WII_REJECT_SPLIT_DEPTH);
 p=wii_gx_pipeline_plan(0x1d022401,0x2177b,0x4511f,0x40,0,0x10000000,0xa07,0x10241a07,0x3b,1);
 assert(p.reason==WII_PIPE_OK&&p.rejection==WII_REJECT_ADD_NONZERO);
 printf("pipeline plan PASS %u equation/sampling/predicate combinations\n",checked);
}
