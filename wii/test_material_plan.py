#!/usr/bin/env python3
"""Actual device masked writes, invalidation and exact full planner values."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parent.parent
fixture=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)','void device_contract_main(void)')
source=fixture+r"""
static WiiMaterialPlans plans;
static unsigned invalidations;
static void invalidate_plans(void){invalidations++;wii_material_plans_invalidate(&plans);}
static uint32_t rng=0x72910385;
static uint32_t next(void){rng=rng*1664525u+1013904223u;return rng;}
static void check_plans(unsigned packet){
    const WiiTMUPipelinePlan *held[2];WiiTMUPipelinePlan copy[2];
    for(unsigned slot=0;slot<2;slot++){
        held[slot]=wii_material_plan_get(&plans,regs[65],regs[68],regs[67],regs[66],regs[77],regs[78],tmu_regs[0],tmu_regs[1],regs[135],packet,slot);
        copy[slot]=wii_gx_tmu_pipeline_plan(regs[65],regs[68],regs[67],regs[66],regs[77],regs[78],tmu_regs[0],tmu_regs[1],regs[135],packet,slot);
        assert(!memcmp(held[slot],&copy[slot],sizeof copy[slot]));
        assert(held[slot]==wii_material_plan_get(&plans,regs[65],regs[68],regs[67],regs[66],regs[77],regs[78],tmu_regs[0],tmu_regs[1],regs[135],packet,slot));
    }
    /* Preparing opposite alpha must not overwrite a recursively held slot. */
    for(unsigned slot=0;slot<2;slot++)assert(!memcmp(held[slot],&copy[slot],sizeof copy[slot]));
}
int main(void){
    device_contract_main();
    uint8_t *bytes=malloc(WII_VOODOO_VRAM_BYTES);assert(bytes);
    wii_voodoo_set_vram(bytes,WII_VOODOO_VRAM_BYTES);
    wii_voodoo_set_material_invalidate(invalidate_plans);voodoo_init();
    const unsigned fbi[]={65,68,67,66,77,78,135,81,82};
    const unsigned tmu[]={0,1,2,3,8,4,7,9};
    for(unsigned i=0;i<100000;i++){
        unsigned unit=next()%2,r,off;uint32_t before,v=next(),mask=next();int expected_change;
        if(i%4==0){mask=0;}else if(i%4==1){mask=~0u;}
        if(i&1){r=fbi[next()%9];off=0x200000+r*4;before=regs[r];expected_change=wii_material_fbi_word(r);}
        else {r=tmu[next()%8];off=0x200000+0x300+r*4+((2u<<unit)<<10);before=tmu_regs[unit][r];expected_change=wii_material_tmu_word(r);}
        unsigned old=invalidations;
        expected_change&=((before&~mask)|(v&mask))!=before;
        voodoo_reg_write(off,v,mask);assert(invalidations-old==(unsigned)expected_change);
        check_plans(i%3?0x3b:next()&255);
        if(i%1000==0){old=invalidations;voodoo_init();assert(invalidations==old+1);check_plans(0x3b);}
    }
    /* Accepted textured configurations, both alpha slots and all packet bytes. */
    voodoo_init();
    voodoo_reg_write(0x200104,0x1d022401,~0u);voodoo_reg_write(0x200110,0x2175b,~0u);
    voodoo_reg_write(0x20010c,0x4511f,~0u);voodoo_reg_write(0x200108,0x40,~0u);
    voodoo_reg_write(0x200300,0x10241a07,~0u);
    unsigned accepted=0;
    for(unsigned packet=0;packet<256;packet++){
        check_plans(packet);accepted+=plans.plan[0].reason==WII_TMU_PIPE_OK;
    }
    assert(accepted);

    /* Broad accepted and rejected TMU equations through actual bus writes. */
    unsigned admitted=0;
    for(unsigned i=0;i<30000;i++){
        voodoo_reg_write(0x200110,0x21329,~0u);
        for(unsigned unit=0;unit<2;unit++){
            unsigned select=(2u<<unit)<<10;
            uint32_t mode=i%3?(next()&0x3ffff000u)|0xa07u:0x10241a07u;
            unsigned lod=next()%32;
            voodoo_reg_write(0x200300+select,mode,~0u);
            voodoo_reg_write(0x200304+select,lod|(lod<<6),~0u);
            voodoo_reg_write(0x200308+select,next(),~0u);
            voodoo_reg_write(0x20030c+select,0,~0u);
            voodoo_reg_write(0x200320+select,0,~0u);
        }
        check_plans(0x3b);admitted+=plans.plan[0].reason==WII_TMU_PIPE_OK;
    }
    assert(admitted>1000);
    /* FIFO type1 material mutations share the same notification path. */
    agp_write(8,1);agp_write(9,256);agp_write(11,0x1000);
    unsigned before_fifo=invalidations;
    word(0x1000,(1u<<16)|(65u<<3)|1u);
    assert(invalidations==before_fifo);
    word(0x1004,regs[65]^1u);assert(invalidations==before_fifo+1);
    check_plans(0x3b);
    unsigned old=invalidations;wii_voodoo_set_renderer(NULL,NULL);assert(invalidations==old+1);
    wii_voodoo_set_material_invalidate(NULL);free(bytes);
    puts("Material plan actual-device100000 masked+30000 equation cases, mask/reset/packet/alpha/borrowed slots PASS");
}
"""
with tempfile.TemporaryDirectory(prefix='viper-material-proof-') as d:
    p=Path(d);(p/'proof.c').write_text(source)
    subprocess.run(['clang','-O2','-std=c11','-DVIPER_WII_MATERIAL_PLAN_CACHE','-fsanitize=address,undefined','-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures'),str(p/'proof.c'),'-o',str(p/'proof')],check=True)
    subprocess.run([str(p/'proof')],check=True)
