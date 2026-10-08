/* Shared host/native proof. Backend supplies complete shadow snapshots. */
#include "gx_combiner_program_cache.h"
#include "gx_color_equation.h"
static GXColor combiner_c0,combiner_c1;
static uint32_t prefix_rng=0x47a19031;
static uint32_t prefix_next(void){prefix_rng^=prefix_rng<<13;prefix_rng^=prefix_rng>>17;prefix_rng^=prefix_rng<<5;return prefix_rng;}
static unsigned prefix_suffix(const WiiTMUPipelinePlan *p,unsigned end){
 return wii_gx_color_equation_at(p->fbi.equation,combiner_c0,combiner_c1,
  end,GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
}
#ifndef PREFIX_REFERENCE_ORIGINAL
#define PREFIX_REFERENCE_ORIGINAL wii_gx_tmu_prefix_original
#endif
static unsigned prefix_proof(unsigned iterations,unsigned *failures,unsigned *dual,
 uint64_t *hits,uint64_t *misses){
 WiiMaterialPlans material={0};WiiCombinerProgramCache cache={0};unsigned cases=0;
 *failures=*dual=0;
 for(unsigned i=0;i<iterations;i++){
  uint32_t regs0[9]={0},regs1[9]={0};uint32_t *regs[2]={regs0,regs1};
  for(unsigned u=0;u<2;u++){
   unsigned lod=prefix_next()%32;
   regs[u][0]=i%3?(prefix_next()&0x3ffff000u)|0xa07u:0x10241a07u;
   regs[u][1]=lod|(lod<<6);regs[u][2]=prefix_next();
  }
  wii_combiner_program_invalidate(&cache);wii_material_plans_invalidate(&material);
  for(unsigned draw=0;draw<6;draw++){
   /* Include packet changes and same-address slot reconstruction. */
   unsigned packet=draw>=4?(i%4?0xff:0x3b):0x3b;
   const WiiTMUPipelinePlan *p=wii_combiner_program_material_get(&material,&cache,
    (i%4?0x1d022401:prefix_next()&0x3fffffffu),0x21329,0x4511f,0x40,0,0,regs0,regs1,0,packet,draw==2?0:1);
   if(p->reason!=WII_TMU_PIPE_OK||p->texture_zero)continue;
   if(p->unit[0].use&&p->unit[1].use)(*dual)++;
   if(draw==3)wii_combiner_program_invalidate(&cache); /* clear/menu/external owner */
   if(draw==3){GX_SetTevColorIn(0,1,2,3,4);GX_SetTevAlphaIn(0,1,2,3,4);}
   combiner_c0=(GXColor){prefix_next(),prefix_next(),prefix_next(),prefix_next()};
   combiner_c1=(GXColor){prefix_next(),prefix_next(),prefix_next(),prefix_next()};
   prefix_backend_prepare(i,draw);
   GX_SetTevColor(GX_TEVREG1,(GXColor){3,9,17,31});GX_SetTevColor(GX_TEVREG2,(GXColor){2,8,16,30});
   /* Perturb suffix stages, paired selector/order words and dynamic colours.
    * Preserve prefix-owned arithmetic, as the production owner must do. */
   GX_SetNumTevStages(1);
   GX_SetTevKColor(GX_KCOLOR2,(GXColor){9,13,7,31});GX_SetTevKColor(GX_KCOLOR3,(GXColor){8,14,6,32});
   if(draw%2==0)GX_SetTevOrder(p->snapshot_first,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
   GX_SetTevKColorSel(p->snapshot_first,prefix_next()&31);
   GX_SetTevKAlphaSel(p->snapshot_first,prefix_next()&31);
   GX_SetTevSwapMode(0,prefix_next()&3,prefix_next()&3);
   prefix_snapshot_initial();
   unsigned original=PREFIX_REFERENCE_ORIGINAL(p);
   unsigned original_end=prefix_suffix(p,original);
   prefix_snapshot_reference();prefix_restore_initial();
   int fault;unsigned actual_end=wii_gx_combiner_program_emit(&cache,p,
    combiner_c0,combiner_c1,&fault);
   unsigned actual=p->fbi_first;
   if(fault){(*failures)++;return cases;}
   if(original!=actual||original_end!=actual_end||!original_end||!prefix_shadow_equal()){
    (*failures)++;return cases;
   }
   cases++;
   if((cases&63)==63)prefix_backend_flush();
  }
 }
 *hits=cache.hits;*misses=cache.misses;return cases;
}
