"""Differential TMU arithmetic against the unmodified MAME function body.

Already fetched RGBA bytes and valid post-fetch LODs; no sampling/GX claims.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/voodoo/voodoo_render.cpp').read_text()
start = source.index('inline rgbaint_t ATTR_FORCE_INLINE rasterizer_texture::combine_texture(')
brace = source.index('{', start)
end, depth = brace + 1, 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
reference = source[start:end].replace(
    'inline rgbaint_t ATTR_FORCE_INLINE rasterizer_texture::combine_texture', 'rgbaint_t reference')
code = r'''
#include "runtime/voodoo/emu.h"
class save_proxy;
#include "runtime/voodoo/voodoo_regs.h"
#include "runtime/voodoo/video/rgbutil.h"
#include "wii/voodoo_tmu_eval.h"
using namespace voodoo;
int m_detailbias,m_detailscale,m_detailmax;
''' + reference + r'''
uint32_t rng=0x12345678;
uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
WiiVoodooRGBA input(){auto v=next();return {uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};}
rgb_t rgb(WiiVoodooRGBA c){return rgb_t(c.a,c.r,c.g,c.b);}
void check(uint32_t mode,WiiVoodooRGBA local,WiiVoodooRGBA other,int lod,uint32_t detail){
 auto p=wii_voodoo_tmu_plan(mode);reg_texture_mode r(mode);
 assert(p.rgb_zero==r.tc_zero_other()&&p.rgb_sub==r.tc_sub_clocal()&&p.rgb_mul==r.tc_mselect());
 assert(p.rgb_reverse==r.tc_reverse_blend()&&p.rgb_add==r.tc_add_aclocal()&&p.rgb_invert==r.tc_invert_output());
 assert(p.alpha_zero==r.tca_zero_other()&&p.alpha_sub==r.tca_sub_clocal()&&p.alpha_mul==r.tca_mselect());
 assert(p.alpha_reverse==r.tca_reverse_blend()&&p.alpha_add==r.tca_add_aclocal()&&p.alpha_invert==r.tca_invert_output());
 reg_texture_detail d(detail);int bias=d.detail_bias();if(bias&32)bias-=64;
 m_detailbias=bias*256;m_detailscale=d.detail_scale();m_detailmax=d.detail_max();
 auto actual=wii_voodoo_tmu_eval(p,local,other,lod,detail);
 auto expected=reference(r,rgbaint_t(rgb(local)),rgbaint_t(rgb(other)),lod);
 assert(actual.r==expected.get_r()&&actual.g==expected.get_g()&&actual.b==expected.get_b()&&actual.a==expected.get_a());
}
int main(){
 const uint8_t bytes[]={0,1,127,128,254,255};
 unsigned count=0;
 for(unsigned n=0;n<1000000;n++){
  uint32_t mode=next(),detail=next();int lod=int(next()%4097)-2048;
  auto local=input(),other=input();
  if(n<46656){unsigned k=n;local={bytes[k%6],bytes[(k/6)%6],bytes[(k/36)%6],bytes[(k/216)%6]};other={bytes[(k/1296)%6],bytes[(k/7776)%6],0,255};}
  check(mode,local,other,lod,detail);count++;
 }
 // Exhaust every detail bias/scale at equality, fractional and byte-wrap boundaries.
 for(unsigned bias=0;bias<64;bias++)for(unsigned scale=0;scale<8;scale++)
 for(unsigned maximum:bytes)for(int offset:{-513,-257,-256,-255,-1,0,1,255,256,257,513}){
  int signed_bias=int(bias);if(bias&32)signed_bias-=64;
  for(unsigned selector:{4u,5u})for(unsigned reverse=0;reverse<2;reverse++){
   uint32_t mode=(selector<<14)|(selector<<23)|(reverse<<17)|(reverse<<26);
   check(mode,{255,128,1,254},{254,127,255,1},signed_bias*256+offset,
       maximum|(bias<<8)|(scale<<14));count++;
  }
 }
 printf("TMU evaluator PASS %u reference comparisons\n",count);
}
'''
with tempfile.TemporaryDirectory(prefix='viper-tmu-eval-') as directory:
    src = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    src.write_text(code)
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-fsanitize=address,undefined', '-I', str(root),
                    str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
    # Separately verify that public headers remain strict C, not C++ adapters.
    c = Path(directory) / 'header.c'
    c.write_text('#include "wii/voodoo_tmu_eval.h"\nint main(void){return wii_voodoo_tmu_detail(0,0);}\n')
    subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-pedantic',
                    '-I', str(root), str(c), '-o', str(Path(directory)/'header')], check=True)
