#pragma once

typedef struct { float x,y,z,width,height,heading;unsigned role;float tilt; } AuthorGate;
static AuthorGate g_author_gates[MISSION_MAX_GATES];
static unsigned g_author_count,g_author_selected,g_author_mission;
static int g_author_error;
static int g_author_start_cp=-1;
static atomic_int g_author_active;
static atomic_int g_author_mouse_pending;
static _Atomic float g_author_mouse_x,g_author_mouse_y;
int enh_gate_editor_mouse(float x,float y) {
    if(!atomic_load(&g_author_active) || x<0 || x>1 || y<0 || y>1) return 0;
    atomic_store(&g_author_mouse_x,x);atomic_store(&g_author_mouse_y,y);
    atomic_store(&g_author_mouse_pending,1);return 1;
}
static int g_author_flying,g_author_edit_start;
static AuthorGate g_author_start;
static float g_author_start_speed;
static unsigned g_author_start_rolling;
static float g_author_start_plane_heading;
static pthread_mutex_t g_author_mutex=PTHREAD_MUTEX_INITIALIZER;

int enh_gate_editor_active(void) {return atomic_load(&g_author_active);}

/* Update only the gate fields of the row identified by its permanent ID. */
static int gate_editor_save(void) {
    const char *path=getenv("RT_MISSIONS_CSV");if(!path) path="missions.csv";
    const char *keys[]={"gatex","gatez","gatew","gatey","gateh","gater","gatetype","endx","endz","endw","cp1","startx","startz","start_heading","lead_in","gatetilt","rolling","rolling_speed"};
    char values[18][1024]={{0}};
    if(g_author_start_cp>=0) {
        snprintf(values[10],sizeof values[10],"%d",g_author_start_cp);
        snprintf(values[11],sizeof values[11],"%.0f",roundf(g_author_start.x));
        snprintf(values[12],sizeof values[12],"%.0f",roundf(g_author_start.z));
        snprintf(values[13],sizeof values[13],"%.2f",g_author_start.heading);
        snprintf(values[16],sizeof values[16],"%u",g_author_start_speed>0 ? (g_author_start_rolling?g_author_start_rolling:75) : 0);
        snprintf(values[17],sizeof values[17],"%.1f",g_author_start_speed);
    }
    for(unsigned i=0;i<g_author_count;i++) for(unsigned j=0;j<7;j++) {
        char item[40];AuthorGate g=g_author_gates[i];
        float numbers[]={g.x,g.z,g.width,g.y,g.height,g.heading};
        if(j==6) snprintf(item,sizeof item,"%s",g.role==1?"fail":g.role==2?"finish":g.role==4?"rstart":"waypoint");
        else if(j==3 && !isfinite(g.y)) snprintf(item,sizeof item,"auto");
        else if(j==0 || j==1 || j==3) snprintf(item,sizeof item,"%.0f",roundf(numbers[j]));
        else snprintf(item,sizeof item,"%.2f",numbers[j]);
        size_t n=strlen(values[j]);snprintf(values[j]+n,sizeof values[j]-n,"%s%s",i?";":"",item);
    }
    /* A rolling-start gate replaces the authored start: clear it from the row. */
    int rstart=0;for(unsigned i=0;i<g_author_count;i++) rstart|=g_author_gates[i].role==4;
    if(rstart) {const unsigned cleared[]={11,12,13,14,16};for(unsigned i=0;i<5;i++) values[cleared[i]][0]=0;}
    for(unsigned i=0;i<g_author_count;i++) {size_t n=strlen(values[15]);snprintf(values[15]+n,sizeof values[15]-n,"%s%.2f",i?";":"",g_author_gates[i].tilt);}
    char temporary[4096];snprintf(temporary,sizeof temporary,"%s.tmp",path);
    FILE *in=fopen(path,"r");if(!in) return 0;
    FILE *out=fopen(temporary,"w");if(!out) {fclose(in);return 0;}
    char line[8192],copy[8192],*fields[128];int columns[18],idcol=-1,found=0,ok=1;
    for(unsigned i=0;i<18;i++) columns[i]=-1;
    if(!fgets(line,sizeof line,in)) ok=0;
    else {
        fputs(line,out);snprintf(copy,sizeof copy,"%s",line);int n=mission_csv_fields(copy,fields);
        for(int i=0;i<n;i++) {
            if(!strcasecmp(fields[i],"id")) idcol=i;
            for(unsigned j=0;j<18;j++) if(!strcasecmp(fields[i],keys[j])) columns[j]=i;
        }
        if(idcol<0) ok=0;
        for(unsigned i=0;i<18;i++) if(columns[i]<0) ok=0;
    }
    while(ok && fgets(line,sizeof line,in)) {
        snprintf(copy,sizeof copy,"%s",line);int n=mission_csv_fields(copy,fields);double id;
        if(idcol>=n || !mission_csv_number(fields[idcol],&id) || id!=g_author_mission) {fputs(line,out);continue;}
        if(found++) {ok=0;break;}
        const char *start=line;int quoted=0,column=0;
        for(const char *p=line;;p++) {
            if(*p=='"') {if(quoted && p[1]=='"') {p++;continue;}quoted=!quoted;}
            if(!*p || (!quoted && (*p==',' || *p=='\r' || *p=='\n'))) {
                int replacement=-1;for(unsigned j=0;j<18;j++) if(columns[j]==column && (j<10 || j==15 || g_author_start_cp>=0 ||
                    (rstart && ((j>=11 && j<=14) || j==16)))) replacement=j;
                if(replacement>=0) fputs(values[replacement],out);else fwrite(start,1,(size_t)(p-start),out);
                if(*p!=',') {fputs(p,out);break;}
                fputc(',',out);start=p+1;column++;
            }
        }
    }
    if(ferror(in)||ferror(out)) ok=0;
    fclose(in);if(fclose(out)) ok=0;
    if(!ok || found!=1) {remove(temporary);return 0;}
    if(rename(temporary,path)) {remove(temporary);return 0;}
    return 1;
}

static AuthorGate gate_editor_new_gate(void) {
    float yaw=atomic_load(&g_debug_camera[4]),pitch=atomic_load(&g_debug_camera[3]);
    return (AuthorGate){atomic_load(&g_debug_camera[0])-sinf(yaw)*cosf(pitch)*5,
        atomic_load(&g_debug_camera[1])+sinf(pitch)*5,
        atomic_load(&g_debug_camera[2])-cosf(yaw)*cosf(pitch)*5,
        36,8,remainderf(yaw*57.2957795f+180,360),0,0};
}

static void gate_editor_add_at_car(void) {
    unsigned at=g_author_count;
    for(unsigned i=0;i<g_author_count;i++) if(g_author_gates[i].role==2) {at=i;break;}
    memmove(g_author_gates+at+1,g_author_gates+at,(g_author_count-at)*sizeof *g_author_gates);
    g_author_gates[at]=gate_editor_new_gate();g_author_selected=at;g_author_count++;
}

int enh_gate_editor_button(int action) {
    if(!enh_gate_editor_active()) {
        if(action!=GATE_EDIT_SELECT || !g_enhanced || !atomic_load(&g_track_debug) ||
           (!race_time_trial() && !mission_engaged()) || MISSION_COUNT==0) return 0;
        unsigned index=(unsigned)atomic_load(&g_mission_selected);
        if(index>=(unsigned)MISSION_COUNT || !k_missions[index].id) return 0;
        const MissionDefinition *d=&k_missions[index];
        pthread_mutex_lock(&g_author_mutex);
        g_author_mission=d->id;g_author_count=d->custom_gates;g_author_selected=0;g_author_error=0;g_author_start_cp=-1;g_author_flying=1;g_author_edit_start=0;
        unsigned cpindex=d->start_cp?d->start_cp-1:0;
        MissionRoadPoint start=cpindex<11?g_mission_cp_point[cpindex]:g_mission_cp_point[0];
        int authored=d->custom_start && !d->rolling_gate;
        g_author_start=(AuthorGate){authored?d->start_x:start.x,NAN,authored?d->start_z:start.z,
            36,8,authored?d->start_heading:start.heading*57.2957795f,3,0};
        g_author_start_speed=d->rolling_metres?d->rolling_speed*3.6f:0;
        g_author_start_plane_heading=start.heading*57.2957795f;
        g_author_start_rolling=d->rolling_metres;
        for(unsigned i=0;i<g_author_count;i++) g_author_gates[i]=(AuthorGate){d->gate_x[i],d->gate_y[i],d->gate_z[i],
            d->gate_width[i],d->gate_height[i]>0?d->gate_height[i]:8,
            isfinite(d->gate_heading[i])?d->gate_heading[i]:atomic_load(&g_debug_heading),d->gate_role[i],d->gate_tilt[i]};
        if(d->custom_finish && g_author_count<MISSION_MAX_GATES-1)
            g_author_gates[g_author_count++]=(AuthorGate){d->end_x,NAN,d->end_z,d->end_width,8,atomic_load(&g_debug_heading),2,0};
        if(!explorer_free()) explorer_free_toggle();
        atomic_store(&g_author_active,1);pthread_mutex_unlock(&g_author_mutex);
        mission_cancel();atomic_store(&g_mission_explore_request,1);
        atomic_store(&g_race_time_trial,1);atomic_store(&g_race_unlimited_laps,1);
        g_paused=0;g_mission_pause_menu=0;return 1;
    }
    pthread_mutex_lock(&g_author_mutex);
    /* Camera mode owns movement inputs, including held/repeated D-pad events.
     * No transform action may leak into the selected gate or start heading. */
    if(g_author_flying && (action==GATE_EDIT_ROTATE_LEFT || action==GATE_EDIT_ROTATE_RIGHT ||
       action==GATE_EDIT_RAISE || action==GATE_EDIT_LOWER)) {
        pthread_mutex_unlock(&g_author_mutex);return 1;
    }
    if(action==GATE_EDIT_CAMERA) g_author_flying=!g_author_flying;
    else if(action==GATE_EDIT_SET_START) {
        unsigned cpindex=0;float nearest=INFINITY;
        float x=atomic_load(&g_debug_camera[0]),z=atomic_load(&g_debug_camera[2]);
        for(unsigned i=0;i<10;i++) if(g_mission_cp_valid & (1u<<i)) {
            float distance=hypotf(x-g_mission_cp_point[i].x,z-g_mission_cp_point[i].z);
            if(distance<nearest) {nearest=distance;cpindex=i;}
        }
        if(!isfinite(nearest)) {g_author_error=1;pthread_mutex_unlock(&g_author_mutex);return 1;}
        /* CSV start CP indexes the checkpoint table with CP-1, not the
         * native progress byte, which can already refer to the next marker. */
        g_author_start_cp=(int)cpindex+1;
        MissionRoadPoint p=g_mission_cp_point[cpindex];
        g_author_start.x=p.x;g_author_start.z=p.z;g_author_start.y=NAN;
        g_author_start.width=36;g_author_start.height=8;g_author_start.role=3;g_author_start.tilt=0;
        g_author_start.heading=p.heading*57.2957795f;g_author_edit_start=1;
        g_author_start_plane_heading=g_author_start.heading;
        float along=0;
        for(unsigned i=0;p.point && i<g_mission_road_count;i++) {
            if(g_mission_road[i].point==p.point) {
                unsigned index=(unsigned)atomic_load(&g_mission_selected);
                int direction=index<(unsigned)MISSION_COUNT && k_missions[index].route_direction<0 ? -1 : 1;
                MissionRoadPoint ahead=mission_road_sample(along+direction*12);
                g_author_start.heading=atan2f(ahead.x-p.x,ahead.z-p.z)*57.2957795f;
                g_author_start_plane_heading=g_author_start.heading;
                break;
            }
            along+=g_mission_road[i].length;
        }
    }
    else if(action==GATE_EDIT_SELECT) {atomic_store(&g_author_active,0);if(explorer_free()) explorer_free_toggle();}
    else if(action==GATE_EDIT_ACCEPT) {
        unsigned finishes=0;int valid=1;
        for(unsigned i=0;i<g_author_count;i++) if(g_author_gates[i].role==2) {
            finishes++;for(unsigned j=i+1;j<g_author_count;j++) valid &= g_author_gates[j].role==1;
        }
        g_author_error=!valid || finishes>1 || !gate_editor_save();
        if(!g_author_error) {
            atomic_store(&g_author_active,0);
            if(explorer_free()) explorer_free_toggle();
            g_mission_reload_force=1;mission_selector_reload();
            if(!g_mission_reload_failed) {
                for(int i=0;i<MISSION_COUNT;i++) if(k_missions[i].id==g_author_mission) {mission_reload(i);break;}
            } else g_author_error=1;
        }
    } else if(action==GATE_EDIT_ADD && g_author_count<MISSION_MAX_GATES-1) {
        gate_editor_add_at_car();g_author_edit_start=0;
    } else if(action==GATE_EDIT_DELETE && g_author_count) {
        memmove(g_author_gates+g_author_selected,g_author_gates+g_author_selected+1,
            (g_author_count-g_author_selected-1)*sizeof *g_author_gates);
        g_author_count--;if(g_author_selected>=g_author_count) g_author_selected=g_author_count?g_author_count-1:0;
    } else if(g_author_edit_start && (action==GATE_EDIT_ROTATE_LEFT || action==GATE_EDIT_ROTATE_RIGHT)) {
        g_author_start.heading=remainderf(g_author_start.heading+(action==GATE_EDIT_ROTATE_RIGHT?5:-5),360);
    } else if(g_author_count) {
        if(action==GATE_EDIT_PREV || action==GATE_EDIT_NEXT) g_author_edit_start=0;
        if(action==GATE_EDIT_PREV) g_author_selected=(g_author_selected+g_author_count-1)%g_author_count;
        if(action==GATE_EDIT_NEXT) g_author_selected=(g_author_selected+1)%g_author_count;
        if(action==GATE_EDIT_ROTATE_LEFT || action==GATE_EDIT_ROTATE_RIGHT)
            g_author_gates[g_author_selected].heading=remainderf(g_author_gates[g_author_selected].heading+(action==GATE_EDIT_ROTATE_RIGHT?5:-5),360);
        if(action==GATE_EDIT_RAISE || action==GATE_EDIT_LOWER) {
            AuthorGate *gate=&g_author_gates[g_author_selected];
            if(!isfinite(gate->y)) gate->y=atomic_load(&g_debug_gate[g_author_selected][1]);
            gate->y+=action==GATE_EDIT_RAISE ? .5f : -.5f;
        }
        if(action==GATE_EDIT_ROLE_UP || action==GATE_EDIT_ROLE_DOWN) {
            AuthorGate *gate=&g_author_gates[g_author_selected];
            static const unsigned order[]={0,1,2,4,3};
            unsigned k=0;while(k<4 && order[k]!=gate->role) k++;
            gate->role=order[(k+(action==GATE_EDIT_ROLE_UP?1:4))%5];
            if(gate->role==4) {
                /* One rolling start, always the first gate. */
                for(unsigned i=0;i<g_author_count;i++) if(i!=g_author_selected && g_author_gates[i].role==4) g_author_gates[i].role=0;
                AuthorGate moved=*gate;
                memmove(g_author_gates+1,g_author_gates,g_author_selected*sizeof *gate);
                g_author_gates[0]=moved;g_author_selected=0;gate=g_author_gates;
            }
            if(gate->role==3) {
                /* Start is a mission setting, never an objective waypoint. */
                g_author_start=*gate;g_author_start_plane_heading=gate->heading;
                g_author_start_cp=0;g_author_edit_start=1;
                memmove(gate,gate+1,(g_author_count-g_author_selected-1)*sizeof *gate);
                g_author_count--;g_author_selected=g_author_count?g_author_selected%g_author_count:0;
            }
        }
    }
    pthread_mutex_unlock(&g_author_mutex);return 1;
}

void enh_gate_editor_axes(float lx,float ly,float rx,float ry,float lt,float rt,float dt) {
    if(!enh_gate_editor_active()) return;
    dt=fmaxf(0,fminf(.05f,dt));
    pthread_mutex_lock(&g_author_mutex);
    if(g_author_flying) {
        explorer_drive(-ly,rt-lt);explorer_look(rx+lx);explorer_pitch(-ry);
        pthread_mutex_unlock(&g_author_mutex);return;
    }
    explorer_drive(0,0);explorer_look(0);explorer_pitch(0);
    if(g_author_edit_start) {
        g_author_start_speed=fmaxf(0,fminf(200,g_author_start_speed-ry*30*dt));
        pthread_mutex_unlock(&g_author_mutex);return;
    }
    if(g_author_count) {
        AuthorGate *g=&g_author_gates[g_author_selected];
        float yaw=atomic_load(&g_debug_camera[4])+3.14159265359f;
        g->x+=(-lx*cosf(yaw)-ly*sinf(yaw))*20*dt;
        g->z+=(lx*sinf(yaw)-ly*cosf(yaw))*20*dt;
        g->width=fmaxf(1,fminf(200,g->width+rx*20*dt));
        g->height=fmaxf(1,fminf(100,g->height-ry*12*dt));
        g->tilt=fmaxf(-90,fminf(90,g->tilt+(rt-lt)*60*dt));
    }
    pthread_mutex_unlock(&g_author_mutex);
}

static void gate_editor_publish(PPCContext *c) {
    pthread_mutex_lock(&g_author_mutex);
    if(atomic_exchange(&g_author_mouse_pending,0) && enh_gate_editor_active()) {
        float mx=atomic_load(&g_author_mouse_x);if(enh_mirrored()) mx=1-mx;
        float right=(mx-.5f)*g_fbw/(g_fbh*.9f),up=(.5f-atomic_load(&g_author_mouse_y))/.9f;
        float yaw=atomic_load(&g_debug_camera[4]),pitch=atomic_load(&g_debug_camera[3]);
        float forward=cosf(pitch)-sinf(pitch)*up;
        float dx=cosf(yaw)*right-sinf(yaw)*forward,dy=sinf(pitch)+cosf(pitch)*up;
        float dz=-sinf(yaw)*right-cosf(yaw)*forward;
        float x=atomic_load(&g_debug_camera[0]),y=atomic_load(&g_debug_camera[1]),z=atomic_load(&g_debug_camera[2]);
        float previous=0,previous_gap=INFINITY;int hit=0;
        for(float distance=1;distance<=500;distance+=4) {
            float ground;
            if(!mission_ground(c,x+dx*distance,z+dz*distance,&ground)) {previous_gap=INFINITY;continue;}
            float gap=y+dy*distance-ground;
            if(gap<=0 && previous_gap>0 && isfinite(previous_gap)) {
                float low=previous,high=distance;
                for(unsigned i=0;i<10;i++) {
                    float middle=(low+high)*.5f,surface;
                    if(!mission_ground(c,x+dx*middle,z+dz*middle,&surface)) break;
                    if(y+dy*middle>surface) low=middle;else high=middle;
                }
                if(g_author_count<MISSION_MAX_GATES-1) {
                    gate_editor_add_at_car();AuthorGate *gate=&g_author_gates[g_author_selected];
                    gate->x=x+dx*high;gate->z=z+dz*high;gate->y=y+dy*high;
                    g_author_edit_start=0;g_author_error=0;hit=1;
                }
                break;
            }
            previous=distance;previous_gap=gap;
        }
        if(!hit) g_author_error=2;
    }
    unsigned count=g_author_count+1;
    const MissionDefinition *d=NULL;
    if(!enh_gate_editor_active()) {
        unsigned index=(unsigned)atomic_load(&g_mission_selected);
        if(index>=(unsigned)MISSION_COUNT) {pthread_mutex_unlock(&g_author_mutex);return;}
        d=&k_missions[index];count=d->custom_gates+(d->custom_finish?1:0);
    }
    for(unsigned i=0;i<count;i++) {
        AuthorGate g=d ? (i<d->custom_gates ? (AuthorGate){d->gate_x[i],d->gate_y[i],d->gate_z[i],
            d->gate_width[i],d->gate_height[i],d->gate_heading[i],d->gate_role[i],d->gate_tilt[i]} :
            (AuthorGate){d->end_x,NAN,d->end_z,d->end_width,8,NAN,2,0}) : (i==g_author_count ? g_author_start : g_author_gates[i]);
        if(!isfinite(g.heading)) {
            float nearest=INFINITY;g.heading=atomic_load(&g_debug_heading);
            for(unsigned j=0;j<g_mission_road_count;j++) {
                float distance=hypotf(g.x-g_mission_road[j].x,g.z-g_mission_road[j].z);
                if(distance<nearest) {nearest=distance;g.heading=g_mission_road[j].heading*57.2957795f;}
            }
        }
        float y=g.y;
        if(!isfinite(y)) {
            int grounded=mission_ground(c,g.x,g.z,&y);
            if(!grounded) y=atomic_load(&g_debug_y);
            /* Resolve auto height once while editing. Native terrain queries can
             * choose a different stacked surface as streaming/camera moves. */
            if(!d && grounded) {
                if(i==g_author_count) g_author_start.y=y;
                else g_author_gates[i].y=y;
            }
        }
        float heading=(g.role==3 ? g_author_start_plane_heading : g.heading)*.01745329252f;
        for(unsigned j=0;j<3;j++) {
            float side=g.width*.5f*((float)j-1),ground=NAN;
            if(!mission_ground(c,g.x+cosf(heading)*side,g.z-sinf(heading)*side,&ground)) ground=NAN;
            atomic_store(&g_debug_gate_ground[i][j],ground);
        }
        float values[]={g.x,y,g.z,sinf(heading),cosf(heading),g.width/2};
        for(unsigned j=0;j<6;j++) atomic_store(&g_debug_gate[i][j],values[j]);
        atomic_store(&g_debug_gate_tilt[i],g.tilt);
        atomic_store(&g_debug_gate_height[i],g.height/2);atomic_store(&g_debug_gate_role[i],g.role);
    }
    atomic_store(&g_debug_gate_count,count);
    pthread_mutex_unlock(&g_author_mutex);
}
