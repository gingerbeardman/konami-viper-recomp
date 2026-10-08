"""Differential-test FBI arithmetic using the actual reference function body.

Inputs are already clamped bytes. Chroma/mask tests are disabled; sampling,
fog, framebuffer blending and depth clamping are outside this test's scope.
"""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = (root / 'runtime/voodoo/voodoo_render.cpp').read_text()
start = source.index('inline bool ATTR_FORCE_INLINE voodoo_renderer::combine_color(')
brace = source.index('{', start)
depth = 1
end = brace + 1
while depth:
    depth += (source[end] == '{') - (source[end] == '}')
    end += 1
reference = source[start:end].replace('inline bool ATTR_FORCE_INLINE voodoo_renderer::combine_color', 'bool reference')
code = r'''
#include "runtime/voodoo/emu.h"
class save_proxy;
#include "runtime/voodoo/voodoo_regs.h"
#include "runtime/voodoo/video/rgbutil.h"
#include "wii/voodoo_color_eval.h"
using namespace voodoo;
struct thread_stats_block {};
struct poly_data { rgb_t color0,color1; };
bool chroma_key_test(thread_stats_block&,const rgbaint_t&,rgb_t){return true;}
bool alpha_mask_test(thread_stats_block&,u32){return true;}
// Adapter for already-clamped input bytes, not an implementation of depth clamping.
s32 clamped_z(s32 z,reg_fbz_colorpath){return z;}
s32 clamped_w(s64 w,reg_fbz_colorpath){return s32(w);}
'''
code += reference
code += r'''
uint32_t rng=1;
uint32_t next(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
WiiVoodooRGBA input(){uint32_t v=next();return {uint8_t(v),uint8_t(v>>8),uint8_t(v>>16),uint8_t(v>>24)};}
rgb_t rgb(WiiVoodooRGBA c){return rgb_t(c.a,c.r,c.g,c.b);}
int main(){
 const uint8_t boundary[]={0,1,127,128,254,255};
 for(unsigned n=0;n<1000000;n++){
  uint32_t cp=next();WiiVoodooColorInputs in={input(),input(),input(),input(),uint8_t(next()),uint8_t(next())};
  if(n<216){unsigned a=n%6,b=(n/6)%6,c=n/36;in.iterated={boundary[a],boundary[b],boundary[c],boundary[a]};}
  WiiVoodooRGBA actual=wii_voodoo_color_eval(wii_voodoo_color_plan(cp),in);
  rgbaint_t color(rgb(in.iterated)),texture(rgb(in.texture));thread_stats_block stats;
  poly_data poly={rgb(in.color0),rgb(in.color1)};
  assert(reference(color,stats,poly,reg_fbz_colorpath(cp),reg_fbz_mode(0),texture,int(in.z_alpha)<<8,in.w_alpha,rgb_t(0)));
  assert(actual.r==color.get_r()&&actual.g==color.get_g()&&actual.b==color.get_b()&&actual.a==color.get_a());
 }
 puts("colour evaluator PASS 1000000 reference comparisons");
}
'''
with tempfile.TemporaryDirectory(prefix='viper-color-eval-') as directory:
    src = Path(directory) / 'test.cpp'
    binary = Path(directory) / 'test'
    src.write_text(code)
    subprocess.run(['clang++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-Wno-sign-compare', '-fsanitize=address,undefined', '-I', str(root), str(src), '-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
