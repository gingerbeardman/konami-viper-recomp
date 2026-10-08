"""Compare shared rejection policy with the actual MAME alpha-test function."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parent.parent
source = (root / 'runtime/voodoo/voodoo_render.cpp').read_text()
start = source.index('inline bool ATTR_FORCE_INLINE voodoo_renderer::alpha_test(')
brace = source.index('{', start)
level = 1
end = brace + 1
while level:
    level += (source[end] == '{') - (source[end] == '}')
    end += 1
reference = source[start:end].replace('inline bool ATTR_FORCE_INLINE voodoo_renderer::alpha_test', 'bool reference_alpha_test')
code = r'''
#include <cstdint>
#include <cassert>
#include <cstdio>
#include "rejection_policy.h"
using u32=uint32_t;
struct thread_stats_block { unsigned afunc_fail=0; };
struct reg_alpha_mode { u32 raw; unsigned alphafunction()const{return(raw>>1)&7;} };
''' + reference + r'''
bool accepts(u32 mode,unsigned a){thread_stats_block s;return !(mode&1)||reference_alpha_test(s,{mode},a,mode>>24);}
int main(){
 unsigned comparisons=0,interactions=0;
 for(unsigned op=0;op<8;op++)for(unsigned ref=0;ref<256;ref++){
  u32 mode=(ref<<24)|(op<<1)|1;
  assert(wii_alpha_accepts_zero(mode)==accepts(mode,0));
  if(wii_rejection_policy(16|1024,mode,0)==WII_REJECT_KEEP_TEST)
   for(unsigned a=0;a<256;a++)for(unsigned key=0;key<2;key++){
    assert(accepts(mode,key?a:0)==(key&&accepts(mode,a)));comparisons++;
   }
 }
 const unsigned values[]={0,1,2,127,254,255},refs[]={0,1,127,255};
 for(unsigned enabled=0;enabled<2;enabled++)for(unsigned op=0;op<8;op++)for(unsigned ref:refs)
 for(unsigned src=0;src<16;src++)for(unsigned dst=0;dst<16;dst++)
 for(unsigned blend=0;blend<2;blend++)for(unsigned writes=0;writes<2;writes++)
 for(unsigned depth=0;depth<8;depth++)for(unsigned positive=0;positive<2;positive++){
  u32 mode=(ref<<24)|(src<<8)|(dst<<12)|(op<<1)|enabled|(blend<<4);
  u32 fbz=16|(depth<<5)|(writes<<10);
  auto policy=wii_rejection_policy(fbz,mode,positive);
  if(policy==WII_REJECT_UNSUPPORTED)continue;
  for(unsigned a:values){
   if(positive&&!a)continue;
   for(unsigned key=0;key<2;key++){
    bool original=key&&accepts(mode,a);
    unsigned encoded=key?(policy==WII_REJECT_BINARY?255:a):0;
    bool actual=policy==WII_REJECT_BINARY?encoded>0:accepts(mode,encoded);
    if(policy==WII_REJECT_ADD_NONZERO||policy==WII_REJECT_SPLIT_DEPTH)actual&=encoded>0;
    if(policy==WII_REJECT_SPLIT_DEPTH){
     assert(blend&&src==1&&(dst==4||dst==5)&&writes);
     assert(!enabled||op==7);
     /* Separate depth draw uses original predicate success, not equation alpha. */
     bool depth_actual=key;
     assert(depth_actual==original);
    }
    assert(key||!actual);
    if(original!=actual){
     /* Only a genuine framebuffer-neutral alpha0 survivor may disappear. */
     assert(original&&!actual&&key&&!a&&(policy==WII_REJECT_ADD_NONZERO||policy==WII_REJECT_SPLIT_DEPTH));
     assert(blend&&src==1&&(dst==4||dst==5));
     assert(policy==WII_REJECT_SPLIT_DEPTH||!writes||depth==2);
     for(unsigned colour=0;colour<256;colour++)assert((colour*(a+1))/256==0);
    }
    interactions++;
   }
  }
 }
 assert(wii_rejection_policy(1u<<18,0,1)==WII_REJECT_UNSUPPORTED);
 printf("rejection policy PASS comparisons=%u interactions=%u\n",comparisons,interactions);
}
'''
with tempfile.TemporaryDirectory(prefix='viper-rejection-') as directory:
    test = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    test.write_text(code)
    subprocess.run(['c++','-std=c++17','-O2','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(root/'wii'),str(test),'-o',str(binary)],check=True)
    subprocess.run([str(binary)],check=True)
