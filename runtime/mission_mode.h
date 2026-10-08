/* Private enhanced-mode GTI Club JAB adapter. Included after race_restart.h. */
#pragma once
#include "mission_rules.h"
#include <limits.h>
#include <stdio.h>
#include <errno.h>
extern int16_t g_analog[4];

#ifndef GAME_ENH_MISSION_STYLE
#define GAME_ENH_MISSION_STYLE 0
#endif

enum { MISSION_HIT_WALL=1, MISSION_HIT_CAR=2, MISSION_HIT_SCENERY=4, MISSION_HIT_BREAKABLE=8, MISSION_HIT_ALL=15 };
typedef struct {
    const char *name, *briefing, *break_label;
    unsigned id;
    char legacy_names[192];
    unsigned start_cp, finish_cp;
    unsigned limit_ms, contacts, collision_types, collision_delay_ms;
    unsigned car, transmission, region, traffic;
    unsigned completion_delay_ms, hint_remaining, hint_idle_ms;
    unsigned gold_ms, silver_ms;
    unsigned rolling_metres, lead_in_metres;
    float rolling_speed;
    unsigned custom_gates, custom_finish, any_order;
    unsigned break_count, break_type;
    float break_radius, break_x[32], break_z[32];
    float end_x, end_z, end_width;
    float gate_x[MISSION_MAX_GATES], gate_z[MISSION_MAX_GATES], gate_width[MISSION_MAX_GATES];
    float gate_y[MISSION_MAX_GATES],gate_height[MISSION_MAX_GATES],gate_heading[MISSION_MAX_GATES],gate_tilt[MISSION_MAX_GATES];
    unsigned gate_role[MISSION_MAX_GATES]; /* 0 waypoint, 1 failure, 2 finish, 4 rolling start */
    char target_models[256], target_label[32], type[24],target_vehicles[64],parked_models[64];
    unsigned vehicle_destroy, region_visit, scenery_touch;
    unsigned target_count, target_at_finish, tuned, custom_start;
    unsigned rolling_gate; /* gate 1 is a rolling start line: the game picks run-up and speed */
    int route_direction; /* zero/auto follows native route order; -1 reverses it */
    float start_x, start_z, start_heading, speed_goal, jump_height;
    unsigned contact_targets;
    float target_x[32],target_z[32],target_radius;
    float target_group_radius, target_height;
    unsigned two_wheel_ms;
    unsigned roll_degrees;
    float wallride_distance;
    float landing_x,landing_z,landing_y,landing_radius,landing_height;
    unsigned landing, handbrake_turn;
    unsigned obstacle, obstacle_model;
    float obstacle_x,obstacle_z,obstacle_heading;
    unsigned forbidden_controls, collect_gap_ms, no_turn_back, park_ms;
    float park_length, park_angle, park_speed;
    char protected_models[256];
    float turn_x,turn_z,turn_radius;
} MissionDefinition;
static MissionDefinition k_missions[96];
/* Inclusive thresholds; zero means this mission has no medal tiers. */
static const char *mission_medal(const MissionDefinition *d, unsigned elapsed) {
    if (!d->gold_ms || elapsed>d->limit_ms) return NULL;
    if (d->gold_ms>500 && elapsed<=d->gold_ms-500) return "PLATINUM";
    if (elapsed<=d->gold_ms) return "GOLD";
    if (elapsed<=d->silver_ms) return "SILVER";
    return "BRONZE";
}
static uint32_t mission_medal_colour(const MissionDefinition *d,unsigned elapsed) {
    if(d->gold_ms>500 && elapsed<=d->gold_ms-500) return 0xe0f4ff;
    return elapsed<=d->gold_ms ? 0xffd800 : elapsed<=d->silver_ms ? 0xc0c0c0 : 0xcd7f32;
}
#define MISSION_COUNT g_mission_count
static int g_mission_count;
#include "mission_csv.h"
static atomic_uint g_mission_broken;
static uint32_t g_mission_break_mask;
/* Course object identity is family/kind/instance, not a recycled collider. */
#define MISSION_MAX_TARGETS 256
typedef struct { unsigned family, kind, index; float x,z; int met; unsigned group; } MissionTarget;
static MissionTarget g_mission_targets[MISSION_MAX_TARGETS];
static unsigned char g_mission_target_groups[MISSION_MAX_TARGETS];
static unsigned g_mission_target_count, g_mission_target_goal;
static atomic_uint g_mission_target_total, g_mission_speed;
static float g_mission_max_speed, g_mission_jump_height;
static unsigned g_mission_two_wheel_ms;
static atomic_uint g_mission_two_wheel_display;
static int g_mission_two_wheel_previous,g_mission_turn_active,g_mission_landed,g_mission_turn_button_previous;
static uint64_t g_mission_turn_release_at;
static int g_mission_rolling_direction;
static float g_mission_turn_previous,g_mission_turn_angle;
static unsigned g_mission_turn_contacts;
static float g_mission_landing_y;
static float g_mission_roll_angle,g_mission_roll_previous;
static float g_mission_roll_right[3];
static int g_mission_roll_vector_valid;
static int g_mission_roll_airborne,g_mission_roll_landed;
static float g_mission_wall_distance;
static int g_mission_wall_previous;
static MissionRun g_mission_run; /* guest thread only */
static atomic_int g_mission_selection_applied;
static atomic_int g_mission_phase, g_mission_request, g_mission_selected;
static atomic_uint g_mission_elapsed, g_mission_contacts, g_mission_gate, g_mission_gates, g_mission_native_success;
static atomic_uint g_mission_countdown, g_mission_distance;
static atomic_uint g_mission_gate_times[MISSION_MAX_GATES];
static atomic_uint_fast64_t g_mission_result_at;
static uint64_t g_mission_countdown_end, g_mission_last_collect_ms, g_mission_park_since;
static float g_mission_challenge_heading;
static atomic_uint g_mission_nearest_item;
static MissionGate g_mission_entry;
static atomic_int g_mission_auto_steer, g_mission_auto_release;
static float g_mission_roll_distance;
static uint64_t g_mission_roll_started;
static int g_mission_native_running;
static unsigned g_mission_placements; /* counts teleports to a mission start */
static unsigned g_mission_trace_marker=UINT_MAX, g_mission_trace_gate=UINT_MAX;
static uint32_t g_mission_course_key, g_mission_car_key;
static atomic_uint g_mission_best;
typedef struct { uint32_t course, car, mission, best; } MissionRecord;
static MissionRecord g_mission_records[256];
static unsigned g_mission_record_count;
static char g_mission_save_path[1024];

static unsigned mission_name_identity(const char *name) {
    unsigned id=2166136261u;
    for(const unsigned char *p=(const unsigned char*)name;*p;p++) id=(id^*p)*16777619u;
    return id | 0x80000000u;
}
static unsigned mission_identity(unsigned selected) {
    return k_missions[selected].id ? 0x40000000u|k_missions[selected].id :
        mission_name_identity(k_missions[selected].name);
}
static unsigned mission_migrate_identity(unsigned identity) {
    if(identity<(unsigned)MISSION_COUNT) return mission_identity(identity);
    if(!(identity&0x80000000u)) return identity;
    for(unsigned i=0;i<(unsigned)MISSION_COUNT;i++) {
        if(identity==mission_name_identity(k_missions[i].name)) return mission_identity(i);
        char aliases[192];snprintf(aliases,sizeof aliases,"%s",k_missions[i].legacy_names);
        char *next=aliases,*alias;
        while((alias=strsep(&next,";"))) if(*alias && identity==mission_name_identity(alias)) return mission_identity(i);
    }
    return identity;
}
static void mission_progress_init(const char *settings) {
    mission_csv_load();
    g_mission_record_count=0; g_mission_save_path[0]=0;
    if (!GAME_ENH_MISSION_STYLE || strlen(settings)>sizeof g_mission_save_path-16) return;
    snprintf(g_mission_save_path,sizeof g_mission_save_path,"%s.missions",settings);
    FILE *f=fopen(g_mission_save_path,"r");
    if (!f) return;
    char line[160];
    while (fgets(line,sizeof line,f) && g_mission_record_count<256) {
        MissionRecord record; char extra;
        if (sscanf(line,"%u %u %u %u %c",&record.course,&record.car,&record.mission,&record.best,&extra)==4 &&
            record.car<256 && record.best>0)
            { record.mission=mission_migrate_identity(record.mission);
              g_mission_records[g_mission_record_count++]=record; }
    }
    fclose(f);
}
static MissionRecord *mission_record(void) {
    unsigned selected=(unsigned)atomic_load(&g_mission_selected);
    for (unsigned i=0; i<g_mission_record_count; i++) {
        MissionRecord *r=&g_mission_records[i];
        if (r->course==g_mission_course_key && r->car==g_mission_car_key && r->mission==mission_identity(selected)) return r;
    }
    return NULL;
}
static unsigned mission_menu_best(unsigned selected) {
    unsigned best=0;
    for(unsigned i=0;i<g_mission_record_count;i++) {
        MissionRecord *r=&g_mission_records[i];
        if(r->mission==mission_identity(selected) && r->car==k_missions[selected].car && r->best && (!best || r->best<best)) best=r->best;
    }
    return best;
}
static int mission_clear_completion(void) {
    if(*g_mission_save_path && remove(g_mission_save_path) && errno!=ENOENT) {
        rt_log("mission: could not clear completion records: %s\n",strerror(errno));
        return 0;
    }
    memset(g_mission_records,0,sizeof g_mission_records);
    g_mission_record_count=0;
    atomic_store(&g_mission_best,0);
    return 1;
}
static int mission_near_gold(const MissionDefinition *d,unsigned ms) {
    return d->gold_ms && ms>d->gold_ms && ms<=d->silver_ms && (uint64_t)ms*100<=(uint64_t)d->gold_ms*105;
}
static void mission_save_completion(void) {
    MissionRecord *r=mission_record();
    if (!r) {
        if (g_mission_record_count==256) return;
        r=&g_mission_records[g_mission_record_count++];
        *r=(MissionRecord){g_mission_course_key,g_mission_car_key,
            mission_identity((unsigned)atomic_load(&g_mission_selected)),0};
    }
    unsigned ms=(unsigned)g_mission_run.elapsed_ms;
    if (!ms || (r->best && r->best<=ms)) return;
    r->best=ms;
    atomic_store(&g_mission_best,mission_menu_best((unsigned)atomic_load(&g_mission_selected)));
    if (!g_mission_save_path[0]) return;
    char path[1040]; snprintf(path,sizeof path,"%s.tmp",g_mission_save_path);
    FILE *f=fopen(path,"w");
    if (!f) { rt_log("mission: could not save completion\n"); return; }
    int ok=1;
    for (unsigned i=0; i<g_mission_record_count; i++) {
        MissionRecord v=g_mission_records[i];
        if (fprintf(f,"%u %u %u %u\n",v.course,v.car,v.mission,v.best)<0) ok=0;
    }
    if (fclose(f)) ok=0;
    if (!ok || rename(path,g_mission_save_path)) {
        remove(path); rt_log("mission: could not save completion\n");
    }
}

static int mission_available(void) { return GAME_ENH_MISSION_STYLE == 1; }
static int mission_phase(void) { return atomic_load(&g_mission_phase); }
static int mission_result(void) {
    int phase=mission_phase();
    return phase>=MISSION_PASSED && rt_now()>=atomic_load(&g_mission_result_at);
}
static int mission_engaged(void) { return mission_phase() >= MISSION_LOADING; }
static atomic_int g_mission_explore_request;
static atomic_int g_mission_exploring;
static void mission_cancel(void) {
    atomic_store(&g_race_mission_practice,0);
    atomic_store(&g_mission_request, -1);
    atomic_store(&g_mission_phase, MISSION_OFF);
}
static void mission_request(int index) {
    if (!mission_available() || index < 0 || index >= MISSION_COUNT) return;
    atomic_store(&g_race_mission_practice,0);
    atomic_store(&g_mission_result_at,0);
    atomic_store(&g_mission_auto_release,0);
    atomic_store(&g_mission_selection_applied,0);
    atomic_store(&g_mission_selected, index);
    atomic_store(&g_mission_best,0);
    atomic_store(&g_mission_request, index+1);
    atomic_store(&g_mission_phase, MISSION_ARMED);
}
/* Native static-car placements retain their TCAR asset descriptors in the
 * course catalog even while their collision/render instances stream out. */
static unsigned mission_tcar_model(PPCContext *c,unsigned descriptor) {
    unsigned groups=LD32(c->r[2]+0x40);
    if(!race_valid(groups,4)) return UINT_MAX;
    unsigned count=LD32(groups);
    if(count>128 || !race_valid(groups,count*36)) return UINT_MAX;
    for(unsigned i=1;i<count;i++) {
        unsigned group=groups+i*36;
        if(LD32(group+8)!=31) continue;
        unsigned base=LD32(group+20),models=LD32(group+16);
        if(models>64 || !race_valid(base,models*16) || descriptor<base ||
           descriptor>=base+models*16 || (descriptor-base)%16) return UINT_MAX;
        return (descriptor-base)/16;
    }
    return UINT_MAX;
}
static uint64_t mission_now_ms(void) { return rt_now() / (CPU_HZ / 1000); }
static MissionPosition mission_car_position(uint32_t car) {
    return (MissionPosition){LDF32(car+0x174), LDF32(car+0x17c), LDF32(car+0x178)};
}

/* Both +3a4 bits are road-boundary responses. Object response a4d74
 * records its counterpart category in collider +438: vehicles are 1/3,
 * breakable course objects are 2. A break clears +3c4 and sets +3c9. */
static unsigned mission_car_contact_types(uint32_t car) {
    unsigned types=(LD8(car+0x3a4)&3) ? MISSION_HIT_WALL : 0;
    if(LD8(car+0x3c9)) types|=MISSION_HIT_BREAKABLE;
    if(LD8(car+0x3c4)) {
        unsigned collider=LD32(car+0x4d8);
        unsigned category=race_valid(collider,0x43a) ? (LD16(collider+0x438)>>8)&31 : 0;
        types|=category==1 || category==3 ? MISSION_HIT_CAR :
            category==2 ? MISSION_HIT_BREAKABLE : MISSION_HIT_SCENERY;
    }
    return types;
}
static int mission_car_contact(uint32_t car) { return mission_car_contact_types(car)!=0; }
static int mission_counted_contact(uint32_t car) {
    return (mission_car_contact_types(car)&k_missions[atomic_load(&g_mission_selected)].collision_types)!=0;
}

/* These are native scenery classes, shared by any CSV recipe. */
typedef struct { const char *name; unsigned create, family, kind; } MissionModel;
static const MissionModel k_mission_models[]={
    {"chairA",0x5ca00,1,0},{"chairB",0x5ca80,1,1},
    {"tableB",0x55250,1,3},{"tentA",0x5cb00,1,4},{"tentC",0x5cb80,1,5},
    {"plantA",0x5cc00,1,6},{"plantB",0x5cc80,1,7},{"walkerA",0x60b50,1,8},
    {"dogA",0x60fc0,1,9},
    {"hitA",0x5ef70,2,11},{"hitB",0x5efb8,2,12},{"hitG",0x5f120,2,17},
    {"phoneA",0x5f168,3,0},{"trashA",0x5f934,3,1},{"pylonA",0x5fb2c,3,8},
    {"boxA",0x5f97c,3,2},{"boxB",0x5f9c4,3,3},{"boxC",0x5fa0c,3,4},
    {"boxD",0x5fa54,3,5},{"boxE",0x5fa9c,3,6},
    {"blockB",0x5fae4,3,7},
    {"sign01B",0x5e494,2,2},{"sign02B",0x5ee50,2,3},
    {"sign03B",0x5ee98,2,4},{"sign04B",0x5eee0,2,5},{"sign05B",0x5ef28,2,6},
    {"break01B",0x60c18,2,0},{"break04B",0x60ca8,2,8},
    {"break04C",0x60cf0,2,9},{"post",0x60d38,2,10},
    {"bench",0x60bd0,5,4}

};
static int mission_model_selected(const char *list,const char *name) {
    while(*list) {
        const char *end=strchr(list,';'); size_t n=end?(size_t)(end-list):strlen(list);
        if(strlen(name)==n && !strncasecmp(list,name,n)) return 1;
        if(!end) break; list=end+1;
    }
    return 0;
}
static int mission_ground(PPCContext *live,float x,float z,float *y);
/* A class can include visually different scenery. Optional native placement
 * indices let CSV recipes select only the intended instances of that class. */
static int mission_scenery_selected(const char *list,const char *name,unsigned index) {
    size_t name_length=strlen(name);
    while(*list) {
        const char *end=strchr(list,';'); if(!end) end=list+strlen(list);
        size_t length=(size_t)(end-list);
        if(length>=name_length && !strncmp(list,name,name_length)) {
            if(length==name_length) return 1;
            if(list[name_length]==':' && length>name_length+1) {
                char *tail; unsigned long value=strtoul(list+name_length+1,&tail,10);
                if(tail==end && value==index) return 1;
            }
        }
        if(!*end) break; list=end+1;
    }
    return 0;
}
static int mission_find_targets(PPCContext *c,const MissionDefinition *d) {
    g_mission_target_count=g_mission_target_goal=0;
    memset(g_mission_target_groups,0,sizeof g_mission_target_groups);
    atomic_store(&g_mission_target_total,0);
    if(*d->parked_models) {
        unsigned catalog=LD32(c->r[2]+0x380);
        if(!race_valid(catalog,8)) return 0;
        unsigned count=LD32(catalog+4),records=catalog+0x308;
        if(count>96 || !race_valid(records,count*36)) return 0;
        int nearest=!strcasecmp(d->parked_models,"nearest"); float best=d->target_radius;
        for(unsigned i=0;i<count;i++) {
            unsigned p=records+i*36,model=mission_tcar_model(c,LD32(p));
            if(model>15) continue;
            char name[8];snprintf(name,sizeof name,"%u",model);
            if(!nearest && strcasecmp(d->parked_models,"all") && !mission_model_selected(d->parked_models,name)) continue;
            float x=LDF32(p+4),z=LDF32(p+12);
            if(!isfinite(x)||!isfinite(z)) continue;
            if(d->contact_targets==1) {
                float distance=hypotf(x-d->target_x[0],z-d->target_z[0]);
                if(distance>best) continue;
                best=distance;g_mission_target_count=0;
            }
            unsigned index=g_mission_target_count++;
            g_mission_targets[index]=(MissionTarget){7,model,i,x,z,0,index};
        }
        g_mission_target_goal=d->target_count ? d->target_count : g_mission_target_count;
        atomic_store(&g_mission_target_total,g_mission_target_goal);
        rt_log("mission: %u parked car targets, goal %u\n",g_mission_target_count,g_mission_target_goal);
        return g_mission_target_goal>0 && g_mission_target_goal<=g_mission_target_count;
    }
    if(d->contact_targets) {
        for(unsigned i=0;i<d->contact_targets;i++)
            g_mission_targets[i]=(MissionTarget){4,0,i,d->target_x[i],d->target_z[i],0,i};
        g_mission_target_count=g_mission_target_goal=d->contact_targets;
        atomic_store(&g_mission_target_total,g_mission_target_goal); return 1;
    }
    if(*d->target_vehicles) {
        unsigned table=LD32(c->r[2]+0x614);
        if(!race_valid(table,12)) return 0;
        unsigned records=LD32(table+4),count=LD32(table+8);
        if(count>MISSION_MAX_TARGETS || !race_valid(records,count*128)) return 0;
        for(unsigned i=0;i<count;i++) {
            unsigned p=records+i*128,kind=LD32(p+0x50);
            char model[16]; snprintf(model,sizeof model,"%u",kind);
            if(!LD32(p+0x10) || !(LD32(p)&0x4000) ||
               (strcasecmp(d->target_vehicles,"all") && !mission_model_selected(d->target_vehicles,model))) continue;
            unsigned index=g_mission_target_count++;
            g_mission_targets[index]=(MissionTarget){6,kind,i,LDF32(p+0x20),LDF32(p+0x28),0,index};
        }
        g_mission_target_goal=d->target_count ? d->target_count : g_mission_target_count;
        atomic_store(&g_mission_target_total,g_mission_target_goal);
        rt_log("mission: %u vehicle targets, goal %u\n",g_mission_target_count,g_mission_target_goal);
        return g_mission_target_goal>0 && g_mission_target_goal<=g_mission_target_count;
    }
    if(!*d->target_models) return 1;
    unsigned world=LD32(c->r[2]+0x53c),catalog=LD32(c->r[2]+0x778);
    if(!race_valid(world,16)||!race_valid(catalog,38*16)) return 0;
    unsigned first=LD32(world),last=LD32(world+12);
    if(first>=last||!race_valid(first,last-first)||last-first>0x400000) return 0;
    for(unsigned p=first;p+68<=last;p+=4) {
        if(LD32(p)!=1) continue;
        for(unsigned m=0;m<sizeof k_mission_models/sizeof *k_mission_models;m++) {
            unsigned cb=LD32(p+20);
            if(!mission_scenery_selected(d->target_models,k_mission_models[m].name,LD16(p+16)) || !race_valid(cb,12) || LD32(cb)!=k_mission_models[m].create) continue;
            unsigned update=LD32(p+24),cleanup=LD32(p+28);
            if(!race_valid(update,12)||!race_valid(cleanup,12)||
                LD32(update+4)!=c->r[2]||LD32(cleanup+4)!=c->r[2]) continue;
            float x=LDF32(p+36),z=LDF32(p+44);
            if(!isfinite(x)||!isfinite(z)||fabsf(x)>100000||fabsf(z)>100000) continue;
            if(d->target_height) { float ground;
                if(!mission_ground(c,x,z,&ground)) return 0;
                if(LDF32(p+40)-ground>d->target_height) continue;
            }
            if(g_mission_target_count==MISSION_MAX_TARGETS) return 0;
            unsigned group=g_mission_target_goal;
            if(d->target_group_radius) for(unsigned j=0;j<g_mission_target_count;j++) {
                MissionTarget t=g_mission_targets[j];
                if(hypotf(t.x-x,t.z-z)<=d->target_group_radius) { group=t.group; break; }
            }
            if(group==g_mission_target_goal) g_mission_target_goal++;
            g_mission_targets[g_mission_target_count++]=(MissionTarget){
                k_mission_models[m].family,k_mission_models[m].kind,LD16(p+16),x,z,0,group};
        }
    }
    unsigned available=g_mission_target_goal;
    if(d->target_count) g_mission_target_goal=d->target_count;
    atomic_store(&g_mission_target_total,g_mission_target_goal);
    rt_log("mission: %u scenery targets, goal %u\n",g_mission_target_count,g_mission_target_goal);
    return g_mission_target_goal>0 && g_mission_target_goal<=available;
}
static void mission_collected(void) {
    atomic_fetch_add(&g_mission_broken,1);
    g_mission_last_collect_ms=mission_now_ms();
}
static void mission_vehicle_mark(PPCContext *c,unsigned record) {
    unsigned table=LD32(c->r[2]+0x614);
    if(!race_valid(table,12)) return;
    unsigned records=LD32(table+4);
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->family==6 && !t->met && record==records+t->index*128) {
            t->met=1; g_mission_target_groups[t->group]=1; mission_collected();
            rt_log("mission: vehicle %u collected\n",t->index); break;
        }
    }
}
/* Native 9eb6c follows the committed vehicle crash state (record +1e=2).
 * Its counterpart is the collider body, not a proximity test. */
static void mission_vehicle_destroy_hook(PPCContext *c) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if(!d->vehicle_destroy) return;
    unsigned car=LD32(c->r[2]+0x488);
    if(getenv("RT_MISSION_TARGET_LOG")) rt_log("mission vehicle crash: target %x counterpart %x player %x\n",c->r[29],c->r[30],LD32(car+0x4d8)+12);
    if(race_valid(car,0x514) && c->r[30]==LD32(car+0x4d8)+12)
        mission_vehicle_mark(c,c->r[29]);
}
static void mission_parked_tick(PPCContext *c,unsigned car,const MissionDefinition *d) {
    if(!*d->parked_models || !mission_car_contact(car)) return;
    unsigned player=LD32(car+0x4d8),pool=LD32(c->r[2]+0x3b0);
    if(!race_valid(player,0x444)||!race_valid(pool,0x18)) return;
    unsigned other=LD32(player+0x43c),count=LD32(pool+4),records=pool+0x18;
    if(getenv("RT_MISSION_TARGET_LOG")) {
        /* Report the first frame of each impact, before a later wall response
         * can replace the native counterpart. Keep this opt-in trace bounded. */
        static unsigned last_other=UINT_MAX,last_flags=UINT_MAX;
        static uint64_t last_ms;
        unsigned flags=LD32(player+0x438);
        uint64_t now=mission_now_ms();
        if(other!=last_other || flags!=last_flags || now-last_ms>1000) {
            MissionPosition pos=mission_car_position(car);
            rt_log("mission parked contact: other %x player %x flags %x prop %x mesh %x at %.2f %.2f %.2f\n",
                other,player,flags,LD32(player+0x430),LD32(player+0x440),pos.x,pos.y,pos.z);
            last_other=other;last_flags=flags;last_ms=now;
        }
    }
    if(!other || count>96 || !race_valid(records,count*128)) return;
    for(unsigned i=0;i<count;i++) {
        unsigned p=records+i*128,collider=LD32(p+0x1c);
        if(!collider || (other!=collider && other!=collider+12)) continue;
        unsigned model=mission_tcar_model(c,LD32(p+8));
        for(unsigned j=0;j<g_mission_target_count;j++) {
            MissionTarget *t=&g_mission_targets[j];
            if(t->family!=7 || t->met || t->kind!=model ||
               hypotf(t->x-LDF32(p+0x24),t->z-LDF32(p+0x2c))>.25f) continue;
            t->met=1;g_mission_target_groups[t->group]=1;mission_collected();
            rt_log("mission: parked car %u model %u collected\n",t->index,t->kind);return;
        }
    }
}
static void mission_vehicle_tick(PPCContext *c,unsigned car,const MissionDefinition *d) {
    if(!*d->target_vehicles) return;
    unsigned table=LD32(c->r[2]+0x614);
    if(!race_valid(table,12)) return;
    unsigned records=LD32(table+4),player=LD32(car+0x4d8);
    unsigned other=race_valid(player,0x444) ? LD32(player+0x43c) : 0;
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        unsigned p=records+t->index*128;
        if(t->family!=6 || !race_valid(p,128)) continue;
        t->x=LDF32(p+0x20); t->z=LDF32(p+0x28);
        if(!d->vehicle_destroy && LD8(car+0x3c4) && LD32(p+0x60) && other==LD32(p+0x60)+12)
            mission_vehicle_mark(c,p);
    }
}
static int mission_auto_collection_complete(const MissionDefinition *d) {
    unsigned goal=d->break_count ? d->break_count : d->target_at_finish ? 0 : g_mission_target_goal;
    return goal && atomic_load(&g_mission_broken)>=goal;
}
static void mission_collection_hint(const MissionDefinition *d,MissionPosition position,uint64_t ms) {
    atomic_store(&g_mission_nearest_item,UINT_MAX);
    unsigned goal=d->break_count ? d->break_count : g_mission_target_goal;
    unsigned done=atomic_load(&g_mission_broken);
    if(g_mission_run.phase!=MISSION_RUNNING || !goal || done>=goal) return;
    uint64_t last=g_mission_last_collect_ms ? g_mission_last_collect_ms : g_mission_run.started;
    if(goal-done>d->hint_remaining && (ms<last || ms-last<d->hint_idle_ms)) return;
    float nearest=INFINITY;
    if(d->break_count) {
        for(unsigned i=0;i<d->break_count;i++) if(!(g_mission_break_mask&(1u<<i)))
            nearest=fminf(nearest,hypotf(position.x-d->break_x[i],position.z-d->break_z[i]));
    } else for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->met || g_mission_target_groups[t->group]) continue;
        nearest=fminf(nearest,hypotf(position.x-t->x,position.z-t->z));
    }
    if(isfinite(nearest)) atomic_store(&g_mission_nearest_item,(unsigned)lroundf(nearest));
}
/* Reject a streamed recreation before its pool allocation. The native create
 * routines already handle this return as an unavailable object. Keep actual
 * scenery identity, rather than addresses in the recycled live-object pool. */
static void mission_target_spawn_hook(PPCContext *c,unsigned family) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    if(k_missions[g_mission_selected].scenery_touch) return; /* Contact does not remove scenery. */
    unsigned kind=family==2 ? c->r[27] : c->r[31];
    unsigned index=family==1 ? c->r[27] : family==2 ? c->r[30] : c->r[29];
    if(family==1 && (kind==8 || kind==9)) return; /* Scared actors remain visible. */
    const MissionDefinition *d=&k_missions[g_mission_selected];
    if(family==5 && kind==d->break_type && d->break_count && race_valid(c->r[27],16)) {
        /* Generic breakable creation receives the original placement transform,
         * whose XYZ starts at +4. This is stable across recycled pool slots. */
        float x=LDF32(c->r[27]+4),z=LDF32(c->r[27]+12);
        for(unsigned i=0;i<d->break_count;i++)
            if((g_mission_break_mask&(UINT32_C(1)<<i)) &&
               hypotf(x-d->break_x[i],z-d->break_z[i])<=d->break_radius) {
                c->r[3]=0;
                return;
            }
    }
    for(unsigned i=0;i<g_mission_target_count;i++) {
        const MissionTarget *t=&g_mission_targets[i];
        if(t->met && t->family==family && t->kind==kind && t->index==index) {
            c->r[3]=0;
            return;
        }
    }
}
static void mission_publish(void);
static void mission_target_hook(PPCContext *c,unsigned family) {
    if(!mission_available()||mission_phase()!=MISSION_RUNNING) return;
    unsigned kind=family==1?c->r[27]:family==2?c->r[28]:c->r[26];
    unsigned live=family==1?c->r[28]:c->r[30];
    unsigned collider=family==1?c->r[26]:family==2?c->r[31]:c->r[28];
    unsigned car=LD32(c->r[2]+0x488);
    if(!race_valid(collider,0x444)||!race_valid(car,0x514)) return;
    unsigned counterpart=LD32(collider+(family==1?0x430:0x43c));
    if(getenv("RT_MISSION_TARGET_LOG")) rt_log("mission target event: family %u kind %u live %x collider %x other %x player %x\n",family,kind,live,collider,counterpart,LD32(car+0x4d8));
    /* Contact records point at the collider body, twelve bytes past its links. */
    if(counterpart!=LD32(car+0x4d8)+0xc) return;
    const MissionDefinition *definition=&k_missions[g_mission_selected];
    for(unsigned m=0;m<sizeof k_mission_models/sizeof *k_mission_models;m++)
        if(k_mission_models[m].family==family && k_mission_models[m].kind==kind &&
           mission_model_selected(definition->protected_models,k_mission_models[m].name)) {
            g_mission_run.phase=MISSION_OBJECTIVE_FAILED;mission_publish();return;
        }

    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->met||t->family!=family||t->kind!=kind) continue;
        unsigned table,offset;
        if(family==1) { table=LD32(c->r[2]+0x2e0); offset=0x10; }
        else if(family==2) { table=LD32(c->r[2]+0x3a0); offset=0xf8; }
        else { table=LD32(c->r[2]+0x3d4); offset=0x60; }
        if(!race_valid(table+kind*12+offset,4)) continue;
        unsigned objects=LD32(table+kind*12+offset);
        if(!race_valid(objects+t->index*4,4)||LD32(objects+t->index*4)!=live) continue;
        t->met=1;
        if(g_mission_target_groups[t->group]) break;
        g_mission_target_groups[t->group]=1; mission_collected();
        rt_log("mission: target %u/%u (%u:%u:%u)\n",atomic_load(&g_mission_broken),g_mission_target_goal,family,kind,t->index);
        break;
    }
}
/* Furniture contact occurs before its impact-strength/state test. A side
 * scrape can be resolved before the player's end-of-frame collision flag. */
static void mission_authored_scenery_contact(PPCContext *c,unsigned family) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    const MissionDefinition *d=&k_missions[g_mission_selected];
    if(!d->contact_targets || *d->parked_models || d->region_visit) return;
    unsigned live=family==1 ? c->r[28] : c->r[30];
    unsigned collider=family==1 ? c->r[26] : c->r[31],car=LD32(c->r[2]+0x488);
    if(!race_valid(live,36) || !race_valid(collider,0x440) || !race_valid(car,0x514) ||
       LD32(collider+(family==1 ? 0x430 : 0x43c))!=LD32(car+0x4d8)+12) return;
    float x=LDF32(live+24),z=LDF32(live+32);
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(!t->met && hypotf(x-t->x,z-t->z)<=d->target_radius) {
            t->met=1; mission_collected();
            rt_log("mission: authored scenery contact %u/%u at %.1f %.1f\n",
                atomic_load(&g_mission_broken),g_mission_target_goal,x,z);
            break;
        }
    }
}
static void mission_contact_targets(const MissionDefinition *d,MissionPosition p,int contact) {
    if(!d->contact_targets||!contact) return;
    float nearest=d->target_radius; int found=-1;
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->met) continue;
        float distance=hypotf(t->x-p.x,t->z-p.z);
        if(distance<=nearest) { nearest=distance; found=(int)i; }
    }
    if(found>=0 && !g_mission_targets[found].met) {
        g_mission_targets[found].met=1; mission_collected();
    }
}
/* This runs immediately after the player's object-contact flag is set. */
static void mission_player_contact_hook(PPCContext *c) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    unsigned car=LD32(c->r[2]+0x488);
    if(c->r[29]!=car || !race_valid(car,0x514)) return;
    const MissionDefinition *d=&k_missions[g_mission_selected];
    if(*d->parked_models || d->region_visit) return;
    unsigned hit=mission_car_contact_types(car);
    mission_contact_targets(d,mission_car_position(car),
        (hit&(MISSION_HIT_SCENERY|MISSION_HIT_BREAKABLE))!=0);
}
/* Authored surface regions count a grounded visit, not a collision or a jump
 * over the area. This lets CSV recipes describe separate patches of grass. */
static void mission_region_targets(PPCContext *c,const MissionDefinition *d,unsigned car) {
    if(!d->region_visit) return;
    MissionPosition p=mission_car_position(car);float y;
    if(mission_ground(c,p.x,p.z,&y) && fabsf(p.y-y)<.6f)
        mission_contact_targets(d,p,1);
}
/* Native animated pedestrian changes from its approach state to an evasive
 * jump at 575e0. The animation request lives at the scenery instance +50. */
static void mission_dodge_hook(PPCContext *c, unsigned request) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    if(getenv("RT_MISSION_TARGET_LOG")) rt_log("mission dodge event: request %x actor %x\n",c->r[28],c->r[26]);
    unsigned table=LD32(c->r[2]+0x2e0),car=LD32(c->r[2]+0x488);
    if(!race_valid(table+9*12+0x10,4)||!race_valid(car,0x514)) return;
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->met||t->family!=1||(t->kind!=8&&t->kind!=9)) continue;
        unsigned objects=LD32(table+t->kind*12+0x10);
        if(!race_valid(objects+t->index*4,4)) continue;
        unsigned live=LD32(objects+t->index*4);
        if(live+0x50!=request) continue;
        /* Actor bodies move independently of their scenery spawn coordinates. */
        unsigned body=race_valid(live,4) ? LD32(live) : 0;
        float x=t->x,z=t->z;
        if(race_valid(body,0x24)) { x=LDF32(body+0x18); z=LDF32(body+0x20); }
        if(hypotf((float)LDF32(car+0x174)-x,(float)LDF32(car+0x178)-z)>12) return;
        t->met=1; mission_collected();
        rt_log("mission: %s %u/%u dodged\n",t->kind==9?"dog":"pedestrian",atomic_load(&g_mission_broken),g_mission_target_goal); return;
    }
}
/* Native 5a39c follows the committed evasive animation. The later bonus branch
 * accepts only human variants 0..4, so hooking its award misses scared dogs. */
static void mission_dog_scare_hook(PPCContext *c) {
    if(!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    unsigned car=LD32(c->r[2]+0x488),table=LD32(c->r[2]+0x2e0);
    if(getenv("RT_MISSION_TARGET_LOG") && race_valid(c->r[1]+0x48,4))
        rt_log("mission scare event: actor %x approaching %x player %x\n",c->r[29],LD32(c->r[1]+0x48),race_valid(car,0x4dc)?LD32(car+0x4d8):0);
    if(!race_valid(car,0x514)||!race_valid(c->r[1]+0x48,4)||
       LD32(c->r[1]+0x48)!=LD32(car+0x4d8)) return;
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->family!=1 || (t->kind!=8 && t->kind!=9) || t->met ||
           !race_valid(table+t->kind*12+0x10,4)) continue;
        unsigned objects=LD32(table+t->kind*12+0x10);
        if(race_valid(objects+t->index*4,4)&&
           LD32(objects+t->index*4)+0x50==c->r[29]) {
            t->met=1; mission_collected();
            rt_log("mission: %s %u/%u scared by player\n",t->kind==9 ? "dog" : "pedestrian",
                atomic_load(&g_mission_broken),g_mission_target_goal); return;
        }
    }
}
static void mission_actor_tick(PPCContext *c) {
    unsigned table=LD32(c->r[2]+0x2e0);
    for(unsigned i=0;i<g_mission_target_count;i++) {
        MissionTarget *t=&g_mission_targets[i];
        if(t->met||t->family!=1||(t->kind!=8&&t->kind!=9)||!race_valid(table+t->kind*12+0x10,4)) continue;
        unsigned objects=LD32(table+t->kind*12+0x10);
        if(!race_valid(objects+t->index*4,4)) continue;
        unsigned live=LD32(objects+t->index*4);
        if(!race_valid(live,4)) continue;
        unsigned body=LD32(live);
        if(!race_valid(body,0x24)) continue;
        float x=LDF32(body+0x18),z=LDF32(body+0x20);
        if(isfinite(x)&&isfinite(z)) { t->x=x; t->z=z; }
    }
}

/* Native 5d1e0 commits a breakable's intact -> broken state after its
 * impact threshold passes and debris is spawned. r21 is scenery kind and
 * r29 its collider; +404/+40c retain the object's original world position.
 * Match authored targets rather than counting impacts or recycled pool IDs. */
static void mission_break_hook(PPCContext *c) {
    if (!mission_available() || mission_phase()!=MISSION_RUNNING) return;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if(!race_valid(c->r[29],0x444)) return;
    float tx=LDF32(c->r[29]+0x404),tz=LDF32(c->r[29]+0x40c);
    unsigned player=LD32(c->r[2]+0x488);
    if(race_valid(player,0x514) && LD32(c->r[29]+0x43c)==LD32(player+0x4d8)+12) {
        for(unsigned i=0;i<g_mission_target_count;i++) {
            MissionTarget *t=&g_mission_targets[i];
            if(t->family==5 && t->kind==c->r[21] && !t->met && hypotf(tx-t->x,tz-t->z)<1) {
                t->met=1; g_mission_target_groups[t->group]=1; mission_collected(); break;
            }
        }
    }
    if(!d->break_count || c->r[21]!=d->break_type) return;
    if(!race_valid(player,0x514) || LD32(c->r[29]+0x43c)!=LD32(player+0x4d8)+12) return;
    float x=LDF32(c->r[29]+0x404), z=LDF32(c->r[29]+0x40c);
    for(unsigned i=0;i<d->break_count;i++) {
        uint32_t bit=UINT32_C(1)<<i;
        if(!(g_mission_break_mask&bit) && hypotf(x-d->break_x[i],z-d->break_z[i])<=d->break_radius) {
            g_mission_break_mask|=bit;
            mission_collected();
            rt_log("mission: destroyed target %u at %.1f %.1f\n",i+1,x,z);
            break;
        }
    }
}
/* CSV input restrictions and a stopping bay defined by the final gate. */
static void mission_special_tick(const MissionDefinition *d,unsigned car,uint64_t ms) {
    if(g_mission_run.phase!=MISSION_RUNNING && g_mission_run.phase!=MISSION_PASSED) return;
    if(((d->forbidden_controls&1) && g_analog[1]>-180) ||
       ((d->forbidden_controls&2) && g_analog[2]>-180) ||
       ((d->forbidden_controls&4) && g_analog[3]>-180) ||
       (d->no_turn_back && fabsf(remainderf(LDF32(car+0xc4)-g_mission_challenge_heading,6.2831853f))>1.5707963f) ||
       (d->collect_gap_ms && g_mission_last_collect_ms && !mission_auto_collection_complete(d) &&
        ms-g_mission_last_collect_ms>d->collect_gap_ms)) {
        g_mission_run.phase=MISSION_OBJECTIVE_FAILED;return;
    }
    if(!d->park_ms || !g_mission_run.gate_count) return;
    MissionGate gate=g_mission_run.gates[g_mission_run.gate_count-1];
    MissionPosition pos=mission_car_position(car);
    float x=pos.x-gate.centre.x,z=pos.z-gate.centre.z;
    float angle=fabsf(remainderf(LDF32(car+0xc4)-atan2f(gate.nx,gate.nz),6.2831853f))*57.2957795f;
    int parked=fabsf(x*gate.nz-z*gate.nx)<=gate.half_width &&
        fabsf(x*gate.nx+z*gate.nz)<=d->park_length*.5f &&
        fabsf(pos.y+1-gate.centre.y)<=gate.half_height+1 &&
        fabs(LDF32(car+0xac))*3.6f<=d->park_speed && angle<=d->park_angle;
    if(!parked) g_mission_park_since=0;
    else if(!g_mission_park_since) g_mission_park_since=ms;
    int complete=parked && ms-g_mission_park_since>=d->park_ms;
    if(g_mission_run.next_gate>=g_mission_run.gate_count)
        g_mission_run.phase=complete ? MISSION_PASSED : MISSION_RUNNING;
}
static void mission_check_objective(void) {
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    /* Judge after the normal rules so collision/time failures take priority
     * over collecting the last target on the same simulation tick. */
    if(d->break_count && g_mission_run.phase==MISSION_RUNNING &&
        atomic_load(&g_mission_broken)>=d->break_count)
        g_mission_run.phase=MISSION_PASSED;
    if(g_mission_target_goal && !d->target_at_finish && g_mission_run.phase==MISSION_RUNNING &&
        atomic_load(&g_mission_broken)>=g_mission_target_goal) g_mission_run.phase=MISSION_PASSED;
    /* A route-guided jump must finish its waypoints and then land. Crossing
     * the final approach gate in the air is not a failed landing attempt. */
    if(d->landing && d->custom_gates && g_mission_run.phase==MISSION_PASSED && !g_mission_landed)
        g_mission_run.phase=MISSION_RUNNING;
    if((d->landing || d->handbrake_turn) && g_mission_landed && g_mission_run.phase==MISSION_RUNNING &&
       (!d->landing || g_mission_run.next_gate>=g_mission_run.gate_count) &&
       (!d->handbrake_turn || !d->custom_finish)) g_mission_run.phase=MISSION_PASSED;
    if(g_mission_run.phase==MISSION_PASSED && ((d->two_wheel_ms && g_mission_two_wheel_ms<d->two_wheel_ms) ||
       (d->roll_degrees && !g_mission_roll_landed) ||
       (d->wallride_distance && g_mission_wall_distance<d->wallride_distance) ||
       ((d->landing || d->handbrake_turn) && !g_mission_landed))) g_mission_run.phase=MISSION_OBJECTIVE_FAILED;
    if(g_mission_run.phase==MISSION_PASSED && atomic_load(&g_mission_broken)<d->break_count)
        g_mission_run.phase=MISSION_OBJECTIVE_FAILED;
    if(g_mission_run.phase==MISSION_PASSED &&
        ((g_mission_target_goal && atomic_load(&g_mission_broken)<g_mission_target_goal)||
         (d->speed_goal && g_mission_max_speed<d->speed_goal)||
         (d->jump_height && g_mission_jump_height<d->jump_height)))
        g_mission_run.phase=MISSION_OBJECTIVE_FAILED;
}

/* Route samples retain the native road heading. Geometry is read only. */
typedef struct { float x, z, heading, length; unsigned checkpoint, node, point, index; } MissionRoadPoint;
static MissionRoadPoint g_mission_road[2048];
static unsigned g_mission_road_count;
static float g_mission_road_length;
static float g_mission_cp_distance[11];
static unsigned g_mission_cp_count;
static MissionRoadPoint g_mission_cp_point[11];
static unsigned g_mission_cp_valid;
static int mission_read_road(uint32_t root) {
    g_mission_road_count = 0; g_mission_road_length = 0;
    g_mission_course_key=2166136261u;
    uint32_t node = root;
    for (unsigned n=0; n<128; n++) {
        if (!race_valid(node, 0x44)) return 0;
        unsigned count = LD16(node); uint32_t data = LD32(node+4);
        if (count < 2 || count > 512 || !race_valid(data, count*32)) return 0;
        for (unsigned i=0; i<count; i++) {
            float x=LDF32(data+i*32), z=LDF32(data+i*32+4), heading=LDF32(data+i*32+20);
            if (!isfinite(x) || !isfinite(z) || !isfinite(heading) ||
                fabsf(x)>100000 || fabsf(z)>100000) return 0;
            if (g_mission_road_count && hypotf(x-g_mission_road[g_mission_road_count-1].x,
                    z-g_mission_road[g_mission_road_count-1].z)<.01f) continue;
            if (g_mission_road_count == 2048) return 0;
            g_mission_course_key=(g_mission_course_key^LD32(data+i*32))*16777619u;
            g_mission_course_key=(g_mission_course_key^LD32(data+i*32+4))*16777619u;
            g_mission_road[g_mission_road_count++] = (MissionRoadPoint){x,z,heading,0,LD8(data+i*32+28),node,data+i*32,i};
        }
        node=LD32(node+12);
        if (node == root) {
            g_mission_cp_count=0; g_mission_cp_valid=0;
            for (unsigned i=0; i<g_mission_road_count; i++) {
                if(g_mission_road[i].checkpoint>2 && g_mission_road[i].checkpoint<=11) {
                    unsigned cp=g_mission_road[i].checkpoint-2;
                    g_mission_cp_distance[cp]=g_mission_road_length;
                    g_mission_cp_point[cp]=g_mission_road[i]; g_mission_cp_valid|=1u<<cp;
                    if(cp>g_mission_cp_count) g_mission_cp_count=cp;
                }
                if(g_mission_road[i].checkpoint==1) {
                    g_mission_cp_distance[0]=g_mission_road_length;
                    g_mission_cp_point[0]=g_mission_road[i]; g_mission_cp_valid|=1;
                }
                MissionRoadPoint *a=&g_mission_road[i], *b=&g_mission_road[(i+1)%g_mission_road_count];
                a->length=hypotf(b->x-a->x,b->z-a->z);
                g_mission_road_length += a->length;
            }
            g_mission_cp_distance[10]=g_mission_road_length+g_mission_cp_distance[0];
            return g_mission_road_count>2 && isfinite(g_mission_road_length) && g_mission_road_length>500;
        }
    }
    return 0;
}
static MissionRoadPoint mission_road_sample(float distance) {
    distance=fmodf(distance,g_mission_road_length);
    if (distance<0) distance+=g_mission_road_length;
    for (unsigned i=0; i<g_mission_road_count; i++) {
        MissionRoadPoint a=g_mission_road[i], b=g_mission_road[(i+1)%g_mission_road_count];
        if (a.length>.01f && distance<=a.length) {
            float t=distance/a.length;
            a.x+=(b.x-a.x)*t; a.z+=(b.z-a.z)*t; return a;
        }
        distance-=a.length;
    }
    return g_mission_road[0];
}

/* Native respawn 90264: f1=x, f2=z, f3=road heading, f4=initial speed.
 * It clears physics, queries terrain, initializes transforms and wheel state.
 * Restore the temporary guest stack and leave live registers/budget intact. */
static int mission_place(PPCContext *live, MissionRoadPoint p, float speed) {
    RtFn place=rt_lookup(0x90264), track=rt_lookup(0x8f384);
    unsigned sp=live->r[1];
    if (!place || !track || sp<16384 || sp>=RAM_SIZE) return 0;
    uint8_t saved[8192]; memcpy(saved,g_ram+sp-sizeof saved,sizeof saved);
    PPCContext c=*live; c.r[1]=sp-512; c.budget=10000000; c.unwind=0;
    c.f[1]=p.x; c.f[2]=p.z; c.f[3]=p.heading; c.f[4]=speed;
    place(&c);
    if (!c.unwind) {
        c.r[3]=LD32(live->r[2]+0x488)+0x4dc; c.f[1]=p.x; c.f[2]=p.z;
        /* Tracking searches around its previous node. Seed the chosen branch
         * before teleporting so a parallel return lane cannot win the search. */
        ST32(c.r[3]+0x28,p.node); ST32(c.r[3]+0x2c,p.point); ST16(c.r[3]+4,p.index);
        ST8(c.r[3]+1,1); ST8(c.r[3]+2,0);
        float native_distance=LDF32(p.point+24);
        STF32(c.r[3]+8,native_distance); STF32(c.r[3]+0xc,native_distance);
        STF32(c.r[3]+0x10,native_distance); STF32(c.r[3]+0x14,native_distance);
        track(&c);
    }
    memcpy(g_ram+sp-sizeof saved,saved,sizeof saved);
    return !c.unwind;
}

/* Query the native terrain at each gate; height bounds reject another road
 * stacked above/below this route. Failed queries abort rather than invent Y. */
static int mission_ground(PPCContext *live, float x, float z, float *y) {
    RtFn tile=rt_lookup(0x5576c), plane=rt_lookup(0x566f0);
    unsigned sp=live->r[1];
    if (!tile || !plane || sp<16384 || sp>=RAM_SIZE) return 0;
    uint8_t saved[8192]; memcpy(saved,g_ram+sp-sizeof saved,sizeof saved);
    PPCContext c=*live; c.r[1]=sp-512; c.budget=10000000; c.unwind=0;
    unsigned scratch=sp-256;
    memset(g_ram+scratch,0,64);
    c.f[1]=x; c.f[2]=z; c.r[5]=scratch; tile(&c);
    if (!c.unwind) {
        STF32(scratch+16,x); STF32(scratch+20,0); STF32(scratch+24,z);
        c.r[3]=scratch+16; c.r[4]=scratch; plane(&c);
    }
    *y=(float)c.f[1];
    /* The native query returns configured fallback heights for missing
     * terrain. In particular 500 is an error surface, not a cliff to spawn on. */
    unsigned defaults=LD32(live->r[2]+0x324);
    int fallback=!c.r[3] || (defaults && c.r[3]==defaults);
    memcpy(g_ram+sp-sizeof saved,saved,sizeof saved);
    return !c.unwind && !fallback && isfinite(*y) && fabsf(*y)<1000;
}
/* Wheel positions are native world-space arrays, not an estimate from body roll.
 * Count only a grounded left or right pair, never front/rear wheels or airborne frames. */
static int mission_on_two_wheels(PPCContext *c,unsigned car) {
    unsigned mask=0;
    for(unsigned i=0;i<4;i++) {
        float x=LDF32(car+0x230+i*4),y=LDF32(car+0x240+i*4),z=LDF32(car+0x250+i*4),ground;
        if(!isfinite(x)||!isfinite(y)||!isfinite(z)||!mission_ground(c,x,z,&ground)) return 0;
        if(fabsf(y-ground)<.2f) mask|=1u<<i;
    }
    return (mask==5 || mask==10) && fabs(LDF32(car+0xac))>1.4f;
}
/* JAB a7900: native corner success, after route, speed and clean-contact
 * checks, before choosing the spoken language. Never infer this from audio. */
static void mission_native_corner_hook(PPCContext *c) {
    if(g_mission_run.phase!=MISSION_RUNNING || c->r[27]!=LD32(c->r[2]+0x488)) return;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if(d->handbrake_turn) g_mission_landed=1;
}
static void mission_stunt_tick(PPCContext *c,unsigned car,const MissionDefinition *d,uint64_t ms) {
    MissionPosition p=mission_car_position(car);
    if(getenv("RT_MISSION_STUNT_LOG")) {
        static uint64_t last_trace;
        if(ms<last_trace || ms-last_trace>=200) {
            float ground=0;int valid=mission_ground(c,p.x,p.z,&ground);
            rt_log("mission stunt: X %.2f Y %.2f Z %.2f ground %.2f valid %d speed %.1f jump %.2f landing Y %.2f distance %.2f\n",
                p.x,p.y,p.z,ground,valid,fabs(LDF32(car+0xac))*3.6f,g_mission_jump_height,
                g_mission_landing_y,hypotf(p.x-d->landing_x,p.z-d->landing_z));
            if(d->handbrake_turn) rt_log("mission turn: yaw %.1f angle %.1f active %d handbrake %d contacts %u\n",
                LDF32(car+0x1b0)*57.2957795f,g_mission_turn_angle*57.2957795f,
                g_mission_turn_active,g_analog[3],g_mission_run.contacts);
            last_trace=ms;
        }
    }
    if(d->wallride_distance) {
        unsigned ride=d->rolling_gate;
        float dx=d->end_x-d->gate_x[ride],dz=d->end_z-d->gate_z[ride],length=dx*dx+dz*dz;
        float along=length>0 ? ((p.x-d->gate_x[ride])*dx+(p.z-d->gate_z[ride])*dz)/length : -1;
        float gap=hypotf(p.x-d->gate_x[ride]-along*dx,p.z-d->gate_z[ride]-along*dz);
        float wx=(LDF32(car+0x234)+LDF32(car+0x23c)-LDF32(car+0x230)-LDF32(car+0x238))*.5f;
        float wy=(LDF32(car+0x244)+LDF32(car+0x24c)-LDF32(car+0x240)-LDF32(car+0x248))*.5f;
        float wz=(LDF32(car+0x254)+LDF32(car+0x25c)-LDF32(car+0x250)-LDF32(car+0x258))*.5f;
        float tilt=fabsf(atan2f(wy,hypotf(wx,wz)));
        int wall=along>=0&&along<=1&&gap<=18&&(LD8(car+0x3a4)&3)&&tilt>.61086524f&&fabs(LDF32(car+0xac))>5.5f;
        float travel=hypotf(p.x-g_mission_run.previous.x,p.z-g_mission_run.previous.z);
        if(wall&&g_mission_wall_previous&&travel<12) g_mission_wall_distance+=travel;
        g_mission_wall_previous=wall;
    }
    if(d->roll_degrees) {
        /* Measure twist from the actual wheel frame. Native Euler yaw can
         * flip by pi upside down, hiding a roll if it defines the reference. */
        float right[3],forward[3];
        for(unsigned axis=0;axis<3;axis++) {
            uint32_t wheels=car+0x230+16*axis;
            right[axis]=(LDF32(wheels+4)+LDF32(wheels+12)-LDF32(wheels)-LDF32(wheels+8))*.5f;
            forward[axis]=(LDF32(wheels)+LDF32(wheels+4)-LDF32(wheels+8)-LDF32(wheels+12))*.5f;
        }
        float fn=hypotf(hypotf(forward[0],forward[1]),forward[2]);
        if(isfinite(fn) && fn>.1f) {
            for(unsigned i=0;i<3;i++) forward[i]/=fn;
            float dot=right[0]*forward[0]+right[1]*forward[1]+right[2]*forward[2];
            for(unsigned i=0;i<3;i++) right[i]-=dot*forward[i];
            float rn=hypotf(hypotf(right[0],right[1]),right[2]);
            if(isfinite(rn) && rn>.1f) {
                for(unsigned i=0;i<3;i++) right[i]/=rn;
                float up=right[2]*forward[0]-right[0]*forward[2],ground=0;
                int valid_ground=mission_ground(c,p.x,p.z,&ground);
                int tilted=up<.9f,air=valid_ground && p.y-ground>.8f;
                if(!g_mission_roll_airborne && (tilted || air)) {
                    g_mission_roll_airborne=1;g_mission_roll_angle=0;
                }
                if(g_mission_roll_airborne && g_mission_roll_vector_valid) {
                    float old[3],projection=0;
                    for(unsigned i=0;i<3;i++) projection+=g_mission_roll_right[i]*forward[i];
                    for(unsigned i=0;i<3;i++) old[i]=g_mission_roll_right[i]-projection*forward[i];
                    float cross[3]={old[1]*right[2]-old[2]*right[1],old[2]*right[0]-old[0]*right[2],old[0]*right[1]-old[1]*right[0]};
                    float sine=0,cosine=0;
                    for(unsigned i=0;i<3;i++) {sine+=forward[i]*cross[i];cosine+=old[i]*right[i];}
                    if(hypotf(sine,cosine)>.1f) g_mission_roll_angle+=atan2f(sine,cosine);
                }
                memcpy(g_mission_roll_right,right,sizeof right);g_mission_roll_vector_valid=1;
                /* Ground contact while sideways/upside down does not finish
                 * an attempt. A complete roll ends when upright and landed. */
                if(g_mission_roll_airborne && !tilted && valid_ground && fabsf(p.y-ground)<.8f) {
                    if(fabsf(g_mission_roll_angle)*57.2957795f>=d->roll_degrees-15) g_mission_roll_landed=1;
                    g_mission_roll_airborne=0;
                }
            }
        }
    }
    if(d->two_wheel_ms) {
        int two=mission_on_two_wheels(c,car);
        uint64_t last=g_mission_run.last_sample ? g_mission_run.last_sample : g_mission_run.started;
        if(two && g_mission_two_wheel_previous && ms>=last && ms-last<250)
            g_mission_two_wheel_ms+=(unsigned)(ms-last);
        g_mission_two_wheel_previous=two; atomic_store(&g_mission_two_wheel_display,g_mission_two_wheel_ms);
    }
    if(d->landing && g_mission_jump_height>=d->jump_height &&
       (!d->custom_gates || g_mission_run.next_gate>=g_mission_run.gate_count)) {
        float ground;
        if(hypotf(p.x-d->landing_x,p.z-d->landing_z)<=d->landing_radius &&
           fabsf(p.y-g_mission_landing_y)<=d->landing_height && mission_ground(c,p.x,p.z,&ground) && fabsf(p.y-ground)<.6f)
            g_mission_landed=1;
    }

}
static void mission_publish(void) {
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if(g_mission_run.phase==MISSION_PASSED && !atomic_load(&g_mission_result_at))
        atomic_store(&g_mission_result_at,rt_now()+(uint64_t)d->completion_delay_ms*(CPU_HZ/1000));
    if(g_mission_run.phase==MISSION_CONTACT_FAILED && !atomic_load(&g_mission_result_at)) {
        unsigned delay=k_missions[atomic_load(&g_mission_selected)].collision_delay_ms;
        atomic_store(&g_mission_result_at,rt_now()+(uint64_t)delay*(CPU_HZ/1000));
    }
    for(unsigned i=0;i<g_mission_run.gate_count;i++)
        if((g_mission_run.any_order ? (g_mission_run.met_mask&(1u<<i)) : i<g_mission_run.next_gate) &&
           !atomic_load(&g_mission_gate_times[i])) atomic_store(&g_mission_gate_times[i],(unsigned)g_mission_run.elapsed_ms);
    atomic_store(&g_mission_elapsed,(unsigned)g_mission_run.elapsed_ms);
    atomic_store(&g_mission_contacts,g_mission_run.contacts);
    atomic_store(&g_mission_native_success,d->handbrake_turn && g_mission_landed);
    atomic_store(&g_mission_gate,g_mission_run.next_gate);
    atomic_store(&g_mission_gates,g_mission_run.gate_count);
    if(g_mission_run.any_order) {
        float nearest=INFINITY;
        for(unsigned i=0;i<g_mission_run.gate_count;i++) if(!(g_mission_run.met_mask&(1u<<i))) {
            MissionPosition g=g_mission_run.gates[i].centre;
            nearest=fminf(nearest,hypotf(g.x-g_mission_run.previous.x,g.z-g_mission_run.previous.z));
        }
        atomic_store(&g_mission_distance,isfinite(nearest) ? (unsigned)lroundf(nearest) : 0);
    } else if (g_mission_run.next_gate < g_mission_run.gate_count) {
        MissionPosition g=g_mission_run.gates[g_mission_run.next_gate].centre;
        atomic_store(&g_mission_distance,(unsigned)lroundf(hypotf(g.x-g_mission_run.previous.x,g.z-g_mission_run.previous.z)));
    } else atomic_store(&g_mission_distance,0);
    atomic_store(&g_mission_phase,g_mission_run.phase);
}
/* Register authored obstacles through the native TCAR catalog so rendering,
 * streaming and collision use the same placement. Retry snapshots may retain
 * our last record; replace it rather than accumulating cars. */
static unsigned g_mission_obstacle_catalog,g_mission_obstacle_index,g_mission_obstacle_descriptor;
static int mission_place_obstacle(PPCContext *live,const MissionDefinition *d) {
    unsigned catalog=LD32(live->r[2]+0x380);
    if(!race_valid(catalog,0x308+128*36)) return !d->obstacle;
    unsigned count=LD32(catalog+4);
    if(catalog==g_mission_obstacle_catalog && count==g_mission_obstacle_index+1 &&
       LD32(catalog+0x308+g_mission_obstacle_index*36)==g_mission_obstacle_descriptor)
        ST32(catalog+4,--count);
    g_mission_obstacle_catalog=0;
    if(!d->obstacle) return 1;
    if(count>=128) return 0;
    unsigned groups=LD32(live->r[2]+0x40),descriptor=0;
    if(!race_valid(groups,4) || LD32(groups)>128) return 0;
    for(unsigned i=1;i<LD32(groups);i++) {
        unsigned group=groups+i*36;
        if(!race_valid(group,36)) return 0;
        if(LD32(group+8)==31 && d->obstacle_model<LD32(group+16)) {
            descriptor=LD32(group+20)+d->obstacle_model*16;break;
        }
    }
    float y;
    RtFn create=rt_lookup(0x5b56c); unsigned sp=live->r[1];
    if(!race_valid(descriptor,16) || !create || sp<16384 || sp>=RAM_SIZE ||
       !mission_ground(live,d->obstacle_x,d->obstacle_z,&y)) return 0;
    uint8_t saved[8192];memcpy(saved,g_ram+sp-sizeof saved,sizeof saved);
    unsigned scratch=sp-256;memset(g_ram+scratch,0,64);
    STF32(scratch,d->obstacle_x);STF32(scratch+4,y);STF32(scratch+8,d->obstacle_z);
    STF32(scratch+16,d->obstacle_heading*.01745329252f);
    PPCContext c=*live;c.r[1]=sp-512;c.budget=10000000;c.unwind=0;
    c.r[3]=scratch;c.r[4]=scratch+12;c.r[5]=descriptor;c.r[6]=1;c.r[7]=0;c.r[8]=0;
    memset(g_ram+catalog+0x308+count*36,0,36);
    create(&c);memcpy(g_ram+sp-sizeof saved,saved,sizeof saved);
    if(c.unwind || LD32(catalog+4)!=count+1) return 0;
    g_mission_obstacle_catalog=catalog;g_mission_obstacle_index=count;
    g_mission_obstacle_descriptor=descriptor;
    return 1;
}
/* Rolling-start run-up behind a start line: as far back as 4 m while the ground
 * stays level enough to drive (no ledge, wall or drop between it and the line). */
static float mission_rstart_runup(PPCContext *c, MissionGate line) {
    float previous, run=0;
    if(!mission_ground(c,line.centre.x,line.centre.z,&previous)) return 0;
    for(float back=1;back<=4;back+=1) {
        float y;
        if(!mission_ground(c,line.centre.x-line.nx*back,line.centre.z-line.nz*back,&y) ||
           fabsf(y-previous)>1) break;
        previous=y; run=back;
    }
    return run;
}
/* Rolling-start speed in m/s for the leg to the next gate. Where the ground drops
 * away (a jump), the speed carries the car 8 m past the far side of the gap, or to
 * the next gate if that is nearer: the game's gravity is about 23.4 m/s2 and a lip
 * lifts a car by about 0.17 of its speed (measured on the Town river jump). On the
 * road the speed falls from 140 to 60 km/h as the turn to the next gate grows from
 * 15 to 90 degrees. */
static float mission_rstart_speed(PPCContext *c, MissionGate line, const MissionGate *next) {
    if(!next) return 100/3.6f;
    float dx=next->centre.x-line.centre.x,dz=next->centre.z-line.centre.z,distance=hypotf(dx,dz);
    if(distance<1) return 100/3.6f;
    dx/=distance;dz/=distance;
    float launch,edge=-1,edge_y=0,bottom=INFINITY,target=distance,target_y=next->centre.y;
    if(mission_ground(c,line.centre.x,line.centre.z,&launch)) {
        float previous=launch;
        for(float along=1;along<distance;along+=1) {
            float y;
            int ground=mission_ground(c,line.centre.x+dx*along,line.centre.z+dz*along,&y);
            if(edge<0) {
                if(!ground || y<launch-2) {edge=along-1;edge_y=previous;}
                else previous=y;
            }
            if(edge<0 || !ground) continue;
            if(y<bottom) bottom=y;
            if(y>bottom+2) {
                if(along+8<target) {target=along+8;mission_ground(c,line.centre.x+dx*target,line.centre.z+dz*target,&target_y);}
                break;
            }
        }
    }
    if(edge>=0) {
        float gap=target-edge,drop=edge_y-target_y,lift=drop+.17f*gap;
        float speed=gap*sqrtf(23.4f/(2*fmaxf(.5f,lift)))*1.03f;
        return fmaxf(40/3.6f,fminf(250/3.6f,speed));
    }
    float turn=acosf(fmaxf(-1,fminf(1,line.nx*dx+line.nz*dz)))*57.2957795f;
    return (140-80*fmaxf(0,fminf(1,(turn-15)/75)))/3.6f;
}
static void mission_begin(PPCContext *c, uint32_t car) {
    memset(&g_mission_run,0,sizeof g_mission_run);
    g_mission_run.vehicle_height=2;
    g_mission_break_mask=0; atomic_store(&g_mission_broken,0);
    g_mission_target_count=g_mission_target_goal=0;
    atomic_store(&g_mission_target_total,0); atomic_store(&g_mission_speed,0);
    g_mission_max_speed=g_mission_jump_height=0; g_mission_last_collect_ms=0;g_mission_park_since=0;
    g_mission_two_wheel_ms=0; g_mission_two_wheel_previous=0; atomic_store(&g_mission_two_wheel_display,0);
    g_mission_roll_angle=g_mission_roll_previous=0;
    g_mission_roll_airborne=g_mission_roll_landed=0;g_mission_roll_vector_valid=0;
    g_mission_wall_distance=0; g_mission_wall_previous=0;
    g_mission_turn_active=g_mission_landed=g_mission_turn_button_previous=0;
    g_mission_turn_release_at=0;
    atomic_store(&g_mission_nearest_item,UINT_MAX);
    atomic_store(&g_mission_result_at,0);
    for(unsigned i=0;i<MISSION_MAX_GATES;i++) atomic_store(&g_mission_gate_times[i],0);
    g_mission_run.phase=MISSION_SETUP_FAILED;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if (!mission_read_road(LD32(0x8c0188))) { mission_publish(); return; }
    if(!mission_place_obstacle(c,d) || !mission_find_targets(c,d)) { mission_publish(); return; }
    g_mission_landing_y=d->landing_y;
    if(d->landing && !isfinite(g_mission_landing_y) && !mission_ground(c,d->landing_x,d->landing_z,&g_mission_landing_y)) { mission_publish(); return; }
    atomic_store(&g_race_mission_practice,g_mission_target_goal && !d->target_at_finish);
    const unsigned recipe[]={d->start_cp,d->finish_cp,d->limit_ms,d->contacts,d->collision_types,
        d->car,d->transmission,d->region,d->gold_ms,d->silver_ms,d->rolling_metres,d->lead_in_metres,(unsigned)(d->rolling_speed*1000),d->break_count,d->break_type,d->tuned,d->target_count,d->target_at_finish,d->custom_start,d->contact_targets,d->traffic,16};
    for (unsigned i=0;i<sizeof recipe/sizeof *recipe;i++)
        g_mission_course_key=(g_mission_course_key^recipe[i])*16777619u;
    for(const unsigned char *s=(const unsigned char*)d->target_models;*s;s++)
        g_mission_course_key=(g_mission_course_key^*s)*16777619u;
    if(d->region_visit) g_mission_course_key=(g_mission_course_key^0x76697369u)*16777619u;
    for(const unsigned char *p=(const unsigned char*)d->parked_models;*p;p++)
        g_mission_course_key=(g_mission_course_key^*p)*16777619u;
    const float extra[]={d->start_x,d->start_z,d->start_heading,d->speed_goal,d->jump_height,d->target_radius,d->target_group_radius};
    for(unsigned i=0;i<sizeof extra/sizeof *extra;i++) { unsigned bits; memcpy(&bits,&extra[i],4); g_mission_course_key=(g_mission_course_key^bits)*16777619u; }
    for(unsigned i=0;i<d->contact_targets;i++) {
        const float values[]={d->target_x[i],d->target_z[i]};
        for(unsigned j=0;j<2;j++) { unsigned bits; memcpy(&bits,&values[j],4); g_mission_course_key=(g_mission_course_key^bits)*16777619u; }
    }
    for(unsigned i=0;i<d->custom_gates;i++) {
        const float values[]={d->gate_x[i],d->gate_z[i],d->gate_width[i]};
        for(unsigned j=0;j<3;j++) { unsigned bits; memcpy(&bits,&values[j],sizeof bits); g_mission_course_key=(g_mission_course_key^bits)*16777619u; }
    }
    for(unsigned i=0;i<d->break_count;i++) {
        const float values[]={d->break_x[i],d->break_z[i],d->break_radius};
        for(unsigned j=0;j<3;j++) { unsigned bits; memcpy(&bits,&values[j],sizeof bits); g_mission_course_key=(g_mission_course_key^bits)*16777619u; }
    }
    if(d->custom_finish) {
        const float values[]={d->end_x,d->end_z,d->end_width};
        for(unsigned j=0;j<3;j++) { unsigned bits; memcpy(&bits,&values[j],sizeof bits); g_mission_course_key=(g_mission_course_key^bits)*16777619u; }
    }
    int direction=d->route_direction<0 ? -1 : 1;
    g_mission_rolling_direction=direction;
    if(direction<0) g_mission_course_key=(g_mission_course_key^0x726576u)*16777619u;
    g_mission_car_key=LD8(car);
    atomic_store(&g_mission_best,mission_menu_best((unsigned)atomic_load(&g_mission_selected)));
    int free_collection=g_mission_target_goal && !d->target_at_finish;
    if(d->start_cp>=g_mission_cp_count+2 || (!free_collection && !d->custom_finish && d->finish_cp>=g_mission_cp_count+2)) { mission_publish(); return; }
    /* Native section checkpoint numbers include the unused marker 2. The
     * first sprint keeps its established first gate; section missions address
     * their native checkpoint numbers rather than compacting that gap. */
    unsigned start_index=d->start_cp ? d->start_cp-1 : 0;
    unsigned finish_index=d->finish_cp>1 ? d->finish_cp-1 : 1;
    if(free_collection || d->custom_finish) finish_index=g_mission_cp_count;
    float start=g_mission_cp_distance[start_index];
    float metres=(g_mission_cp_distance[finish_index]-start)*direction;
    if(metres<=0) metres+=g_mission_road_length;
    if (direction>0 && !d->start_cp && !d->rolling_metres && !d->custom_start) {
        /* Start at the native grid, before the start line, rather than teleporting
         * a sprint to an arbitrary percentage of the lap. */
        MissionPosition pos=mission_car_position(car);
        float nearest=INFINITY, dist=0;
        for (unsigned i=0;i<g_mission_road_count;i++) {
            MissionRoadPoint a=g_mission_road[i], b=g_mission_road[(i+1)%g_mission_road_count];
            float dx=b.x-a.x, dz=b.z-a.z, norm=dx*dx+dz*dz;
            float t=norm>.01f ? fmaxf(0,fminf(1,((pos.x-a.x)*dx+(pos.z-a.z)*dz)/norm)) : 0;
            float gap=hypotf(pos.x-a.x-t*dx,pos.z-a.z-t*dz);
            if (gap<nearest) { nearest=gap; start=dist+t*a.length; }
            dist+=a.length;
        }
        if (start>g_mission_cp_distance[1]) start-=g_mission_road_length;
        metres=g_mission_cp_distance[1]-start;
        rt_log("mission: native grid %.1f, %.1f; road %.1f m\n",pos.x,pos.z,start);
    }
    unsigned count=1;
    if (!count || count>MISSION_MAX_GATES) { mission_publish(); return; }
    for (unsigned i=0; i<count; i++) {
        float dist=start+direction*metres*(i+1)/count;
        MissionRoadPoint p=mission_road_sample(dist), ahead=mission_road_sample(dist+direction);
        float dx=ahead.x-p.x, dz=ahead.z-p.z, norm=hypotf(dx,dz), y;
        if (norm<.01f || !mission_ground(c,p.x,p.z,&y)) { mission_publish(); return; }
        g_mission_run.gates[i]=(MissionGate){{p.x,y,p.z},dx/norm,dz/norm,18,4,0};
    }
    MissionGate rolling_line; memset(&rolling_line,0,sizeof rolling_line);
    if (d->custom_gates || d->custom_finish) {
        MissionGate finish=g_mission_run.gates[count-1];
        unsigned ordered=0;int explicit_finish=0;
        for (unsigned i=0;i<d->custom_gates+d->custom_finish;i++) {
            float gx=i<d->custom_gates ? d->gate_x[i] : d->end_x;
            float gz=i<d->custom_gates ? d->gate_z[i] : d->end_z;
            float width=i<d->custom_gates ? d->gate_width[i] : d->end_width;
            float nearest=INFINITY, nx=0,nz=1,y, along=0;
            float approach_x=i ? d->gate_x[i-1] : d->start_x;
            float approach_z=i ? d->gate_z[i-1] : d->start_z;
            float ax=gx-approach_x,az=gz-approach_z,an=hypotf(ax,az),nearest_gap=INFINITY;
            for (unsigned j=0;j<g_mission_road_count;j++) {
                MissionRoadPoint a=g_mission_road[j],b=g_mission_road[(j+1)%g_mission_road_count];
                float dx=b.x-a.x,dz=b.z-a.z,n=dx*dx+dz*dz;
                float t=n>.01f ? fmaxf(0,fminf(1,((gx-a.x)*dx+(gz-a.z)*dz)/n)) : 0;
                float gate_along=along+t*a.length; along+=a.length;
                /* Parallel roads can share scenery coordinates but travel in
                 * opposite directions. Only this mission section owns its gates. */
                float progress=fmodf((gate_along-start)*direction+g_mission_road_length,g_mission_road_length);
                if(!d->custom_start && progress>metres+.01f) continue;
                float gap=hypotf(gx-a.x-t*dx,gz-a.z-t*dz);
                float alignment=an>.01f&&n>.01f ? (dx*ax+dz*az)*direction/(sqrtf(n)*an) : 0;
                float score=gap+(d->custom_start ? fmaxf(0,-alignment)*25 : 0);
                if(n>.01f && score<nearest) { nearest=score; nearest_gap=gap; nx=direction*dx/sqrtf(n); nz=direction*dz/sqrtf(n); }
            }
            /* A shortcut can have no centreline. Its ordered approach still
             * gives a gate direction; ordinary road/finish gates retain their
             * own road heading rather than inheriting the spawn heading. */
            if(d->custom_start && nearest_gap>18 && an>.01f) { nx=ax/an; nz=az/an; nearest=0; }
            if(!isfinite(nearest)) { mission_publish(); return; }
            if(i<d->custom_gates && isfinite(d->gate_y[i])) y=d->gate_y[i];
            else if(!mission_ground(c,gx,gz,&y)) { mission_publish(); return; }
            unsigned role=i<d->custom_gates ? d->gate_role[i] : 2;
            if(i<d->custom_gates && isfinite(d->gate_heading[i]) && d->gate_height[i]>0) {
                float heading=d->gate_heading[i]*.01745329252f;nx=sinf(heading);nz=cosf(heading);
            }
            if(i<d->custom_gates && d->gate_height[i]>0 && isfinite(d->gate_y[i])) y=d->gate_y[i];
            float height=i<d->custom_gates && d->gate_height[i]>0 ? d->gate_height[i]/2 : 4;
            MissionGate gate={{gx,y,gz},nx,nz,width/2,height,0};
            if(i<d->custom_gates) gate.tilt=d->gate_tilt[i]*.01745329252f;
            if(role==1) g_mission_run.failure_gates[g_mission_run.failure_gate_count++]=gate;
            else if(role==4) rolling_line=gate;
            else {g_mission_run.gates[ordered++]=gate;if(role==2) explicit_finish=1;}
        }
        count=ordered;
        if(!explicit_finish && !d->any_order) g_mission_run.gates[count++]=finish;
        if(d->any_order) g_mission_run.any_order=1;
    }
    MissionRoadPoint entry=mission_road_sample(start), entry_ahead=mission_road_sample(start+direction);
    float entry_dx=entry_ahead.x-entry.x, entry_dz=entry_ahead.z-entry.z;
    float entry_norm=hypotf(entry_dx,entry_dz), entry_y;
    if (entry_norm<.01f || !mission_ground(c,entry.x,entry.z,&entry_y)) { mission_publish(); return; }
    g_mission_entry=(MissionGate){{entry.x,entry_y,entry.z},entry_dx/entry_norm,entry_dz/entry_norm,18,4,0};
    g_mission_roll_distance=start-direction*(float)d->rolling_metres;
    MissionRoadPoint spawn=mission_road_sample(g_mission_roll_distance);
    /* Derive the actual travel heading from geometry, rather than a table
     * entry or a manual flip that can send the car away from its gates. */
    MissionRoadPoint spawn_ahead=mission_road_sample(g_mission_roll_distance+direction*12);
    spawn.heading=atan2f(spawn_ahead.x-spawn.x,spawn_ahead.z-spawn.z);
    if(d->custom_start) {
        /* Custom coordinates can be far from the nominal checkpoint. Seed the
         * native tracker at their actual road node so streaming and recovery
         * follow the new position, including on parallel return lanes. */
        float nearest=INFINITY,along=0,entry_distance=0;
        float heading=d->start_heading*.01745329252f,start_x=d->start_x,start_z=d->start_z;
        if(d->rolling_gate) {
            /* The authored gate is the timing line; the car starts behind it. */
            float run=mission_rstart_runup(c,rolling_line);
            heading=atan2f(rolling_line.nx,rolling_line.nz);
            start_x=rolling_line.centre.x-rolling_line.nx*run;start_z=rolling_line.centre.z-rolling_line.nz*run;
        }
        for(unsigned j=0;j<g_mission_road_count;j++) {
            MissionRoadPoint a=g_mission_road[j],b=g_mission_road[(j+1)%g_mission_road_count];
            float dx=b.x-a.x,dz=b.z-a.z,n=hypotf(dx,dz);
            float t=n>.01f ? fmaxf(0,fminf(1,((start_x-a.x)*dx+(start_z-a.z)*dz)/(n*n))) : 0;
            float gap=hypotf(start_x-a.x-t*dx,start_z-a.z-t*dz);
            float alignment=n>.01f ? (dx*sinf(heading)+dz*cosf(heading))/n : 0;
            float score=gap+fmaxf(0,-alignment)*25;
            if(score<nearest) { nearest=score; spawn=a;entry_distance=along+t*a.length;g_mission_rolling_direction=alignment<0 ? -1 : 1; }
            along+=a.length;
        }
        spawn.x=start_x; spawn.z=start_z; spawn.heading=heading;
        float spawn_y;
        if(!mission_ground(c,spawn.x,spawn.z,&spawn_y)) {
            rt_log("mission: no terrain at custom start %.1f, %.1f\n",spawn.x,spawn.z);
            mission_publish(); return;
        }
        if(d->lead_in_metres) {
            /* The authored point belongs to the challenge. Move the timing
             * line upstream along the road; auto-drive must end before it. */
            entry_distance-=g_mission_rolling_direction*d->lead_in_metres;
            spawn=mission_road_sample(entry_distance);
            MissionRoadPoint ahead=mission_road_sample(entry_distance+g_mission_rolling_direction*12);
            heading=atan2f(ahead.x-spawn.x,ahead.z-spawn.z);spawn.heading=heading;
            if(!mission_ground(c,spawn.x,spawn.z,&spawn_y)) {mission_publish();return;}
        }
        if(d->rolling_metres) {
            MissionRoadPoint line=mission_road_sample(entry_distance),line_ahead=mission_road_sample(entry_distance+g_mission_rolling_direction*12);
            float line_heading=atan2f(line_ahead.x-line.x,line_ahead.z-line.z);
            g_mission_entry=(MissionGate){{spawn.x,spawn_y,spawn.z},sinf(line_heading),cosf(line_heading),18,4,0};
            g_mission_roll_distance=entry_distance-g_mission_rolling_direction*d->rolling_metres;
            spawn=mission_road_sample(g_mission_roll_distance);
            MissionRoadPoint ahead=mission_road_sample(g_mission_roll_distance+g_mission_rolling_direction*12);
            spawn.heading=atan2f(ahead.x-spawn.x,ahead.z-spawn.z);
        }
        if(d->rolling_gate) g_mission_entry=rolling_line;
    }
    float spawn_speed=d->rolling_speed;
    if(d->rolling_gate && !(spawn_speed>0)) spawn_speed=mission_rstart_speed(c,rolling_line,count ? &g_mission_run.gates[0] : NULL);
    if(d->rolling_gate) rt_log("mission: rolling start %.1f, %.1f heading %.1f at %.0f km/h\n",
        spawn.x,spawn.z,spawn.heading*57.2957795f,spawn_speed*3.6f);
    if (!mission_place(c,spawn,spawn_speed)) { mission_publish(); return; }
    unsigned timing=LD32(c->r[2]+0x554), route=car+0x4dc;
    if (race_valid(timing,0xd4)) {
        ST8(timing+0x11,LD8(route+1)); ST8(timing+0x12,LD8(route+2));
        STF32(timing+0x1c,LDF32(route+8)); STF32(timing+0xc8,LDF32(route+8));
        ST32(timing+0xd0,LD32(route+0x2c));
        ST8(timing+0xcc,0); ST8(timing+0xcd,0); ST8(timing+0xce,0);
    }
    unsigned waypoints=0;
    for(unsigned i=0;i<d->custom_gates;i++) if(d->gate_role[i]!=1 && d->gate_role[i]!=4) waypoints++;
    g_mission_run.required_pass_mask=waypoints ? (1u<<waypoints)-1 : 0;
    g_mission_run.gate_count=count; g_mission_run.limit_ms=d->limit_ms;
    g_mission_run.contact_limit=d->contacts;
    /* Whole-course collection runs may cross the finish and continue another lap. */
    if((g_mission_target_goal && !d->target_at_finish) || (d->handbrake_turn && !d->custom_finish)) g_mission_run.gate_count=0;
    if(d->landing) g_mission_run.gate_count=waypoints;
    g_mission_run.previous=mission_car_position(car);
    g_mission_challenge_heading=LDF32(car+0xc4);
    g_mission_run.phase=d->rolling_metres || d->rolling_gate ? MISSION_ROLLING : MISSION_RUNNING;
    g_mission_trace_marker=UINT_MAX; g_mission_trace_gate=UINT_MAX;
    g_mission_placements++;
    g_mission_roll_started=mission_now_ms();
    g_mission_run.started=g_mission_roll_started;
    atomic_store(&g_mission_auto_steer,0);
    atomic_store(&g_mission_countdown,0);
    mission_publish();
    rt_log("mission: %s, start %.1f m, section %.1f m, %u gates\n",d->name,start,metres,g_mission_run.gate_count);
    if(getenv("RT_MISSION_GATE_LOG") && g_mission_run.phase==MISSION_ROLLING)
        rt_log("mission timing line: XYZ %.2f %.2f %.2f heading %.2f\n",g_mission_entry.centre.x,g_mission_entry.centre.y,
            g_mission_entry.centre.z,atan2f(g_mission_entry.nx,g_mission_entry.nz)*57.2957795f);
    if(getenv("RT_MISSION_GATE_LOG")) for(unsigned i=0;i<g_mission_run.gate_count;i++) {
        MissionGate *gate=&g_mission_run.gates[i];
        rt_log("mission gate %u: XYZ %.2f %.2f %.2f normal %.3f %.3f width %.1f height %.1f\n",
            i+1,gate->centre.x,gate->centre.y,gate->centre.z,gate->nx,gate->nz,gate->half_width*2,gate->half_height*2);
    }

}
static void mission_explore_tick(PPCContext *c) {
    if(atomic_load(&g_mission_explore_request) && race_valid(c->r[2],0x558)) {
        unsigned state=LD32(c->r[2]+0x54);
        if(race_valid(state,0x14)) {
            ST32(state+0x10,LD32(state+0x10)|0x400000u);
            atomic_store(&g_race_time_trial,1);
            atomic_store(&g_race_unlimited_laps,1);
            atomic_store(&g_practice_reset,1);
            atomic_store(&g_mission_explore_request,0);
            atomic_store(&g_mission_exploring,1);
            rt_log("mission: continuing in unlimited Time Attack at current position\n");
        }
    }
}
static void mission_tick(PPCContext *c) {
    if (!mission_available()) return;
    mission_explore_tick(c);
    int request=atomic_exchange(&g_mission_request,0);
    if (request<0) { memset(&g_mission_run,0,sizeof g_mission_run);
    g_mission_break_mask=0; atomic_store(&g_mission_broken,0); g_mission_native_running=0; return; }
    if (request>0) {
        memset(&g_mission_run,0,sizeof g_mission_run);
    g_mission_break_mask=0; atomic_store(&g_mission_broken,0);
        g_mission_run.phase=MISSION_ARMED; g_mission_native_running=0;
    }
    if (mission_phase()==MISSION_OFF) return;
    if (!race_valid(c->r[2],0x558) || !race_restart_available() || race_restart_pending()) return;
    uint32_t car=LD32(c->r[2]+0x488);
    if (!race_valid(car,0x514) || c->r[3]!=car+0x4dc) return;
    if (g_mission_run.phase==MISSION_ARMED && g_mission_native_running) mission_begin(c,car);
    uint64_t ms=mission_now_ms();
    if (g_mission_run.phase==MISSION_ROLLING) {
        MissionPosition pos=mission_car_position(car);
        const MissionDefinition *rolling=&k_missions[atomic_load(&g_mission_selected)];
        /* A rolling-start line times from its plane: its car starts on or just behind it. */
        float past=(pos.x-g_mission_entry.centre.x)*g_mission_entry.nx+(pos.z-g_mission_entry.centre.z)*g_mission_entry.nz;
        if (rolling->rolling_gate ? past>=0 : mission_crossing(g_mission_entry,g_mission_run.previous,pos)>=0) {
            g_mission_run.phase=MISSION_RUNNING; g_mission_run.started=ms;
            g_mission_challenge_heading=LDF32(car+0xc4);
            atomic_store(&g_mission_auto_steer,0);
            atomic_store(&g_mission_auto_release,1);
            g_mission_run.last_sample=ms; g_mission_run.previous=pos;
            mission_publish(); rt_log("mission: rolling start handed over at entry checkpoint\n");
        /* Respawn can leave a contact flag set on the first physics frame.
         * Let the approach recover from contact; only a timed-out approach
         * is a setup failure. Mission collision judging starts at handover. */
        } else if (ms-g_mission_roll_started>10000) {
            g_mission_run.phase=MISSION_SETUP_FAILED; mission_publish();
            rt_log("mission: rolling approach timed out\n");
        } else {
            /* Follow the short approach with native physics and ordinary controls. */
            const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
            int direction=g_mission_rolling_direction;
            float nearest=INFINITY, distance=0, along=g_mission_roll_distance;
            for (unsigned i=0;i<g_mission_road_count;i++) {
                MissionRoadPoint a=g_mission_road[i], b=g_mission_road[(i+1)%g_mission_road_count];
                float dx=b.x-a.x,dz=b.z-a.z,n=dx*dx+dz*dz;
                float t=n>.01f ? fmaxf(0,fminf(1,((pos.x-a.x)*dx+(pos.z-a.z)*dz)/n)) : 0;
                float gap=hypotf(pos.x-a.x-t*dx,pos.z-a.z-t*dz);
                float progress=fmodf((distance+t*a.length-g_mission_roll_distance)*direction+g_mission_road_length,g_mission_road_length);
                if (progress<=d->rolling_metres+20 && gap<nearest) { nearest=gap; along=distance+t*a.length; }
                distance+=a.length;
            }
            /* Aim roughly one second ahead, rather than chasing a point
             * immediately in front of a fast car through the entry line. */
            float lookahead=fmaxf(12,fminf(40,(float)fabs(LDF32(car+0xac))));
            MissionRoadPoint target=mission_road_sample(along+direction*lookahead);
            /* Approach the timing line along its authored heading. Road
             * lookahead can turn around the next bend before handover. */
            if(d->custom_start && hypotf(pos.x-g_mission_entry.centre.x,pos.z-g_mission_entry.centre.z)<lookahead) {
                float heading=d->start_heading*.01745329252f;
                target.x=g_mission_entry.centre.x+sinf(heading)*lookahead;
                target.z=g_mission_entry.centre.z+cosf(heading)*lookahead;
            }
            if(d->rolling_gate) {
                /* Hold the straight line through the start gate. */
                float ahead=(pos.x-g_mission_entry.centre.x)*g_mission_entry.nx+(pos.z-g_mission_entry.centre.z)*g_mission_entry.nz+lookahead;
                target.x=g_mission_entry.centre.x+g_mission_entry.nx*ahead;
                target.z=g_mission_entry.centre.z+g_mission_entry.nz*ahead;
            }
            float desired=atan2f(target.x-pos.x,target.z-pos.z);
            float error=remainderf(desired-LDF32(car+0xc4),6.28318530717958647692f);
            atomic_store(&g_mission_auto_steer,(int)lroundf(fmaxf(-200,fminf(200,-error*160))));
            g_mission_run.previous=pos;
        }
    } else if (g_mission_run.phase==MISSION_COUNTDOWN) {
        if (ms<g_mission_countdown_end) atomic_store(&g_mission_countdown,
            (unsigned)((g_mission_countdown_end-ms+999)/1000));
        else {
            g_mission_run.phase=MISSION_RUNNING; g_mission_run.started=ms;
            g_mission_run.previous=mission_car_position(car); mission_publish();
            rt_log("mission: go\n");
        }
    } else if (g_mission_run.phase==MISSION_RUNNING) {
        unsigned marker=LD8(car+0x4de);
        if(marker!=g_mission_trace_marker || g_mission_run.next_gate!=g_mission_trace_gate) {
            MissionPosition pos=mission_car_position(car);
            rt_log("mission: route marker %u, gates %u/%u, position %.1f %.1f %.1f\n",marker,g_mission_run.next_gate,g_mission_run.gate_count,pos.x,pos.y,pos.z);
            g_mission_trace_marker=marker; g_mission_trace_gate=g_mission_run.next_gate;
        }
        float speed=(float)fabs(LDF32(car+0xac))*3.6f;
        if(isfinite(speed)) { g_mission_max_speed=fmaxf(g_mission_max_speed,speed); atomic_store(&g_mission_speed,(unsigned)lroundf(g_mission_max_speed)); }
        const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
        mission_vehicle_tick(c,car,d);
        mission_parked_tick(c,car,d);
        if(d->region_visit) mission_region_targets(c,d,car);
        else if(!*d->parked_models) mission_contact_targets(d,mission_car_position(car),
            (mission_car_contact_types(car)&(MISSION_HIT_WALL|MISSION_HIT_SCENERY|MISSION_HIT_BREAKABLE))!=0);
        if(d->jump_height) {
            MissionPosition p=mission_car_position(car); float ground;
            if(mission_ground(c,p.x,p.z,&ground)) g_mission_jump_height=fmaxf(g_mission_jump_height,p.y-ground);
        }
        mission_stunt_tick(c,car,d,ms);
        uint64_t judge_ms=mission_auto_collection_complete(d) && g_mission_last_collect_ms ? g_mission_last_collect_ms : ms;
        if(getenv("RT_MISSION_GATE_LOG") && g_mission_run.next_gate<g_mission_run.gate_count) {
            MissionGate gate=g_mission_run.gates[g_mission_run.next_gate];
            MissionPosition now=mission_car_position(car),old=g_mission_run.previous;
            float before=(old.x-gate.centre.x)*gate.nx+(old.z-gate.centre.z)*gate.nz;
            float after=(now.x-gate.centre.x)*gate.nx+(now.z-gate.centre.z)*gate.nz;
            if(before<0 && after>=0) rt_log("mission gate crossing %u: XYZ %.2f %.2f %.2f lateral %.2f vertical %.2f\n",
                g_mission_run.next_gate+1,now.x,now.y,now.z,
                (now.x-gate.centre.x)*gate.nz-(now.z-gate.centre.z)*gate.nx,now.y-gate.centre.y);
        }
        mission_rules_step(&g_mission_run,mission_car_position(car),mission_counted_contact(car),judge_ms);
        mission_special_tick(d,car,ms);
        mission_check_objective();
        mission_actor_tick(c);
        mission_collection_hint(d,mission_car_position(car),ms);
        int previous=mission_phase();
        if (g_mission_run.phase==MISSION_PASSED && previous==MISSION_RUNNING) mission_save_completion();
        mission_publish();
        if (g_mission_run.phase>=MISSION_PASSED && previous==MISSION_RUNNING)
            rt_log("mission: result %d, %u ms, %u contacts, %u/%u gates\n",mission_phase(),
                atomic_load(&g_mission_elapsed),atomic_load(&g_mission_contacts),
                atomic_load(&g_mission_gate),atomic_load(&g_mission_gates));
    }
}
/* Use the same packed choices consumed by the native loader: car bits 9..11,
 * tuned bit 8, course bits 12..13. Transmission is config byte 0. */
static int mission_native_call_arg(PPCContext *live, unsigned address, unsigned argument) {
    RtFn fn=rt_lookup(address); unsigned sp=live->r[1];
    if (!fn || sp<16384 || sp>=RAM_SIZE) return 0;
    uint8_t saved[8192]; memcpy(saved,g_ram+sp-sizeof saved,sizeof saved);
    PPCContext c=*live; c.r[1]=sp-512; c.budget=10000000; c.unwind=0;
    c.r[3]=argument;
    fn(&c); memcpy(g_ram+sp-sizeof saved,saved,sizeof saved);
    return !c.unwind;
}
/* The native checkpoint owns the finish line's road width, height and direction.
 * Optional scenery gates remain judged by their own bounded crossing planes. */
static void mission_checkpoint_hook(PPCContext *c) {
    if (!mission_available() || mission_phase()!=MISSION_RUNNING ||
        !race_valid(c->r[2],0x48c)) return;
    unsigned car=LD32(c->r[2]+0x488);
    if (!race_valid(car,0x514) || c->r[3]!=car+0x4dc) return;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    if(d->custom_finish || d->any_order || (g_mission_target_goal && !d->target_at_finish)) return;
    for(unsigned i=0;i<d->custom_gates;i++) if(d->gate_role[i]==2) return;
    unsigned marker=d->finish_cp==1 ? 3 : d->finish_cp+1;
    rt_log("mission: native checkpoint event marker %u, expected %u, gates %u/%u\n",LD8(c->r[3]+2),marker,g_mission_run.next_gate,g_mission_run.gate_count);
    if (LD8(c->r[3]+2)!=marker ||
        g_mission_run.next_gate+1!=g_mission_run.gate_count) return;
    uint64_t ms=mission_now_ms();
    mission_rules_step(&g_mission_run,mission_car_position(car),mission_counted_contact(car),ms);
    if (g_mission_run.phase==MISSION_RUNNING || g_mission_run.phase==MISSION_PASSED) {
        g_mission_run.next_gate=g_mission_run.gate_count;
        g_mission_run.phase=MISSION_PASSED;
        mission_special_tick(d,car,ms);
        mission_check_objective();
        if(g_mission_run.phase==MISSION_PASSED) mission_save_completion();
        rt_log("mission: native finish checkpoint %u accepted, %u ms\n",d->finish_cp,(unsigned)g_mission_run.elapsed_ms);
    }
    mission_publish();
}
static int mission_native_call(PPCContext *live, unsigned address) {
    return mission_native_call_arg(live,address,live->r[3]);
}
static void mission_dispatch_hook(PPCContext *c) {
    if (!mission_available() || mission_phase()!=MISSION_ARMED) return;
    unsigned toc=c->r[2];
    if (!race_valid(toc,0x7a4)) return;
    unsigned state=LD32(toc+0x54), config=LD32(toc+0x124);
    if (!race_valid(state,0x20) || !race_valid(config,0x100)) return;
    unsigned flags=LD32(state+4), mode=(flags>>23)&15;
    const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
    ST32(state+0x10,d->traffic ? LD32(state+0x10)&~0x400000u : LD32(state+0x10)|0x400000u);
    if (!atomic_load(&g_mission_selection_applied) &&
        (mode==6 || mode==7 || mode==8 || (mode==9 && race_restart_pending()==2))) {
        /* Car selection normally loads the swappable engine sample bank
         * (8af94..8b018). Skipping it leaves the attract/menu bank resident:
         * the engine loop then plays those unrelated samples at engine pitch.
         * Preserve the native packed-choice lookup, including its alternate bank. */
        static const unsigned char engine_banks[16]={4,1,2,3,1,1,4,5,7,7,7,7,7,7,7,7};
        ST32(state+4,(LD32(state+4)&~0x3f00u)|
            (d->car<<9)|(d->tuned<<8)|(d->region<<12));
        ST8(config,d->transmission);
        unsigned engine_bank=engine_banks[(LD32(state+4)>>9&7)|((LD32(state+4)>>8&1)<<3)];
        if (!mission_native_call_arg(c,0x3b320,0xf0000000u|(engine_bank<<16)) ||
            !mission_native_call(c,0x8d8ac) ||
            !mission_native_call_arg(c,0x55168,1) ||
            !mission_native_call(c,0x8e17c)) return;
        flags=(LD32(state+4)&~0x07803f00u)|0x04800000u|
            (d->car<<9)|(d->tuned<<8)|(d->region<<12);
        ST32(state+4,flags); ST8(config+4,d->region);
        atomic_store(&g_mission_selection_applied,1);
        g_mission_native_running=0;
        rt_log("mission: assigned car %u, transmission %u, region %u; direct loader\n",
            d->car,d->transmission,d->region);
    } else if (mode==10) {
        /* 8e860 waits for loader/player readiness in stage 0, then interpolates
         * the course camera for 60 frames in stage 1. Finish that interpolation
         * at its endpoint so native camera and race initialization still run. */
        unsigned base=LD32(toc+0x3c);
        if (base<=RAM_SIZE-0x200000 && race_valid(base+0x1fffd0,8) &&
            LD8(base+0x1fffd0)==1) {
            ST8(base+0x1fffd1,0); ST16(base+0x1fffd2,60); ST16(base+0x1fffd4,0);
        }
    } else if (mode==9) {
        ST32(state+4,(flags&~0x3f00u)|(d->car<<9)|(d->tuned<<8)|(d->region<<12));
        ST8(config,d->transmission);
        ST8(config+4,d->region);
    }
}
static void mission_race_hook(PPCContext *c) {
    if (!mission_available() || mission_phase()==MISSION_OFF) return;
    if (c->r[4]<4 && mission_phase()==MISSION_ARMED) {
        unsigned race=LD32(c->r[2]+0x7a0), config=LD32(c->r[2]+0x124);
        if (race_valid(race,0x18) && race_valid(config,0x100) &&
            /* Keep the selected engine sequence alive across the instant start.
             * A full sound reset stops it; the running path only updates it. */
            mission_native_call(c,0xb49c0) && mission_native_call(c,0xb49fc)) {
            ST8(config+1,0); ST8(race+0x13,4); ST16(race+0x16,1); c->r[4]=4;
        }
    }
    if (c->r[4]==5) g_mission_native_running=1;
    /* Keep the running state: it owns native timer, audio and race bookkeeping.
     * Mission judging freezes results; the held native time prevents race timeout. */
}
