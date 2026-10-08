#!/usr/bin/env python3
"""Actual TMU/FBI emitter and actual-device plan-lifetime differential proof."""
from pathlib import Path
import re,subprocess,tempfile
root=Path(__file__).resolve().parent.parent
sdk=root/'wii/tmu_prefix_test_constants.h'
headers=['gx_tmu_prefix_cache.h','gx_tmu_equation.h','gx_color_equation.h','gx_tev_stage.h','tmu_prefix_probe_common.h']
names=set(re.findall(r'\bGX_[A-Z0-9_]+\b','\n'.join((root/'wii'/h).read_text() for h in headers)))
macros={m[1]:m[2] for m in re.finditer(r'^#define\s+(GX_[A-Z0-9_]+)\s+([^\n]+)',sdk.read_text(),re.M)}
missing=names-macros.keys()
if missing:raise SystemExit('Missing SDK constants '+str(missing))
constants='\n'.join('#define '+name+' '+macros[name] for name in sorted(names))
mock=r'''
#include <gccore.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
static struct Shadow {uint32_t color_in[16][4],alpha_in[16][4],color_op[16][5],alpha_op[16][5],order[16][3],ksel[16][2],swap[16][2],konst[4],color[4],resources[2],count,dirty;} shadow,initial,reference;
static unsigned arithmetic_writes;
static void GX_SetTevColorIn(unsigned s,unsigned a,unsigned b,unsigned c,unsigned d){uint32_t v[]={a,b,c,d};memcpy(shadow.color_in[s],v,sizeof v);arithmetic_writes++;}
static void GX_SetTevAlphaIn(unsigned s,unsigned a,unsigned b,unsigned c,unsigned d){uint32_t v[]={a,b,c,d};memcpy(shadow.alpha_in[s],v,sizeof v);arithmetic_writes++;}
static void GX_SetTevColorOp(unsigned s,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e){uint32_t v[]={a,b,c,d,e};memcpy(shadow.color_op[s],v,sizeof v);arithmetic_writes++;}
static void GX_SetTevAlphaOp(unsigned s,unsigned a,unsigned b,unsigned c,unsigned d,unsigned e){uint32_t v[]={a,b,c,d,e};memcpy(shadow.alpha_op[s],v,sizeof v);arithmetic_writes++;}
static void GX_SetTevOrder(unsigned s,unsigned a,unsigned b,unsigned c){shadow.order[s][0]=a;shadow.order[s][1]=b;shadow.order[s][2]=c;shadow.dirty|=1;}
static void GX_SetTevKColorSel(unsigned s,unsigned a){shadow.ksel[s][0]=a;}
static void GX_SetTevKAlphaSel(unsigned s,unsigned a){shadow.ksel[s][1]=a;}
static void GX_SetTevSwapMode(unsigned s,unsigned a,unsigned b){shadow.swap[s][0]=a;shadow.swap[s][1]=b;}
static void GX_SetNumTevStages(unsigned n){shadow.count=n;shadow.dirty|=4;}
static uint32_t rgba(GXColor c){return c.r|(c.g<<8)|(c.b<<16)|((uint32_t)c.a<<24);}
static void GX_SetTevColor(unsigned r,GXColor c){shadow.color[r]=rgba(c);}
static void GX_SetTevKColor(unsigned r,GXColor c){shadow.konst[r]=rgba(c);}
static void prefix_snapshot_initial(void){initial=shadow;}
static void prefix_snapshot_reference(void){reference=shadow;}
static void prefix_restore_initial(void){shadow=initial;}
static int prefix_shadow_equal(void){return !memcmp(&shadow,&reference,sizeof shadow);}
static void prefix_backend_prepare(unsigned i,unsigned draw){shadow.resources[0]=i;shadow.resources[1]=draw;shadow.dirty|=1;}
static void prefix_backend_flush(void){shadow.dirty=0;}
'''
proof=mock+r'''
#include "gx_tmu_prefix_cache.h"
#include "tmu_prefix_reference.h"
#define PREFIX_REFERENCE_ORIGINAL prefix_source_original
#include "tmu_prefix_probe_common.h"
int main(void){unsigned failures,dual;uint64_t hits=0,misses=0;unsigned cases=prefix_proof(65536,&failures,&dual,&hits,&misses);assert(!failures&&cases>100000&&dual>10000&&hits>10000&&misses>10000);printf("TMU prefix actual emitter host PASS cases=%u dual=%u hits=%llu misses=%llu\n",cases,dual,(unsigned long long)hits,(unsigned long long)misses);}
'''
# Reuse the original actual-device contract fixture; change only cache ownership
# plumbing so the tested bus writes exercise the new shared lifetime helpers.
device=(root/'wii/test_material_plan.py').read_text()
# Importing executes its old test; use its literal source and fixture instead.
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
body=device.split('source=fixture+r"""',1)[1].split('"""',1)[0]
body=body.replace('static WiiMaterialPlans plans;','static WiiMaterialPlans plans;\nstatic WiiTMUPrefixCache prefix_cache;')
body=body.replace('wii_material_plans_invalidate(&plans);','wii_tmu_prefix_material_invalidate(&plans,&prefix_cache);')
body=body.replace('wii_material_plan_get(&plans,','wii_tmu_prefix_material_get(&plans,&prefix_cache,')
body=body.replace('unsigned old=invalidations;\n        expected_change','unsigned old=invalidations;prefix_cache.plan=&plans.plan[0];\n        expected_change')
body=body.replace('voodoo_reg_write(off,v,mask);assert(invalidations-old==(unsigned)expected_change);','voodoo_reg_write(off,v,mask);assert(invalidations-old==(unsigned)expected_change);assert(expected_change?prefix_cache.plan==NULL:prefix_cache.plan==&plans.plan[0]);')
body=body.replace('static void check_plans(unsigned packet){','static void check_plans(unsigned packet){\n    int changed=!plans.have_packet||plans.packet!=packet;prefix_cache.plan=&plans.plan[0];')
body=body.replace('copy[slot]=wii_gx_tmu_pipeline_plan(', 'if(changed)assert(prefix_cache.plan==NULL);\n        copy[slot]=wii_gx_tmu_pipeline_plan(')
body=body.replace('Material plan actual-device','TMU prefix lifetime actual-device')
with tempfile.TemporaryDirectory(prefix='tmu-prefix-') as directory:
 p=Path(directory);subprocess.run(['python3',str(root/'wii/extract_tmu_prefix_reference.py'),str(p/'tmu_prefix_reference.h')],check=True);(p/'gccore.h').write_text('#ifndef PREFIX_TEST_GCCORE_H\n#define PREFIX_TEST_GCCORE_H\n'+constants+'\ntypedef unsigned char u8;typedef struct {u8 r,g,b,a;} GXColor;\n#endif\n')
 for name,source in [('emitter',proof),('device',mock+'\n#include "gx_tmu_prefix_cache.h"\n'+fixture+body)]:
  (p/(name+'.c')).write_text(source)
  subprocess.run(['clang','-O2','-std=c11','-DVIPER_WII_MATERIAL_PLAN_CACHE','-fsanitize=address,undefined','-I'+directory,'-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures'),str(p/(name+'.c')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
