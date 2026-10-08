/* Mission CSV loads transactionally at startup and between mission runs. */
#pragma once
#include <ctype.h>
#include <stdlib.h>
#include <strings.h>
static char g_mission_csv_names[96][96], g_mission_csv_briefs[96][192], g_mission_csv_break_labels[96][32];
static char *mission_csv_trim(char *s) {
    while(isspace((unsigned char)*s)) s++;
    char *end=s+strlen(s); while(end>s && isspace((unsigned char)end[-1])) *--end=0;
    return s;
}
static int mission_csv_fields(char *line,char **out) {
    int n=0; char *src=line,*dst=line;
    while(*src && n<128) {
        out[n++]=dst; int quoted=*src=='"'; if(quoted) src++;
        while(*src) {
            if(quoted && *src=='"') { src++; if(*src=='"') { *dst++=*src++; continue; } quoted=0; continue; }
            if(!quoted && *src==',') { src++; break; }
            if(!quoted && (*src=='\r'||*src=='\n')) { src+=strlen(src); break; }
            *dst++=*src++;
        }
        *dst++=0;
    }
    for(int i=0;i<n;i++) out[i]=mission_csv_trim(out[i]);
    return n;
}
static const char *mission_csv_value(char **headers,int nh,char **fields,int nf,const char *key) {
    for(int i=0;i<nh;i++) if(!strcasecmp(headers[i],key)) return i<nf ? fields[i] : "";
    return "";
}
static int mission_csv_number(const char *s,double *value) {
    char *end; *value=strtod(s,&end);
    if(!*s || *end || !isfinite(*value) || *value<0 || *value>UINT_MAX) { *value=0; return 0; }
    return 1;
}
static int mission_csv_width(const char *s,float *width) {
    if(!strcasecmp(s,"kerb") || !strcasecmp(s,"curb")) *width=5;
    else if(!strcasecmp(s,"lane")) *width=9;
    else if(!strcasecmp(s,"full road width") || !strcasecmp(s,"road")) *width=36;
    else { double v; if(!mission_csv_number(s,&v)||v<=0||v>200) return 0; *width=v; }
    return 1;
}
static unsigned g_mission_csv_revision;
static void mission_csv_load(void) {
    if(!GAME_ENH_MISSION_STYLE) return;
    const char *path=getenv("RT_MISSIONS_CSV"); if(!path) path="missions.csv";
    FILE *f=fopen(path,"r"); if(!f) { rt_log("mission CSV: could not open %s; no missions loaded\n",path); return; }
    char header[8192],line[8192],*headers[128],*fields[128];
    if(!fgets(header,sizeof header,f)) { fclose(f); return; }
    int nh=mission_csv_fields(header,headers), count=0,line_no=1;
    int has_ids=0;for(int i=0;i<nh;i++) if(!strcasecmp(headers[i],"id")) has_ids=1;
    MissionDefinition loaded[96]={0};
    char names[96][96]={0}, briefs[96][192]={0}, break_labels[96][32]={0};
    while(fgets(line,sizeof line,f)) {
        line_no++; if(!*mission_csv_trim(line)||*mission_csv_trim(line)=='#') continue;
        int nf=mission_csv_fields(line,fields),ok=1; double v;
        if(count==96) { rt_log("mission CSV: too many rows\n"); count=0; break; }
        MissionDefinition *d=&loaded[count];
#define CSV(key) mission_csv_value(headers,nh,fields,nf,key)
        /* Keep unfinished course surveys in the same authoring file without
         * exposing objectives whose coordinates have not been verified. */
        const char *enabled=CSV("enabled");
        if(!strcmp(enabled,"0") || !strcasecmp(enabled,"false")) continue;
        if(*enabled && strcmp(enabled,"1") && strcasecmp(enabled,"true")) {
            rt_log("mission CSV: invalid enabled value at row %d; retaining previously loaded missions\n",line_no);
            count=0; break;
        }
        if(has_ids) {
            if(!mission_csv_number(CSV("id"),&v) || v<1 || v>0x3fffffff || floor(v)!=v) ok=0;
            else d->id=(unsigned)v;
            for(int i=0;i<count;i++) if(loaded[i].id==d->id) ok=0;
        }
        snprintf(d->legacy_names,sizeof d->legacy_names,"%s",CSV("legacy_names"));
        const char *name=CSV("name");
        ok &= *name!=0; snprintf(names[count],96,"%s",name);
        d->name=names[count]; d->briefing=briefs[count];
        snprintf(d->type,sizeof d->type,"%s",CSV("type"));
        snprintf(d->target_vehicles,sizeof d->target_vehicles,"%s",CSV("target_vehicles"));
        if(*d->target_vehicles && strcasecmp(d->target_vehicles,"all")) {
            char models[64]; snprintf(models,sizeof models,"%s",d->target_vehicles);
            char *next=models,*model;
            while((model=strsep(&next,";"))) ok &= mission_csv_number(model,&v)&&v<=15&&floor(v)==v;
        }
        snprintf(d->parked_models,sizeof d->parked_models,"%s",CSV("parked_models"));
        if(*d->parked_models && strcasecmp(d->parked_models,"all") && strcasecmp(d->parked_models,"nearest")) {
            char models[64];snprintf(models,sizeof models,"%s",d->parked_models);
            char *next=models,*model;
            while((model=strsep(&next,";"))) ok &= mission_csv_number(model,&v)&&v<=15&&floor(v)==v;
        }
        if(*CSV("obstacle_model")) {
            d->obstacle=1;
            ok &= mission_csv_number(CSV("obstacle_model"),&v)&&v>=0&&v<=15&&floor(v)==v;d->obstacle_model=(unsigned)v;
            ok &= mission_csv_number(CSV("obstacle_x"),&v);d->obstacle_x=v;
            ok &= mission_csv_number(CSV("obstacle_z"),&v);d->obstacle_z=v;
            ok &= mission_csv_number(CSV("obstacle_heading"),&v);d->obstacle_heading=v;
        }
        d->scenery_touch=!strcasecmp(d->type,"TOUCH") || !strcasecmp(d->type,"TAG");
        const char *event=CSV("target_event");
        if(!strcasecmp(event,"destroy")) d->vehicle_destroy=1;
        else if(!strcasecmp(event,"visit")) d->region_visit=1;
        else if(!strcasecmp(event,"touch")) d->scenery_touch=1;
        else if(*event && strcasecmp(event,"contact")) ok=0;
        snprintf(d->protected_models,sizeof d->protected_models,"%s",CSV("protected_models"));
        char controls[128];snprintf(controls,sizeof controls,"%s",CSV("forbidden_controls"));
        char *control_next=controls,*control;
        while(*controls && (control=strsep(&control_next,";"))) {
            if(!strcasecmp(control,"accelerator")) d->forbidden_controls|=1;
            else if(!strcasecmp(control,"brake")) d->forbidden_controls|=2;
            else if(!strcasecmp(control,"handbrake")) d->forbidden_controls|=4;
            else ok=0;
        }
        if(*CSV("collect_gap")) {ok &= mission_csv_number(CSV("collect_gap"),&v)&&v>0&&v<=60;d->collect_gap_ms=(unsigned)lround(v*1000);}
        if(*CSV("no_turn_back")) {ok &= mission_csv_number(CSV("no_turn_back"),&v)&&v<=1;d->no_turn_back=(unsigned)v;}
        d->park_length=6;d->park_angle=15;d->park_speed=2;
        if(*CSV("park_seconds")) {ok &= mission_csv_number(CSV("park_seconds"),&v)&&v>0&&v<=30;d->park_ms=(unsigned)lround(v*1000);}
        const char *park_keys[]={"park_length","park_angle","park_speed"};
        float *park_values[]={&d->park_length,&d->park_angle,&d->park_speed};
        for(unsigned i=0;i<3;i++) if(*CSV(park_keys[i])) {ok &= mission_csv_number(CSV(park_keys[i]),&v)&&v>0&&v<=180;*park_values[i]=v;}
        d->completion_delay_ms=750; d->hint_remaining=3; d->hint_idle_ms=30000;
        if(*CSV("completion_delay")) { ok &= mission_csv_number(CSV("completion_delay"),&v)&&v<=5; d->completion_delay_ms=(unsigned)lround(v*1000); }
        if(*CSV("hint_remaining")) { ok &= mission_csv_number(CSV("hint_remaining"),&v)&&v<=256&&floor(v)==v; d->hint_remaining=(unsigned)v; }
        if(*CSV("hint_seconds")) { ok &= mission_csv_number(CSV("hint_seconds"),&v)&&v<=3600; d->hint_idle_ms=(unsigned)lround(v*1000); }
        if(*CSV("target_height")) { ok &= mission_csv_number(CSV("target_height"),&v)&&v<=100; d->target_height=v; }
        if(*CSV("two_wheel_seconds")) { ok &= mission_csv_number(CSV("two_wheel_seconds"),&v)&&v<=600; d->two_wheel_ms=(unsigned)lround(v*1000); }
        if(*CSV("roll_degrees")) { ok &= mission_csv_number(CSV("roll_degrees"),&v)&&v>=360&&v<=1080&&floor(v)==v; d->roll_degrees=(unsigned)v; }
        if(*CSV("wallride_distance")) { ok &= mission_csv_number(CSV("wallride_distance"),&v)&&v>0&&v<=2000; d->wallride_distance=v; }
        if(*CSV("turn_degrees")) { ok &= mission_csv_number(CSV("turn_degrees"),&v)&&v>=45&&v<=360; d->handbrake_turn=(unsigned)v; }
        if(d->handbrake_turn) {
            const char *keys[]={"turnx","turnz","turn_radius"}; float *dest[]={&d->turn_x,&d->turn_z,&d->turn_radius};
            for(unsigned j=0;j<3;j++) { char *end; const char *text=CSV(keys[j]); *dest[j]=strtof(text,&end); ok &= *text&&!*end&&isfinite(*dest[j]); }
            ok &= d->turn_radius>0&&d->turn_radius<=100;
        }
        if(*CSV("landingx")) {
            const char *keys[]={"landingx","landingz","landingy","landing_radius","landing_height"};
            float *dest[]={&d->landing_x,&d->landing_z,&d->landing_y,&d->landing_radius,&d->landing_height};
            for(unsigned j=0;j<5;j++) { char *end; const char *text=CSV(keys[j]);
                if(j==2 && !*text) { *dest[j]=NAN; continue; }
                *dest[j]=strtof(text,&end); ok &= *text&&!*end&&isfinite(*dest[j]); }
            ok &= d->landing_radius>0&&d->landing_height>0; d->landing=1;
        }
        const char *mode=CSV("mode");
        if(!*mode || !strcasecmp(mode,"time_attack") || !strcasecmp(mode,"time attack") || !strcasecmp(mode,"tt")) d->traffic=0;
        else if(!strcasecmp(mode,"traffic") || !strcasecmp(mode,"race")) d->traffic=1;
        else ok=0;
        if(*d->target_vehicles) ok &= d->traffic!=0;
        ok &= mission_csv_number(CSV("car"),&v) && v>=1 && v<=5 && floor(v)==v; d->car=v>=1 ? (unsigned)v-1 : 0;
        const char *tr=CSV("transmission"); if(!strcasecmp(tr,"at")) d->transmission=0; else if(!strcasecmp(tr,"mt")) d->transmission=1; else ok=0;
        const char *course=CSV("course"); if(!strcasecmp(course,"town")) d->region=0; else if(!strcasecmp(course,"coast")) d->region=1; else if(!strcasecmp(course,"mountain")) d->region=2; else ok=0;
        const char *direction=CSV("direction");
        if(!*direction || !strcasecmp(direction,"auto")) d->route_direction=0;
        else if(!strcasecmp(direction,"forward")) d->route_direction=1;
        else if(!strcasecmp(direction,"reverse") || !strcasecmp(direction,"opposite")) d->route_direction=-1;
        else ok=0;
        ok &= mission_csv_number(CSV("cp1"),&v)&&v<=10&&floor(v)==v; d->start_cp=(unsigned)v;
        ok &= mission_csv_number(CSV("cp2"),&v)&&v>=1&&v<=10&&floor(v)==v; d->finish_cp=(unsigned)v;
        d->contacts=UINT_MAX; if(*CSV("collisions") && strcasecmp(CSV("collisions"),"unlimited")) { ok &= mission_csv_number(CSV("collisions"),&v)&&v<UINT_MAX&&floor(v)==v; d->contacts=(unsigned)v; }
        d->collision_types=MISSION_HIT_ALL;
        if(*CSV("collision_types")) {
            char kinds[128]; snprintf(kinds,sizeof kinds,"%s",CSV("collision_types"));
            char *next=kinds,*kind; d->collision_types=0;
            while((kind=strsep(&next,";"))) {
                kind=mission_csv_trim(kind);
                if(!strcasecmp(kind,"all")) d->collision_types|=MISSION_HIT_ALL;
                else if(!strcasecmp(kind,"walls")) d->collision_types|=MISSION_HIT_WALL;
                else if(!strcasecmp(kind,"cars")) d->collision_types|=MISSION_HIT_CAR;
                else if(!strcasecmp(kind,"scenery")) d->collision_types|=MISSION_HIT_SCENERY;
                else if(!strcasecmp(kind,"breakables")) d->collision_types|=MISSION_HIT_BREAKABLE;
                else ok=0;
            }
        }
        if(*CSV("collision_delay")) { ok &= mission_csv_number(CSV("collision_delay"),&v)&&v<=5; d->collision_delay_ms=(unsigned)lround(v*1000); }
        const char *times[]={CSV("gold"),CSV("silver"),CSV("bronze")}; unsigned *ms[]={&d->gold_ms,&d->silver_ms,&d->limit_ms};
        for(int i=0;i<3;i++) if(*times[i]) { ok &= mission_csv_number(times[i],&v)&&v<=3600; *ms[i]=(unsigned)lround(v*1000); }
        if(d->gold_ms) { d->silver_ms=d->gold_ms+2000; d->limit_ms=d->gold_ms+4000; }
        if(d->gold_ms || d->silver_ms) ok &= d->gold_ms>0 && d->gold_ms<=d->silver_ms && d->silver_ms<=d->limit_ms;
        d->rolling_metres=0; d->rolling_speed=0;
        if(*CSV("rolling")) { ok &= mission_csv_number(CSV("rolling"),&v)&&v<=200; d->rolling_metres=(unsigned)v; }
        if(*CSV("lead_in")) { ok &= mission_csv_number(CSV("lead_in"),&v)&&v<=500&&floor(v)==v; d->lead_in_metres=(unsigned)v; }
        if(*CSV("rolling_speed")) { ok &= mission_csv_number(CSV("rolling_speed"),&v)&&v<=300; d->rolling_speed=v/3.6; }
        if(d->rolling_metres) ok &= d->rolling_speed>0;
        for(unsigned i=0;i<MISSION_MAX_GATES;i++) {d->gate_y[i]=NAN;d->gate_heading[i]=NAN;d->gate_height[i]=8;}
        char xs[256],zs[256],ws[256]; snprintf(xs,sizeof xs,"%s",CSV("gatex")); snprintf(zs,sizeof zs,"%s",CSV("gatez")); snprintf(ws,sizeof ws,"%s",CSV("gatew"));
        char *xp=xs,*zp=zs,*wp=ws;
        while(*xp && d->custom_gates<MISSION_MAX_GATES-1) {
            char *x=strsep(&xp,";"),*z=strsep(&zp,";"),*w=strsep(&wp,";");
            char *end; unsigned i=d->custom_gates++; d->gate_x[i]=strtof(x,&end); ok &= *x && !*end && isfinite(d->gate_x[i]);
            if(!z||!w) { ok=0; break; } d->gate_z[i]=strtof(z,&end); ok &= *z && !*end && isfinite(d->gate_z[i]);
            ok &= mission_csv_width(mission_csv_trim(w),&d->gate_width[i]); if(!xp) break;
        }
        if((xp&&*xp)||(zp&&*zp)||(wp&&*wp)) ok=0;
        const char *order=CSV("gate_order");
        if(!strcasecmp(order,"any")) { d->any_order=1; ok &= d->custom_gates>0; }
        else if(*order && strcasecmp(order,"ordered")) ok=0;
        const char *gatekeys[]={"gatey","gateh","gater","gatetype","gatetilt"};
        for(unsigned key=0;key<5;key++) if(*CSV(gatekeys[key])) {
            char list[2048];snprintf(list,sizeof list,"%s",CSV(gatekeys[key]));
            char *next=list,*part;unsigned i=0;
            while((part=strsep(&next,";"))) {
                if(i>=d->custom_gates) {ok=0;break;}
                if(key==3) {
                    if(!strcasecmp(part,"waypoint")) d->gate_role[i]=0;
                    else if(!strcasecmp(part,"fail")) d->gate_role[i]=1;
                    else if(!strcasecmp(part,"finish")) d->gate_role[i]=2;
                    else if(!strcasecmp(part,"rstart")) d->gate_role[i]=4;
                    else ok=0;
                } else if(strcasecmp(part,"auto")) {
                    char *end;float value=strtof(part,&end);ok &= *part && !*end && isfinite(value);
                    if(key==0) d->gate_y[i]=value;
                    if(key==1) {d->gate_height[i]=value;ok &= value>0 && value<=100;}
                    if(key==2) d->gate_heading[i]=value;
                    if(key==4) {d->gate_tilt[i]=value;ok &= fabsf(value)<=90;}
                }
                i++;
            }
            ok &= i==d->custom_gates;
        }
        unsigned finishes=0;for(unsigned i=0;i<d->custom_gates;i++) if(d->gate_role[i]==2) {
            finishes++;for(unsigned j=i+1;j<d->custom_gates;j++) ok &= d->gate_role[j]==1;
        }
        ok &= finishes<=1;
        /* A rolling start is the first gate: the timing line the car approaches. */
        for(unsigned i=0;i<d->custom_gates;i++) if(d->gate_role[i]==4) { ok &= i==0; d->rolling_gate=1; }
        /* An explicit finish gate replaces CP2, so checkpoint ordering only
         * constrains missions that actually finish at a native checkpoint. */
        if(!d->custom_gates && !finishes && !*CSV("endx"))
            ok &= d->route_direction<0 ? d->finish_cp!=d->start_cp : d->finish_cp>d->start_cp;
        if(*CSV("endx") || *CSV("endz") || *CSV("endw")) {
            char *end; const char *x=CSV("endx"),*z=CSV("endz");
            d->custom_finish=1; d->end_x=strtof(x,&end); ok &= *x && !*end && isfinite(d->end_x);
            d->end_z=strtof(z,&end); ok &= *z && !*end && isfinite(d->end_z);
            ok &= mission_csv_width(CSV("endw"),&d->end_width);
        }
        if(*CSV("breakx") || *CSV("breakz")) {
            char bx[512],bz[512]; snprintf(bx,sizeof bx,"%s",CSV("breakx")); snprintf(bz,sizeof bz,"%s",CSV("breakz"));
            char *xnext=bx,*znext=bz;
            while(xnext && *xnext && d->break_count<32) {
                char *x=strsep(&xnext,";"),*z=strsep(&znext,";"),*end;
                unsigned i=d->break_count++;
                d->break_x[i]=strtof(x,&end); ok &= *x && !*end && isfinite(d->break_x[i]);
                if(!z) { ok=0; break; }
                d->break_z[i]=strtof(z,&end); ok &= *z && !*end && isfinite(d->break_z[i]);
            }
            if((xnext&&*xnext)||(znext&&*znext)||!d->break_count) ok=0;
            ok &= mission_csv_number(CSV("breaktype"),&v)&&v<=7&&floor(v)==v; d->break_type=(unsigned)v;
            ok &= mission_csv_number(CSV("breakradius"),&v)&&v>0&&v<=10; d->break_radius=v;
            ok &= *CSV("breaklabel")!=0; snprintf(break_labels[count],32,"%s",CSV("breaklabel"));
            /* Each target must have a distinct matching area. */
            for(unsigned i=0;i<d->break_count;i++) for(unsigned j=0;j<i;j++)
                ok &= hypotf(d->break_x[i]-d->break_x[j],d->break_z[i]-d->break_z[j])>2*d->break_radius;
        } else if(*CSV("breaktype") || *CSV("breakradius")) ok=0;
        const char *description=CSV("description");
        ok &= *description!=0; snprintf(briefs[count],192,"%s",description);
        snprintf(d->target_models,sizeof d->target_models,"%s",CSV("target_models"));
        snprintf(d->target_label,sizeof d->target_label,"%s",CSV("target_label"));
        if(*CSV("target_group_radius")) { ok &= mission_csv_number(CSV("target_group_radius"),&v)&&v>0&&v<=10; d->target_group_radius=v; }
        if(*CSV("targetx") || *CSV("targetz")) {
            char tx[512],tz[512]; snprintf(tx,sizeof tx,"%s",CSV("targetx")); snprintf(tz,sizeof tz,"%s",CSV("targetz"));
            char *xp=tx,*zp=tz;
            while(xp && *xp && d->contact_targets<32) {
                char *x=strsep(&xp,";"),*z=strsep(&zp,";"),*end;
                unsigned i=d->contact_targets++; d->target_x[i]=strtof(x,&end); ok &= *x && !*end && isfinite(d->target_x[i]);
                if(!z) { ok=0; break; } d->target_z[i]=strtof(z,&end); ok &= *z && !*end && isfinite(d->target_z[i]);
            }
            ok &= !(xp&&*xp) && !(zp&&*zp) && !*d->target_models && !d->break_count && *d->target_label;
            ok &= mission_csv_number(CSV("targetradius"),&v)&&v>0&&v<=10; d->target_radius=v;
        }
        if(*d->parked_models) {
            ok &= !*d->target_models && !*d->target_vehicles && !d->vehicle_destroy && !d->region_visit && !d->break_count && *d->target_label;
            ok &= !strcasecmp(d->parked_models,"nearest") ? d->contact_targets==1 : d->contact_targets<=1;
        }
        if(*d->target_models || *d->target_vehicles) ok &= *d->target_label!=0 && !d->break_count;
        if(*d->target_vehicles) ok &= !*d->target_models && !d->contact_targets;
        if(d->region_visit) ok &= d->contact_targets>0;
        if(*CSV("target_count") && strcasecmp(CSV("target_count"),"all")) {
            ok &= mission_csv_number(CSV("target_count"),&v)&&v>=1&&v<=256&&floor(v)==v;
            d->target_count=(unsigned)v; ok &= *d->target_models!=0 || *d->target_vehicles!=0 || *d->parked_models!=0;
        }
        if(*CSV("target_finish")) {
            if(!strcasecmp(CSV("target_finish"),"finish")) d->target_at_finish=1;
            else if(strcasecmp(CSV("target_finish"),"automatic")) ok=0;
        }
        if(*CSV("speed")) { ok &= mission_csv_number(CSV("speed"),&v)&&v>0&&v<=300; d->speed_goal=v; }
        if(*CSV("tuned")) { ok &= mission_csv_number(CSV("tuned"),&v)&&v<=1&&floor(v)==v; d->tuned=(unsigned)v; }
        if(*CSV("jump_height")) { ok &= mission_csv_number(CSV("jump_height"),&v)&&v>0&&v<=20; d->jump_height=v; }
        if(*CSV("startx") || *CSV("startz") || *CSV("start_heading")) {
            const char *keys[]={"startx","startz","start_heading"};
            float *dest[]={&d->start_x,&d->start_z,&d->start_heading};
            for(unsigned i=0;i<3;i++) { const char *s=CSV(keys[i]); char *end;
                *dest[i]=strtof(s,&end); ok &= *s && !*end && isfinite(*dest[i]); }
            d->custom_start=1;
            ok &= fabsf(d->start_x)<100000 && fabsf(d->start_z)<100000;
            /* Rolling custom starts use these coordinates as the timing line. */
        }
        if(d->rolling_gate) {
            /* The game chooses the run-up; a rolling_speed still overrides the speed. */
            ok &= !d->custom_start && !d->rolling_metres && !d->lead_in_metres;
            d->custom_start=1; d->start_x=d->gate_x[0]; d->start_z=d->gate_z[0]; d->start_heading=d->gate_heading[0];
        }
#undef CSV
        if(d->park_ms) ok &= d->custom_gates>0 && d->gate_role[d->custom_gates-1]==2;
        if(d->lead_in_metres) ok &= d->custom_start;
        /* The wall ride runs from its one gate (after any rolling start) to the end gate. */
        unsigned wall=d->rolling_gate;
        if(d->wallride_distance) ok &= d->custom_gates==1+wall && d->custom_finish &&
            hypotf(d->end_x-d->gate_x[wall],d->end_z-d->gate_z[wall])>1;
        if(!ok) { rt_log("mission CSV: invalid row %d; retaining previously loaded missions\n",line_no); count=0; break; }
        count++;
    }
    fclose(f); if(count) { memcpy(g_mission_csv_names,names,sizeof names); memcpy(g_mission_csv_briefs,briefs,sizeof briefs); memcpy(g_mission_csv_break_labels,break_labels,sizeof break_labels);
        memcpy(k_missions,loaded,count*sizeof *loaded);
        for(int i=0;i<count;i++) { k_missions[i].name=g_mission_csv_names[i]; k_missions[i].briefing=g_mission_csv_briefs[i]; k_missions[i].break_label=g_mission_csv_break_labels[i]; }
        g_mission_count=count; ++g_mission_csv_revision; rt_log("mission CSV: loaded %d missions from %s\n",count,path); }
}

/* Preserve other fields verbatim; locate the permanent ID, with legacy name fallback. */
static int mission_csv_save_gold(const char *name,unsigned gold) {
    const char *path=getenv("RT_MISSIONS_CSV"); if(!path) path="missions.csv";
    unsigned identity=0;
    for(int i=0;i<MISSION_COUNT;i++) if(k_missions[i].name && !strcmp(k_missions[i].name,name)) {identity=k_missions[i].id;break;}
    FILE *in=fopen(path,"r"); if(!in) return 0;
    char temp[4096]; if(snprintf(temp,sizeof temp,"%s.tmp",path)>=(int)sizeof temp) { fclose(in); return 0; }
    FILE *out=fopen(temp,"w"); if(!out) { fclose(in); return 0; }
    char line[8192],copy[8192],*fields[128]; int columns[3]={-1,-1,-1}, namecol=-1,idcol=-1,found=0,ok=1;
    if(!fgets(line,sizeof line,in)) ok=0;
    else {
        fputs(line,out); strcpy(copy,line); int n=mission_csv_fields(copy,fields);
        for(int i=0;i<n;i++) { if(!strcasecmp(fields[i],"name")) namecol=i;
            if(!strcasecmp(fields[i],"id")) idcol=i;
            const char *keys[]={"gold","silver","bronze"};
            for(int j=0;j<3;j++) if(!strcasecmp(fields[i],keys[j])) columns[j]=i; }
        if(namecol<0 || columns[0]<0 || columns[1]<0 || columns[2]<0) ok=0;
    }
    while(ok && fgets(line,sizeof line,in)) {
        strcpy(copy,line); int n=mission_csv_fields(copy,fields);
        int matches=namecol<n && !strcmp(fields[namecol],name);
        if(identity && idcol>=0) {
            double value=0;matches=idcol<n && mission_csv_number(fields[idcol],&value) && value==identity;
        }
        if(!matches) { fputs(line,out); continue; }
        if(found++) { ok=0; break; } /* Ambiguous names must not overwrite two rows. */
        int col=0,quoted=0; const char *start=line,*p=line;
        for(;;p++) {
            if(*p=='"') { if(quoted && p[1]=='"') { p++; continue; } quoted=!quoted; }
            if(!*p || (!quoted && (*p==',' || *p=='\r' || *p=='\n'))) {
                int medal=-1; for(int j=0;j<3;j++) if(col==columns[j]) medal=j;
                if(medal>=0) fprintf(out,"%.3f",(gold+medal*2000)/1000.0);
                else fwrite(start,1,p-start,out);
                if(*p!=',') { fputs(p,out); break; }
                fputc(',',out); start=p+1; col++;
            }
        }
        if(col<columns[0] || col<columns[1] || col<columns[2]) ok=0;
    }
    if(ferror(in) || ferror(out)) ok=0;
    fclose(in); if(fclose(out)) ok=0;
    if(ok && found==1 && !rename(temp,path)) return 1;
    remove(temp); return 0;
}
