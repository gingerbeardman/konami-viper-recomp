"""Check that actual TMU emitter overwrites the discarded stage-zero setup.

Symbolic setter fields, not a hardware rasterizer. Native RAM/EFB is the final gate.
"""
from pathlib import Path
import re
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
header=(root/'wii/gx_tmu_equation.h').read_text()
names=sorted(set(re.findall(r'\bGX_[A-Z][A-Z0-9_]+\b',header)))
fixed={'GX_TEVPREV':0,'GX_TEVREG0':1,'GX_TEVREG1':2,'GX_TEVREG2':3,'GX_TEV_ADD':0,'GX_TEV_SUB':1,'GX_TRUE':1,'GX_FALSE':0}
constants='\n'.join(f'#define {n} {fixed.get(n,i+32)}' for i,n in enumerate(sorted(set(names)|set(fixed))))
functions={'GX_SetTevOrder':3,'GX_SetTevKColorSel':1,'GX_SetTevKAlphaSel':1,'GX_SetTevColorIn':4,'GX_SetTevAlphaIn':4,'GX_SetTevColorOp':5,'GX_SetTevAlphaOp':5}
pre='''#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t u8;
static unsigned state[16][7][5];
static unsigned dirty;
'''
for k,(name,n) in enumerate(functions.items()):
 args=','.join('unsigned a'+str(i) for i in range(n))
 pre+=f'static void {name}(unsigned s,{args}){{'+''.join(f'state[s][{k}][{i}]=a{i};' for i in range(n))+('dirty|=1;' if k==0 else '')+'}\n'
pre+='static void GX_SetNumTevStages(unsigned n){state[0][0][4]=n;dirty|=4;}\n'
main=r'''
int main(void){
 unsigned rng=0x17384059,saved[16][7][5];
 for(unsigned n=0;n<100000;n++){
  rng=rng*1664525u+1013904223u;
  WiiVoodooTMUPlan p=wii_voodoo_tmu_plan(rng);
  memset(state,0x59,sizeof state);dirty=0;
  /* Default texture program touches these five complete fields only. */
  GX_SetTevOrder(0,91,92,93);GX_SetTevColorIn(0,94,95,96,97);
  GX_SetTevAlphaIn(0,98,99,100,101);GX_SetTevColorOp(0,0,0,0,1,0);
  GX_SetTevAlphaOp(0,0,0,0,1,0);
  unsigned end=wii_gx_tmu_equation(p,0,2,3,4,5,6,GX_TEVREG0,7,8,9,10);
  assert(end);memcpy(saved,state,sizeof state);unsigned saved_dirty=dirty;
  memset(state,0x59,sizeof state);dirty=0;
  assert(end==wii_gx_tmu_equation(p,0,2,3,4,5,6,GX_TEVREG0,7,8,9,10));
  assert(!memcmp(saved,state,sizeof state)&&saved_dirty==dirty);
 }
 puts("Native texture bind symbolic stage overwrite PASS: 100000 TMU equations");
}
'''
with tempfile.TemporaryDirectory() as temp:
 p=Path(temp);(p/'gccore.h').write_text(constants+'\ntypedef unsigned char u8;\n')
 (p/'test.c').write_text(pre+'\n#include "gx_tmu_equation.h"\n'+main)
 subprocess.run(['clang','-O2','-std=c11','-fsanitize=address,undefined','-I'+temp,'-I'+str(root/'wii'),str(p/'test.c'),'-o',str(p/'test')],check=True)
 subprocess.run([str(p/'test')],check=True)
