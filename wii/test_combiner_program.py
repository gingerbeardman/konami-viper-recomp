#!/usr/bin/env python3
"""Compare combined cache against actual renderer prefix and original FBI emitter."""
import ast,re,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parent.parent
# Load only the literal mock; importing the prefix test would run unrelated tests.
tree=ast.parse((root/'wii/test_tmu_prefix.py').read_text())
mock=next(ast.literal_eval(n.value) for n in tree.body if isinstance(n,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='mock' for t in n.targets))
mock=mock.replace('shadow.dirty|=1;}\nstatic void prefix_backend_flush','shadow.dirty=0;}\nstatic void prefix_backend_flush')
headers=['gx_combiner_program_cache.h','gx_tmu_equation.h','gx_color_equation.h','gx_tev_stage.h','combiner_program_probe_common.h']
names=set(re.findall(r'\bGX_[A-Z0-9_]+\b','\n'.join((root/'wii'/h).read_text() for h in headers)))
macros={m[1]:m[2] for m in re.finditer(r'^#define\s+(GX_[A-Z0-9_]+)\s+([^\n]+)',(root/'wii/tmu_prefix_test_constants.h').read_text(),re.M)}
assert not names-macros.keys(),names-macros.keys()
proof=mock+r"""
#include "gx_tmu_prefix_cache.h"
#include "tmu_prefix_reference.h"
#define PREFIX_REFERENCE_ORIGINAL prefix_source_original
#include "combiner_program_probe_common.h"
int main(void){unsigned failures,dual;uint64_t hits=0,misses=0;
 unsigned cases=prefix_proof(65536,&failures,&dual,&hits,&misses);
 printf("Combiner source-reference cases=%u failures=%u dual=%u hits=%llu misses=%llu\n",cases,failures,dual,(unsigned long long)hits,(unsigned long long)misses);
 assert(!failures&&cases>100000&&hits>10000&&misses>10000);
}
"""
device=(root/'wii/test_material_plan.py').read_text()
# Importing executes its old test; use its literal source and fixture instead.
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
body=device.split('source=fixture+r"""',1)[1].split('"""',1)[0]
body=body.replace('static WiiMaterialPlans plans;','static WiiMaterialPlans plans;\nstatic WiiCombinerProgramCache prefix_cache;')
body=body.replace('wii_material_plans_invalidate(&plans);','wii_combiner_program_material_invalidate(&plans,&prefix_cache);')
body=body.replace('wii_material_plan_get(&plans,','wii_combiner_program_material_get(&plans,&prefix_cache,')
body=body.replace('unsigned old=invalidations;\n        expected_change','unsigned old=invalidations;prefix_cache.plan=&plans.plan[0];\n        expected_change')
body=body.replace('voodoo_reg_write(off,v,mask);assert(invalidations-old==(unsigned)expected_change);','voodoo_reg_write(off,v,mask);assert(invalidations-old==(unsigned)expected_change);assert(expected_change?prefix_cache.plan==NULL:prefix_cache.plan==&plans.plan[0]);')
body=body.replace('static void check_plans(unsigned packet){','static void check_plans(unsigned packet){\n    int changed=!plans.have_packet||plans.packet!=packet;prefix_cache.plan=&plans.plan[0];')
body=body.replace('copy[slot]=wii_gx_tmu_pipeline_plan(', 'if(changed)assert(prefix_cache.plan==NULL);\n        copy[slot]=wii_gx_tmu_pipeline_plan(')
body=body.replace('Material plan actual-device','Combiner lifetime actual-device')
counted=mock.replace(
 'static void GX_SetTevOrder(unsigned s,unsigned a,unsigned b,unsigned c){shadow.order[s][0]=a;shadow.order[s][1]=b;shadow.order[s][2]=c;shadow.dirty|=1;}',
 'static unsigned set_color,set_kcolor,set_stages,set_order;\nstatic void GX_SetTevOrder(unsigned s,unsigned a,unsigned b,unsigned c){shadow.order[s][0]=a;shadow.order[s][1]=b;shadow.order[s][2]=c;shadow.dirty|=1;set_order++;}')
counted=counted.replace('static void GX_SetNumTevStages(unsigned n){shadow.count=n;shadow.dirty|=4;}','static void GX_SetNumTevStages(unsigned n){shadow.count=n;shadow.dirty|=4;set_stages++;}')
counted=counted.replace('static void GX_SetTevColor(unsigned r,GXColor c){shadow.color[r]=rgba(c);}','static void GX_SetTevColor(unsigned r,GXColor c){shadow.color[r]=rgba(c);set_color++;}')
counted=counted.replace('static void GX_SetTevKColor(unsigned r,GXColor c){shadow.konst[r]=rgba(c);}','static void GX_SetTevKColor(unsigned r,GXColor c){shadow.konst[r]=rgba(c);set_kcolor++;}')
optional=counted+r'''
#include "gx_combiner_program_cache.h"
static uint32_t rng=0x47a19031;
static uint32_t next_u(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void clear_sets(void){set_color=set_kcolor=set_stages=set_order=0;}
static void poison_constants(void){
 GX_SetTevColor(GX_TEVREG1,(GXColor){1,2,3,4});GX_SetTevColor(GX_TEVREG2,(GXColor){5,6,7,8});
 GX_SetTevKColor(GX_KCOLOR2,(GXColor){9,9,9,9});GX_SetTevKColor(GX_KCOLOR3,(GXColor){8,8,8,8});
}
static void poison_order(const WiiTMUPipelinePlan *p){
 GX_SetTevOrder(p->fbi_first,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 if((p->fbi_first^1u)<16u)GX_SetTevOrder(p->fbi_first^1u,1,2,GX_COLOR0A0);
 GX_SetNumTevStages(1);
}
static unsigned emit_mode(WiiCombinerProgramCache *c,const WiiTMUPipelinePlan *p,GXColor c0,GXColor c1,int mode,int constants_needed,int *fault){
 if(mode)return wii_gx_combiner_program_emit_optional(c,p,c0,c1,constants_needed,fault);
 return wii_gx_combiner_program_emit(c,p,c0,c1,fault);
}
int main(void){
 uint32_t regs0[9]={0},regs1[9]={0},alt0[9]={0},alt1[9]={0};
 const WiiTMUPipelinePlan *probe=NULL;WiiMaterialPlans search={0};
 for(unsigned n=0;n<4096&&!probe;n++){
  unsigned lod=next_u()%32;
  regs0[0]=(next_u()&0x3ffff000u)|0xa07u;regs1[0]=(next_u()&0x3ffff000u)|0xa07u;
  regs0[1]=lod|(lod<<6);regs1[1]=lod|(lod<<6);regs0[2]=next_u();regs1[2]=next_u();
  wii_material_plans_invalidate(&search);
  const WiiTMUPipelinePlan *p=wii_material_plan_get(&search,0x1d022401,0x21329,0x4511f,0x40,0,0,regs0,regs1,0,0x3b,1);
  if(p->reason==WII_TMU_PIPE_OK&&!p->texture_zero&&(p->unit[0].use||p->unit[1].use))probe=p;
 }
 assert(probe&&(probe->unit[0].use||probe->unit[1].use));
 memcpy(alt0,regs0,sizeof alt0);memcpy(alt1,regs1,sizeof alt1);
 alt0[2]^=0x00ff00ffu;alt1[2]^=0x0ff0f00fu;
 WiiMaterialPlans differ={0};
 const WiiTMUPipelinePlan *rewritten=wii_material_plan_get(&differ,0x1d022401,0x21329,0x4511f,0x40,0,0,alt0,alt1,0,0xff,1);
 assert(rewritten->reason==WII_TMU_PIPE_OK&&!rewritten->texture_zero);
 assert(memcmp(probe,rewritten,sizeof *probe));
 struct Shadow shot[2][6];
 unsigned sc[2][6],sk[2][6],ss[2][6],so[2][6];
 uint64_t hits[2],misses[2];
 unsigned pair_sibling[2];
 GXColor c0=(GXColor){11,22,33,44},c1=(GXColor){55,66,77,88},n0=(GXColor){90,80,70,60},n1=(GXColor){15,25,35,45};
 for(int mode=0;mode<2;mode++){
  memset(&shadow,0,sizeof shadow);
  WiiMaterialPlans material={0};WiiCombinerProgramCache cache={0};
  const WiiTMUPipelinePlan *p=wii_combiner_program_material_get(&material,&cache,0x1d022401,0x21329,0x4511f,0x40,0,0,regs0,regs1,0,0x3b,1);
  assert(p->unit[0].use||p->unit[1].use);
  poison_constants();poison_order(p);clear_sets();
  int fault=0;unsigned end=emit_mode(&cache,p,c0,c1,mode,0,&fault);
  assert(!fault&&end==p->snapshot_first&&cache.misses==1&&cache.hits==0);
  shot[mode][0]=shadow;sc[mode][0]=set_color;sk[mode][0]=set_kcolor;ss[mode][0]=set_stages;so[mode][0]=set_order;
  poison_order(p);clear_sets();
  end=emit_mode(&cache,p,c0,c1,mode,0,&fault);
  assert(!fault&&end==p->snapshot_first&&cache.hits==1);
  shot[mode][1]=shadow;sc[mode][1]=set_color;sk[mode][1]=set_kcolor;ss[mode][1]=set_stages;so[mode][1]=set_order;
  unsigned sibling=p->fbi_first^1u;pair_sibling[mode]=sibling<16?shadow.order[sibling][0]:0;
  wii_combiner_program_invalidate(&cache);
  poison_constants();poison_order(p);clear_sets();
  end=emit_mode(&cache,p,c0,c1,mode,0,&fault);
  assert(!fault&&cache.misses==2&&cache.hits==1);
  shot[mode][2]=shadow;sc[mode][2]=set_color;sk[mode][2]=set_kcolor;ss[mode][2]=set_stages;so[mode][2]=set_order;
  end=emit_mode(&cache,p,c0,c1,mode,0,&fault);assert(!fault&&cache.hits==2);
  const WiiTMUPipelinePlan *q=wii_combiner_program_material_get(&material,&cache,0x1d022401,0x21329,0x4511f,0x40,0,0,alt0,alt1,0,0xff,1);
  assert(cache.plan==NULL&&q!=NULL);
  poison_constants();poison_order(q);clear_sets();
  end=emit_mode(&cache,q,c0,c1,mode,0,&fault);
  assert(!fault&&end==q->snapshot_first&&cache.misses==3);
  shot[mode][3]=shadow;sc[mode][3]=set_color;sk[mode][3]=set_kcolor;ss[mode][3]=set_stages;so[mode][3]=set_order;
  poison_order(q);clear_sets();
  unsigned before_pair=shadow.order[q->fbi_first^1u][0];
  end=emit_mode(&cache,q,c0,c1,mode,0,&fault);
  assert(!fault&&cache.hits==3);
  assert((q->fbi_first^1u)>=16||shadow.order[q->fbi_first^1u][0]==before_pair);
  assert(shadow.order[q->fbi_first][0]==GX_TEXCOORDNULL&&shadow.order[q->fbi_first][1]==GX_TEXMAP_NULL&&shadow.order[q->fbi_first][2]==GX_COLOR0A0);
  assert(shadow.count==end&&(shadow.dirty&5)==5);
  shot[mode][4]=shadow;sc[mode][4]=set_color;sk[mode][4]=set_kcolor;ss[mode][4]=set_stages;so[mode][4]=set_order;
  poison_constants();clear_sets();
  end=emit_mode(&cache,q,n0,n1,mode,1,&fault);
  assert(!fault&&cache.hits==4);
  assert(shadow.color[GX_TEVREG1]==(n0.r|(n0.g<<8)|(n0.b<<16)|((uint32_t)n0.a<<24)));
  assert(shadow.color[GX_TEVREG2]==(n1.r|(n1.g<<8)|(n1.b<<16)|((uint32_t)n1.a<<24)));
  shot[mode][5]=shadow;sc[mode][5]=set_color;sk[mode][5]=set_kcolor;ss[mode][5]=set_stages;so[mode][5]=set_order;
  hits[mode]=cache.hits;misses[mode]=cache.misses;
 }
 for(int step=0;step<6;step++){
  assert(!memcmp(&shot[0][step],&shot[1][step],sizeof shot[0][step]));
  assert(ss[0][step]==ss[1][step]&&ss[0][step]>0);
  assert(so[0][step]==so[1][step]&&so[0][step]>0);
 }
 assert(pair_sibling[0]==pair_sibling[1]);
 assert(sc[0][0]==sc[1][0]&&sc[0][0]>=2&&sk[0][0]==sk[1][0]&&sk[0][0]>0);
 assert(sc[1][1]==0&&sk[1][1]==0&&sc[0][1]>=2&&sk[0][1]>0);
 assert(sc[1][1]+sk[1][1]+ss[1][1]+so[1][1]<sc[0][1]+sk[0][1]+ss[0][1]+so[0][1]);
 assert(sc[0][2]==sc[1][2]&&sc[1][2]>=2&&sk[0][2]==sk[1][2]&&sk[1][2]>0);
 assert(sc[0][3]==sc[1][3]&&sc[1][3]>=2&&sk[0][3]==sk[1][3]&&sk[1][3]>0);
 assert(memcmp(&shot[0][1],&shot[0][3],sizeof shot[0][1]));
 assert(sc[1][4]==0&&sk[1][4]==0&&so[1][4]>0&&ss[1][4]>0);
 assert(sc[0][5]==sc[1][5]&&sc[1][5]>=2&&sk[0][5]==sk[1][5]&&sk[1][5]>0);
 assert(hits[0]==hits[1]&&misses[0]==misses[1]&&hits[0]==4&&misses[0]==3);
 printf("Combiner optional constants host PASS hits=%llu misses=%llu skipped_color=%u baseline_color=%u\n",
  (unsigned long long)hits[1],(unsigned long long)misses[1],sc[1][1],sc[0][1]);
}
'''
with tempfile.TemporaryDirectory(prefix='combiner-proof-') as directory:
 p=Path(directory)
 subprocess.run(['python3',str(root/'wii/extract_tmu_prefix_reference.py'),str(p/'tmu_prefix_reference.h')],check=True)
 constants='\n'.join('#define '+name+' '+macros[name] for name in sorted(names))
 (p/'gccore.h').write_text('#ifndef COMBINER_TEST_GCCORE_H\n#define COMBINER_TEST_GCCORE_H\n'+constants+'\ntypedef unsigned char u8;typedef struct {u8 r,g,b,a;} GXColor;\n#endif\n')
 for name,source in [('proof',proof),('device',mock+'\n#include "gx_combiner_program_cache.h"\n'+fixture+body),('optional',optional)]:
  (p/(name+'.c')).write_text(source)
  subprocess.run(['clang','-O2','-std=c11','-fsanitize=address,undefined','-I'+directory,'-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures'),'-DVIPER_WII_MATERIAL_PLAN_CACHE',str(p/(name+'.c')),'-o',str(p/name)],check=True)
  subprocess.run([str(p/name)],check=True)
