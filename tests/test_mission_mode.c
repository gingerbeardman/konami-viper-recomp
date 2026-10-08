#define GAME_ENH_MISSION_STYLE 1
#define GAME_ENH_RACE_RESTART_STYLE 1
#include "../runtime/runtime.h"
#include "../runtime/race_restart.h"
#include "../runtime/mission_mode.h"
#include <assert.h>
#include <stdio.h>

uint8_t *g_ram;
int16_t g_analog[4];
static uint64_t clock_ms;
static const uint32_t test_car=0x500000;
static unsigned placed, tracked, prepared, released, sound_initialized, engine_bank_requested;
static float placed_heading;
static unsigned invalid_terrain;
static void mock_sound(PPCContext *c) { assert(engine_bank_requested==sound_initialized+1); sound_initialized++; assert((LD32(0x600004)&0x3f00)==0); (void)c; }
static void mock_prepare(PPCContext *c) { assert(sound_initialized==prepared+1); prepared++; ST8(0x600014,0); c->r[3]=123; }
static void mock_cleanup(PPCContext *c) { assert(c->r[3]==1); }
static void mock_bank(PPCContext *c) { assert(c->r[3]==0xf0040000u); engine_bank_requested++; }
static void mock_release(PPCContext *c) { released++; c->r[3]=123; }
uint64_t rt_now(void) { return clock_ms*(CPU_HZ/1000); }
void rt_log(const char *fmt, ...) { (void)fmt; }
uint32_t rt_mmio_r32(uint32_t a) { (void)a; assert(0); return 0; }
uint32_t rt_mmio_r16(uint32_t a) { (void)a; assert(0); return 0; }
uint32_t rt_mmio_r8(uint32_t a) { (void)a; assert(0); return 0; }
void rt_mmio_w32(uint32_t a,uint32_t v) { (void)a;(void)v;assert(0); }
void rt_mmio_w16(uint32_t a,uint32_t v) { (void)a;(void)v;assert(0); }
void rt_mmio_w8(uint32_t a,uint32_t v) { (void)a;(void)v;assert(0); }
static void mock_place(PPCContext *c) {
    const MissionDefinition *placing=&k_missions[atomic_load(&g_mission_selected)];
    assert(placing->rolling_gate ? c->f[4]>0 : c->f[4]==placing->rolling_speed); placed++; placed_heading=(float)c->f[3];
    STF32(test_car+0x174,c->f[1]); STF32(test_car+0x178,c->f[2]); STF32(test_car+0x17c,10);
    ST8(test_car+0x3a4,0);
    memset(g_ram+c->r[1]-100,0x77,100); c->r[3]=0; c->budget-=100;
}
static void mock_track(PPCContext *c) {
    assert(c->r[3]==test_car+0x4dc); tracked++;
    assert(LD32(c->r[3]+0x28)==0x40000);
    assert(LD32(c->r[3]+0x2c)>=0x40100 && LD32(c->r[3]+0x2c)<0x402a0);
    assert(LD8(c->r[3]+1)==1);
    ST8(c->r[3]+1,2); ST8(c->r[3]+2,6); STF32(c->r[3]+8,3210);
    ST32(c->r[3]+0x2c,0x40120);
}
static void mock_tile(PPCContext *c) { memset(g_ram+c->r[5],0xbb,16); }
static void mock_ground(PPCContext *c) {
    c->f[1]=invalid_terrain ? 500 : 10;
    if(invalid_terrain) c->r[3]=0;
}
static void mock_obstacle(PPCContext *c) {
    unsigned catalog=LD32(c->r[2]+0x380),n=LD32(catalog+4),p=catalog+0x308+n*36;
    ST32(p,c->r[5]);memcpy(g_ram+p+4,g_ram+c->r[3],12);
    memcpy(g_ram+p+16,g_ram+c->r[4],12);ST32(catalog+4,n+1);
}
RtFn rt_lookup(uint32_t address) {
    switch (address) {
    case 0x5b56c: return mock_obstacle;
    case 0x55168: return mock_cleanup;
    case 0x3b320: return mock_bank;
    case 0x8d8ac: return mock_sound;
    case 0x8e17c: return mock_prepare;
    case 0xb49c0: case 0xb49fc: return mock_release;
    case 0x90264: return mock_place;
    case 0x8f384: return mock_track;
    case 0x5576c: return mock_tile;
    case 0x566f0: return mock_ground;
    default: assert(0); return NULL;
    }
}
int main(int argc, char **argv) {
    setenv("RT_MISSIONS_CSV","tests/fixtures/missions.csv",1); mission_csv_load();
    assert(k_missions[0].limit_ms==21000);
    assert(k_missions[2].limit_ms==21000 && k_missions[2].gold_ms==17000 && k_missions[2].silver_ms==19000);
    assert(strcmp(mission_medal(&k_missions[0],17000),"GOLD")==0);
    assert(strcmp(mission_medal(&k_missions[0],16500),"PLATINUM")==0);
    assert(strcmp(mission_medal(&k_missions[0],16501),"GOLD")==0);
    assert(mission_medal_colour(&k_missions[0],16500)==0xe0f4ff);
    assert(strcmp(mission_medal(&k_missions[0],17001),"SILVER")==0);
    assert(strcmp(mission_medal(&k_missions[0],19000),"SILVER")==0);
    assert(strcmp(mission_medal(&k_missions[0],19001),"BRONZE")==0);
    assert(strcmp(mission_medal(&k_missions[0],21000),"BRONZE")==0);
    assert(mission_medal(&k_missions[0],21001)==NULL);
    assert(mission_medal(&k_missions[1],1000)==NULL);
    assert(!mission_near_gold(&k_missions[0],k_missions[0].gold_ms));
    assert(mission_near_gold(&k_missions[0],k_missions[0].gold_ms+1));
    assert(!mission_near_gold(&k_missions[0],k_missions[0].gold_ms*106/100));
    assert(argc==2); mission_progress_init(argv[1]);
    g_ram=calloc(1,RAM_SIZE); assert(g_ram);
    PPCContext c={0}; c.r[2]=0x100000; c.r[1]=0xf00000; c.r[3]=test_car+0x4dc;
    float ground_y;
    invalid_terrain=1;
    assert(!mission_ground(&c,100,-110,&ground_y));
    assert(ground_y==500);
    invalid_terrain=0;
    assert(mission_ground(&c,100,-110,&ground_y) && ground_y==10);
    ST32(c.r[2]+0x488,test_car); ST32(c.r[2]+0x554,0x530000);
    ST32(0x8c0188,0x40000); ST16(0x40000,13); ST32(0x40004,0x40100); ST32(0x4000c,0x40000);
    ST8(0x40100+28,1);
    for (unsigned i=1;i<=10;i++) {
        STF32(0x40100+i*32,(i>=9 ? i*100+200 : i*100)); if (i<10) ST8(0x40100+i*32+28,i+2);
    }
    STF32(0x40100+11*32,1200); STF32(0x40100+11*32+4,1000);
    STF32(0x40100+12*32+4,1000);
    memset(g_ram+c.r[1]-8192,0xa5,8192);
    atomic_store(&g_race_ready,1);
    mission_request(0); mission_tick(&c); assert(mission_phase()==MISSION_ARMED && !placed);
    PPCContext saved=c; c.r[4]=5; mission_race_hook(&c); assert(c.r[4]==5); c=saved;
    mission_tick(&c); assert(mission_phase()==MISSION_RUNNING && placed==1 && tracked==1);
    assert(!memcmp(&c,&saved,sizeof c));
    assert(LD8(0x530011)==2 && LD8(0x530012)==6);
    assert(LDF32(0x53001c)==3210 && LDF32(0x5300c8)==3210 && LD32(0x5300d0)==0x40120);
    for (unsigned i=0;i<8192;i++) assert(g_ram[c.r[1]-8192+i]==0xa5);
    assert(LDF32(test_car+0x174)==0 && g_mission_run.gate_count==1);
    clock_ms=6000; STF32(test_car+0x174,120); mission_tick(&c);
    assert(mission_phase()==MISSION_PASSED && atomic_load(&g_mission_gate)==1);
    assert(atomic_load(&g_mission_best)==6000 && g_mission_record_count==1);
    mission_progress_init(argv[1]); assert(g_mission_record_count==1 && mission_record()->best==6000);
    g_mission_car_key=1; assert(!mission_record()); g_mission_car_key=0;
    g_mission_run.elapsed_ms=7000; mission_save_completion(); assert(mission_record()->best==6000);
    /* Retuning medal thresholds changes the recipe key, but the menu and
     * result screen must retain the same best time for the mission name/car. */
    unsigned old_key=g_mission_course_key;
    g_mission_course_key^=0x1000; g_mission_run.elapsed_ms=7000;
    mission_save_completion(); assert(mission_record()->best==7000);
    assert(mission_menu_best(0)==6000 && atomic_load(&g_mission_best)==6000);
    g_mission_course_key=old_key;
    MissionDefinition swap=k_missions[0]; k_missions[0]=k_missions[1]; k_missions[1]=swap;
    assert(mission_menu_best(1)==6000 && mission_menu_best(0)==0);
    swap=k_missions[0]; k_missions[0]=k_missions[1]; k_missions[1]=swap;
    c.r[4]=5; mission_race_hook(&c); assert(c.r[4]==5);
    mission_cancel(); mission_tick(&c); assert(mission_phase()==MISSION_OFF);
    c.r[4]=5; mission_race_hook(&c); assert(c.r[4]==5);
    /* A fresh request cannot inherit the previous run's gates or result. */
    mission_request(1); mission_tick(&c); assert(mission_phase()==MISSION_ARMED);
    mission_race_hook(&c); mission_tick(&c); assert(mission_phase()==MISSION_ROLLING && placed==2);
    assert(LDF32(test_car+0x174)==225); /* 75 m before native section CP4 */
    /* A first-frame respawn contact must allow the rolling approach to continue. */
    ST8(test_car+0x3a4,1);
    STF32(test_car+0x174,280); STF32(test_car+0xc4,0); clock_ms+=300; mission_tick(&c);
    assert(mission_phase()==MISSION_ROLLING && atomic_load(&g_mission_auto_steer)<0);
    ST8(test_car+0x3a4,0);
    assert(!g_mission_run.elapsed_ms && !g_mission_run.contacts && !g_mission_run.next_gate);
    STF32(test_car+0x174,305); clock_ms+=1000; mission_tick(&c);
    assert(mission_phase()==MISSION_RUNNING && g_mission_run.started==clock_ms);
    assert(atomic_load(&g_mission_auto_release));
    assert(g_mission_run.gate_count==1 && g_mission_run.gates[0].centre.x==400);
    MissionRun clean_finish=g_mission_run;
    mission_rules_step(&clean_finish,(MissionPosition){405,10,10},0,clock_ms+2000);
    assert(clean_finish.phase==MISSION_PASSED); /* Racing line can leave the centreline. */
    assert(fabsf(placed_heading-1.57079632679489661923f)<.0001f);
    assert(!atomic_load(&g_mission_contacts) && !atomic_load(&g_mission_gate));
    assert(mission_phase()==MISSION_RUNNING);
    ST8(test_car+0x3a4,2); clock_ms+=34; mission_tick(&c); assert(mission_phase()==MISSION_CONTACT_FAILED);
    assert(!mission_result()); clock_ms+=749; assert(!mission_result());
    clock_ms++; assert(mission_result());
    mission_cancel(); mission_tick(&c);
    /* Untimed missions remain active after the sprint deadline. */
    mission_request(1); mission_tick(&c); c.r[4]=5; mission_race_hook(&c); mission_tick(&c);
    assert(mission_phase()==MISSION_ROLLING);
    STF32(test_car+0x174,305); clock_ms+=1000; mission_tick(&c);
    clock_ms+=600000; mission_tick(&c); assert(mission_phase()==MISSION_RUNNING);
    mission_cancel(); mission_tick(&c);
    mission_request(3); mission_tick(&c); mission_race_hook(&c); mission_tick(&c);
    assert(mission_phase()==MISSION_ROLLING && g_mission_run.gate_count==3);
    g_mission_run.phase=MISSION_RUNNING; mission_publish();
    MissionRun bus=g_mission_run;
    assert(bus.required_pass_mask==3u);
    bus.started=0; bus.last_sample=0;
    assert(bus.gates[0].centre.z<0 && bus.gates[1].centre.z>0);
    assert(fabsf(bus.gates[0].centre.x-927)<.01f);
    assert(fabsf(bus.gates[1].centre.x-1004)<.01f);
    bus.previous=(MissionPosition){907,10,0};
    mission_rules_step(&bus,(MissionPosition){947,10,0},0,1000);
    assert(bus.next_gate==0 && bus.phase==MISSION_GATE_FAILED);
    mission_rules_step(&bus,(MissionPosition){917,10,-11},0,1500);
    assert(bus.phase==MISSION_GATE_FAILED); /* Cannot rescue a missed stop by returning. */
    bus=g_mission_run; bus.started=0; bus.last_sample=0;
    bus.previous=(MissionPosition){907,10,-11};
    mission_rules_step(&bus,(MissionPosition){947,10,-11},0,2000);
    assert(bus.next_gate==1);
    mission_rules_step(&bus,(MissionPosition){960,10,11},0,4000);
    mission_rules_step(&bus,(MissionPosition){1020,10,11},0,5000);
    assert(bus.next_gate==2);
    mission_rules_step(&bus,(MissionPosition){1110,10,0},0,700000);
    assert(bus.phase==MISSION_PASSED); /* both stops and the exit, without a deadline */
    /* Native checkpoint can finish on a road elevation outside our sampled plane.
     * It must not skip an unpassed bus stop or accept an earlier checkpoint. */
    g_mission_run=bus; g_mission_run.phase=MISSION_RUNNING; g_mission_run.next_gate=1;
    g_mission_run.previous=(MissionPosition){1090,30,0}; g_mission_run.last_sample=clock_ms;
    g_mission_run.started=clock_ms; mission_publish();
    STF32(test_car+0x174,1110); STF32(test_car+0x17c,30); ST8(test_car+0x3a4,0);
    ST8(test_car+0x4de,11); clock_ms+=1000; mission_checkpoint_hook(&c);
    assert(mission_phase()==MISSION_RUNNING && g_mission_run.next_gate==1);
    g_mission_run.next_gate=2; ST8(test_car+0x4de,10); mission_checkpoint_hook(&c);
    assert(mission_phase()==MISSION_RUNNING);
    ST8(test_car+0x4de,11); mission_checkpoint_hook(&c);
    assert(mission_phase()==MISSION_PASSED && g_mission_run.next_gate==3);
    mission_cancel(); mission_tick(&c);
    /* Broken route pointers fail safely; no placement or guest-stack writes. */
    ST32(0x4000c,0); mission_request(0); mission_tick(&c); c.r[4]=5; mission_race_hook(&c); mission_tick(&c);
    assert(mission_phase()==MISSION_SETUP_FAILED && placed==4);
    mission_cancel(); mission_tick(&c);
    c.r[2]=RAM_SIZE-1; mission_request(0); mission_tick(&c); assert(mission_phase()==MISSION_ARMED);
    /* Fixed choices bypass selectors, apply once on retry, and leave normal play alone. */
    c.r[2]=0x100000; c.r[1]=0xf00000;
    ST32(c.r[2]+0x54,0x600000); ST32(c.r[2]+0x124,0x610000);
    ST32(c.r[2]+0x7a0,0x620000);
    mission_request(2); ST32(0x600004,(6u<<23)|0x80);
    saved=c; mission_dispatch_hook(&c); assert(!memcmp(&c,&saved,sizeof c));
    assert(prepared==1 && ((LD32(0x600004)>>23)&15)==9);
    assert(LD32(0x600010)&0x400000u);
    assert(((LD32(0x600004)>>9)&7)==0 && ((LD32(0x600004)>>8)&1)==0);
    assert(LD8(0x610004)==0 && ((LD32(0x600004)>>12)&3)==0);
    atomic_store(&g_race_restart,2); mission_dispatch_hook(&c); assert(prepared==1);
    mission_request(1); mission_dispatch_hook(&c); assert(prepared==2 && LD8(0x610004)==0);
    ST32(c.r[2]+0x3c,0x700000); ST32(0x600004,10u<<23);
    ST8(0x8fffd0,0); ST16(0x8fffd2,123);
    mission_dispatch_hook(&c); assert(LD16(0x8fffd2)==123); /* readiness wait survives */
    ST8(0x8fffd0,1); ST8(0x8fffd1,240); ST16(0x8fffd4,64);
    mission_dispatch_hook(&c);
    assert(LD16(0x8fffd2)==60 && !LD8(0x8fffd1) && !LD16(0x8fffd4));
    c.r[4]=0; mission_race_hook(&c);
    assert(released==2 && c.r[4]==4 && LD8(0x620013)==4 && LD16(0x620016)==1);
    mission_race_hook(&c); assert(released==2); /* no repeated clock/control resets */
    mission_cancel(); ST32(0x600004,6u<<23); mission_dispatch_hook(&c);
    assert(prepared==2 && LD32(0x600004)==(6u<<23));
    ST32(0x600004,11u<<23); k_missions[1].traffic=1;
    mission_request(1); mission_dispatch_hook(&c); assert(!(LD32(0x600010)&0x400000));
    k_missions[1].traffic=0; mission_dispatch_hook(&c); assert(LD32(0x600010)&0x400000);
    mission_cancel();
    /* Broken scenery clears solid contact, but its break flag must still fail.
     * Solid scenery without a road-contact bit must count too. */
    ST8(test_car+0x3a4,0); ST8(test_car+0x3c4,0); ST8(test_car+0x3c9,1);
    assert(mission_car_contact(test_car));
    MissionRun pillar={0}; pillar.phase=MISSION_RUNNING; pillar.gate_count=1;
    pillar.contact_limit=0; pillar.previous=mission_car_position(test_car);
    mission_rules_step(&pillar,pillar.previous,mission_car_contact(test_car),1);
    assert(pillar.phase==MISSION_CONTACT_FAILED && pillar.contacts==1);
    ST8(test_car+0x3c9,0); ST8(test_car+0x3c4,1); assert(mission_car_contact(test_car));
    ST8(test_car+0x3c4,0); assert(!mission_car_contact(test_car));
    atomic_store(&g_mission_selected,0);
    k_missions[0].collision_types=MISSION_HIT_WALL;
    ST8(test_car+0x3c9,1); assert(!mission_counted_contact(test_car));
    k_missions[0].collision_types=MISSION_HIT_BREAKABLE; assert(mission_counted_contact(test_car));
    ST8(test_car+0x3c9,0); ST8(test_car+0x3c4,1); ST32(test_car+0x4d8,0x540000);
    ST16(0x540438,0x100); assert(mission_car_contact_types(test_car)==MISSION_HIT_CAR);
    assert(!mission_counted_contact(test_car));
    k_missions[0].collision_types=MISSION_HIT_CAR; assert(mission_counted_contact(test_car));
    ST16(0x540438,0x300); assert(mission_car_contact_types(test_car)==MISSION_HIT_CAR);
    ST16(0x540438,0x200); assert(mission_car_contact_types(test_car)==MISSION_HIT_BREAKABLE);
    ST16(0x540438,0x400); assert(mission_car_contact_types(test_car)==MISSION_HIT_SCENERY);
    ST8(test_car+0x3c4,0); ST8(test_car+0x3a4,2); assert(mission_car_contact_types(test_car)==MISSION_HIT_WALL);
    ST8(test_car+0x3a4,0);

    char csv_path[1024]; snprintf(csv_path,sizeof csv_path,"%s.csv",argv[1]);
    FILE *csv=fopen(csv_path,"w"); assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,collisions,collision_types,gold,silver,bronze,gatex,gatez,gatew,rolling,rolling_speed,endx,endz,endw\n"
          "\"CUSTOM, GATES\",\"DRIVE BEHIND BOTH BUS STOPS, THEN FINISH\",2,mt,town,9,10,0,walls;breakables,17,19,21,594.4;530.1,696.7;657.5,kerb;full road width,75,108,1050,0,full road width\n",csv);
    fclose(csv); setenv("RT_MISSIONS_CSV",csv_path,1); mission_csv_load();
    assert(MISSION_COUNT==1 && !strcmp(k_missions[0].name,"CUSTOM, GATES"));
    assert(!strcmp(k_missions[0].briefing,"DRIVE BEHIND BOTH BUS STOPS, THEN FINISH"));
    assert(k_missions[0].collision_types==(MISSION_HIT_WALL|MISSION_HIT_BREAKABLE));
    assert(k_missions[0].car==1 && k_missions[0].transmission==1 && k_missions[0].contacts==0);
    assert(k_missions[0].custom_finish && k_missions[0].end_x==1050 && k_missions[0].end_width==36);
    assert(k_missions[0].custom_gates==2 && k_missions[0].gate_width[0]==5 && k_missions[0].gate_width[1]==36);
    assert(k_missions[0].gold_ms==17000 && k_missions[0].limit_ms==21000 && k_missions[0].rolling_metres==75);
    /* An unfinished survey row must neither appear in selection nor make a
     * valid neighbouring mission fail to load. Legacy rows default enabled. */
    MissionDefinition saved_definition=k_missions[0];
    csv=fopen(csv_path,"w"); assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,type,target_models,target_event,target_count,target_label\n"
          "KIOSKS,TOUCH EVERY KIOSK,1,at,town,0,10,COLLECT,hitA;hitB,touch,all,KIOSKS\n",csv);
    fclose(csv); mission_csv_load();
    assert(MISSION_COUNT==1 && k_missions[0].scenery_touch);
    assert(!k_missions[0].vehicle_destroy && !strcmp(k_missions[0].type,"COLLECT"));
    assert(!strcmp(k_missions[0].target_models,"hitA;hitB"));

    csv=fopen(csv_path,"w"); assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,enabled\n"
          "DRAFT,UNFINISHED,,,,,,0\n"
          "READY,DRIVE,1,at,town,9,10,1\n",csv); fclose(csv);
    mission_csv_load(); assert(MISSION_COUNT==1 && !strcmp(k_missions[0].name,"READY"));
    k_missions[0]=saved_definition;
    csv=fopen(csv_path,"w"); assert(csv); fputs("car,transmission,course,cp1,cp2\n1,at,town,9,3\n",csv); fclose(csv);
    mission_csv_load(); assert(MISSION_COUNT==1 && k_missions[0].finish_cp==10);
    /* A rolling-start gate is the first gate and replaces the authored start. */
    csv=fopen(csv_path,"w"); assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,gatex,gatez,gatew,gatetype\n"
          "ROLL,JUMP,1,at,town,0,10,900;1000,0;0,road;road,rstart;finish\n",csv); fclose(csv);
    mission_csv_load(); assert(MISSION_COUNT==1 && k_missions[0].rolling_gate && k_missions[0].custom_start);
    assert(k_missions[0].gate_role[0]==4 && k_missions[0].start_x==900);
    const char *bad_rolling[]={
        "name,description,car,transmission,course,cp1,cp2,gatex,gatez,gatew,gatetype\n"
        "BAD,JUMP,1,at,town,0,10,900;1000,0;0,road;road,waypoint;rstart\n",
        "name,description,car,transmission,course,cp1,cp2,gatex,gatez,gatew,gatetype,startx,startz,start_heading\n"
        "BAD,JUMP,1,at,town,0,10,900;1000,0;0,road;road,rstart;finish,880,0,90\n",
        "name,description,car,transmission,course,cp1,cp2,gatex,gatez,gatew,gatetype,rolling,rolling_speed\n"
        "BAD,JUMP,1,at,town,0,10,900;1000,0;0,road;road,rstart;finish,75,108\n"};
    for(unsigned i=0;i<3;i++) {
        csv=fopen(csv_path,"w"); assert(csv); fputs(bad_rolling[i],csv); fclose(csv);
        mission_csv_load(); assert(strcmp(k_missions[0].name,"BAD"));
    }
    k_missions[0]=saved_definition;
    /* A closer opposite-flow segment before CP9 must not own CSV gate direction. */
    ST32(0x4000c,0x40000); STF32(0x40100+7*32,1100);
    k_missions[0].gate_x[0]=900; k_missions[0].gate_z[0]=0;
    k_missions[0].gate_x[1]=1000; k_missions[0].gate_z[1]=0;
    c.r[3]=test_car+0x4dc; c.r[4]=5;
    atomic_store(&g_mission_selected,0); mission_begin(&c,test_car);
    assert(mission_phase()==MISSION_ROLLING && g_mission_run.gate_count==3);
    assert(g_mission_run.gates[0].nx>.99f && g_mission_run.gates[1].nx>.99f);
    k_missions[0].custom_finish=1; k_missions[0].end_x=1050; k_missions[0].end_z=0; k_missions[0].end_width=36;
    mission_begin(&c,test_car);
    assert(g_mission_run.gates[2].centre.x==1050 && g_mission_run.gates[2].half_width==18);
    g_mission_run.phase=MISSION_RUNNING; g_mission_run.next_gate=2; mission_publish();
    ST8(test_car+0x4de,11); mission_checkpoint_hook(&c);
    assert(mission_phase()==MISSION_RUNNING); /* Native CP10 cannot bypass custom finish. */
    MissionRun end_run=g_mission_run; end_run.started=clock_ms; end_run.last_sample=clock_ms;
    end_run.previous=(MissionPosition){1040,10,0};
    mission_rules_step(&end_run,(MissionPosition){1060,10,0},0,clock_ms+1000);
    assert(end_run.phase==MISSION_PASSED);
    /* Editor roles remain separate: fail planes never enter the ordered
     * waypoint list, and an explicit finish replaces the native endpoint. */
    MissionDefinition editor_saved=k_missions[0];
    k_missions[0].custom_finish=0;
    k_missions[0].gate_role[0]=1;k_missions[0].gate_role[1]=2;
    mission_begin(&c,test_car);
    assert(g_mission_run.failure_gate_count==1 && g_mission_run.gate_count==1);
    MissionRun editor_run=g_mission_run;editor_run.phase=MISSION_RUNNING;
    editor_run.started=clock_ms;editor_run.last_sample=clock_ms;
    editor_run.previous=(MissionPosition){890,10,0};
    mission_rules_step(&editor_run,(MissionPosition){910,10,0},0,clock_ms+1000);
    assert(editor_run.phase==MISSION_OBJECTIVE_FAILED);
    editor_run=g_mission_run;editor_run.phase=MISSION_RUNNING;
    editor_run.started=clock_ms;editor_run.last_sample=clock_ms;
    editor_run.previous=(MissionPosition){990,10,0};
    mission_rules_step(&editor_run,(MissionPosition){1010,10,0},0,clock_ms+1000);
    assert(editor_run.phase==MISSION_PASSED);
    g_mission_run.phase=MISSION_RUNNING;g_mission_run.next_gate=0;mission_publish();
    ST8(test_car+0x4de,11);mission_checkpoint_hook(&c);
    assert(mission_phase()==MISSION_RUNNING);
    k_missions[0]=editor_saved;mission_begin(&c,test_car);
    MissionDefinition before_custom_roll=k_missions[0];
    k_missions[0].custom_start=1;k_missions[0].start_x=900;k_missions[0].start_z=0;
    k_missions[0].start_heading=90;k_missions[0].rolling_metres=50;
    mission_begin(&c,test_car);
    assert(mission_phase()==MISSION_ROLLING);
    assert(fabsf(g_mission_entry.centre.x-900)<.01f && g_mission_entry.nx>.99f);
    assert(fabs(LDF32(test_car+0x174)-850)<.01f);
    /* A challenge anchor remains a waypoint; the rolling handover moves
     * upstream, leaving the whole stunt under player control. */
    k_missions[0].lead_in_metres=80;
    mission_begin(&c,test_car);
    assert(mission_phase()==MISSION_ROLLING);
    assert(fabsf(g_mission_entry.centre.x-820)<.01f);
    MissionRoadPoint approach_spawn=mission_road_sample(g_mission_roll_distance);
    assert(fabs(LDF32(test_car+0x174)-approach_spawn.x)<.01f);
    assert(hypotf(g_mission_entry.centre.x-900,g_mission_entry.centre.z)>50);
    assert(!g_mission_run.next_gate && !g_mission_landed);
    /* A rolling-start gate is the timing line: the car starts behind it on its
     * heading and is timed from it; the line itself is not a waypoint. */
    MissionDefinition rolling=before_custom_roll;
    rolling.custom_start=1;rolling.rolling_gate=1;rolling.custom_gates=2;rolling.custom_finish=0;
    rolling.rolling_metres=0;rolling.lead_in_metres=0;rolling.rolling_speed=0;
    rolling.start_x=900;rolling.start_z=0;rolling.start_heading=90;
    for(unsigned i=0;i<2;i++) {
        rolling.gate_x[i]=900+100.f*i;rolling.gate_z[i]=0;rolling.gate_width[i]=36;rolling.gate_height[i]=8;
        rolling.gate_heading[i]=90;rolling.gate_y[i]=NAN;rolling.gate_tilt[i]=0;
    }
    rolling.gate_role[0]=4;rolling.gate_role[1]=2;
    k_missions[0]=rolling;mission_begin(&c,test_car);
    assert(mission_phase()==MISSION_ROLLING);
    assert(fabsf(g_mission_entry.centre.x-900)<.01f && g_mission_entry.nx>.99f);
    assert(LDF32(test_car+0x174)<900 && LDF32(test_car+0x174)>889.99f);
    assert(g_mission_run.gate_count==1 && fabsf(g_mission_run.gates[0].centre.x-1000)<.01f);
    assert(g_mission_run.required_pass_mask==1);
    k_missions[0]=before_custom_roll;
    /* An explicit reverse section faces and crosses against native route order.
     * Its rolling approach starts beyond CP3, rather than on the outgoing leg. */
    csv=fopen(csv_path,"w");assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,direction,rolling,rolling_speed\n"
          "RETURN LEG,DRIVE BACK,1,at,town,3,2,reverse,75,108\n",csv);
    fclose(csv);mission_csv_load();
    assert(MISSION_COUNT==1 && k_missions[0].route_direction==-1);
    mission_begin(&c,test_car);
    assert(mission_phase()==MISSION_ROLLING);
    assert(fabs(LDF32(test_car+0x174)-275)<.01f);
    assert(fabsf(placed_heading+1.57079632679f)<.01f);
    assert(g_mission_entry.nx<-.99f && g_mission_run.gates[0].nx<-.99f);
    assert(fabsf(g_mission_run.gates[0].centre.x-100)<.01f);
    atomic_store(&g_mission_request,0);atomic_store(&g_race_restart,0);
    STF32(test_car+0x174,195);clock_ms+=1000;c.r[3]=test_car+0x4dc;mission_tick(&c);
    assert(mission_phase()==MISSION_RUNNING);
    MissionRun reverse=g_mission_run;reverse.started=clock_ms;reverse.last_sample=clock_ms;
    reverse.previous=(MissionPosition){110,10,0};
    mission_rules_step(&reverse,(MissionPosition){90,10,0},0,clock_ms+1000);
    assert(reverse.phase==MISSION_PASSED);
    csv=fopen(csv_path,"w");assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,direction\n"
          "OPPOSITE,DRIVE BACK,1,at,town,3,2,opposite\n",csv);
    fclose(csv);mission_csv_load();
    assert(MISSION_COUNT==1 && k_missions[0].route_direction==-1);
    csv=fopen(csv_path,"w");assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,direction\n"
          "INVALID,DRIVE,1,at,town,3,2,sideways\n",csv);
    fclose(csv);mission_csv_load();assert(k_missions[0].route_direction==-1);
    /* Destruction objectives count distinct authored targets, not collisions. */
    csv=fopen(csv_path,"w"); assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,breakx,breakz,breaktype,breakradius,breaklabel\n"
          "SMASH,DESTROY BOTH,1,at,town,2,3,114;109,215;225,2,1.5,PILLARS\n",csv);
    fclose(csv); mission_csv_load();
    assert(k_missions[0].break_count==2 && !strcmp(k_missions[0].break_label,"PILLARS"));
    assert(k_missions[0].custom_gates==0 && k_missions[0].contacts==UINT_MAX);
    g_mission_break_mask=0; atomic_store(&g_mission_broken,0);
    atomic_store(&g_mission_phase,MISSION_RUNNING);
    c.r[29]=0x540000; c.r[21]=2;
    STF32(0x540404,114); STF32(0x54040c,215);
    ST32(test_car+0x4d8,0x550000);
    ST32(0x54043c,0x56000c); /* A different car broke this pillar. */
    mission_break_hook(&c); assert(!atomic_load(&g_mission_broken) && !g_mission_break_mask);
    ST32(0x54043c,0x55000c);
    mission_break_hook(&c); mission_break_hook(&c); assert(atomic_load(&g_mission_broken)==1);
    g_mission_run.phase=MISSION_RUNNING; mission_check_objective();
    assert(g_mission_run.phase==MISSION_RUNNING);
    STF32(0x540404,109); STF32(0x54040c,225);
    c.r[21]=3; mission_break_hook(&c); assert(atomic_load(&g_mission_broken)==1);
    c.r[21]=2; STF32(0x540404,500); mission_break_hook(&c); assert(atomic_load(&g_mission_broken)==1);
    g_mission_run.phase=MISSION_PASSED; mission_check_objective(); assert(g_mission_run.phase==MISSION_OBJECTIVE_FAILED);
    STF32(0x540404,109); mission_break_hook(&c); assert(atomic_load(&g_mission_broken)==2);
    g_mission_run.phase=MISSION_RUNNING; mission_check_objective(); assert(g_mission_run.phase==MISSION_PASSED);
    /* A last-target hit cannot override an already judged failure. */
    g_mission_run.phase=MISSION_CONTACT_FAILED; mission_check_objective();
    assert(g_mission_run.phase==MISSION_CONTACT_FAILED);
    g_mission_run.phase=MISSION_TIME_FAILED; mission_check_objective();
    assert(g_mission_run.phase==MISSION_TIME_FAILED);
    mission_begin(&c,test_car); assert(!atomic_load(&g_mission_broken) && !g_mission_break_mask);
    csv=fopen(csv_path,"w");assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,targetx,targetz,targetradius,target_label,target_event\n"
          "GRASS,VISIT BOTH PATCHES,1,at,coast,0,8,100;200,20;30,4,GRASS,visit\n",csv);
    fclose(csv);mission_csv_load();
    assert(k_missions[0].region_visit && k_missions[0].contact_targets==2);
    assert(k_missions[0].target_x[1]==200 && k_missions[0].target_z[1]==30);
    csv=fopen(csv_path,"w");assert(csv);
    fputs("name,description,car,transmission,course,cp1,cp2,target_event\n"
          "INVALID,NO PATCHES,1,at,coast,0,8,visit\n",csv);
    fclose(csv);mission_csv_load();
    assert(!strcmp(k_missions[0].name,"GRASS")); /* Invalid edits retain the working recipe. */
    remove(csv_path);
    /* Reset clears memory and the saved file, and stays clear after reloading. */
    assert(mission_clear_completion()); assert(!g_mission_record_count && !atomic_load(&g_mission_best));
    FILE *cleared=fopen(g_mission_save_path,"r"); assert(!cleared);
    mission_progress_init(argv[1]); assert(!g_mission_record_count);
    /* Challenge restrictions apply after control handover; parking needs
     * continuous stopping, alignment and the final waypoint. */
    MissionDefinition special={.forbidden_controls=2};
    g_mission_run.phase=MISSION_RUNNING;g_analog[1]=g_analog[2]=g_analog[3]=-200;
    mission_special_tick(&special,test_car,1000);assert(g_mission_run.phase==MISSION_RUNNING);
    g_analog[2]=200;mission_special_tick(&special,test_car,1100);
    assert(g_mission_run.phase==MISSION_OBJECTIVE_FAILED);g_analog[2]=-200;
    special=(MissionDefinition){.park_ms=2000,.park_length=6,.park_angle=15,.park_speed=2};
    g_mission_run.phase=MISSION_PASSED;g_mission_run.gate_count=g_mission_run.next_gate=1;
    g_mission_run.gates[0]=(MissionGate){{0,0,0},0,1,2,4,0};g_mission_park_since=0;
    STF32(test_car+0x174,0);STF32(test_car+0x178,0);STF32(test_car+0x17c,0);
    STF32(test_car+0xc4,0);STF32(test_car+0xac,0);
    mission_special_tick(&special,test_car,1000);assert(g_mission_run.phase==MISSION_RUNNING);
    mission_special_tick(&special,test_car,2999);assert(g_mission_run.phase==MISSION_RUNNING);
    STF32(test_car+0xac,2);mission_special_tick(&special,test_car,3000);assert(!g_mission_park_since);
    STF32(test_car+0xac,0);mission_special_tick(&special,test_car,4000);
    mission_special_tick(&special,test_car,6000);assert(g_mission_run.phase==MISSION_PASSED);
    special=(MissionDefinition){.collect_gap_ms=8000};g_mission_run.phase=MISSION_RUNNING;
    g_mission_target_goal=2;atomic_store(&g_mission_broken,1);g_mission_last_collect_ms=1000;
    mission_special_tick(&special,test_car,9001);assert(g_mission_run.phase==MISSION_OBJECTIVE_FAILED);
    setenv("RT_MISSIONS_CSV","missions.csv",1);g_mission_count=0;mission_csv_load();
    assert(MISSION_COUNT==71); /* Includes FASHIONABLY LATE for gate-editor tuning. */
    assert(k_missions[55].id==56 && k_missions[MISSION_COUNT-1].id==71);
    /* Native parked obstacle registration preserves context, replaces on retry,
     * removes for another mission, and rejects unavailable models. */
    PPCContext obstacle_context={0};obstacle_context.r[2]=0x154da8;obstacle_context.r[1]=0x700000;
    ST32(0x154da8+0x380,0x650000);ST32(0x650004,0);
    ST32(0x154da8+0x40,0x660000);ST32(0x660000,2);
    ST32(0x660000+36+8,31);ST32(0x660000+36+16,16);ST32(0x660000+36+20,0x670000);
    MissionDefinition obstacle={.obstacle=1,.obstacle_model=2,.obstacle_x=729,.obstacle_z=1205,.obstacle_heading=90};
    PPCContext context_before=obstacle_context;
    assert(mission_place_obstacle(&obstacle_context,&obstacle));
    assert(!memcmp(&context_before,&obstacle_context,sizeof context_before));
    assert(LD32(0x650004)==1 && LD32(0x650308)==0x670020);
    assert(LDF32(0x65030c)==729 && LDF32(0x650310)==10 && LDF32(0x650314)==1205);
    assert(fabs(LDF32(0x65031c)-1.5707963)<.001f);
    assert(mission_place_obstacle(&obstacle_context,&obstacle) && LD32(0x650004)==1);
    obstacle.obstacle=0;assert(mission_place_obstacle(&obstacle_context,&obstacle) && LD32(0x650004)==0);
    obstacle.obstacle=1;obstacle.obstacle_model=16;assert(!mission_place_obstacle(&obstacle_context,&obstacle));
    free(g_ram);
    puts("mission adapter: native initialization, terrain gates, context/stack preservation, immediate start, completion, retry, contact failure and invalid routes passed");
    return 0;
}
