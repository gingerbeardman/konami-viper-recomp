#pragma once
#include "course_reverse.h"
static int g_reverse_selected,g_reverse_applied,g_reverse_previous_mode=-1;
static uint32_t g_reverse_root;
typedef struct {uint32_t node,data;unsigned count;uint8_t *bytes;} ReverseCopy;
static ReverseCopy g_reverse_copy[128];
static unsigned g_reverse_copy_count;
/* Queue entries are fresh copies of native placement matrices. Change their
 * local orientation before the renderer projects them; do not mutate scenery
 * records or camera-transformed matrices that can survive between frames. */
static void course_reverse_sign(PPCContext *c,uint32_t entry) {
    if(!g_reverse_applied || !race_valid(entry,124) || !race_valid(c->r[2]+0x40,4)) return;
    uint32_t descriptor=LD32(entry+112),groups=LD32(c->r[2]+0x40),code=UINT32_MAX;
    if(!race_valid(groups,4)) return;
    unsigned count=LD32(groups);
    if(count>128 || !race_valid(groups,count*36)) return;
    for(unsigned i=1;i<count;i++) {
        uint32_t group=groups+i*36,base=LD32(group+20);unsigned n=LD32(group+16);
        if(n<=65536 && descriptor>=base && descriptor-base<n*16 && !((descriptor-base)&15)) {
            code=(LD32(group+8)<<16)|((descriptor-base)/16);break;
        }
    }
    if(!((code>=0x1011f && code<=0x10121) || (code>=0x2000a && code<=0x20010) || code==0x300aa)) return;
    static unsigned logged;
    if(getenv("RT_ENH_LOG") && logged++<4) rt_log("enhanced: reverse sign resource %x queue %x\n",code,entry);
    /* Native 3x3 placement matrix: local X/Z columns rotate through 180 degrees.
     * Position and the local vertical axis remain unchanged. */
    for(unsigned row=0;row<3;row++) for(unsigned column=0;column<3;column+=2) {
        uint32_t p=entry+12+4*(row*3+column);STF32(p,-LDF32(p));
    }
}
static void course_reverse_restore(void) {
    int restore=g_reverse_applied && LD32(0x8c0188)==g_reverse_root;
    for(unsigned i=0;i<g_reverse_copy_count;i++) {
        ReverseCopy *copy=&g_reverse_copy[i];
        if(restore) {memcpy(g_ram+copy->node,copy->bytes,0x44);memcpy(g_ram+copy->data,copy->bytes+0x44,copy->count*32);}
        free(copy->bytes);copy->bytes=NULL;
    }
    if(restore && g_reverse_copy_count) ST32(0x8c0188,g_reverse_copy[0].node);
    g_reverse_copy_count=0;g_reverse_applied=0;
}
static int course_reverse_capture(uint32_t root) {
    uint32_t nodes[128]={root};unsigned n=1;
    for(unsigned i=0;i<n;i++) {
        if(!race_valid(nodes[i],0x44)) return 0;
        unsigned count=LD16(nodes[i]);uint32_t data=LD32(nodes[i]+4);
        if(count<2 || count>512 || !race_valid(data,count*32)) return 0;
        ReverseCopy *copy=&g_reverse_copy[g_reverse_copy_count];
        *copy=(ReverseCopy){nodes[i],data,count,malloc(0x44+count*32)};
        if(!copy->bytes) return 0;
        memcpy(copy->bytes,g_ram+nodes[i],0x44);memcpy(copy->bytes+0x44,g_ram+data,count*32);
        g_reverse_copy_count++;
        for(unsigned k=0;k<3;k++) for(unsigned side=0;side<2;side++) {
            uint32_t link=LD32(nodes[i]+8+k*20+side*4);if(!link) continue;
            unsigned j=0;for(;j<n && nodes[j]!=link;j++);
            if(j==n) {if(n==128) return 0;nodes[n++]=link;}
        }
    }
    return 1;
}
static void course_reverse_dispatch(PPCContext *c) {
    if(!mission_available() || GAME_ENH_RACE_RESTART_STYLE!=1 || !race_valid(c->r[2],0x558)) return;
    uint32_t state=LD32(c->r[2]+0x54);if(!race_valid(state,0x14)) return;
    int mode=(LD32(state+4)>>23)&15;
    if(mode==9 && g_reverse_applied) course_reverse_restore();
    if((mode==0 || mode==6) && mode!=g_reverse_previous_mode) g_reverse_selected=0;
    if(mission_engaged()) g_reverse_selected=0;
    g_reverse_previous_mode=mode;
}
static void course_reverse_confirm(PPCContext *c) {
    if(!mission_available() || GAME_ENH_RACE_RESTART_STYLE!=1 || mission_engaged()) return;
    extern int16_t g_analog[4];
    static int previous_brake=9999;
    if(getenv("RT_ENH_LOG") && g_analog[2]!=previous_brake) {
        rt_log("enhanced: course confirmation brake %d native %u\n",g_analog[2],c->r[3]);previous_brake=g_analog[2];
    }

    /* Native chooser returns zero on confirmation/timeout. Brake also confirms. */
    if(g_analog[2]>100) {c->r[3]=0;g_reverse_selected=1;rt_log("enhanced: Brake confirms Reverse course\n");}
    else if(c->r[3]==0) g_reverse_selected=0;
}
static void course_reverse_race(PPCContext *c) {
    if(!mission_available() || GAME_ENH_RACE_RESTART_STYLE!=1 || mission_engaged() || g_reverse_applied ||
        (!g_reverse_selected && !getenv("RT_REVERSE")) || !race_restart_available()) return;
    uint32_t car=LD32(c->r[2]+0x488),course=LD32(c->r[2]+0x400),root=LD32(0x8c0188),new_root;
    if(!race_valid(car,0x514) || !race_valid(course,0x30)) return;
    float length=LDF32(course+0x2c);
    if(!course_reverse_capture(root)) {course_reverse_restore();return;}
    /* The reversed origin must precede the start line, as in the native race.
     * Cutting at the old origin would wrap immediately after the initial start. */
    float finish=-1,origin=INFINITY;
    for(unsigned i=0;i<g_reverse_copy_count;i++) for(unsigned j=0;j<g_reverse_copy[i].count;j++) {
        uint32_t p=g_reverse_copy[i].data+32*j;
        if(LD8(p+28)==1) finish=LDF32(p+24);
    }
    for(unsigned i=0;i<g_reverse_copy_count;i++) for(unsigned j=0;j<g_reverse_copy[i].count;j++) {
        float distance=LDF32(g_reverse_copy[i].data+32*j+24);
        if(distance>=finish+35 && distance<origin) origin=distance;
    }
    if(finish<0 || !isfinite(origin) || !course_reverse_graph(root,length,&new_root)) {
        course_reverse_restore();rt_log("enhanced: reverse course graph rejected\n");return;
    }
    for(unsigned i=0;i<g_reverse_copy_count;i++) for(unsigned j=0;j<g_reverse_copy[i].count;j++) {
        uint32_t p=g_reverse_copy[i].data+32*j+24;
        STF32(p,fmodf(LDF32(p)+origin,length));
    }
    ST32(0x8c0188,new_root);g_reverse_root=new_root;g_reverse_applied=1;
    if(!mission_read_road(new_root) || !(g_mission_cp_valid&1)) {course_reverse_restore();return;}
    MissionRoadPoint spawn=mission_road_sample(g_mission_cp_distance[0]-35);
    if(!mission_place(c,spawn,0)) {course_reverse_restore();return;}
    uint32_t route=car+0x4dc,timing=LD32(c->r[2]+0x554);
    ST8(route+1,1);ST8(route+2,0);
    if(race_valid(timing,0xd4)) {ST8(timing+0x11,1);ST8(timing+0x12,0);STF32(timing+0x1c,LDF32(route+8));STF32(timing+0xc8,LDF32(route+8));}
    rt_log("enhanced: reverse course started at %.1f %.1f heading %.1f\n",spawn.x,spawn.z,spawn.heading*57.2957795f);
}
