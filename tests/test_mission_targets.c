#define main mission_adapter_test_main
#include "test_mission_mode.c"
#undef main

int main(int argc,char **argv) {
    g_ram=calloc(1,RAM_SIZE); assert(g_ram);
    PPCContext c={0}; c.r[2]=0x154da8; c.r[1]=0x100000;
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb"); assert(f);
        assert(fread(g_ram,1,RAM_SIZE,f)==RAM_SIZE); fclose(f);
        mission_csv_load(); assert(MISSION_COUNT>0);
        for(int i=0;i<MISSION_COUNT;i++) {
            const char *region=getenv("RT_MISSION_TEST_REGION");
            if(region && k_missions[i].region!=(unsigned)atoi(region)) continue;
            assert(mission_find_targets(&c,&k_missions[i]));
            printf("%s: %u targets, goal %u\n",k_missions[i].name,g_mission_target_count,g_mission_target_goal);
        }
        free(g_ram); return 0;
    }
    assert(mission_scenery_selected("hitA;hitB","hitA",9));
    assert(mission_scenery_selected("hitA:2;hitB:4","hitA",2));
    assert(mission_scenery_selected("hitA:2;hitB:4","hitB",4));
    assert(!mission_scenery_selected("hitA:2;hitB:4","hitA",4));
    assert(!mission_scenery_selected("hitA:2;hitB:4","hitB",2));
    assert(!mission_scenery_selected("hitA:2x;hitB:","hitA",2));
    /* 55 distinct chairs, with native instance identity and two kinds. */
    MissionDefinition barrel={.roll_degrees=360};
    STF32(test_car+0x174,0); STF32(test_car+0x178,0); STF32(test_car+0x17c,12);
    STF32(test_car+0x1b0,0);
    for(unsigned step=0;step<=4;step++) {
        float a=step*1.57079632679f;
        for(unsigned wheel=0;wheel<4;wheel++) {
            float side=(wheel&1) ? 1 : -1;
            STF32(test_car+0x230+wheel*4,side*cosf(a));
            STF32(test_car+0x240+wheel*4,12+side*sinf(a));
            STF32(test_car+0x250+wheel*4,wheel<2 ? -1 : 1);
        }
        mission_stunt_tick(&c,test_car,&barrel,step*16);
        assert(!g_mission_roll_landed); /* Rotation alone is not a landing. */
    }
    STF32(test_car+0x17c,10);
    mission_stunt_tick(&c,test_car,&barrel,100);
    assert(g_mission_roll_landed);
    g_mission_roll_angle=0; g_mission_roll_airborne=g_mission_roll_landed=0;
    STF32(test_car+0x17c,12);
    mission_stunt_tick(&c,test_car,&barrel,120);
    STF32(test_car+0x17c,10);
    mission_stunt_tick(&c,test_car,&barrel,140);
    assert(!g_mission_roll_landed); /* An ordinary jump cannot satisfy a roll. */
    /* Both directions survive native yaw flipping upside down. Brief ground
     * contact on the side/roof must not reset an otherwise complete roll. */
    for(int direction=-1;direction<=1;direction+=2) {
        g_mission_roll_angle=0;g_mission_roll_airborne=g_mission_roll_landed=0;
        g_mission_roll_vector_valid=0;
        for(unsigned step=0;step<=8;step++) {
            float a=direction*.785398163397f*step;
            STF32(test_car+0x17c,step>=5 && step<8 ? 12 : 10);
            STF32(test_car+0x1b0,step>=3 && step<=5 ? 3.14159265359f : 0);
            for(unsigned wheel=0;wheel<4;wheel++) {
                float side=(wheel&1) ? 1 : -1;
                STF32(test_car+0x230+wheel*4,side*cosf(a));
                STF32(test_car+0x240+wheel*4,10+side*sinf(a));
                STF32(test_car+0x250+wheel*4,wheel<2 ? -1 : 1);
            }
            mission_stunt_tick(&c,test_car,&barrel,200+step*16);
            if(step<8) assert(!g_mission_roll_landed);
        }
        assert(g_mission_roll_landed && fabsf(g_mission_roll_angle)>6);
    }
    MissionDefinition wallride={.wallride_distance=80,.custom_gates=1,.custom_finish=1,.end_x=100};
    STF32(test_car+0xac,10); ST8(test_car+0x3a4,1);
    for(unsigned wheel=0;wheel<4;wheel++) {
        float side=(wheel&1)?1:-1;
        STF32(test_car+0x230+wheel*4,side);
        STF32(test_car+0x240+wheel*4,10+side);
        STF32(test_car+0x250+wheel*4,0);
    }
    g_mission_run.previous=(MissionPosition){0,10,0};
    mission_stunt_tick(&c,test_car,&wallride,150);
    STF32(test_car+0x174,5); mission_stunt_tick(&c,test_car,&wallride,166);
    assert(g_mission_wall_distance==5);
    ST8(test_car+0x3a4,0); mission_stunt_tick(&c,test_car,&wallride,180);
    assert(g_mission_wall_distance==5); /* A tilted car without wall contact isn't wall riding. */
    /* Approach waypoints must precede a jump landing. The last waypoint
     * alone keeps the mission running until the grounded landing is judged. */
    MissionDefinition guided_jump={.landing=1,.custom_gates=5,.jump_height=1.5};
    k_missions[g_mission_selected]=guided_jump; g_mission_target_goal=0;
    g_mission_jump_height=2; g_mission_landed=0;
    g_mission_run.gate_count=5; g_mission_run.next_gate=4;
    g_mission_run.phase=MISSION_RUNNING; mission_check_objective();
    assert(g_mission_run.phase==MISSION_RUNNING);
    g_mission_run.next_gate=5; g_mission_run.phase=MISSION_PASSED;
    mission_check_objective(); assert(g_mission_run.phase==MISSION_RUNNING);
    g_mission_landed=1; mission_check_objective(); assert(g_mission_run.phase==MISSION_PASSED);
    g_mission_run.phase=MISSION_TIME_FAILED; mission_check_objective();
    assert(g_mission_run.phase==MISSION_TIME_FAILED);
    g_mission_run.phase=MISSION_RUNNING;g_mission_run.gate_count=0;g_mission_landed=0;
    /* A perfect handbrake turn needs a clean, grounded release near 180
     * degrees. Sliding past the zone, hitting scenery or over-rotating fails. */
    MissionDefinition turn={.handbrake_turn=180,.turn_radius=25};
    STF32(test_car+0x174,0);STF32(test_car+0x178,0);STF32(test_car+0x17c,10);
    STF32(test_car+0xac,10);STF32(test_car+0x1b0,0);
    g_mission_turn_active=g_mission_landed=0;g_mission_run.contacts=0;g_analog[3]=200;
    mission_stunt_tick(&c,test_car,&turn,200);
    STF32(test_car+0x1b0,1.57079632679f);mission_stunt_tick(&c,test_car,&turn,216);
    STF32(test_car+0x1b0,3.14159265359f);mission_stunt_tick(&c,test_car,&turn,232);
    assert(!g_mission_landed);g_analog[3]=-200;mission_stunt_tick(&c,test_car,&turn,248);
    assert(g_mission_landed);
    for(unsigned failure=0;failure<3;failure++) {
        g_mission_turn_active=g_mission_landed=0;g_mission_run.contacts=0;g_analog[3]=200;
        STF32(test_car+0x174,0);STF32(test_car+0x1b0,0);
        mission_stunt_tick(&c,test_car,&turn,300);
        STF32(test_car+0x1b0,1.57079632679f);mission_stunt_tick(&c,test_car,&turn,316);
        if(failure==0) g_mission_run.contacts=1;
        if(failure==1) STF32(test_car+0x174,26);
        STF32(test_car+0x1b0,failure==2 ? 3.66519142919f : 3.14159265359f);
        mission_stunt_tick(&c,test_car,&turn,332);
        g_analog[3]=-200;mission_stunt_tick(&c,test_car,&turn,348);
        assert(!g_mission_landed && !g_mission_turn_active);
    }
    /* A collision cancels the entire press, rather than restarting the
     * angular reference while the same handbrake input remains held. */
    g_mission_turn_active=g_mission_landed=g_mission_turn_button_previous=0;
    g_mission_run.contacts=0;g_analog[3]=200;
    STF32(test_car+0x174,0);STF32(test_car+0x1b0,0);
    mission_stunt_tick(&c,test_car,&turn,350);assert(g_mission_turn_active);
    g_mission_run.contacts=1;mission_stunt_tick(&c,test_car,&turn,351);
    assert(!g_mission_turn_active);
    mission_stunt_tick(&c,test_car,&turn,352);assert(!g_mission_turn_active);
    g_analog[3]=0;mission_stunt_tick(&c,test_car,&turn,353);
    g_analog[3]=200;mission_stunt_tick(&c,test_car,&turn,354);
    assert(g_mission_turn_active); /* A fresh press starts a fresh attempt. */
    g_analog[3]=0;mission_stunt_tick(&c,test_car,&turn,355);
    /* Releasing early ends the attempt; later unbraked rotation cannot count. */
    g_mission_turn_active=g_mission_landed=0;g_mission_run.contacts=0;g_analog[3]=200;
    STF32(test_car+0x174,0);STF32(test_car+0x1b0,0);
    mission_stunt_tick(&c,test_car,&turn,360);
    STF32(test_car+0x1b0,1.57079632679f);mission_stunt_tick(&c,test_car,&turn,376);
    g_analog[3]=0;mission_stunt_tick(&c,test_car,&turn,392);
    assert(!g_mission_turn_active && !g_mission_landed);
    STF32(test_car+0x1b0,3.14159265359f);mission_stunt_tick(&c,test_car,&turn,408);
    assert(!g_mission_landed);
    STF32(test_car+0x174,0);g_mission_run.contacts=0;
    /* Accumulate only grounded side pairs: ordinary driving and jumps cannot
     * earn two-wheel time, and a gap in contact cannot bridge two samples. */
    MissionDefinition two_wheels={.two_wheel_ms=10000};
    g_mission_two_wheel_ms=0;g_mission_two_wheel_previous=0;g_mission_run.last_sample=0;
    for(unsigned i=0;i<4;i++) {
        STF32(test_car+0x230+4*i,i&1 ? 1 : -1);
        STF32(test_car+0x240+4*i,i&1 ? 11 : 10);
        STF32(test_car+0x250+4*i,i<2 ? -1 : 1);
    }
    mission_stunt_tick(&c,test_car,&two_wheels,400);g_mission_run.last_sample=400;
    mission_stunt_tick(&c,test_car,&two_wheels,416);assert(g_mission_two_wheel_ms==16);
    for(unsigned i=0;i<4;i++) STF32(test_car+0x240+4*i,12);
    g_mission_run.last_sample=416;mission_stunt_tick(&c,test_car,&two_wheels,432);
    assert(g_mission_two_wheel_ms==16 && !g_mission_two_wheel_previous);
    /* Vehicles use native instance/collider ownership, never just distance. */
    MissionDefinition vehicles={.traffic=1,.target_count=2,.vehicle_destroy=1};
    strcpy(vehicles.target_vehicles,"all");
    ST32(c.r[2]+0x614,0x300000); ST32(0x300004,0x310000); ST32(0x300008,3);
    for(unsigned i=0;i<3;i++) {
        unsigned p=0x310000+i*128;
        ST32(p,0x4000); ST32(p+0x10,0x320000); ST32(p+0x50,i);
        ST32(p+0x60,0x330000+i*0x500); STF32(p+0x20,100+i*10); STF32(p+0x28,200);
    }
    assert(mission_find_targets(&c,&vehicles));
    assert(g_mission_target_count==3 && g_mission_target_goal==2);
    k_missions[0]=vehicles; atomic_store(&g_mission_selected,0);
    atomic_store(&g_mission_phase,MISSION_RUNNING); atomic_store(&g_mission_broken,0);
    ST32(c.r[2]+0x488,test_car); ST32(test_car+0x4d8,0x550000);
    ST8(test_car+0x3c4,1); ST32(0x55043c,0x33000c);
    mission_vehicle_tick(&c,test_car,&vehicles);
    assert(!atomic_load(&g_mission_broken)); /* A bump isn't destruction. */
    c.r[29]=0x310000; c.r[30]=0x56000c; mission_vehicle_destroy_hook(&c);
    assert(!atomic_load(&g_mission_broken)); /* NPC crashes cannot count. */
    c.r[30]=0x55000c; mission_vehicle_destroy_hook(&c); mission_vehicle_destroy_hook(&c);
    assert(atomic_load(&g_mission_broken)==1);
    k_missions[0].vehicle_destroy=0;
    ST32(0x55043c,0x33050c);
    mission_vehicle_tick(&c,test_car,&k_missions[0]);
    assert(atomic_load(&g_mission_broken)==2);
    /* Parked cars use stable catalog identity and exact live collider ownership. */
    ST32(c.r[2]+0x40,0x340000);ST32(0x340000,2);
    ST32(0x340024+8,31);ST32(0x340024+16,64);ST32(0x340024+20,0x350000);
    ST32(c.r[2]+0x380,0x360000);ST32(0x360004,2);
    for(unsigned i=0;i<2;i++) {
        unsigned p=0x360308+i*36;
        ST32(p,0x350000+(5+i)*16);STF32(p+4,10+i*10);STF32(p+12,20);
    }
    MissionDefinition parked={0};strcpy(parked.parked_models,"5");
    assert(mission_find_targets(&c,&parked));assert(g_mission_target_count==1);
    assert(g_mission_targets[0].family==7 && g_mission_targets[0].kind==5);
    ST32(c.r[2]+0x3b0,0x370000);ST32(0x370004,1);
    ST32(0x370020,0x350000+5*16);ST32(0x370034,0x380000);
    STF32(0x37003c,10);STF32(0x370044,20);
    atomic_store(&g_mission_broken,0);ST8(test_car+0x3c4,1);
    ST32(0x55043c,0x39000c);mission_parked_tick(&c,test_car,&parked);
    assert(!atomic_load(&g_mission_broken)); /* Nearby/NPC contact isn't this car. */
    ST32(0x55043c,0x38000c);STF32(0x37003c,30);
    mission_parked_tick(&c,test_car,&parked);assert(!atomic_load(&g_mission_broken));
    STF32(0x37003c,10);mission_parked_tick(&c,test_car,&parked);
    mission_parked_tick(&c,test_car,&parked);assert(atomic_load(&g_mission_broken)==1);
    strcpy(parked.parked_models,"nearest");parked.contact_targets=1;
    parked.target_x[0]=19;parked.target_z[0]=20;parked.target_radius=5;
    assert(mission_find_targets(&c,&parked));assert(g_mission_targets[0].kind==6);
    /* An anchor selects a particular model, ignoring a nearer different car. */
    strcpy(parked.parked_models,"5");parked.target_radius=25;
    assert(mission_find_targets(&c,&parked));assert(g_mission_target_count==1 && g_mission_targets[0].kind==5);
    parked.target_radius=5;assert(!mission_find_targets(&c,&parked));
    strcpy(parked.parked_models,"nearest");
    parked.target_x[0]=100;assert(!mission_find_targets(&c,&parked));
    MissionDefinition d={.start_cp=0,.finish_cp=10};
    strcpy(d.target_models,"chairA;chairB"); strcpy(d.target_label,"CHAIRS");
    ST32(c.r[2]+0x53c,0x200000); ST32(c.r[2]+0x778,0x210000);
    ST32(0x200000,0x220000); ST32(0x20000c,0x230000);
    for(unsigned m=0;m<2;m++) {
        unsigned cb=0x240000+m*48;
        ST32(0x210004+m*16,cb); ST32(cb,k_mission_models[m].create);
        ST32(cb+4,c.r[2]); ST32(cb+16,c.r[2]); ST32(cb+28,c.r[2]);
    }
    for(unsigned i=0;i<55;i++) {
        unsigned p=0x220000+i*68,m=i<43?0:1,index=i<43?i:i-43,cb=0x240000+m*48;
        ST32(p,1); ST16(p+16,index); ST32(p+20,cb); ST32(p+24,cb+12); ST32(p+28,cb+24);
        STF32(p+36,i*3); STF32(p+44,100);
    }
    assert(mission_find_targets(&c,&d)); assert(g_mission_target_count==55 && g_mission_target_goal==55);
    k_missions[0]=d; atomic_store(&g_mission_selected,0); atomic_store(&g_mission_broken,0);
    atomic_store(&g_mission_phase,MISSION_RUNNING); g_mission_run.phase=MISSION_RUNNING;
    ST32(c.r[2]+0x488,test_car); ST32(test_car+0x4d8,0x550000);
    ST32(c.r[2]+0x2e0,0x250000); ST32(0x250010,0x260000); ST32(0x25001c,0x261000);
    for(unsigned i=0;i<55;i++) {
        unsigned kind=i<43?0:1,index=i<43?i:i-43,live=0x270000+i*0x60;
        ST32((kind?0x261000:0x260000)+index*4,live);
        c.r[27]=kind; c.r[28]=live; c.r[26]=0x540000;
        ST32(0x540430,0x560000); mission_target_hook(&c,1);
        assert(atomic_load(&g_mission_broken)==i); /* NPC hits don't count. */
        ST32(0x540430,0x55000c); mission_target_hook(&c,1); mission_target_hook(&c,1);
        assert(atomic_load(&g_mission_broken)==i+1);
        /* Streaming the same chair back in rejects its native recreation. */
        c.r[31]=kind; c.r[27]=index; c.r[3]=1;
        mission_target_spawn_hook(&c,1); assert(c.r[3]==0);
        mission_check_objective(); assert(g_mission_run.phase==(i==54?MISSION_PASSED:MISSION_RUNNING));
    }
    d.target_count=20; assert(mission_find_targets(&c,&d)); assert(g_mission_target_goal==20);
    d.target_count=56; assert(!mission_find_targets(&c,&d));
    /* Fruit crates in one stand share one objective. */
    d.target_count=0; d.target_group_radius=4;
    assert(mission_find_targets(&c,&d)); assert(g_mission_target_goal<55);
    /* Pedestrians count native evasive transitions once, within player range. */
    g_mission_target_count=g_mission_target_goal=1;
    g_mission_targets[0]=(MissionTarget){.family=1,.kind=8,.index=0,.x=10,.z=20};
    ST32(0x250010+8*12,0x262000); ST32(0x262000,0x280000);
    STF32(test_car+0x174,10); STF32(test_car+0x178,20);
    c.r[28]=0x280050; atomic_store(&g_mission_broken,0);
    mission_dodge_hook(&c,c.r[28]); mission_dodge_hook(&c,c.r[28]);
    assert(atomic_load(&g_mission_broken)==1);
    c.r[31]=8; c.r[27]=0; c.r[3]=1;
    mission_target_spawn_hook(&c,1); assert(c.r[3]==1); /* Dodged people remain. */
    g_mission_targets[0].met=0; STF32(test_car+0x174,100);
    mission_dodge_hook(&c,c.r[28]); assert(atomic_load(&g_mission_broken)==1);
    /* Dogs share the reaction routine, but not the pedestrian instance array. */
    g_mission_targets[0]=(MissionTarget){.family=1,.kind=9,.index=0,.x=10,.z=20};
    ST32(0x250010+9*12,0x263000); ST32(0x263000,0x281000);
    STF32(test_car+0x174,10); atomic_store(&g_mission_broken,0);
    mission_dodge_hook(&c,0x280050); assert(atomic_load(&g_mission_broken)==0);
    mission_dodge_hook(&c,0x281050); mission_dodge_hook(&c,0x281050);
    assert(atomic_load(&g_mission_broken)==1);
    g_mission_targets[0].met=0; atomic_store(&g_mission_broken,0);
    c.r[29]=0x281050; ST32(c.r[1]+0x48,0x560000);
    mission_dog_scare_hook(&c); assert(atomic_load(&g_mission_broken)==0);
    ST32(c.r[1]+0x48,LD32(test_car+0x4d8));
    mission_dog_scare_hook(&c); mission_dog_scare_hook(&c);
    assert(atomic_load(&g_mission_broken)==1);
    g_mission_targets[0].met=0;
    ST32(0x281000,0x282000); STF32(0x282018,30); STF32(0x282020,40);
    mission_actor_tick(&c);
    assert(g_mission_targets[0].x==30 && g_mission_targets[0].z==40);
    /* The committed scare event also belongs to People Pleaser. */
    g_mission_targets[0]=(MissionTarget){.family=1,.kind=8,.index=0};
    atomic_store(&g_mission_broken,0);c.r[29]=0x281050;
    mission_dog_scare_hook(&c); assert(!atomic_load(&g_mission_broken));
    c.r[29]=0x280050;ST32(c.r[1]+0x48,0x560000);
    mission_dog_scare_hook(&c); assert(!atomic_load(&g_mission_broken));
    ST32(c.r[1]+0x48,LD32(test_car+0x4d8));
    mission_dog_scare_hook(&c);mission_dog_scare_hook(&c);
    assert(atomic_load(&g_mission_broken)==1);
    /* Solid kiosks and breakable props use different native arrays/registers. */
    for(unsigned family=2;family<=3;family++) {
        unsigned kind=family==2 ? 12 : 0,table=0x290000,objects=0x291000,live=0x292000;
        ST32(c.r[2]+(family==2 ? 0x3a0 : 0x3d4),table);
        ST32(table+kind*12+(family==2 ? 0xf8 : 0x60),objects); ST32(objects,live);
        g_mission_targets[0]=(MissionTarget){.family=family,.kind=kind,.index=0,.group=0};
        memset(g_mission_target_groups,0,sizeof g_mission_target_groups);
        atomic_store(&g_mission_broken,0); c.r[30]=live;
        if(family==2) { c.r[28]=kind; c.r[31]=0x540000; }
        else { c.r[26]=kind; c.r[28]=0x540000; }
        ST32(0x54043c,0x55000c); mission_target_hook(&c,family); mission_target_hook(&c,family);
        assert(atomic_load(&g_mission_broken)==1);
        c.r[3]=1;
        if(family==2) { c.r[27]=kind; c.r[30]=0; }
        else { c.r[31]=kind; c.r[29]=0; }
        mission_target_spawn_hook(&c,family); assert(c.r[3]==0);
        k_missions[g_mission_selected].scenery_touch=1; c.r[3]=1;
        mission_target_spawn_hook(&c,family); assert(c.r[3]==1); /* Touched scenery remains. */
        k_missions[g_mission_selected].scenery_touch=0;
        g_mission_targets[0].met=0; c.r[3]=1;
        mission_target_spawn_hook(&c,family); assert(c.r[3]==1);
        g_mission_targets[0].met=1; atomic_store(&g_mission_phase,MISSION_OFF);
        mission_target_spawn_hook(&c,family); assert(c.r[3]==1); /* Normal races unaffected. */
        atomic_store(&g_mission_phase,MISSION_RUNNING);
    }
    /* Benches stream through the generic breakable pool. A previously destroyed
     * placement must fail the visibility check before allocating that pool. */
    g_mission_target_count=1;
    g_mission_targets[0]=(MissionTarget){.family=5,.kind=4,.index=2,.met=1};
    c.r[31]=4; c.r[29]=2; c.r[3]=1;
    mission_target_spawn_hook(&c,5); assert(c.r[3]==0);
    c.r[29]=3; c.r[3]=1;
    mission_target_spawn_hook(&c,5); assert(c.r[3]==1);
    c.r[29]=2; g_mission_targets[0].met=0;
    mission_target_spawn_hook(&c,5); assert(c.r[3]==1);
    /* Authored cloister targets have coordinates rather than catalog indices. */
    MissionDefinition *pillars=&k_missions[g_mission_selected];
    pillars->break_count=2; pillars->break_type=0; pillars->break_radius=2;
    pillars->break_x[0]=10; pillars->break_z[0]=20;
    pillars->break_x[1]=30; pillars->break_z[1]=40;
    g_mission_break_mask=1; c.r[31]=0; c.r[27]=0x293000;
    STF32(c.r[27]+4,10); STF32(c.r[27]+12,20); c.r[3]=1;
    mission_target_spawn_hook(&c,5); assert(c.r[3]==0);
    STF32(c.r[27]+4,30); STF32(c.r[27]+12,40); c.r[3]=1;
    mission_target_spawn_hook(&c,5); assert(c.r[3]==1);
    g_mission_break_mask=0; STF32(c.r[27]+4,10); STF32(c.r[27]+12,20);
    mission_target_spawn_hook(&c,5); assert(c.r[3]==1); /* Retry restores pillars. */
    pillars->break_count=0;
    d=(MissionDefinition){.contact_targets=2,.target_radius=3.5,.target_at_finish=1};
    d.target_x[0]=10; d.target_x[1]=20;
    assert(mission_find_targets(&c,&d)); atomic_store(&g_mission_broken,0);
    mission_contact_targets(&d,(MissionPosition){10,0,0},0); assert(!atomic_load(&g_mission_broken));
    mission_contact_targets(&d,(MissionPosition){10,0,0},1);
    mission_contact_targets(&d,(MissionPosition){10,0,0},1); assert(atomic_load(&g_mission_broken)==1);
    mission_contact_targets(&d,(MissionPosition){20,0,0},1); assert(atomic_load(&g_mission_broken)==2);
    /* A market stall side impact places the car away from the stall centre.
     * Require actual contact and deduplicate approaches from either side. */
    d.target_radius=6.5;
    assert(mission_find_targets(&c,&d)); atomic_store(&g_mission_broken,0);
    mission_contact_targets(&d,(MissionPosition){4,0,0},0); assert(!atomic_load(&g_mission_broken));
    mission_contact_targets(&d,(MissionPosition){4,0,0},1); assert(atomic_load(&g_mission_broken)==1);
    mission_contact_targets(&d,(MissionPosition){16,0,0},1); assert(atomic_load(&g_mission_broken)==2);
    mission_contact_targets(&d,(MissionPosition){24,0,0},1); assert(atomic_load(&g_mission_broken)==2);
    /* Native stall contact is counted even when the player flag has cleared. */
    k_missions[g_mission_selected]=d;
    assert(mission_find_targets(&c,&d)); atomic_store(&g_mission_broken,0);
    c.r[28]=0x292000; c.r[26]=0x540000;
    STF32(c.r[28]+24,10); STF32(c.r[28]+32,0);
    ST32(c.r[26]+0x430,0x56000c);
    mission_authored_scenery_contact(&c,1); assert(!atomic_load(&g_mission_broken));
    ST32(c.r[26]+0x430,LD32(test_car+0x4d8)+12);
    mission_authored_scenery_contact(&c,1); mission_authored_scenery_contact(&c,1);
    assert(atomic_load(&g_mission_broken)==1);
    c.r[30]=c.r[28]; c.r[31]=c.r[26];
    STF32(c.r[30]+24,20); ST32(c.r[31]+0x43c,LD32(test_car+0x4d8)+12);
    mission_authored_scenery_contact(&c,2); assert(atomic_load(&g_mission_broken)==2);
    assert(mission_find_targets(&c,&d)); atomic_store(&g_mission_broken,0);
    c.r[29]=test_car; STF32(test_car+0x174,4); STF32(test_car+0x178,0);
    ST8(test_car+0x3c4,1); ST16(LD32(test_car+0x4d8)+0x438,2<<8);
    mission_player_contact_hook(&c); assert(atomic_load(&g_mission_broken)==1);
    mission_player_contact_hook(&c); assert(atomic_load(&g_mission_broken)==1);
    ST8(test_car+0x3c4,0);
    d.target_radius=3.5;
    d.region_visit=1;assert(mission_find_targets(&c,&d));atomic_store(&g_mission_broken,0);
    STF32(test_car+0x174,10);STF32(test_car+0x178,0);STF32(test_car+0x17c,15);
    mission_region_targets(&c,&d,test_car);assert(!atomic_load(&g_mission_broken));
    STF32(test_car+0x17c,10);
    mission_region_targets(&c,&d,test_car);mission_region_targets(&c,&d,test_car);
    assert(atomic_load(&g_mission_broken)==1);
    STF32(test_car+0x174,20);invalid_terrain=1;
    mission_region_targets(&c,&d,test_car);assert(atomic_load(&g_mission_broken)==1);
    invalid_terrain=0;mission_region_targets(&c,&d,test_car);
    assert(atomic_load(&g_mission_broken)==2);
    k_missions[0]=d; g_mission_run.phase=MISSION_RUNNING; mission_check_objective(); assert(g_mission_run.phase==MISSION_RUNNING);
    k_missions[0]=(MissionDefinition){.speed_goal=170}; g_mission_target_goal=0;
    g_mission_run.phase=MISSION_PASSED; g_mission_max_speed=169.99; mission_check_objective(); assert(g_mission_run.phase==MISSION_OBJECTIVE_FAILED);
    g_mission_run.phase=MISSION_PASSED; g_mission_max_speed=170; mission_check_objective(); assert(g_mission_run.phase==MISSION_PASSED);
    k_missions[0]=(MissionDefinition){.jump_height=1.5};
    g_mission_run.phase=MISSION_PASSED; g_mission_jump_height=1; mission_check_objective(); assert(g_mission_run.phase==MISSION_OBJECTIVE_FAILED);
    g_mission_run.phase=MISSION_PASSED; g_mission_jump_height=2; mission_check_objective(); assert(g_mission_run.phase==MISSION_PASSED);
    /* Hints appear for a small remainder or after an idle interval. */
    d=(MissionDefinition){.hint_remaining=3,.hint_idle_ms=30000,.completion_delay_ms=750};
    g_mission_run.phase=MISSION_RUNNING; g_mission_run.started=1000;
    g_mission_last_collect_ms=0; g_mission_target_count=g_mission_target_goal=5;
    memset(g_mission_target_groups,0,sizeof g_mission_target_groups);
    for(unsigned i=0;i<5;i++) g_mission_targets[i]=(MissionTarget){.x=10+i*10,.group=i};
    atomic_store(&g_mission_broken,0);
    mission_collection_hint(&d,(MissionPosition){0},30999); assert(atomic_load(&g_mission_nearest_item)==UINT_MAX);
    mission_collection_hint(&d,(MissionPosition){0},31000); assert(atomic_load(&g_mission_nearest_item)==10);
    g_mission_last_collect_ms=31000; atomic_store(&g_mission_broken,1); g_mission_targets[0].met=1;
    mission_collection_hint(&d,(MissionPosition){0},31001); assert(atomic_load(&g_mission_nearest_item)==UINT_MAX);
    atomic_store(&g_mission_broken,2); g_mission_target_groups[1]=1;
    mission_collection_hint(&d,(MissionPosition){0},31001); assert(atomic_load(&g_mission_nearest_item)==30);
    atomic_store(&g_mission_broken,5); k_missions[0]=d;
    mission_collection_hint(&d,(MissionPosition){0},31001); assert(atomic_load(&g_mission_nearest_item)==UINT_MAX);
    clock_ms=32000; atomic_store(&g_mission_result_at,0); g_mission_run.phase=MISSION_PASSED;
    g_mission_run.elapsed_ms=1234; mission_publish(); assert(!mission_result());
    clock_ms+=749; assert(!mission_result()); clock_ms++; assert(mission_result());
    assert(g_mission_run.elapsed_ms==1234);
    k_missions[0]=(MissionDefinition){.landing=1,.completion_delay_ms=750};
    clock_ms=34000;atomic_store(&g_mission_result_at,0);mission_publish();
    assert(!mission_result());clock_ms+=749;assert(!mission_result());
    clock_ms++;assert(mission_result() && g_mission_run.elapsed_ms==1234);
    /* CSV save locates reordered names and preserves quoted descriptions. */
    const char *csv="/private/tmp/mission-gold-save-test.csv";
    FILE *f=fopen(csv,"w"); assert(f);
    fputs("name,description,gold,silver,bronze\nOTHER,other,1,3,5\nTARGET,\"quoted, description\",8,10,12\n",f); fclose(f);
    setenv("RT_MISSIONS_CSV",csv,1);
    assert(mission_csv_save_gold("TARGET",9000));
    char contents[512]={0}; f=fopen(csv,"r"); assert(f); fread(contents,1,sizeof contents-1,f); fclose(f);
    assert(strstr(contents,"OTHER,other,1,3,5"));
    assert(strstr(contents,"TARGET,\"quoted, description\",9.000,11.000,13.000"));
    assert(!mission_csv_save_gold("MISSING",10000));
    unsetenv("RT_MISSIONS_CSV"); remove(csv);
    free(g_ram); puts("mission targets: discovery, 55 chairs, player ownership, deduplication, automatic/finish objectives, speed and jump thresholds passed");
}
