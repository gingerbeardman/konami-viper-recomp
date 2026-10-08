#include "gx_material_plan.h"
#include "gx_material_run.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static uint32_t rng=0x47a19031u;
static uint32_t next_u(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static const WiiTMUPipelinePlan *ok_plan(WiiMaterialPlans *plans,uint32_t regs0[9],uint32_t regs1[9]){
    for(unsigned n=0;n<4096;n++){
        unsigned lod=next_u()%32;
        regs0[0]=(next_u()&0x3ffff000u)|0xa07u;regs1[0]=(next_u()&0x3ffff000u)|0xa07u;
        regs0[1]=lod|(lod<<6);regs1[1]=lod|(lod<<6);regs0[2]=next_u();regs1[2]=next_u();
        wii_material_plans_invalidate(plans);
        const WiiTMUPipelinePlan *plan=wii_material_plan_get(plans,0x1d022401,0x21329,0x4511f,0x40,0,0,regs0,regs1,0,0x3b,1);
        if(plan->reason==WII_TMU_PIPE_OK&&!plan->texture_zero)return plan;
    }
    return NULL;
}
int main(void){
    uint32_t regs0[9]={0},regs1[9]={0},alt0[9]={0},alt1[9]={0};
    WiiMaterialPlans plans={0},other={0};
    const WiiTMUPipelinePlan *plan=ok_plan(&plans,regs0,regs1);
    assert(plan&&plan->snapshot_stages<=1);
    unsigned stages=wii_material_run_color_stages(plan);
    assert(stages==plan->rejection_first+plan->fbi.rejection_stages);
    assert(stages==plan->snapshot_first+plan->snapshot_stages+plan->fbi.rejection_stages);
    WiiMaterialRun run={0};
    assert(!wii_material_run_resume(&run,plan));
    wii_material_run_publish(&run,plan,stages);
    assert(run.valid&&run.emits==1&&wii_material_run_resume(&run,plan));
    wii_material_run_invalidate(&run);
    assert(!run.valid&&!wii_material_run_resume(&run,plan)&&run.emits==1);
    wii_material_run_publish(&run,plan,stages);
    assert(wii_material_run_resume(&run,plan));
    memcpy(alt0,regs0,sizeof alt0);memcpy(alt1,regs1,sizeof alt1);alt0[2]^=0x00ff00ffu;
    const WiiTMUPipelinePlan *rewritten=wii_material_plan_get(&other,0x1d022401,0x21329,0x4511f,0x40,0,0,alt0,alt1,0,0xff,1);
    assert(rewritten&&rewritten!=plan);
    assert(!wii_material_run_resume(&run,rewritten));
    wii_material_run_publish(&run,plan,0);
    assert(!run.valid&&!wii_material_run_resume(&run,plan));
    WiiTMUPipelinePlan broken=*plan;broken.snapshot_stages=2;
    assert(!wii_material_run_color_stages(&broken));
    broken=*plan;broken.rejection_first++;
    assert(!wii_material_run_color_stages(&broken));
    broken=*plan;broken.fbi.rejection_stages++;
    assert(!wii_material_run_color_stages(&broken));
    puts("Material-run suffix resume/invalidate/stage contract PASS");
}
