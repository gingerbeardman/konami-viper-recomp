/*
 * Enhanced ("conversion") mode: optional additions on top of the faithful port, enabled with
 * --enhanced. The original mode is never affected: everything here is gated on g_enhanced, and
 * the enhanced mode keeps its own NVRAM (<binary>_enhanced_nvram.bin).
 *
 * What the game data needs (addresses, scripts) comes from the "enhanced" section of
 * games/<id>/game.json, through game_config.h:
 *   - setup: a scripted TEST MODE pass run on first launch after the calibration (free play);
 *   - blank_strings: game strings emptied in RAM, e.g. the "FREE PLAY" and "PRESS START BUTTON"
 *     captions. The game module is loaded by the kernel at runtime, so the strings are checked
 *     every frame and emptied whenever their original text is found (also after a reload);
 *   - hooks: addresses where the recompiled code calls rt_hook(). "attract" is the free-play
 *     branch of the credit display (a Konami library routine shared by the games): the game
 *     draws it only while no game is in progress, so it tells the attract mode apart.
 *     "projection" and "viewport" follow the gl library's writes of its projection slots and
 *     viewport, for the widescreen option; "name_index" and "name_confirm" sit in the rankings'
 *     name entry, typed on the keyboard (see name_entry);
 *   - widescreen: where the gl library keeps that state.
 */
#include "runtime.h"
#include "track_explorer.h"
#include "game_config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include "race_restart.h"
#include "mission_mode.h"
#include "course_reverse_runtime.h"
static int g_practice_text_ready;

int g_enhanced;

/* attract detection: the frame at which the "attract" hook last ran */
#define ATTRACT_GRACE_FRAMES 30
static uint64_t g_frame, g_attract_frame;
static int g_attract, g_enh_log = -1;

int enh_in_attract(void) { return g_attract; }

/* port settings (<binary>_settings.ini, see below) */
static struct { int fullscreen, show_fps, scale, aspect, show_gyro, texture_filter, draw_distance, mirror, mission_cursor; } g_set = { 0, 0, 1, 0, 0, 0, 0 };
static int g_mission_cursor;

/* ================================================================== widescreen */
/* The games draw a 512x384 picture through Konami's gl library, which keeps its state at fixed
 * addresses: two projection slots (perspective for the 3D, orthographic for the 2D), each with
 * six matrix terms (row 0 first) and the frustum they come from (left, right, bottom, top, near,
 * far), and the viewport (x scale, x centre, y scale, y centre). The library culls objects
 * against that frustum and clips to it.
 * A picture k times wider (Hor+: same vertical field of view) keeps every pixel where it was:
 * row 0 of each projection is divided by k and the viewport x scale multiplied by k, which
 * cancel out on screen, while the frustum is widened k times around its centre. The game then
 * draws the same picture plus what lies left and right of it, at x from -M to 512 + M; the
 * Voodoo core renders the displayed buffers with a margin of M pixels on each side
 * (voodoo_set_wide). The HUD stays where it was, in the 4:3 centre.
 * The hooks follow every write of that state, so a new value is widened once; a change of the
 * option rescales what is already there. */
static const int k_wide_margin[] = { 0, 51, 85, 199 };      /* 4:3, 16:10, 16:9, 21:9 at 384 lines */
#define N_ASPECTS 4
void voodoo_set_wide(int margin);
static double g_wide_k = 1.0;                /* wanted: picture width / 512 */
static double g_slot_k[2], g_vp_k;           /* applied to each projection slot / the viewport (0: unseen) */
static int g_slot_mirror[2];
int enh_mirrored(void) { return g_enhanced && g_set.mirror && race_restart_available(); }
static struct { double near, far; float depth[2]; int valid, applied; } g_distance_slot[2];
static void mirror_slot(int slot) {
    int desired=enh_mirrored() && g_distance_slot[slot].valid;
    if(desired!=g_slot_mirror[slot]) {
        uint32_t matrix=GAME_ENH_WIDE_PROJ_MATRIX+24*slot;
        STF32(matrix,-LDF32(matrix));STF32(matrix+4,-LDF32(matrix+4));
        g_slot_mirror[slot]=desired;
    }
}
int enh_mirror_projection(void) {
    unsigned slot=LD8(0x3452);
    return slot<2 && g_slot_mirror[slot];
}
static const double k_draw_distance[] = { 1, 2, 4 };

static void distance_apply(int s) {
    if (!g_distance_slot[s].valid || g_distance_slot[s].applied == g_set.draw_distance) return;
    uint32_t m = GAME_ENH_WIDE_PROJ_MATRIX + 24 * s, fr = GAME_ENH_WIDE_PROJ_FRUSTUM + 24 * s;
    double n = g_distance_slot[s].near, f = g_distance_slot[s].far * k_draw_distance[g_set.draw_distance];
    STF32(fr + 20, f);
    /* Keep the original depth terms exactly when restoring the default. */
    STF32(m + 16, g_set.draw_distance ? -(f + n) / (f - n) : g_distance_slot[s].depth[0]);
    STF32(m + 20, g_set.draw_distance ? -2 * f * n / (f - n) : g_distance_slot[s].depth[1]);
    g_distance_slot[s].applied = g_set.draw_distance;
}

static void distance_capture(int s) {
    uint32_t m = GAME_ENH_WIDE_PROJ_MATRIX + 24 * s, fr = GAME_ENH_WIDE_PROJ_FRUSTUM + 24 * s;
    double n = LDF32(fr + 16), f = LDF32(fr + 20);
    double a = LDF32(m + 16), b = LDF32(m + 20);
    /* Recognize the perspective depth pair, excluding orthographic/menu slots. */
    g_distance_slot[s].valid = isfinite(n) && isfinite(f) && n > 0 && f > n &&
        fabs(a + (f + n) / (f - n)) < 1e-5 &&
        fabs(b + 2 * f * n / (f - n)) < 1e-5 * fmax(1, fabs(b));
    g_distance_slot[s].near = n; g_distance_slot[s].far = f;
    g_distance_slot[s].depth[0] = a; g_distance_slot[s].depth[1] = b;
    g_distance_slot[s].applied = 0;
    distance_apply(s);
}

static void widen_slot(int s, double f) {    /* f: new factor / applied factor */
    uint32_t m = GAME_ENH_WIDE_PROJ_MATRIX + 24 * s, fr = GAME_ENH_WIDE_PROJ_FRUSTUM + 24 * s;
    double l = LDF32(fr), r = LDF32(fr + 4), mid = (l + r) / 2, half = (r - l) / 2;
    STF32(m, LDF32(m) / f);
    STF32(m + 4, LDF32(m + 4) / f);
    STF32(fr, mid - half * f);
    STF32(fr + 4, mid + half * f);
}

static void set_aspect(int a) {              /* the Voodoo margin follows from the next frame */
    if (GAME_ENH_WIDE_VIEWPORT) voodoo_set_wide(k_wide_margin[a]);
}

static void wide_tick(int aspect) {         /* guest thread, every frame: follow the option */
    if (!GAME_ENH_WIDE_VIEWPORT) return;
    g_wide_k = (512.0 + 2 * k_wide_margin[aspect]) / 512.0;
    for(int s=0;s<2;s++) if(g_slot_k[s]) mirror_slot(s);
    for (int s = 0; s < 2; s++)
        if (g_slot_k[s] && g_slot_k[s] != g_wide_k) { widen_slot(s, g_wide_k / g_slot_k[s]); g_slot_k[s] = g_wide_k; }
    if (g_vp_k && g_vp_k != g_wide_k) {
        STF32(GAME_ENH_WIDE_VIEWPORT, LDF32(GAME_ENH_WIDE_VIEWPORT) * g_wide_k / g_vp_k);
        g_vp_k = g_wide_k;
    }
}

/* ================================================================== name entry */
/* The rankings' name entry picks each letter with the steering wheel: the game turns the wheel
 * position into an index on its wheel of characters (the profile's chars, then DEL and END), and
 * a pedal confirms it. In the enhanced mode the keyboard types the letters instead: the
 * "name_index" hook replaces the index the game took from the wheel with the chosen one, and the
 * "name_confirm" hook makes the confirmation check succeed once for each typed key (DEL and END
 * included: Backspace and Enter). Left/Right step through the wheel, and the game's own
 * confirmation (a pedal) still accepts the letter shown, for a gamepad.
 * The frontend queues the keys; the guest thread takes them at the hooks, one per call. */
#define NAME_GRACE_FRAMES 10
#define NAME_QUEUE 32
enum { NAME_STEP_LEFT = -1, NAME_STEP_RIGHT = -2 };
static volatile uint64_t g_name_frame;       /* the frame at which the "name_index" hook last ran */
static int g_name_idx, g_name_confirm;       /* guest thread: the index shown, a typed key to confirm */
static volatile int g_name_queue[NAME_QUEUE];
static _Atomic unsigned g_name_w, g_name_r;  /* written by the frontend / by the guest thread */

static int name_count(void) { return GAME_ENH_NAME_CHARS ? (int)strlen(GAME_ENH_NAME_CHARS) + 2 : 0; }

int enh_name_entry_active(void) {
    return g_enhanced && g_name_frame && g_frame - g_name_frame <= NAME_GRACE_FRAMES;
}

static void name_push(int v) {
    unsigned w = g_name_w;
    if (w - g_name_r >= NAME_QUEUE) return;          /* full: drop the key */
    g_name_queue[w % NAME_QUEUE] = v;
    g_name_w = w + 1;
}

/* a typed character ('\b' DEL, '\r' END): 1 if the wheel has it */
int enh_name_type(int ch) {
    if (!enh_name_entry_active()) return 0;
    const char *chars = GAME_ENH_NAME_CHARS;
    int n = name_count();
    if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
    int idx = ch == '\b' ? n - 2 : ch == '\r' ? n - 1 : -1;
    const char *p = ch > 0 && idx < 0 ? strchr(chars, ch) : NULL;
    if (p) idx = (int)(p - chars);
    if (idx < 0) return 0;
    name_push(idx);
    return 1;
}

void enh_name_step(int dir) {
    if (enh_name_entry_active()) name_push(dir < 0 ? NAME_STEP_LEFT : NAME_STEP_RIGHT);
}

static void name_index_hook(PPCContext *c) {
    int n = name_count();
    if (!n) return;
    if (!enh_name_entry_active()) {             /* a new name entry: start from the game's letter */
        uint32_t v = c->r[GAME_ENH_NAME_INDEX_REG];
        g_name_idx = v < (uint32_t)n ? (int)v : 0;
        g_name_confirm = 0;
        g_name_r = g_name_w;
        if (g_enh_log) rt_log("enhanced: name entry\n");
    }
    g_name_frame = g_frame;
    while (!g_name_confirm && g_name_r != g_name_w) {
        unsigned r = g_name_r;
        int v = g_name_queue[r % NAME_QUEUE];
        g_name_r = r + 1;
        if (v == NAME_STEP_LEFT) g_name_idx = (g_name_idx + n - 1) % n;
        else if (v == NAME_STEP_RIGHT) g_name_idx = (g_name_idx + 1) % n;
        else { g_name_idx = v; g_name_confirm = 1; }
    }
    c->r[GAME_ENH_NAME_INDEX_REG] = (uint32_t)g_name_idx;
    if (GAME_ENH_NAME_FIELD_REG >= 0) ST16(c->r[GAME_ENH_NAME_FIELD_REG] + GAME_ENH_NAME_FIELD_OFF, (uint32_t)g_name_idx);
}

static void name_confirm_hook(PPCContext *c) {
    if (!g_name_confirm) return;
    c->r[GAME_ENH_NAME_CONFIRM_REG] = 1;
    g_name_confirm = 0;
}

/* Debug data is published by the guest and read by the SDL drawing thread. */
static atomic_int g_track_debug, g_debug_teleport;
static _Atomic float g_debug_gate_tilt[MISSION_MAX_GATES];
static _Atomic float g_debug_gate_ground[MISSION_MAX_GATES][3];
int enh_track_debug_step(int direction) {
    if(!atomic_load(&g_track_debug) || (!race_time_trial() && !mission_engaged()) || enh_paused() || enh_menu_active()) return 0;
    atomic_store(&g_debug_teleport,direction); return 1;
}
static _Atomic float g_debug_x, g_debug_y, g_debug_z, g_debug_heading;
int enh_track_debug_position_text(char *text,size_t size) {
    if(!atomic_load(&g_track_debug) || (!race_time_trial() && !mission_engaged()) || !text || !size) return 0;
    snprintf(text,size,"%.0f,%.0f",roundf(atomic_load(&g_debug_x)),roundf(atomic_load(&g_debug_z)));
    return 1;
}

int enh_track_debug_heading_text(char *text,size_t size) {
    if(!atomic_load(&g_track_debug) || (!race_time_trial() && !mission_engaged()) || !text || !size) return 0;
    snprintf(text,size,"%.0f",roundf(atomic_load(&g_debug_heading)));
    return 1;
}
static atomic_uint g_debug_cp, g_debug_object, g_debug_gate_count;
static atomic_uint g_debug_gate_mask,g_debug_gate_any;
static atomic_uint g_debug_vehicle_kind=UINT_MAX, g_debug_vehicle_index;
static atomic_int g_debug_vehicle_parked;
static _Atomic float g_debug_vehicle_x,g_debug_vehicle_z,g_debug_vehicle_distance;
static _Atomic float g_debug_gate[MISSION_MAX_GATES][6];
static _Atomic float g_debug_gate_height[MISSION_MAX_GATES];
static atomic_uint g_debug_gate_role[MISSION_MAX_GATES];
static void gate_editor_publish(PPCContext *c);
static _Atomic float g_debug_camera[5];
static _Atomic float g_debug_view[12],g_debug_projection[4],g_debug_viewport[4];
static atomic_int g_debug_view_valid;
static float g_mission_finish_camera[6];
static int g_mission_finish_camera_saved;
static void mission_finish_camera(void) {
    if(mission_phase()!=MISSION_PASSED) {g_mission_finish_camera_saved=0;return;}
    for(unsigned i=0;i<6;i++) {
        unsigned address=0x8c1cf8+i*4;
        if(!g_mission_finish_camera_saved) g_mission_finish_camera[i]=LDF32(address);
        else STF32(address,g_mission_finish_camera[i]);
    }
    g_mission_finish_camera_saved=1;
}
static _Atomic float g_debug_object_x, g_debug_object_y, g_debug_object_z;
static _Atomic unsigned char g_debug_object_name[48];
static _Atomic unsigned char g_debug_solid_name[32];
enum { DEBUG_ITEM_LIMIT=64 };
static atomic_uint g_debug_item_count;
static _Atomic float g_debug_item[DEBUG_ITEM_LIMIT][3];
static _Atomic unsigned char g_debug_item_name[DEBUG_ITEM_LIMIT][32];
static void track_debug_item(unsigned *count,float x,float y,float z,const char *name) {
    if(*count>=DEBUG_ITEM_LIMIT || !isfinite(x)||!isfinite(y)||!isfinite(z) ||
       hypotf(x-atomic_load(&g_debug_x),z-atomic_load(&g_debug_z))>80) return;
    unsigned n=(*count)++;
    atomic_store(&g_debug_item[n][0],x);atomic_store(&g_debug_item[n][1],y);
    atomic_store(&g_debug_item[n][2],z);
    char label[32]={0};snprintf(label,sizeof label,"%s",name);
    for(unsigned j=0;label[j];j++) label[j]=(char)toupper((unsigned char)label[j]);
    for(unsigned j=0;j<sizeof label;j++) atomic_store(&g_debug_item_name[n][j],(unsigned char)label[j]);
}
/* Resolve a scene descriptor through the game's resource registry. Names are
 * exported to the drawing thread, which never walks mutable guest tables. */
static void track_debug_object_name(PPCContext *c,unsigned object) {
    static unsigned previous_object,previous_descriptor;
    unsigned descriptor=race_valid(object,28) ? LD32(object+24) : 0;
    if(object==previous_object && descriptor==previous_descriptor) return;
    previous_object=object;previous_descriptor=descriptor;
    char name[48]="";unsigned code=UINT_MAX;
    unsigned groups=LD32(c->r[2]+0x40),registry=LD32(c->r[2]+0x78);
    if(descriptor && race_valid(groups,4)) {
        unsigned count=LD32(groups);
        if(count<=128 && race_valid(groups,count*36)) for(unsigned i=1;i<count;i++) {
            unsigned group=groups+i*36,n=LD32(group+16),base=LD32(group+20);
            if(n<=65536 && descriptor>=base && descriptor-base<n*16 && !((descriptor-base)&15)) {
                code=(LD32(group+8)<<16)|((descriptor-base)/16);break;
            }
        }
    }
    if(code!=UINT_MAX && race_valid(registry,48)) {
        unsigned count=LD32(registry+24),entries=LD32(registry+28);
        if(count<=16384 && race_valid(entries,count*12)) for(unsigned i=0;i<count;i++) {
            unsigned entry=entries+i*12,key=LD32(entry);
            if(LD32(entry+4)!=code || !race_valid(key,sizeof name)) continue;
            for(unsigned j=0;j<sizeof name-1 && LD8(key+j);j++) name[j]=(char)toupper(LD8(key+j));
            break;
        }
    }
    for(unsigned i=0;i<sizeof name;i++) atomic_store(&g_debug_object_name[i],(unsigned char)name[i]);
}
void enh_track_debug_toggle(void) {
    if(!atomic_fetch_xor(&g_track_debug,1) && mission_engaged()) {
        mission_cancel();atomic_store(&g_mission_explore_request,1);
    }
}
/* Native view slot zero: a column-major world-to-camera rotation followed by
 * a camera-space translation. Capture after the game builds it, before HUD view. */
unsigned long long voodoo_swap_count(void);
uint64_t voodoo_frame_swaps(void);
/* Views are built a frame ahead of the picture on screen, so each one is
 * tagged with the swaps made before its frame and drawn with that frame. */
#define DEBUG_VIEW_RING 8
static struct {
    atomic_ullong tag;      /* swaps before the frame, plus one; 0 while written */
    _Atomic float view[12];
    atomic_int valid;
} g_debug_view_ring[DEBUG_VIEW_RING];
static unsigned g_debug_view_next;
static void track_debug_view_capture(PPCContext *c) {
    unsigned state=LD32(c->r[2]+0x6c);
    unsigned long long swaps=voodoo_swap_count();
    unsigned slot=g_debug_view_next;
    /* Several views can be built before one swap; keep the last in one slot. */
    unsigned prev=(slot+DEBUG_VIEW_RING-1)%DEBUG_VIEW_RING;
    if(atomic_load(&g_debug_view_ring[prev].tag)==swaps+1) slot=prev;
    else g_debug_view_next=(slot+1)%DEBUG_VIEW_RING;
    atomic_store(&g_debug_view_ring[slot].tag,0);
    int valid=race_valid(state+0xa4,48);
    if(valid) for(unsigned i=0;i<12;i++) atomic_store(&g_debug_view_ring[slot].view[i],LDF32(state+0xa4+i*4));
    atomic_store(&g_debug_view_ring[slot].valid,valid);
    atomic_store(&g_debug_view_ring[slot].tag,swaps+1);
}
/* Select the latest view built before the displayed frame's swap. */
static void track_debug_view_select(void) {
    unsigned long long shown=voodoo_frame_swaps();
    int best=-1;unsigned long long best_tag=0;
    for(int i=0;i<DEBUG_VIEW_RING;i++) {
        unsigned long long tag=atomic_load(&g_debug_view_ring[i].tag);
        if(tag && tag<=shown && tag>best_tag) {best=i;best_tag=tag;}
    }
    if(best<0) {atomic_store(&g_debug_view_valid,0);return;}
    float v[12];
    for(unsigned i=0;i<12;i++) v[i]=atomic_load(&g_debug_view_ring[best].view[i]);
    int valid=atomic_load(&g_debug_view_ring[best].valid);
    if(atomic_load(&g_debug_view_ring[best].tag)!=best_tag) return;   /* overwritten meanwhile */
    for(unsigned i=0;i<12;i++) atomic_store(&g_debug_view[i],v[i]);
    atomic_store(&g_debug_view_valid,valid);
}
/* The slot is rewritten each frame: perspective for the scene, then
 * orthographic for the HUD. Keep the latest perspective terms (after mirror
 * and widescreen adjustment) with the viewport they render through. */
static void track_debug_projection_capture(unsigned slot) {
    for(unsigned i=0;i<4;i++) {
        atomic_store(&g_debug_projection[i],LDF32(GAME_ENH_WIDE_PROJ_MATRIX+24*slot+i*4));
        atomic_store(&g_debug_viewport[i],LDF32(GAME_ENH_WIDE_VIEWPORT+i*4));
    }
}
static void track_debug_camera_capture(void) {
    const unsigned camera[]={0x8c1cf8,0x8c1cfc,0x8c1d00,0x8c1d04,0x8c1d08};
    for(unsigned i=0;i<5;i++) atomic_store(&g_debug_camera[i],LDF32(camera[i]));
}
static void track_debug_tick(PPCContext *c) {
    if (!atomic_load(&g_track_debug) || (!race_time_trial() && !mission_engaged())) return;
    /* Debugging a mission is unrestricted exploration, including when debug
     * was already enabled before this mission loaded. */
    if(mission_phase()==MISSION_RUNNING || mission_phase()==MISSION_ROLLING) {
        mission_cancel();atomic_store(&g_mission_explore_request,1);
    }
    unsigned car=LD32(c->r[2]+0x488);
    if (!race_valid(car,0x514)) return;
    int step=atomic_exchange(&g_debug_teleport,0);
    if(step && mission_read_road(LD32(0x8c0188))) {
        MissionPosition pos=mission_car_position(car); float along=0,current=0,nearest=INFINITY;
        for(unsigned i=0;i<g_mission_road_count;i++) {
            MissionRoadPoint p=g_mission_road[i]; float distance=hypotf(pos.x-p.x,pos.z-p.z);
            if(distance<nearest) { nearest=distance; current=along; } along+=p.length;
        }
        float best=INFINITY,target=0; unsigned target_checkpoint=0; along=0;
        for(unsigned i=0;i<g_mission_road_count;i++) {
            unsigned marker=g_mission_road[i].checkpoint;
            if(marker==1 || (marker>=3 && marker<=11)) {
                float delta=fmodf((step>0 ? along-current : current-along)+g_mission_road_length,g_mission_road_length);
                if(delta>8 && delta<best) { best=delta; target=along; target_checkpoint=g_mission_road[i].checkpoint; }
            }
            along+=g_mission_road[i].length;
        }
        MissionRoadPoint p=mission_road_sample(target),ahead=mission_road_sample(target+12);
        p.heading=atan2f(ahead.x-p.x,ahead.z-p.z);
        if(isfinite(best) && mission_place(c,p,0)) ST8(car+0x4de,target_checkpoint);
    }
    atomic_store(&g_debug_x,LDF32(car+0x174));
    atomic_store(&g_debug_z,LDF32(car+0x178));
    atomic_store(&g_debug_y,LDF32(car+0x17c));
    atomic_store(&g_debug_heading,remainderf(LDF32(car+0x1b0)*57.2957795f,360.f));
    atomic_store(&g_debug_cp,LD8(car+0x4de));
    atomic_store(&g_debug_object,0);
    unsigned vehicles=LD32(c->r[2]+0x614),kind=UINT_MAX,index=0;
    float nearest_vehicle=INFINITY,vx=0,vz=0;
    unsigned labels=0;
    if(race_valid(vehicles,24)) for(unsigned group=0;group<2;group++) {
        unsigned records=LD32(vehicles+group*12+4),count=LD32(vehicles+group*12+8);
        if(count>128 || !race_valid(records,count*128)) continue;
        for(unsigned i=0;i<count;i++) {
            unsigned p=records+i*128;
            if(!LD32(p+16) || !(LD32(p)&0x4000) || LD32(p+0x50)>15) continue;
            float x=LDF32(p+0x20),z=LDF32(p+0x28);
            char label[32];snprintf(label,sizeof label,"CAR %u TYPE %u",group*128+i,LD32(p+0x50));
            track_debug_item(&labels,x,LDF32(p+0x24)+2,z,label);
            float distance=hypotf(x-LDF32(car+0x174),z-LDF32(car+0x178));
            if(distance<nearest_vehicle) {
                nearest_vehicle=distance;kind=LD32(p+0x50);index=group*128+i;vx=x;vz=z;
            }
        }
    }
    int parked=0;
    unsigned catalog=LD32(c->r[2]+0x380);
    if(race_valid(catalog,8)) {
        unsigned count=LD32(catalog+4),records=catalog+0x308;
        if(count<=96 && race_valid(records,count*36)) for(unsigned i=0;i<count;i++) {
            unsigned p=records+i*36,model=mission_tcar_model(c,LD32(p));
            if(model>15) continue;
            float x=LDF32(p+4),z=LDF32(p+12);
            if(!isfinite(x)||!isfinite(z)) continue;
            char label[32];snprintf(label,sizeof label,"PARKED %u MODEL %u",i,model);
            track_debug_item(&labels,x,LDF32(p+8)+2,z,label);
            float distance=hypotf(x-LDF32(car+0x174),z-LDF32(car+0x178));
            if(distance<nearest_vehicle) {
                nearest_vehicle=distance;kind=model;index=i;vx=x;vz=z;parked=1;
            }
        }
    }
    float nearest_solid=INFINITY; char solid_name[32]={0};
    unsigned world=LD32(c->r[2]+0x53c);
    if(race_valid(world,16)) {
        unsigned first=LD32(world),last=LD32(world+12);
        if(first<last && last-first<=0x400000 && race_valid(first,last-first))
        for(unsigned p=first;p+68<=last;p+=4) {
            if(LD32(p)!=1) continue;
            float x=LDF32(p+36),y=LDF32(p+40),z=LDF32(p+44);
            if(!isfinite(x)||!isfinite(z)||hypotf(x-LDF32(car+0x174),z-LDF32(car+0x178))>80) continue;
            unsigned callback=LD32(p+20);
            if(!race_valid(callback,12)) continue;
            for(unsigned m=0;m<sizeof k_mission_models/sizeof *k_mission_models;m++) {
                if(LD32(callback)!=k_mission_models[m].create) continue;
                char label[32];snprintf(label,sizeof label,"%s %u",k_mission_models[m].name,LD16(p+16));
                track_debug_item(&labels,x,y+2,z,label);
                if(k_mission_models[m].family==2 && (k_mission_models[m].kind==11 || k_mission_models[m].kind==12)) {
                    float distance=hypotf(x-LDF32(car+0x174),z-LDF32(car+0x178));
                    unsigned bounds=LD32(p+64);float scale=LDF32(p+12);
                    if(distance<nearest_solid && race_valid(bounds,28) && isfinite(scale)) {
                        nearest_solid=distance;
                        snprintf(solid_name,sizeof solid_name,"%s:%u W%.1f D%.1f H%.1f",k_mission_models[m].name,LD16(p+16),
                            (LDF32(bounds+16)-LDF32(bounds+4))*scale,
                            (LDF32(bounds+24)-LDF32(bounds+12))*scale,
                            (LDF32(bounds+20)-LDF32(bounds+8))*scale);
                    }
                }
                break;
            }
        }
    }
    for(unsigned i=0;i<sizeof solid_name;i++) atomic_store(&g_debug_solid_name[i],(unsigned char)toupper((unsigned char)solid_name[i]));
    atomic_store(&g_debug_item_count,labels);
    atomic_store(&g_debug_vehicle_parked,parked);
    atomic_store(&g_debug_vehicle_kind,kind);atomic_store(&g_debug_vehicle_index,index);
    atomic_store(&g_debug_vehicle_x,vx);atomic_store(&g_debug_vehicle_z,vz);
    atomic_store(&g_debug_vehicle_distance,nearest_vehicle);
    /* The camera hook captures the view after free-roam updates. */
    for (unsigned i=0;i<g_mission_run.gate_count;i++) {
        MissionGate g=g_mission_run.gates[i];
        float values[]={g.centre.x,g.centre.y,g.centre.z,g.nx,g.nz,g.half_width};
        for (unsigned j=0;j<6;j++) atomic_store(&g_debug_gate[i][j],values[j]);
        atomic_store(&g_debug_gate_height[i],g.half_height);atomic_store(&g_debug_gate_role[i],0);
        atomic_store(&g_debug_gate_tilt[i],g.tilt*57.2957795f);
    }
    atomic_store(&g_debug_gate_count,mission_engaged()?g_mission_run.gate_count:0);
    if(!mission_engaged() || enh_gate_editor_active()) gate_editor_publish(c);
    atomic_store(&g_debug_gate_mask,g_mission_run.met_mask);
    atomic_store(&g_debug_gate_any,g_mission_run.any_order);
}
typedef struct { uint32_t addr; const char *name; } Hook;
static const Hook k_hooks[] = GAME_ENH_HOOKS;
enum { HOOK_NONE, HOOK_ATTRACT, HOOK_PROJECTION, HOOK_VIEWPORT, HOOK_NAME_INDEX, HOOK_NAME_CONFIRM, HOOK_EXPLORER_CAMERA, HOOK_EXPLORER_RACE, HOOK_RACE_DISPATCH, HOOK_RACE_COUNTDOWN, HOOK_RACE_LAPS, HOOK_PRACTICE_HUD, HOOK_PRACTICE_HUD_END, HOOK_RACE_TIME_BONUS, HOOK_RACE_LAP_CLOCK, HOOK_PRACTICE_TEXT, HOOK_PRACTICE_CHECKPOINT, HOOK_SCENERY_VISIBILITY, HOOK_SCENERY_CELL_DISTANCE, HOOK_SCENERY_OBJECT_DISTANCE, HOOK_MISSION_BREAK, HOOK_MISSION_FURNITURE, HOOK_MISSION_SOLID, HOOK_MISSION_PROP, HOOK_MISSION_DODGE, HOOK_MISSION_DODGE_REACTIVE, HOOK_MISSION_SPAWN_FURNITURE, HOOK_MISSION_SPAWN_SOLID, HOOK_MISSION_SPAWN_PROP, HOOK_MISSION_WRONG_VOICE, HOOK_MISSION_WRONG_HUD, HOOK_MISSION_VEHICLE_DESTROY, HOOK_MISSION_DOG_SCARE, HOOK_MISSION_TOAST, HOOK_COURSE_CONFIRM, HOOK_COURSE_SIGN_31, HOOK_COURSE_SIGN_12, HOOK_COURSE_SIGN_30, HOOK_MISSION_TOUCH, HOOK_SCENERY_MODEL, HOOK_MISSION_SPAWN_BREAKABLE, HOOK_MISSION_FURNITURE_CONTACT, HOOK_MISSION_PLAYER_CONTACT, HOOK_MISSION_NATIVE_CORNER, HOOK_DEBUG_VIEW };

static int hook_kind(uint32_t pc) {
    static const char *const names[] = { "", "attract", "projection", "viewport", "name_index", "name_confirm", "explorer_camera", "explorer_race", "race_dispatch", "race_countdown", "race_laps", "practice_hud", "practice_hud_end", "race_time_bonus", "race_lap_clock", "practice_text", "practice_checkpoint", "scenery_visibility", "scenery_cell_distance", "scenery_object_distance", "mission_break", "mission_furniture", "mission_solid", "mission_prop", "mission_dodge", "mission_dodge_reactive", "mission_spawn_furniture", "mission_spawn_solid", "mission_spawn_prop", "mission_wrong_voice", "mission_wrong_hud", "mission_vehicle_destroy", "mission_dog_scare", "mission_toast", "course_confirm", "course_sign_31", "course_sign_12", "course_sign_30", "mission_touch", "scenery_model", "mission_spawn_breakable", "mission_furniture_contact", "mission_player_contact", "mission_native_corner", "debug_view" };
    for (const Hook *h = k_hooks; h->name; h++)
        if (h->addr == pc)
            for (int k = 1; k < (int)(sizeof names / sizeof names[0]); k++)
                if (!strcmp(h->name, names[k])) return k;
    return HOOK_NONE;
}

static void attract_hook(void) {
    if (g_enhanced) { race_restart_cancel(); if (mission_phase() != MISSION_ARMED) mission_cancel(); }
    g_attract_frame = g_frame ? g_frame : 1;
}

static void practice_text_hook(PPCContext *c);

void rt_hook(PPCContext *c, uint32_t pc) {
    switch (hook_kind(pc)) {
    /* GTI scenery has its own cell visibility table and per-cell/object ranges,
     * independent of the GL far plane. Expanded ranges must reach cells omitted
     * by the original table; the native range, cone and depth tests still apply. */
    case HOOK_COURSE_SIGN_31: course_reverse_sign(c,c->r[31]); break;
    case HOOK_COURSE_SIGN_12: course_reverse_sign(c,c->r[12]); break;
    case HOOK_COURSE_SIGN_30: course_reverse_sign(c,c->r[30]); break;
    case HOOK_SCENERY_VISIBILITY:
        if (g_enhanced && g_set.draw_distance) c->cr[0] &= ~2;
        break;
    case HOOK_SCENERY_CELL_DISTANCE:
        if (g_enhanced && g_set.draw_distance && isfinite(c->f[4]) && c->f[4] > 0)
            c->f[4] *= k_draw_distance[g_set.draw_distance];
        break;
    case HOOK_SCENERY_MODEL:
        if(atomic_load(&g_track_debug) && race_valid(c->r[3],28)) {
            unsigned object=c->r[3];float x=LDF32(object),z=LDF32(object+8);
            float px=atomic_load(&g_debug_x),pz=atomic_load(&g_debug_z);
            float ox=atomic_load(&g_debug_object_x),oz=atomic_load(&g_debug_object_z);
            if(isfinite(x) && isfinite(z) && (!atomic_load(&g_debug_object) || hypotf(x-px,z-pz)<hypotf(ox-px,oz-pz))) {
                atomic_store(&g_debug_object_x,x);atomic_store(&g_debug_object_y,LDF32(object+4));
                atomic_store(&g_debug_object_z,z);atomic_store(&g_debug_object,object);
                track_debug_object_name(c,object);
            }
        }
        break;
    case HOOK_SCENERY_OBJECT_DISTANCE:
        if (g_enhanced && g_set.draw_distance && isfinite(c->f[6]) && c->f[6] > 0)
            c->f[6] *= k_draw_distance[g_set.draw_distance];
        break;
    case HOOK_RACE_DISPATCH: if (g_enhanced) { course_reverse_dispatch(c); race_dispatch_hook(c); mission_dispatch_hook(c); } break;
    case HOOK_RACE_COUNTDOWN: if (g_enhanced) { race_countdown_hook(c); if (mission_engaged()) c->r[5] = (c->r[8] >> 6) & 0x3fff; } break;
    case HOOK_RACE_TIME_BONUS: if (g_enhanced) { race_time_bonus_hook(c); if (mission_engaged()) c->r[3] = 0; } break;
    case HOOK_RACE_LAP_CLOCK: if (g_enhanced) race_lap_clock_hook(c); break;
    case HOOK_PRACTICE_TEXT: if (g_enhanced && g_practice_text_ready) practice_text_hook(c); break;
    case HOOK_MISSION_BREAK: if(g_enhanced) mission_break_hook(c); break;
    case HOOK_MISSION_SPAWN_FURNITURE: if(g_enhanced) mission_target_spawn_hook(c,1); break;
    case HOOK_MISSION_SPAWN_SOLID: if(g_enhanced) mission_target_spawn_hook(c,2); break;
    case HOOK_MISSION_SPAWN_PROP: if(g_enhanced) mission_target_spawn_hook(c,3); break;
    case HOOK_MISSION_SPAWN_BREAKABLE: if(g_enhanced) mission_target_spawn_hook(c,5); break;
    case HOOK_MISSION_FURNITURE: if(g_enhanced) mission_target_hook(c,1); break;
    case HOOK_MISSION_NATIVE_CORNER: if(g_enhanced) mission_native_corner_hook(c); break;
    case HOOK_MISSION_PLAYER_CONTACT: if(g_enhanced) mission_player_contact_hook(c); break;
    case HOOK_MISSION_FURNITURE_CONTACT: if(g_enhanced) mission_authored_scenery_contact(c,1); break;
    case HOOK_MISSION_TOUCH:
        if(g_enhanced && mission_available()) {
            if(k_missions[g_mission_selected].scenery_touch) mission_target_hook(c,2);
            mission_authored_scenery_contact(c,2);
        }
        break;
    case HOOK_MISSION_SOLID: if(g_enhanced) mission_target_hook(c,2); break;
    case HOOK_MISSION_PROP: if(g_enhanced) mission_target_hook(c,3); break;
    case HOOK_MISSION_DOG_SCARE: if(g_enhanced) mission_dog_scare_hook(c); break;
    case HOOK_MISSION_DODGE: if(g_enhanced) mission_dodge_hook(c,c->r[28]); break;
    case HOOK_MISSION_DODGE_REACTIVE: if(g_enhanced) mission_dodge_hook(c,c->r[31]); break;
    case HOOK_PRACTICE_CHECKPOINT: if (g_enhanced) {
        if(!mission_engaged()) race_practice_checkpoint_hook(c);
        mission_checkpoint_hook(c);
        if(mission_engaged()) c->hook_return=1; /* Entry hook: host judges mission checkpoints. */
    } break;
    case HOOK_MISSION_TOAST: if(g_enhanced && mission_engaged()) c->hook_return=1; break;
    case HOOK_MISSION_WRONG_VOICE: if(g_enhanced && mission_engaged()) {
        c->r[3]=0;
        /* Course-arrow rendering reads the native wrong-way state directly,
         * independently of the voice and banner branches. Clear that flag while
         * retaining the checkpoint tracker that follows it in the same record. */
        if(race_valid(c->r[27],0x4dd)) ST8(c->r[27]+0x4dc,0);
    } break;
    case HOOK_MISSION_VEHICLE_DESTROY: if(g_enhanced) mission_vehicle_destroy_hook(c); break;
    case HOOK_MISSION_WRONG_HUD: if(g_enhanced && mission_engaged()) {
        c->r[5]=0;
        /* Clear the native interpolated warning opacity too: suppressing its
         * target alone leaves a warning from the approach/retry fading out. */
        if(race_valid(c->r[30],0x2c)) { ST8(c->r[30]+0x27,0); ST8(c->r[30]+0x28,0); }
    } break;
    case HOOK_RACE_LAPS: if (g_enhanced) { track_debug_tick(c); mission_tick(c); if (!mission_engaged() || atomic_load(&g_race_mission_practice)) race_laps_hook(c); } break;
    case HOOK_PRACTICE_HUD:
        if (g_enhanced) {
            race_practice_hud_hook(c);
            if (mission_engaged() && !g_practice_hud_state && race_valid(c->r[2],0xf8)) {
                uint32_t sprites=LD32(c->r[2]+0xf4);
                if (race_valid(sprites,0x166c)) {
                    g_practice_hud_state=sprites; g_practice_hud_colour=LD32(sprites+0x1668);
                    ST32(sprites+0x1668,0);
                }
            }
        }
        break;
    case HOOK_PRACTICE_HUD_END: if (g_enhanced) race_practice_hud_end_hook(c); break;
    case HOOK_EXPLORER_CAMERA: if (g_enhanced) {explorer_camera(c, g_frame);mission_finish_camera();track_debug_camera_capture();} break;
    case HOOK_EXPLORER_RACE: if (g_enhanced) { course_reverse_race(c); explorer_race(c); mission_race_hook(c); } break;
    case HOOK_COURSE_CONFIRM: if(g_enhanced) course_reverse_confirm(c); break;
    case HOOK_NAME_INDEX: if (g_enhanced) name_index_hook(c); break;
    case HOOK_NAME_CONFIRM: if (g_enhanced) name_confirm_hook(c); break;
    case HOOK_ATTRACT: attract_hook(); break;
    case HOOK_DEBUG_VIEW: if(g_enhanced) track_debug_view_capture(c); break;
    case HOOK_PROJECTION: {                  /* the current slot has just been written */
        uint32_t s = LD8(GAME_ENH_WIDE_PROJ_SLOT);
        if (!g_enhanced || s > 1) break;
        g_slot_k[s] = 1.0;g_slot_mirror[s]=0;
        distance_capture((int)s);mirror_slot((int)s);
        if (g_wide_k != 1.0) { widen_slot((int)s, g_wide_k); g_slot_k[s] = g_wide_k; }
        if (g_distance_slot[s].valid) track_debug_projection_capture(s);
        break;
    }
    case HOOK_VIEWPORT:
        if (!g_enhanced) break;
        g_vp_k = g_wide_k;
        if (g_wide_k != 1.0) STF32(GAME_ENH_WIDE_VIEWPORT, LDF32(GAME_ENH_WIDE_VIEWPORT) * g_wide_k);
        if (LD8(GAME_ENH_WIDE_PROJ_SLOT) < 2 && g_distance_slot[LD8(GAME_ENH_WIDE_PROJ_SLOT)].valid)
            track_debug_projection_capture(LD8(GAME_ENH_WIDE_PROJ_SLOT));
        break;
    default: break;
    }
}

typedef struct { uint32_t addr; const char *text; } BlankString;
static const BlankString k_blank[] = GAME_ENH_BLANK_STRINGS;

/* RT_ENH_BLANK="addr:text,addr:text" adds strings at run time (for finding new ones) */
static BlankString g_extra[16];
static int g_nextra = -1;

static void parse_extra(void) {
    g_nextra = 0;
    const char *e = getenv("RT_ENH_BLANK");
    while (e && *e && g_nextra < 16) {
        char *colon;
        uint32_t addr = (uint32_t)strtoul(e, &colon, 16);
        if (*colon != ':') break;
        const char *text = colon + 1, *end = strchr(text, ',');
        size_t n = end ? (size_t)(end - text) : strlen(text);
        char *copy = malloc(n + 1);
        memcpy(copy, text, n);
        copy[n] = 0;
        g_extra[g_nextra++] = (BlankString){ addr, copy };
        e = end ? end + 1 : text + n;
    }
}

static void blank(const BlankString *b) {
    size_t n = strlen(b->text);
    if (!n || b->addr + n >= RAM_SIZE) return;
    if (memcmp(g_ram + b->addr, b->text, n) == 0) g_ram[b->addr] = 0;
}

/* RT_NVRAM_POKE="seconds:addr=value,..." writes NVRAM bytes (and fixes the checksum) at run time */
static void nvram_poke_tick(void) {
    static const char *next = (const char *)-1;
    if (next == (const char *)-1) next = getenv("RT_NVRAM_POKE");
    while (next && *next) {
        char *p;
        double t = strtod(next, &p);
        if (*p != ':' || (double)rt_now() / CPU_HZ < t) return;
        uint32_t addr = (uint32_t)strtoul(p + 1, &p, 16);
        uint32_t val = *p == '=' ? (uint32_t)strtoul(p + 1, &p, 16) : 0;
        if (addr < 0x1ff0) { hw_nvram()[addr] = (uint8_t)val; hw_nvram_options_fix(hw_nvram()); rt_log("enhanced: NVRAM %04x = %02x\n", addr, val); }
        next = *p == ',' ? p + 1 : NULL;
    }
}

static void menu_tick(void);
static void scripted_menu(void);
static void fps_tick(const uint32_t *buf, int w, int h);
static int count_game_options(void);

/* called at every published frame, on the guest thread */
void enh_on_frame(const uint32_t *buf, int w, int h) {
    if (!g_enhanced) return;
    g_frame++;
    explorer_on_frame(g_frame);
    fps_tick(buf, w, h);
    if (g_enh_log < 0) g_enh_log = getenv("RT_ENH_LOG") != NULL;
    int attract = g_attract_frame && g_frame - g_attract_frame <= ATTRACT_GRACE_FRAMES;
    if (attract != g_attract && g_enh_log) rt_log("enhanced: %s\n", attract ? "attract mode" : "game in progress");
    g_attract = attract;
    wide_tick(g_set.aspect);
    if (GAME_ENH_WIDE_PROJ_MATRIX)
        for (int s = 0; s < 2; s++) distance_apply(s);
    menu_tick();
    scripted_menu();
    nvram_poke_tick();
    if (g_nextra < 0) parse_extra();
    for (const BlankString *b = k_blank; b->text; b++) blank(b);
    for (int i = 0; i < g_nextra; i++) blank(&g_extra[i]);
}

/* ================================================================== game font (A8 texture) */
/* The pages (A8 textures of one game file) are stacked into one atlas. Each size is a grid of
 * fixed cells with one entry per row of characters (a space marks an unused cell); the glyphs
 * themselves are proportional, so each one gets its ink width. Sizes are drawn at their scale,
 * sampling the atlas bilinearly (Thrill Drive 2 has a single size, drawn at three scales). */
typedef struct { int cw, ch; float scale; } FontSize;
typedef struct { int size, y; const char *chars; } FontRow;
static const FontSize k_font_sizes[] = GAME_ENH_FONT_SIZES;
static const FontRow k_font_rows[] = GAME_ENH_FONT_ROWS;
static const uint32_t k_font_pages[] = GAME_ENH_FONT_PAGES;
enum { FONT_LARGE, FONT_MEDIUM, FONT_SMALL, FONT_NSIZES };

typedef struct { int16_t x, y, w, h, twin; } Glyph;  /* ink bounds in the atlas; w = 0: missing;
                                                       twin: drawn again that many cell rows higher */
static uint8_t g_mission_icons[256*384*2];
static int g_mission_icons_ready;
static uint8_t *g_font;                             /* GAME_ENH_FONT_W x (pages * PAGE_H) alpha */
static int g_font_h;
static uint8_t g_system_font[128*128];
static int g_system_font_ready;
static Glyph g_glyph[FONT_NSIZES][128];

/* ================================================================== port settings */
/* <binary>_settings.ini next to the executable: options of the port itself (not of the game,
 * which keeps its own in the NVRAM). One "key = value" per line. */
void voodoo_set_scale(int n);
void voodoo_set_texture_filter(int mode);
static char g_settings_path[1024];

static void settings_load(void) {
    FILE *f = fopen(g_settings_path, "r");
    if (!f) return;
    char line[256], key[64];
    int v;
    while (fgets(line, sizeof line, f))
        if (sscanf(line, " %63[a-z_] = %d", key, &v) == 2) {
            if (!strcmp(key, "fullscreen")) g_set.fullscreen = v != 0;
            else if (!strcmp(key, "stick_response")) frontend_set_stick_response(v);
            else if (!strcmp(key, "switch_trigger_layout")) frontend_set_switch_trigger_layout(v);
            else if (!strcmp(key, "show_fps")) g_set.show_fps = v != 0;
            else if (!strcmp(key, "render_scale")) g_set.scale = v < 1 ? 1 : v > 2 ? 2 : v;
            else if (!strcmp(key, "texture_filter")) g_set.texture_filter = v >= 0 && v <= 2 ? v : 0;
            else if (!strcmp(key, "mirror")) g_set.mirror = v!=0;
            else if (!strcmp(key,"mission_cursor")) g_set.mission_cursor=v>=0 && v<=MISSION_COUNT+1 ? v : 0;
            else if (!strcmp(key, "draw_distance")) g_set.draw_distance = v >= 0 && v <= 2 ? v : 0;
            else if (!strcmp(key, "show_gyro")) g_set.show_gyro = v != 0;
            else if (!strcmp(key, "gyro")) frontend_gyro_set_enabled(v != 0);
            else if (!strcmp(key, "gyro_sensitivity")) frontend_gyro_set_sensitivity(v);
            else if (!strcmp(key, "rumble_multiplier")) frontend_set_rumble_multiplier(v);
            else if (!strcmp(key, "aspect")) g_set.aspect = v < 0 || v >= N_ASPECTS ? 0 : v;
        }
    fclose(f);
}

static void settings_save(void) {
    FILE *f = fopen(g_settings_path, "w");
    if (!f) { rt_log("enhanced: cannot write %s\n", g_settings_path); return; }
    fprintf(f, "# " GAME_TITLE ", enhanced mode: port settings\n");
    fprintf(f, "stick_response = %d\n", frontend_stick_response());
    fprintf(f, "switch_trigger_layout = %d\n", frontend_switch_trigger_layout());
    fprintf(f, "fullscreen = %d\nshow_fps = %d\nrender_scale = %d\n", g_set.fullscreen, g_set.show_fps, g_set.scale);
    fprintf(f, "# 0 = 4:3, 1 = 16:10, 2 = 16:9, 3 = 21:9\naspect = %d\n", g_set.aspect);
    fprintf(f, "gyro = %d\ngyro_sensitivity = %d\n", frontend_gyro_enabled(), frontend_gyro_sensitivity());
    fprintf(f, "show_gyro = %d\n", g_set.show_gyro);
    fprintf(f, "texture_filter = %d\n", g_set.texture_filter);
    fprintf(f,"mirror = %d\n",g_set.mirror);
    fprintf(f,"mission_cursor = %d\n",g_set.mission_cursor);
    fprintf(f, "# 0 = original, 1 = 2x, 2 = 4x draw distance\ndraw_distance = %d\n", g_set.draw_distance);
    fprintf(f, "rumble_multiplier = %d\n", frontend_rumble_multiplier());
    fclose(f);
}

void enh_controller_settings_changed(void) { if (g_enhanced) settings_save(); }

int enh_texture_filter(void) { return g_enhanced ? g_set.texture_filter : 0; }
int enh_want_fullscreen(void) { return g_enhanced && g_set.fullscreen; }
void enh_set_fullscreen(int on) { if (g_enhanced && g_set.fullscreen != !!on) { g_set.fullscreen = !!on; settings_save(); } }

void enh_init(const char *work, const char *settings) {
    atomic_store(&g_track_debug,getenv("RT_TRACK_DEBUG")!=NULL);
    if(getenv("RT_MIRROR")) g_set.mirror=1;
    if (!g_enhanced) return;
    count_game_options();
    mission_progress_init(settings);
    snprintf(g_settings_path, sizeof g_settings_path, "%s", settings);
    settings_load();
    g_mission_cursor=g_set.mission_cursor;
    voodoo_set_scale(g_set.scale);       /* the only place the render scale is set */
    set_aspect(g_set.aspect);
    voodoo_set_texture_filter(g_enhanced ? g_set.texture_filter : 0);
    if(GAME_ENH_MISSION_STYLE) {
        char font_path[1024];
        snprintf(font_path,sizeof font_path,"%s/fs/game/vram/include/f08x16-font.zin",work);
        FILE *system=fopen(font_path,"rb");
        g_system_font_ready=system && fread(g_system_font,1,sizeof g_system_font,system)==sizeof g_system_font;
        if(system) fclose(system);
    }
    const char *file = GAME_ENH_FONT_FILE;
    if (!file) return;
    char path[1024];
    g_practice_text_ready = 0;
    snprintf(path, sizeof path, "%s/fs/%s", work, file);
    int npages = (int)(sizeof k_font_pages / sizeof k_font_pages[0]);
    size_t page = (size_t)GAME_ENH_FONT_W * GAME_ENH_FONT_PAGE_H;
    uint8_t *atlas = malloc(page * npages);
    FILE *f = fopen(path, "rb");
    int ok = f != NULL;
    for (int i = 0; ok && i < npages; i++)
        ok = !fseek(f, (long)k_font_pages[i], SEEK_SET) && fread(atlas + page * i, 1, page, f) == page;
    if (f && GAME_ENH_MISSION_STYLE) g_mission_icons_ready=!fseek(f,0x3a0a8,SEEK_SET) && fread(g_mission_icons,1,256*256*2,f)==256*256*2 &&
        !fseek(f,0x2a09c,SEEK_SET) && fread(g_mission_icons+256*256*2,1,256*128*2,f)==256*128*2;
    if (f) fclose(f);
    if (!ok) { rt_log("enhanced: cannot read the menu font from %s\n", path); free(atlas); return; }
    g_font_h = GAME_ENH_FONT_PAGE_H * npages;
    for (const FontRow *r = k_font_rows; r->size >= 0; r++) {
        const FontSize *fs = &k_font_sizes[r->size];
        int col = 0;
        for (const char *p = r->chars; *p; p++, col++) {
            if (*p == ' ' || (unsigned char)*p >= 128) continue;
            int cx = col * fs->cw, lo = fs->cw, hi = -1;
            for (int x = 0; x < fs->cw && cx + x < GAME_ENH_FONT_W; x++)
                for (int y = 0; y < fs->ch && r->y + y < g_font_h; y++)
                    if (atlas[(r->y + y) * GAME_ENH_FONT_W + cx + x] > 40) { if (x < lo) lo = x; if (x > hi) hi = x; break; }
            if (hi >= lo) g_glyph[r->size][(unsigned char)*p] = (Glyph){ (int16_t)(cx + lo), (int16_t)r->y, (int16_t)(hi - lo + 1), (int16_t)fs->ch, 0 };
        }
    }
    for (int z = 0; z < FONT_NSIZES; z++)      /* no colon (Thrill Drive 2): two full stops */
        if (!g_glyph[z][':'].w && g_glyph[z]['.'].w) {
            g_glyph[z][':'] = g_glyph[z]['.'];
            g_glyph[z][':'].twin = (int16_t)(k_font_sizes[z].ch * 3 / 8);
        }
    g_font = atlas;
    g_practice_text_ready = GAME_ENH_PRACTICE_TEXT_RENDER != 0;
}

static int font_px(int z, int v) { return (int)(v * k_font_sizes[z].scale + 0.5f); }
static int font_height(int z) { return font_px(z, k_font_sizes[z].ch); }
static int glyph_space(int z) { return font_px(z, k_font_sizes[z].cw / 2); }
static int glyph_gap(int z) { return font_px(z, k_font_sizes[z].cw / 8 + 1); }
static int glyph_width(int z, const Glyph *g) { return font_px(z, g->w); }

static int text_width(int z, const char *s) {
    int w = 0;
    for (; *s; s++) {
        const Glyph *g = &g_glyph[z][(unsigned char)*s & 127];
        w += g->w ? glyph_width(z, g) + glyph_gap(z) : glyph_space(z);
    }
    return w;
}

static void blend(uint32_t *px, uint32_t rgb, int a) {
    uint32_t d = *px;
    int r = (((rgb >> 16) & 255) * a + ((d >> 16) & 255) * (255 - a)) / 255;
    int g = (((rgb >> 8) & 255) * a + ((d >> 8) & 255) * (255 - a)) / 255;
    int b = ((rgb & 255) * a + (d & 255) * (255 - a)) / 255;
    *px = 0xff000000u | (uint32_t)(r << 16) | (uint32_t)(g << 8) | (uint32_t)b;
}

/* alpha of glyph g at (u, v) in glyph pixels, bilinear */
static int glyph_alpha(const Glyph *g, float u, float v) {
    int x0 = (int)floorf(u), y0 = (int)floorf(v);   /* floor: u, v start at -1/4 at 2X */
    float fx = u - x0, fy = v - y0;
    int a[4];
    for (int k = 0; k < 4; k++) {
        int x = x0 + (k & 1), y = y0 + (k >> 1);
        a[k] = (x < 0 || y < 0 || x >= g->w || y >= g->h) ? 0 : g_font[(g->y + y) * GAME_ENH_FONT_W + g->x + x];
    }
    return (int)((a[0] * (1 - fx) + a[1] * fx) * (1 - fy) + (a[2] * (1 - fx) + a[3] * fx) * fy + 0.5f);
}

/* The overlay is laid out in logical units, 384 lines high (512 wide, or wider in widescreen);
 * on a scaled frame (resolution option) the primitives map them to the real pixels, g_ui real
 * pixels per logical one. */
static float g_ui = 1.0f;
static int g_fbw, g_fbh;

static void draw_text(uint32_t *fb, int w, int h, int z, int x, int y, const char *s, uint32_t rgb) {
    (void)w; (void)h;
    float inv = 1.0f / (k_font_sizes[z].scale * g_ui);
    float fx = x * g_ui;
    int py0 = (int)(y * g_ui + 0.5f);
    for (; *s; s++) {
        const Glyph *g = &g_glyph[z][(unsigned char)*s & 127];
        if(*s=='-' && !g->w) {
            /* Native atlases omit this glyph. A blank minus made negative
             * track-debug coordinates look positive. Keep its measured width. */
            int width=(int)(glyph_space(z)*g_ui+.5f);
            int height=(int)(font_height(z)*g_ui+.5f),stroke=height/10;
            if(stroke<1) stroke=1;
            int left=(int)(fx+.5f),top=py0+height/2,shadow=(int)(2*g_ui+.5f);
            for(int pass=0;pass<2;pass++) for(int dy=0;dy<stroke;dy++) for(int dx=0;dx<width;dx++) {
                int px=left+dx+(pass ? 0 : shadow),py=top+dy+(pass ? 0 : shadow);
                if(px>=0&&px<g_fbw&&py>=0&&py<g_fbh)
                    blend(&fb[py*g_fbw+px],pass ? rgb : 0,pass ? 255 : 153);
            }
            fx+=glyph_space(z)*g_ui;continue;
        }
        if (!g->w) { fx += glyph_space(z) * g_ui; continue; }
        int gw = (int)(glyph_width(z, g) * g_ui + 0.5f), gh = (int)(font_px(z, g->h) * g_ui + 0.5f);
        int px0 = (int)(fx + 0.5f), off0 = (int)(2 * g_ui + 0.5f);
        for (int pass = 0; pass < 4; pass++) {      /* drop shadow, then the glyph (and its twin) */
            int off = pass & 1 ? 0 : off0, up = pass & 2 ? (int)(font_px(z, g->twin) * g_ui + 0.5f) : 0;
            uint32_t col = pass & 1 ? rgb : 0x000000;
            if (up == 0 && pass >= 2) continue;
            for (int gy = 0; gy < gh; gy++) {
                int py = py0 + gy + off - up;
                if (py < 0 || py >= g_fbh) continue;
                for (int gx = 0; gx < gw; gx++) {
                    int px = px0 + gx + off;
                    if (px < 0 || px >= g_fbw) continue;
                    int a = inv == 1.0f ? g_font[(g->y + gy) * GAME_ENH_FONT_W + g->x + gx]
                                        : glyph_alpha(g, (gx + 0.5f) * inv - 0.5f, (gy + 0.5f) * inv - 0.5f);
                    if (!(pass & 1)) a = a * 3 / 5;
                    if (a) blend(&fb[py * g_fbw + px], col, a);
                }
            }
        }
        fx += (glyph_width(z, g) + glyph_gap(z)) * g_ui;
    }
}

static void draw_centered(uint32_t *fb, int w, int h, int z, int y, const char *s, uint32_t rgb) {
    draw_text(fb, w, h, z, (w - text_width(z, s)) / 2, y, s, rgb);
}

static void dim_rect(uint32_t *fb, int w, int h, int x0, int y0, int x1, int y1, int a) {
    (void)w; (void)h;
    int X0 = (int)(x0 * g_ui), Y0 = (int)(y0 * g_ui), X1 = (int)(x1 * g_ui), Y1 = (int)(y1 * g_ui);
    for (int y = Y0 < 0 ? 0 : Y0; y < Y1 && y < g_fbh; y++)
        for (int x = X0 < 0 ? 0 : X0; x < X1 && x < g_fbw; x++) blend(&fb[y * g_fbw + x], 0x000000, a);
}

/* ================================================================== fps counter */
/* frames the game drew (Voodoo buffer swaps) per emulated second: 30 on these games. Distinct
 * pictures would undercount static screens and slow fades. */
static volatile int g_fps;

static void fps_tick(const uint32_t *buf, int w, int h) {
    (void)buf; (void)w; (void)h;
    static unsigned long long last_swaps;
    static uint64_t last_sec;
    uint64_t sec = rt_now() / (uint64_t)CPU_HZ;
    if (sec != last_sec) {
        unsigned long long n = voodoo_swap_count();
        g_fps = (int)(n - last_swaps);
        last_swaps = n;
        last_sec = sec;
    }
}

/* ================================================================== game options (NVRAM fields) */
/* The TEST MODE settings the OPTIONS pages edit: a field of `bits` at `shift` in the byte (size 1)
 * or big-endian word (size 2) at `addr` of the option block. Values run from min to max; `values`
 * holds "english|italian" labels, one per line (NULL: shown as numbers). */
typedef struct {
    int page; const char *label_en, *label_it; uint32_t addr; int size, shift, bits, min, max, language;
    const char *values;
} GameOption;
static const GameOption k_game_options[] = GAME_ENH_GAME_OPTIONS;
#define MAX_GAME_OPTIONS 16
static int g_n_game_options;
static volatile int g_opt_value[MAX_GAME_OPTIONS];   /* staged values, edited by the menu */
static volatile int g_opt_dirty;

static int count_game_options(void) {
    while (g_n_game_options < MAX_GAME_OPTIONS && k_game_options[g_n_game_options].page >= 0) g_n_game_options++;
    return g_n_game_options;
}

static uint32_t field_get(const uint8_t *nv, const GameOption *o) {
    uint32_t v = o->size == 2 ? (uint32_t)(nv[o->addr] << 8 | nv[o->addr + 1]) : nv[o->addr];
    return (v >> o->shift) & ((1u << o->bits) - 1);
}

static void field_set(uint8_t *nv, const GameOption *o, uint32_t val) {
    uint32_t mask = ((1u << o->bits) - 1) << o->shift;
    uint32_t v = o->size == 2 ? (uint32_t)(nv[o->addr] << 8 | nv[o->addr + 1]) : nv[o->addr];
    v = (v & ~mask) | ((val << o->shift) & mask);
    if (o->size == 2) { nv[o->addr] = (uint8_t)(v >> 8); nv[o->addr + 1] = (uint8_t)v; }
    else nv[o->addr] = (uint8_t)v;
}

/* the label of value v in language lang (0 English, 1 Italian) */
static const char *option_value_text(const GameOption *o, int v, int lang, char *buf, size_t n) {
    if (!o->values) { snprintf(buf, n, "%d", v); return buf; }
    const char *p = o->values;
    for (int i = o->min; i < v && p; i++) { p = strchr(p, '\n'); if (p) p++; }
    if (!p) { snprintf(buf, n, "%d", v); return buf; }
    const char *bar = strchr(p, '|'), *end = strchr(p, '\n');
    if (!end) end = p + strlen(p);
    const char *s0 = lang && bar && bar < end ? bar + 1 : p, *s1 = lang || !bar || bar > end ? end : bar;
    snprintf(buf, n, "%.*s", (int)(s1 - s0), s0);
    return buf;
}

static void options_read(void) {
    const uint8_t *nv = hw_nvram();
    for (int i = 0; i < g_n_game_options; i++) {
        int v = (int)field_get(nv, &k_game_options[i]);
        if (v < k_game_options[i].min || v > k_game_options[i].max) v = k_game_options[i].min;
        g_opt_value[i] = v;
    }
    g_opt_dirty = 0;
}

/* ================================================================== texts (English / Italian) */
/* The menus follow the game's language option (value 2 = Italian). The fonts have no accented
 * letters, so the Italian texts avoid them. */
enum { T_START, T_OPTIONS, T_CREDITS, T_QUIT, T_GAME, T_SOUND, T_DISPLAY, T_BACK, T_WINDOW, T_FULLSCREEN,
       T_SHOW_FPS, T_OFF, T_ON, T_LOADING, T_APPLYING, T_ORIGINAL_GAME, T_RECOMPILATION, T_VOODOO,
       T_PRESS_START_BACK, T_PAUSE, T_RESUME, T_MAIN_MENU, T_RESOLUTION, T_ASPECT, T_STICK_RESPONSE, T_CONTROLS, T_GYRO, T_RECENTER, T_SHOW_GYRO, T_RUMBLE, T_TEXTURE_FILTER, T_SWITCH_TRIGGERS, T_RESTART_RACE, T_UNLIMITED_LAPS, T_DRAW_DISTANCE, T_MISSIONS, T_TRACK_DEBUG, T_MIRROR, T_MISSION_PRACTICE, T_COUNT };
static const char *const k_text[T_COUNT][2] = {
    { "START GAME", "INIZIA PARTITA" }, { "OPTIONS", "OPZIONI" }, { "CREDITS", "RICONOSCIMENTI" },
    { "QUIT", "ESCI" }, { "GAME", "GIOCO" }, { "SOUND", "AUDIO" }, { "DISPLAY", "SCHERMO" },
    { "BACK", "INDIETRO" }, { "WINDOW", "FINESTRA" }, { "FULLSCREEN", "SCHERMO INTERO" },
    { "SHOW FPS", "MOSTRA FPS" }, { "OFF", "NO" }, { "ON", "SI" }, { "LOADING", "CARICAMENTO" },
    { "APPLYING SETTINGS", "APPLICAZIONE IMPOSTAZIONI" }, { "ORIGINAL GAME", "GIOCO ORIGINALE" },
    { "STATIC RECOMPILATION", "RICOMPILAZIONE STATICA" }, { "VOODOO GRAPHICS CORE", "GRAFICA VOODOO" },
    { "PRESS START TO GO BACK", "PREMI START PER TORNARE" }, { "PAUSE", "PAUSA" },
    { "RESUME", "RIPRENDI" }, { "MAIN MENU", "MENU PRINCIPALE" }, { "RESOLUTION", "RISOLUZIONE" },
    { "ASPECT RATIO", "FORMATO" },
    { "STICK RESPONSE", "RISPOSTA STICK" },
    { "CONTROLS", "COMANDI" }, { "GYRO SENSITIVITY", "STERZO GIROSCOPIO" },
    { "RECENTER GYRO", "RICENTRA" },
    { "SHOW STEERING METER", "MOSTRA IN GIOCO" },
    { "RUMBLE STRENGTH", "VIBRAZIONE" },
    { "TEXTURE FILTER", "FILTRO TEXTURE" },
    { "SWITCH ZL", "GRILLETTO ZL" },
    { "RESTART RACE", "RICOMINCIA GARA" },
    { "UNLIMITED LAPS", "GIRI ILLIMITATI" },
    { "DRAW DISTANCE", "DISTANZA VISUALE" },
    { "MISSIONS", "MISSIONI" },
    { "TRACK DEBUG", "DEBUG PISTA" },
    { "MIRROR COURSE", "PERCORSO SPECULARE" },
    { "EXPLORE IN TIME ATTACK", "ESPLORA IN TIME ATTACK" },
};
static const char *const k_texture_filter_name[] = { "ORIGINAL", "NEAREST", "BILINEAR" };
static const char *const k_draw_distance_name[] = { "ORIGINAL", "2X", "4X" };
static const char *const k_stick_response_name[] = { "LINEAR", "SOFT", "EXTRA SOFT" };
static const char *const k_aspect_name[N_ASPECTS] = { "4:3", "16:10", "16:9", "21:9" };

static int menu_language(void) {
    for (int i = 0; i < g_n_game_options; i++)
        if (k_game_options[i].language) return g_opt_value[i] == 2;
    return 0;
}
#define T(id) k_text[id][menu_language()]

/* ================================================================== attract menu */
/* Shown over the attract mode. The frontend (host main thread) sends the actions and draws
 * the overlay; the guest thread updates the attract state. Plain ints are enough: each field
 * has a single writer. */
enum { SCREEN_MAIN, SCREEN_OPTIONS, SCREEN_PAGE, SCREEN_CREDITS, SCREEN_MISSIONS };
enum { PAGE_GAME, PAGE_SOUND, PAGE_DISPLAY, PAGE_CONTROLS, N_PAGES };
static const int k_main_items[] = { T_START, T_OPTIONS, T_CREDITS, T_QUIT };
static int main_item(int index) {
    if (mission_available()) {
        if (index==1) return T_MISSIONS;
        if (index>1) index--;
    }
    return k_main_items[index];
}
#define N_MAIN_ITEMS (mission_available() ? 5 : 4)
static int g_mission_pause_menu;
static int g_mission_clear_confirm, g_mission_clear_yes, g_mission_clear_error;
#define MENU_GRACE_FRAMES 300      /* the Konami logo gap in the attract loop lasts about 4 s */
#define START_HOLD_FRAMES 12

static volatile int g_screen, g_cursor, g_quit;
static volatile int g_opt_cursor, g_page, g_page_cursor;
static volatile int g_start_hold;          /* frames START is still held for the game */
static volatile int g_starting;            /* START GAME chosen, waiting for the game to begin */
static volatile uint64_t g_starting_frame;
static volatile int g_apply;               /* 1: write the staged options, 2: written, restart */
static volatile int g_paused, g_pause_cursor;
static int g_pause_controls, g_controls_cursor, g_pause_practice;
static int g_mission_after_return;
static volatile int g_returning;           /* MAIN MENU from the pause: back to the attract */
static uint64_t g_return_t0;
static volatile int g_booted;              /* the attract hook has run once since the start */
static int g_headless;

void enh_set_headless(int on) { g_headless = on; }

/* until the game reaches the attract mode, and while applying settings, the frontend runs the
 * emulation unpaced and muted behind a LOADING screen */
#define BOOT_TURBO_LIMIT 60          /* emulated seconds: never keep a LOADING screen forever */
int enh_turbo(void) {
#ifdef GAME_ENH_HOOK_ATTRACT
    if (!g_enhanced || !g_font) return 0;
    if (g_apply || g_returning || race_restart_pending() || mission_phase()==MISSION_ARMED) return 1;
    return !g_booted && rt_now() < (uint64_t)BOOT_TURBO_LIMIT * CPU_HZ;
#else
    return 0;
#endif
}
int enh_restart_requested(void) { return g_apply == 2; }

/* ------------------------------------------------------------------ pause (Esc in play) */
int enh_paused(void) { return g_paused || mission_result(); }
int enh_inputs_owned(void) { return enh_gate_editor_active() || g_returning || race_restart_pending() || mission_phase() == MISSION_COUNTDOWN || mission_phase() == MISSION_ROLLING || mission_phase()==MISSION_PASSED || mission_result(); }

/* Esc from the frontend: 1 if the enhanced mode handled it (pause, or back in a submenu) */
int enh_escape(void) {
    if (!g_enhanced || !g_font || g_apply || g_returning || race_restart_pending() || !g_booted) return 0;
    if (mission_result() && !g_paused) {
        g_paused=1; g_pause_cursor=0; g_pause_controls=0; g_mission_pause_menu=0;
        return 1;
    }
    if (g_paused && g_mission_pause_menu) { g_mission_pause_menu = 0; return 1; }
    if (g_paused) { if (g_pause_controls) g_pause_controls = 0; else g_paused = 0; return 1; }
    if (enh_menu_active()) {
        if (g_screen == SCREEN_MAIN) return 0;      /* the main menu: Esc quits, as before */
        enh_menu_action(ENH_BACK);
        return 1;
    }
    if (enh_turbo() || g_starting) return 1;
    g_paused = 1;
    g_pause_cursor = 0;
    g_pause_controls = 0;
    extern uint8_t g_in[8];
    g_pause_practice = race_time_trial() &&
        (g_headless ? !(g_in[4] & 1) : frontend_shift_up_held());
    return 1;
}

void enh_focus_lost(void) {
    if (!atomic_load(&g_track_debug) && !g_paused && !enh_menu_active() && !enh_in_attract()) enh_escape();
}

static void controls_change(int row, int dir) {
    if (row == 0) {
        if (!frontend_gyro_enabled()) {
            if (dir > 0) frontend_gyro_set_enabled(1);
        } else if (dir < 0 && frontend_gyro_sensitivity() <= 50) {
            frontend_gyro_set_enabled(0);
        } else {
            frontend_gyro_set_sensitivity(frontend_gyro_sensitivity() + dir * 10);
        }
    } else if (row == 1) g_set.show_gyro = !g_set.show_gyro;
    else if (row == 2) frontend_gyro_recenter();
    else if (row == 3) frontend_set_rumble_multiplier(frontend_rumble_multiplier() + dir * 50);
    else if (row == 4) frontend_set_stick_response((frontend_stick_response() + dir + 3) % 3);
    else if (row == 5) frontend_set_switch_trigger_layout(!frontend_switch_trigger_layout());
    if (row != 2) settings_save();
}

static const int k_controls_order[] = { -1, 3, 0, 1, 2, 4, 5 }; /* Back, rumble, gyro, overlay, recenter, stick, triggers */
enum { N_CONTROLS = GAME_HAS_HANDBRAKE ? 7 : 6 };

static int pause_items(int *items) {
    int n = 0;
    items[n++] = T_RESUME;
    items[n++] = T_CONTROLS;
    if (race_restart_available()) items[n++] = T_RESTART_RACE;
    if (mission_engaged()) items[n++] = T_MISSION_PRACTICE;
    if (g_pause_practice && race_time_trial() && !mission_engaged()) items[n++] = T_UNLIMITED_LAPS;
    if (mission_available() && race_restart_available()) items[n++] = T_MISSIONS;
    if (mission_available() && (race_time_trial() || mission_engaged())) items[n++] = T_TRACK_DEBUG;
    items[n++] = T_MAIN_MENU;
    return n;
}

/* A mission retry needs the complete native teardown, not a partial restoration
 * of pre-race control records: traffic and timing own additional live state. */
static void mission_selector_reload(void);
static int g_mission_reload_force;
static void mission_reload(int index) {
    unsigned selected=mission_identity((unsigned)index);
    mission_cancel();
    atomic_store(&g_mission_explore_request,0);atomic_store(&g_mission_exploring,0);
    atomic_store(&g_track_debug,0);
    if(explorer_active()) {if(explorer_free()) explorer_free_toggle();else explorer_toggle();}
    g_mission_reload_force=1;mission_selector_reload();
    for(int i=0;i<MISSION_COUNT;i++) if(selected==mission_identity((unsigned)i)) {index=i;break;}
    if(index>=MISSION_COUNT) index=MISSION_COUNT-1;
    mission_request(index);
    race_restart_cancel();
    g_mission_after_return=1; g_returning=1; g_return_t0=0;
    g_mission_pause_menu=0; g_paused=0;
}

int enh_mission_retry(void) {
    if(!g_enhanced || enh_gate_editor_active() || !mission_engaged() || !race_restart_available() ||
       g_returning || g_mission_pause_menu) return 0;
    mission_reload(atomic_load(&g_mission_selected));
    return 1;
}

static int g_mission_time_save_error;
static void mission_remember_cursor(void) {
    if(g_set.mission_cursor!=g_mission_cursor) {
        g_set.mission_cursor=g_mission_cursor;settings_save();
    }
}
static int mission_page_action(int action) {
    if(action!=ENH_PAGE_UP && action!=ENH_PAGE_DOWN) return 0;
    int pages=(MISSION_COUNT+11)/10,row=g_mission_cursor%10;
    int page=(g_mission_cursor/10+(action==ENH_PAGE_DOWN ? 1 : pages-1))%pages;
    g_mission_cursor=page*10+row;
    if(g_mission_cursor>MISSION_COUNT+1) g_mission_cursor=MISSION_COUNT+1;
    mission_remember_cursor();
    return 1;
}
static int mission_time_action(int action) {
    if((action!=ENH_LEFT && action!=ENH_RIGHT) || g_mission_cursor>=MISSION_COUNT) return 0;
    MissionDefinition *d=&k_missions[g_mission_cursor];
    unsigned gold=d->gold_ms ? d->gold_ms : 10000;
    if(d->gold_ms) gold=action==ENH_LEFT ? (gold>1000 ? gold-1000 : 1000) : gold+1000;
    if(gold>3596000) gold=3596000;
    g_mission_time_save_error=!mission_csv_save_gold(d->name,gold);
    if(!g_mission_time_save_error) { d->gold_ms=gold; d->silver_ms=gold+2000; d->limit_ms=gold+4000; }
    return 1;
}
static int mission_clear_action(int action) {
    if(g_mission_clear_confirm) {
        if(action==ENH_BACK) g_mission_clear_confirm=0;
        else if(action==ENH_UP || action==ENH_DOWN || action==ENH_LEFT || action==ENH_RIGHT)
            g_mission_clear_yes=!g_mission_clear_yes;
        else if(action==ENH_OK) {
            if(g_mission_clear_yes) g_mission_clear_error=!mission_clear_completion();
            g_mission_clear_confirm=0;
        }
        return 1;
    }
    if(action==ENH_OK && g_mission_cursor==MISSION_COUNT) {
        g_mission_clear_confirm=1; g_mission_clear_yes=0; g_mission_clear_error=0;
        return 1;
    }
    return 0;
}
static void pause_action(int action) {
    if (g_mission_pause_menu) {
        if(mission_clear_action(action)) return;
        if(mission_time_action(action)) return;
        if(mission_page_action(action)) return;
        if (action == ENH_UP) g_mission_cursor = (g_mission_cursor + MISSION_COUNT+1) % (MISSION_COUNT+2);
        if (action == ENH_DOWN) g_mission_cursor = (g_mission_cursor + 1) % (MISSION_COUNT+2);
        if(action==ENH_UP || action==ENH_DOWN) mission_remember_cursor();
        if (action == ENH_BACK || (action == ENH_OK && g_mission_cursor == MISSION_COUNT+1)) g_mission_pause_menu=0;
        else if (action == ENH_OK) {
            atomic_store(&g_race_unlimited_laps,0);
            mission_reload(g_mission_cursor);
        }
        return;
    }
    if (g_pause_controls) {
        switch (action) {
        case ENH_UP: g_controls_cursor = (g_controls_cursor + N_CONTROLS - 1) % N_CONTROLS; break;
        case ENH_DOWN: g_controls_cursor = (g_controls_cursor + 1) % N_CONTROLS; break;
        case ENH_BACK: g_pause_controls = 0; break;
        case ENH_OK:
            if (g_controls_cursor == 0) { g_pause_controls = 0; break; }
            controls_change(k_controls_order[g_controls_cursor], 1);
            break;
        case ENH_LEFT: case ENH_RIGHT:
            if (g_controls_cursor > 0 && g_controls_cursor != 4) controls_change(k_controls_order[g_controls_cursor], action == ENH_LEFT ? -1 : 1);
            break;
        default: break;
        }
        return;
    }
    int items[8], count = pause_items(items);
    switch (action) {
    case ENH_UP: g_pause_cursor = (g_pause_cursor + count - 1) % count; break;
    case ENH_DOWN: g_pause_cursor = (g_pause_cursor + 1) % count; break;
    case ENH_BACK: g_paused = 0; break;
    case ENH_LEFT: case ENH_RIGHT:
        if (items[g_pause_cursor] == T_TRACK_DEBUG) enh_track_debug_toggle();
        else if (items[g_pause_cursor] == T_UNLIMITED_LAPS) race_unlimited_toggle();
        break;
    case ENH_OK:
        if (g_pause_cursor == 0) g_paused = 0;
        else if (g_pause_cursor == 1) { g_pause_controls = 1; g_controls_cursor = 0; }
        else if (items[g_pause_cursor] == T_RESTART_RACE) {
            if (explorer_active()) { if (explorer_free()) explorer_free_toggle(); else explorer_toggle(); }
            if (mission_engaged() || atomic_load(&g_mission_exploring)) mission_reload(atomic_load(&g_mission_selected));
            else { race_restart_request(); g_paused = 0; }
        }
        else if (items[g_pause_cursor] == T_MISSION_PRACTICE) {
            mission_cancel();
            atomic_store(&g_mission_explore_request,1);
            atomic_store(&g_track_debug,1);
            g_paused=0;
        }
        else if (items[g_pause_cursor] == T_TRACK_DEBUG) enh_track_debug_toggle();
        else if (items[g_pause_cursor] == T_UNLIMITED_LAPS) race_unlimited_toggle();
        else if (items[g_pause_cursor] == T_MISSIONS) {
            mission_cancel();g_mission_pause_menu=1;
        }
        else {
            mission_cancel(); race_restart_cancel();
            if (GAME_ENH_TEST_GAME_MODE >= 0) { g_returning = 1; g_return_t0 = 0; }
            else g_apply = 2;                         /* no known route: reboot the game */
            g_paused = 0;
        }
        break;
    default: break;
    }
}

int enh_menu_active(void) {
    if (!g_enhanced || !g_font || g_starting || g_apply || g_returning || race_restart_pending() || g_paused) return 0;
    return g_attract_frame && g_frame - g_attract_frame <= MENU_GRACE_FRAMES;
}

int enh_start_held(void) { return g_start_hold > 0; }
int enh_quit_requested(void) { return g_quit; }

/* the rows of an options page: game options of that page, or the port options (DISPLAY) */
static int page_rows(int page, int *rows) {
    int n = 0;
    if (page == PAGE_CONTROLS) {
        rows[n++] = -5; rows[n++] = -6; rows[n++] = -7; rows[n++] = -8; rows[n++] = -9;
        if (GAME_HAS_HANDBRAKE) rows[n++] = -11;
        return n;
    }
    if (page == PAGE_DISPLAY) {
        rows[n++] = -1;
        rows[n++] = -3;
        if (GAME_ENH_WIDE_VIEWPORT) rows[n++] = -4;
        rows[n++] = -10;
#ifdef GAME_ENH_HOOK_SCENERY_CELL_DISTANCE
        rows[n++] = -12;
#endif
        rows[n++] = -2;
        return n;
    }
    for (int i = 0; i < g_n_game_options; i++)
        if (k_game_options[i].page == page) rows[n++] = i;
    if(page==PAGE_GAME && GAME_ENH_RACE_RESTART_STYLE==1) rows[n++]=-13;
    return n;
}

static void page_change(int row, int dir) {
    if(row==-13) {g_set.mirror=!g_set.mirror;settings_save();return;}
    if (row == -12) {
        g_set.draw_distance = (g_set.draw_distance + dir + 3) % 3;
        settings_save();
        return;
    }
    if (row == -10) {
        g_set.texture_filter = (g_set.texture_filter + dir + 3) % 3;
        voodoo_set_texture_filter(g_set.texture_filter);
        settings_save();
        return;
    }
    if (row <= -5 && row >= -8) { controls_change(-row - 5, dir); return; }
    if (row == -1) { g_set.fullscreen = !g_set.fullscreen; settings_save(); return; }
    if (row == -2) { g_set.show_fps = !g_set.show_fps; settings_save(); return; }
    if (row == -3) { g_set.scale = g_set.scale == 1 ? 2 : 1; voodoo_set_scale(g_set.scale); settings_save(); return; }
    if (row == -4) { g_set.aspect = (g_set.aspect + dir + N_ASPECTS) % N_ASPECTS; set_aspect(g_set.aspect); settings_save(); return; }
    const GameOption *o = &k_game_options[row];
    int v = g_opt_value[row] + dir, span = o->max - o->min + 1;
    g_opt_value[row] = o->min + ((v - o->min) % span + span) % span;
    g_opt_dirty = 1;
}

static void leave_options(void) {
    g_screen = SCREEN_MAIN;
    if (g_opt_dirty) g_apply = 1;            /* the guest thread writes them and restarts */
}

void enh_menu_action(int action) {
    if (g_paused) { pause_action(action); return; }
    if (mission_result()) {
        if (action == ENH_OK && race_restart_available()) {
            mission_reload(atomic_load(&g_mission_selected));
        } else if (action == ENH_BACK) {
            mission_cancel(); g_paused=1; g_mission_pause_menu=1;
        }
        return;
    }
    if (!enh_menu_active()) return;
    if (g_screen == SCREEN_MISSIONS) {
        if(mission_clear_action(action)) return;
        if(mission_time_action(action)) return;
        if(mission_page_action(action)) return;
        if (action == ENH_UP) g_mission_cursor=(g_mission_cursor+MISSION_COUNT+1)%(MISSION_COUNT+2);
        if (action == ENH_DOWN) g_mission_cursor=(g_mission_cursor+1)%(MISSION_COUNT+2);
        if(action==ENH_UP || action==ENH_DOWN) mission_remember_cursor();
        if (action == ENH_BACK || (action == ENH_OK && g_mission_cursor==MISSION_COUNT+1)) g_screen=SCREEN_MAIN;
        else if (action == ENH_OK) {
            mission_request(g_mission_cursor); g_starting=1; g_starting_frame=g_frame; g_start_hold=START_HOLD_FRAMES;
        }
        return;
    }
    if (g_screen == SCREEN_PAGE) {
        int rows[MAX_GAME_OPTIONS + 2], n = page_rows(g_page, rows);
        if (g_page == PAGE_CONTROLS && (action == ENH_LEFT || action == ENH_RIGHT || action == ENH_OK)) {
            if (g_page_cursor == 0) {
                if (action == ENH_OK) g_screen = SCREEN_OPTIONS;
            } else if (g_page_cursor != 4 || action == ENH_OK) {
                controls_change(k_controls_order[g_page_cursor], action == ENH_LEFT ? -1 : 1);
            }
            return;
        }
        switch (action) {
        case ENH_UP: g_page_cursor = (g_page_cursor + n) % (n + 1); break;
        case ENH_DOWN: g_page_cursor = (g_page_cursor + 1) % (n + 1); break;
        case ENH_BACK: g_screen = SCREEN_OPTIONS; break;
        case ENH_LEFT: case ENH_RIGHT: case ENH_OK:
            if (g_page_cursor == n) { if (action == ENH_OK) g_screen = SCREEN_OPTIONS; }
            else if (rows[g_page_cursor] != -7 || action == ENH_OK) page_change(rows[g_page_cursor], action == ENH_LEFT ? -1 : 1);
            break;
        default: break;
        }
        return;
    }
    if (g_screen == SCREEN_OPTIONS) {
        switch (action) {
        case ENH_UP: g_opt_cursor = (g_opt_cursor + N_PAGES) % (N_PAGES + 1); break;
        case ENH_DOWN: g_opt_cursor = (g_opt_cursor + 1) % (N_PAGES + 1); break;
        case ENH_BACK: leave_options(); break;
        case ENH_OK:
            if (g_opt_cursor == N_PAGES) leave_options();
            else { g_page = g_opt_cursor; g_page_cursor = 0; g_screen = SCREEN_PAGE; }
            break;
        default: break;
        }
        return;
    }
    if (g_screen == SCREEN_CREDITS) {
        if (action == ENH_OK || action == ENH_BACK) g_screen = SCREEN_MAIN;
        return;
    }
    switch (action) {
    case ENH_UP: g_cursor = (g_cursor + N_MAIN_ITEMS - 1) % N_MAIN_ITEMS; break;
    case ENH_DOWN: g_cursor = (g_cursor + 1) % N_MAIN_ITEMS; break;
    case ENH_OK:
        if (main_item(g_cursor) == T_START) { mission_cancel(); g_starting = 1; g_starting_frame = g_frame; g_start_hold = START_HOLD_FRAMES; }
        else if (main_item(g_cursor) == T_OPTIONS) { g_screen = SCREEN_OPTIONS; g_opt_cursor = 0; options_read(); }
        else if (main_item(g_cursor) == T_CREDITS) g_screen = SCREEN_CREDITS;
        else if (main_item(g_cursor) == T_MISSIONS) { g_screen=SCREEN_MISSIONS; }
        else g_quit = 1;
        break;
    default: break;
    }
}

/* per-frame menu bookkeeping, on the guest thread (called from enh_on_frame) */
extern uint8_t g_in[8];
extern int16_t g_analog[4];
void nvram_save(void);

static void menu_tick(void) {
    if(enh_gate_editor_active()) {
        g_analog[0]=0;g_analog[1]=-200;g_analog[2]=g_analog[3]=200;
        g_in[3]=g_in[4]=0xff;return;
    }
    /* SDL publishes the live held controls when ownership ends. Only headless
     * runs need neutral inputs; a guest-side clear can race that first publish. */
    if (atomic_exchange(&g_mission_auto_release,0) && g_headless) {
        g_analog[0]=0; g_analog[1]=g_analog[2]=g_analog[3]=-200;
        g_in[3]=g_in[4]=0xff;
    }
    if (mission_phase() == MISSION_COUNTDOWN || mission_phase()==MISSION_PASSED || mission_result()) {
        g_analog[0]=0; g_analog[1]=g_analog[2]=g_analog[3]=-200;
        g_in[3]=g_in[4]=0xff;
    }
    if (mission_phase()==MISSION_ROLLING) {
        g_analog[0]=(int16_t)atomic_load(&g_mission_auto_steer);
        g_analog[1]=200; g_analog[2]=g_analog[3]=-200;
        g_in[3]=g_in[4]=0xff;
    }
    if (race_restart_pending()) {
        g_in[3] = g_in[4] = 0xff;
        g_analog[0] = 0;
        g_analog[1] = g_analog[2] = g_analog[3] = -200;
    }
    if (g_attract_frame && !g_booted) { g_booted = 1; options_read(); }
    /* START GAME presses START for a few frames (IN3 bit 4, active low); the SDL frontend
     * rewrites IN3 every loop and also honours enh_start_held(), headless runs rely on this */
    if (g_start_hold > 0) {
        g_in[3] &= (uint8_t)~0x10;
        if (--g_start_hold == 0) g_in[3] |= 0x10;
    }
    if (g_starting) {
        /* the game has begun once the attract hook stops; if it keeps running (START ignored,
         * e.g. during the boot screens), give the menu back after a few seconds */
        if (g_frame - g_attract_frame > 60) { g_starting = 0; g_screen = SCREEN_MAIN; g_cursor = 0; g_attract_frame = 0; }
        else if (g_frame - g_starting_frame > 240) g_starting = 0;
    }
    if (g_returning) {
        /* as on a cabinet: TEST opens TEST MODE, GAME MODE leaves it for the attract. All of it
         * runs fast-forwarded behind the loading screen; a reboot is the fallback. */
        uint64_t now = rt_now();
        if (!g_return_t0) { g_return_t0 = now; g_attract_frame = 0; }
        double t = (double)(now - g_return_t0) / CPU_HZ, t_start = 12.0 + GAME_ENH_TEST_GAME_MODE * 0.8 + 1.0;
        uint8_t in3 = 0xff, in4 = 0xff;
        if (t < 0.4) in3 &= (uint8_t)~0x02;                                   /* TEST */
        for (int k = 0; k < GAME_ENH_TEST_GAME_MODE; k++)
            if (t >= 12.0 + k * 0.8 && t < 12.3 + k * 0.8) in4 &= (uint8_t)~0x01;   /* SHIFT UP */
        if (t >= t_start && t < t_start + 0.3) in3 &= (uint8_t)~0x10;          /* START on GAME MODE */
        g_in[3] = in3;
        g_in[4] = in4;
        if (t > t_start + 0.5 && g_attract_frame) {
            g_returning = 0; g_screen = SCREEN_MAIN; g_cursor = 0;
            rt_log("enhanced: back to the attract mode\n");
            if (g_mission_after_return) {
                g_mission_after_return=0;
                g_starting=1; g_starting_frame=g_frame; g_start_hold=START_HOLD_FRAMES;
            }
        } else if (t > 60.0) {
            g_returning = 0; g_apply = 2;
            rt_log("enhanced: the attract mode did not come back, restarting\n");
            if (g_headless) rt_fatal("restart");
        }
    }
    if (g_apply == 1) {
        /* the game reads its settings only at boot: write them, save, then restart the process */
        uint8_t *nv = hw_nvram();
        for (int i = 0; i < g_n_game_options; i++) field_set(nv, &k_game_options[i], (uint32_t)g_opt_value[i]);
        hw_nvram_options_fix(nv);
        nvram_save();
        rt_log("enhanced: settings written, restarting\n");
        g_apply = 2;
        if (g_headless) rt_fatal("restart for the new settings");
    }
}

static void draw_menu(uint32_t *fb, int w, int h);

static void meter_rect(uint32_t *fb, int x0, int y0, int x1, int y1, uint32_t color) {
    x0 = (int)(x0 * g_ui); y0 = (int)(y0 * g_ui);
    x1 = (int)(x1 * g_ui); y1 = (int)(y1 * g_ui);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > g_fbw) x1 = g_fbw;
    if (y1 > g_fbh) y1 = g_fbh;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) fb[y * g_fbw + x] = 0xff000000u | color;
}

static void draw_controls(uint32_t *fb, int w, int h, int cursor) {
    const int labels[] = { T_BACK, T_RUMBLE, T_GYRO, T_SHOW_GYRO, T_RECENTER, T_STICK_RESPONSE, T_SWITCH_TRIGGERS };
    const int positions[] = { 60, 96, 132, 164, 196, 228, 260 };
    const int z = FONT_MEDIUM;
    dim_rect(fb, w, h, 0, 0, w, h, 190);
    draw_centered(fb, w, h, FONT_LARGE, 24, T(T_CONTROLS), 0xffd800);
    for (int i = 0; i < N_CONTROLS; i++) {
        char value[32] = "";
        if (i == 2) {
            if (frontend_gyro_enabled()) snprintf(value, sizeof value, "%.1fX", frontend_gyro_sensitivity() / 100.0);
            else snprintf(value, sizeof value, "%s", T(T_OFF));
        }
        if (i == 5) snprintf(value, sizeof value, "%s", k_stick_response_name[frontend_stick_response()]);
        if (i == 6) snprintf(value, sizeof value, "%s", frontend_switch_trigger_layout() ? "HANDBRAKE" : "BRAKE");
        if (i == 3) snprintf(value, sizeof value, "%s", T(g_set.show_gyro ? T_ON : T_OFF));
        if (i == 1) {
            if (frontend_rumble_multiplier()) snprintf(value, sizeof value, "%.1fX", frontend_rumble_multiplier() / 100.0);
            else snprintf(value, sizeof value, "%s", T(T_OFF));
        }
        uint32_t col = i == cursor ? 0xffd800 : 0xffffff;
        int y = positions[i];
        draw_text(fb, w, h, z, 40, y, T(labels[i]), col);
        draw_text(fb, w, h, z, w - 40 - text_width(z, value), y, value, col);
    }
    const int centre = w / 2, half = 110, y = 345;
    int ready = frontend_gyro_ready();
    double position = ready ? frontend_gyro_position() : 0;
    int marker = centre + (int)lround(fmax(-1, fmin(1, position)) * half);
    meter_rect(fb, centre - half, y - 2, centre + half, y + 2, 0x606060);
    if (ready) meter_rect(fb, marker < centre ? marker : centre, y - 3,
                           marker > centre ? marker : centre, y + 3, 0x40ff40);
    meter_rect(fb, centre - 1, y - 9, centre + 1, y + 9, 0xffffff);
    meter_rect(fb, marker - 3, y - 7, marker + 3, y + 7, ready ? 0xffd800 : 0x808080);
    draw_text(fb, w, h, FONT_SMALL, 40, y - 11, "L", 0xc0c0c0);
    draw_text(fb, w, h, FONT_SMALL, w - 40 - text_width(FONT_SMALL, "R"), y - 11, "R", 0xc0c0c0);
    const char *status = !frontend_gyro_available() ? "NO GYRO CONTROLLER CONNECTED" :
        !frontend_gyro_enabled() ? "GYRO OFF" : !ready ? "HOLD CONTROLLER UPRIGHT" : "HIGHER SENSITIVITY NEEDS LESS TILT";
    if (cursor == 6) status = frontend_switch_trigger_layout() ? "ZL: HANDBRAKE   ZR: GAS" : "ZL: BRAKE   ZR: GAS";
    draw_centered(fb, w, h, FONT_SMALL, 290, status, 0xc0c0c0);
    draw_centered(fb, w, h, FONT_SMALL, 310, cursor == 6 ? "L / R: SHIFT DOWN / UP" : "L3: RECENTER   R3: TOGGLE", 0xc0c0c0);
}


static void practice_time_text(char *text, size_t size, unsigned ms) {
    snprintf(text, size, "%u'%02u\"%03u", ms / 60000, ms / 1000 % 60, ms % 1000);
}

/* Add practice text through the native formatter/queue; its renderer owns filtering.
 * The copied context and temporary stack leave the interrupted game call intact. */
static void practice_native_entry(PPCContext *c, RtFn draw, uint32_t text, int row,
                                  const char *label, const char *value) {
    memcpy(g_ram + text, label, strlen(label) + 1);
    c->r[3] = 2; c->r[4] = row; c->r[5] = 0x00ff00; c->r[6] = text;
    draw(c);
    if (c->unwind) return;
    memcpy(g_ram + text, value, strlen(value) + 1);
    c->r[3] = 4; c->r[4] = row + 2; c->r[5] = 0xffffff; c->r[6] = text;
    draw(c);
}

/* Overlay positions are published to the guest thread. Its own formatter
 * and text queue render the diagnostic glyphs with the HUD texture/filter. */
typedef struct { int column,row;uint32_t colour;char text[96]; } DebugNativeText;
static DebugNativeText g_debug_text_pending[96],g_debug_text_published[96];
static unsigned g_debug_text_pending_count,g_debug_text_published_count;
static pthread_mutex_t g_debug_text_mutex=PTHREAD_MUTEX_INITIALIZER;
static void debug_native_text_hook(PPCContext *live) {
    static uint64_t frame=UINT64_MAX;
    if(!atomic_load(&g_track_debug) || frame==g_frame) return;
    frame=g_frame;
    RtFn draw=rt_lookup(GAME_ENH_PRACTICE_TEXT_RENDER);
    unsigned sp=live->r[1];if(!draw || sp<8192 || sp>=RAM_SIZE) return;
    DebugNativeText lines[96];unsigned count;
    pthread_mutex_lock(&g_debug_text_mutex);
    count=g_debug_text_published_count;memcpy(lines,g_debug_text_published,count*sizeof *lines);
    pthread_mutex_unlock(&g_debug_text_mutex);
    uint8_t saved[8192];memcpy(saved,g_ram+sp-sizeof saved,sizeof saved);
    PPCContext c=*live;c.r[1]=sp-256;c.budget=10000000;c.unwind=0;
    unsigned text=sp-128;
    for(unsigned i=0;i<count && !c.unwind;i++) {
        memcpy(g_ram+text,lines[i].text,strlen(lines[i].text)+1);
        c.r[3]=lines[i].column;c.r[4]=lines[i].row;c.r[5]=lines[i].colour;c.r[6]=text;
        draw(&c);
    }
    memcpy(g_ram+sp-sizeof saved,saved,sizeof saved);
}

static void practice_text_hook(PPCContext *live) {
    debug_native_text_hook(live);
    int mission=mission_engaged();
    if (!mission && !race_practice_active(live)) return;
    uint32_t format = live->r[6];
    int course_label = 0;
    for (unsigned i = 0; i < sizeof g_practice_courses / sizeof *g_practice_courses; i++) {
        size_t size = strlen(g_practice_courses[i]) + 1;
        if (race_valid(format, size) && !memcmp(g_ram + format, g_practice_courses[i], size)) course_label = 1;
    }
    RtFn draw = rt_lookup(GAME_ENH_PRACTICE_TEXT_RENDER);
    unsigned sp = live->r[1];
    if (!draw || sp < 8192 || sp >= RAM_SIZE) return;
    if (mission) {
        if(race_valid(format,64)) {
            uint8_t *end=memchr(g_ram+format,0,64);
            if(end) live->r[6]=(uint32_t)(end-g_ram);
        }
    } else race_practice_text_hook(live);
    static uint64_t mission_hud_frame=UINT64_MAX;
    if (mission) {
        if(mission_hud_frame==g_frame) return;
        mission_hud_frame=g_frame;
    } else if (!course_label || !race_practice_hud_visible()) return;
    uint8_t saved[8192]; memcpy(saved, g_ram + sp - sizeof saved, sizeof saved);
    PPCContext c = *live;
    c.r[1] = sp - 256; c.budget = 10000000; c.unwind = 0;
    uint32_t text = sp - 128;
    if (mission) {
        const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
        unsigned total=atomic_load(&g_mission_target_total);
        char progress[32],time[24];
        snprintf(progress,sizeof progress,"%u OF %u",
            d->break_count || total ? atomic_load(&g_mission_broken) : atomic_load(&g_mission_gate),
            d->break_count ? d->break_count : total ? total : atomic_load(&g_mission_gates));
        if(d->handbrake_turn) snprintf(progress,sizeof progress,"%u OF 1",atomic_load(&g_mission_native_success));
        if(d->speed_goal) snprintf(progress,sizeof progress,"%u / %.0f KM/H",atomic_load(&g_mission_speed),d->speed_goal);
        if(d->roll_degrees) {
            if(g_mission_roll_landed) snprintf(progress,sizeof progress,"COMPLETE");
            else snprintf(progress,sizeof progress,"%.0f / %u DEG",fabsf(g_mission_roll_angle)*57.2957795f,d->roll_degrees);
        }
        practice_time_text(time,sizeof time,atomic_load(&g_mission_elapsed));
        practice_native_entry(&c,draw,text,2,"MISSION",d->name);
        if(!c.unwind) practice_native_entry(&c,draw,text,8,
            d->break_count ? d->break_label : total ? d->target_label : d->speed_goal ? "TOP SPEED" : d->roll_degrees ? "ROLL" : d->handbrake_turn ? "TOAST" : "WAYPOINTS",progress);
        if(!c.unwind) practice_native_entry(&c,draw,text,14,"TIME",time);
        unsigned nearest=atomic_load(&g_mission_nearest_item);
        if(!c.unwind && mission_phase()==MISSION_RUNNING && nearest!=UINT_MAX) {
            char distance[32];snprintf(distance,sizeof distance,"%u M",nearest);
            practice_native_entry(&c,draw,text,20,"NEAREST ITEM",distance);
        }
        memcpy(g_ram + sp - sizeof saved,saved,sizeof saved);
        return;
    }
    char lap[16], best[24] = "NONE", current[24];
    snprintf(lap, sizeof lap, "%u", atomic_load(&g_practice_lap));
    unsigned ms = atomic_load(&g_practice_best_ms);
    if (ms) practice_time_text(best, sizeof best, ms);
    practice_time_text(current, sizeof current, atomic_load(&g_practice_current_ms));
    practice_native_entry(&c, draw, text, 2, "LAP", lap);
    if (!c.unwind) practice_native_entry(&c, draw, text, 8, "BEST", best);
    if (!c.unwind) practice_native_entry(&c, draw, text, 14,
        g_practice_courses[atomic_load(&g_practice_course)], current);
    unsigned checkpoint = atomic_load(&g_practice_checkpoint);
    if (!c.unwind && race_practice_checkpoint_visible()) {
        char label[24], split[24];
        snprintf(label, sizeof label, "CHECKPOINT %u", checkpoint);
        practice_time_text(split, sizeof split, atomic_load(&g_practice_checkpoint_ms));
        practice_native_entry(&c, draw, text, 20, label, split);
    }
    memcpy(g_ram + sp - sizeof saved, saved, sizeof saved);
}

static void mission_icon_tinted(uint32_t *fb,int x,int y,int w,int h,int sx,int sy,int sw,int sh,uint32_t tint) {
    if(!g_mission_icons_ready) return;
    int px=(int)(x*g_ui),py=(int)(y*g_ui),pw=(int)(w*g_ui),ph=(int)(h*g_ui);
    for(int j=0;j<ph;j++) for(int i=0;i<pw;i++) {
        int tx=sx+i*sw/pw,ty=sy+j*sh/ph;
        /* The source atlas stores big-endian RGBA4444, before the native
         * uploader converts it to the Voodoo texture representation. */
        unsigned n=(ty*256+tx)*2,v=g_mission_icons[n]<<8|g_mission_icons[n+1];
        int dx=px+i,dy=py+j;
        if(dx<0||dx>=g_fbw||dy<0||dy>=g_fbh) continue;
        unsigned rgb=((v>>12&15)*17<<16)|((v>>8&15)*17<<8)|(v>>4&15)*17;
        rgb=(((rgb>>16&255)*(tint>>16&255)/255)<<16)|
            (((rgb>>8&255)*(tint>>8&255)/255)<<8)|((rgb&255)*(tint&255)/255);
        blend(&fb[dy*g_fbw+dx],rgb,(v&15)*17);
    }
}
static void mission_icon(uint32_t *fb,int x,int y,int w,int h,int sx,int sy,int sw,int sh) {
    mission_icon_tinted(fb,x,y,w,h,sx,sy,sw,sh,0xffffff);
}
static void mission_badge(uint32_t *fb,int x,int y,int size,uint32_t colour,int near) {
    int px=(int)(x*g_ui),py=(int)(y*g_ui),n=(int)(size*g_ui),height=near ? n*3/2 : n;
    unsigned digit=colour==0xe0f4ff ? 4 : colour==0xffd800 ? 1 : colour==0xc0c0c0 ? 2 : 3;
    static const unsigned numerals[]={0,0x2c97,0x73e7,0x73cf,0x7be4}; /* 3 x 5; platinum P */
    /* Subpixel coverage keeps the small circles round at their cardinal points
     * and lets the shield retain a fine outline at native resolution. */
    for(int j=0;j<height;j++) for(int i=0;i<n;i++) {
        unsigned red=0,green=0,blue=0,coverage=0;
        for(int sy=0;sy<4;sy++) for(int sx=0;sx<4;sx++) {
            float u=(i+(sx+.5f)/4)/n,v=(j+(sy+.5f)/4)/height;
            float r=hypotf(u-.5f,v-.5f);
            int inside=near ? (v>=.22f*(1-2*fabsf(u-.5f)) && v<=1-.66f*fabsf(u-.5f)) : r<.48f;
            if(!inside) continue;
            uint32_t ink=near ? (u<.5f ? 0xffe02e : 0x20c862) :
                r>.32f && r<.39f ? ((colour>>1)&0x7f7f7f) : colour;
            if(near) {
                float top=.22f*(1-2*fabsf(u-.5f)),bottom=1-.66f*fabsf(u-.5f);
                float edge=fminf(fminf(u,1-u),fminf((v-top)/1.0925f,(bottom-v)/1.1982f));
                if(edge<.6f/size) ink=0xffffff;
            }
            if(!near && u>=.35f && u<.65f && v>=.25f && v<.75f) {
                int gx=(int)((u-.35f)*10),gy=(int)((v-.25f)*10);
                if(numerals[digit] & (1u<<(14-gy*3-gx))) ink=0x483e28;
            }
            red+=ink>>16&255;green+=ink>>8&255;blue+=ink&255;coverage++;
        }
        int dx=px+i,dy=py+j;
        if(coverage && dx>=0&&dx<g_fbw&&dy>=0&&dy<g_fbh)
            blend(&fb[dy*g_fbw+dx],(red/coverage<<16)|(green/coverage<<8)|(blue/coverage),coverage*255/16);
    }
}
static void mission_checkmark(uint32_t *fb,int x,int y,int size,uint32_t colour) {
    int px=(int)(x*g_ui),py=(int)(y*g_ui),n=(int)ceilf(size*g_ui);
    const float points[3][2]={{.12f,.48f},{.38f,.75f},{.88f,.18f}};
    for(int j=0;j<n;j++) for(int i=0;i<n;i++) {
        float u=(i+.5f)/n,v=(j+.5f)/n,distance=1;
        for(int segment=0;segment<2;segment++) {
            float ax=points[segment][0],ay=points[segment][1];
            float dx=points[segment+1][0]-ax,dy=points[segment+1][1]-ay;
            float t=fmaxf(0,fminf(1,((u-ax)*dx+(v-ay)*dy)/(dx*dx+dy*dy)));
            distance=fminf(distance,hypotf(u-ax-t*dx,v-ay-t*dy));
        }
        int sx=px+i,sy=py+j;
        unsigned alpha=(unsigned)(255*fmaxf(0,fminf(1,(.10f-distance)*n+.5f)));
        if(alpha && sx>=0&&sx<g_fbw&&sy>=0&&sy<g_fbh) blend(&fb[sy*g_fbw+sx],colour,alpha);
    }
}
static void mission_menu_text(uint32_t *fb,int w,int h,int x,int y,const char *s,uint32_t colour,float scale) {
    float original=g_ui; g_ui*=scale;
    draw_text(fb,w,h,FONT_SMALL,(int)(x/scale),(int)(y/scale),s,colour);
    g_ui=original;
}
static void mission_page_dots(uint32_t *fb,int w,int page,int pages) {
    float centre_y=306*g_ui;
    for(int p=0;p<pages;p++) {
        float centre_x=(w*.5f+(p-(pages-1)*.5f)*14)*g_ui;
        float radius=2.5f*g_ui;
        for(int y=(int)floorf(centre_y-radius);y<=(int)ceilf(centre_y+radius);y++)
            for(int x=(int)floorf(centre_x-radius);x<=(int)ceilf(centre_x+radius);x++) {
                float distance=hypotf(x+.5f-centre_x,y+.5f-centre_y);
                if(distance<=radius && (p==page || distance>=radius-g_ui) &&
                   x>=0 && x<g_fbw && y>=0 && y<g_fbh)
                    blend(&fb[y*g_fbw+x],0xffffff,255);
            }
    }
}
/* Reload only between runs, on the UI thread that owns mission selection. */
static uint64_t g_mission_reload_toast_until;
static int g_mission_reload_failed;
static void mission_selector_reload(void) {
    static unsigned polls,previous_hash;
    if(mission_engaged()) return;
    int force=g_mission_reload_force;g_mission_reload_force=0;
    if(!force && (polls++%30)) return;
    const char *path=getenv("RT_MISSIONS_CSV");if(!path) path="missions.csv";
    FILE *file=fopen(path,"rb");if(!file) return;
    unsigned hash=2166136261u;int ch;
    while((ch=fgetc(file))!=EOF) hash=(hash^(unsigned char)ch)*16777619u;
    int failed=ferror(file);fclose(file);if(failed || (!force && hash==previous_hash)) return;
    previous_hash=hash;
    unsigned selected=g_mission_cursor<MISSION_COUNT ? mission_identity((unsigned)g_mission_cursor) : 0;
    unsigned revision=g_mission_csv_revision;
    mission_csv_load();
    g_mission_reload_failed=revision==g_mission_csv_revision;
    if(!g_mission_reload_failed) for(unsigned i=0;i<g_mission_record_count;i++)
        g_mission_records[i].mission=mission_migrate_identity(g_mission_records[i].mission);
    g_mission_reload_toast_until=rt_now()+3*CPU_HZ;
    if(selected) for(int i=0;i<MISSION_COUNT;i++) if(selected==mission_identity((unsigned)i)) { g_mission_cursor=i;break; }
    if(g_mission_cursor>MISSION_COUNT+1) g_mission_cursor=MISSION_COUNT+1;
}
static void mission_draw_selector(uint32_t *fb, int w, int h) {
    mission_selector_reload();
    dim_rect(fb,w,h,0,0,w,h,190);
    draw_centered(fb,w,h,FONT_MEDIUM,24,T(T_MISSIONS),0xffd800);
    int first=(g_mission_cursor/10)*10;
    const int inset=12; /* Narrow the name column and centre the table. */
    mission_menu_text(fb,w,h,38+inset,64,"NO.",0x00ff00,.55f);
    mission_menu_text(fb,w,h,78+inset,64,"MISSION",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-344-inset,64,"LOCATION",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-282-inset,64,"MODE",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-217-inset,64,"CAR",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-164-inset,64,"GEAR",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-115-inset,64,"TYPE",0x00ff00,.55f);
    mission_menu_text(fb,w,h,w-64-inset,64,"BEST",0x00ff00,.55f);
    for (int i=first; i<=MISSION_COUNT+1 && i<first+10; i++) {
        int y=84+(i-first)*21;
        uint32_t colour=i==g_mission_cursor ? 0xffd800 : 0xffffff;
        if(i==g_mission_cursor) dim_rect(fb,w,h,32+inset,y-2,w-32-inset,y+18,90);
        if(i>=MISSION_COUNT) {
            mission_menu_text(fb,w,h,78+inset,y,i==MISSION_COUNT ? "CLEAR MISSION RECORDS" : T(T_BACK),
                i==MISSION_COUNT ? (i==g_mission_cursor ? 0xff7070 : 0xe84848) : colour,.55f);
            continue;
        }
        const MissionDefinition *d=&k_missions[i];
        char number[12]; snprintf(number,sizeof number,"%02d",i+1);
        float original_ui=g_ui;g_ui*=.6f;
        draw_text(fb,w,h,FONT_MEDIUM,60+(int)(inset/.6f),(int)((y-3)/.6f),number,colour);
        g_ui=original_ui;
        unsigned best=mission_menu_best(i);
        if(best) {
            if(!d->gold_ms) mission_checkmark(fb,63+inset,y+1,10,0x40ff40);
            else mission_badge(fb,62+inset,y,12,mission_medal_colour(d,best),0);
        }
        mission_menu_text(fb,w,h,78+inset,y,d->name,colour,.55f);
        static const char *const locations[]={"TOWN","COAST","MOUNT"};
        mission_menu_text(fb,w,h,w-344-inset,y,locations[d->region],colour,.55f);
        static const int cars[5][4]={{0,112,80,36},{88,220,80,36},{88,148,80,36},{168,148,80,36},{160,112,80,36}};
        int sx=cars[d->car][0],sy=cars[d->car][1];
        if(d->tuned && d->car==0) sx=80;
        else if(d->tuned && (d->car==2 || d->car==3)) sy=184;
        mission_icon_tinted(fb,w-287-inset,y-2,60,20,88,256,96,32,d->traffic ? 0xc0c0c0 : 0xffd800);
        mission_icon(fb,w-217-inset,y-2,44,20,sx,sy,80,36);
        if(d->tuned) mission_icon(fb,w-184-inset,y+12,11,4,214,242,40,10);
        mission_icon(fb,w-164-inset,y-2,d->transmission ? 31 : 20,20,d->transmission ? 0 : 70,48,d->transmission ? 56 : 35,36);
        const char *type=*d->type ? d->type : "DRIVE";
        if(!strcasecmp(type,"JUMP") || !strcasecmp(type,"TURN")) type="STUNT";
        mission_menu_text(fb,w,h,w-115-inset,y,type,colour,.55f);
        if((d->limit_ms || d->vehicle_destroy) && best) {
            char time[24]; snprintf(time,sizeof time,"%.2f",best/1000.0);
            mission_menu_text(fb,w,h,w-64-inset,y,time,colour,.55f);
            if(mission_near_gold(d,best)) mission_badge(fb,w-19-inset,y-3,10,0,1);
        }
    }
    mission_page_dots(fb,w,first/10,(MISSION_COUNT+11)/10);
    if (g_mission_cursor<MISSION_COUNT) {
        const MissionDefinition *d=&k_missions[g_mission_cursor];
        char limits[80];
        if (!d->limit_ms) snprintf(limits,sizeof limits,d->contacts==0 ?
            "NO TIME LIMIT - NO COLLISIONS" : "NO TIME LIMIT");
        else if (d->gold_ms) snprintf(limits,sizeof limits,"GOLD %.0f S - SILVER %.0f S - BRONZE %.0f S",
            d->gold_ms/1000.0,d->silver_ms/1000.0,d->limit_ms/1000.0);
        else snprintf(limits,sizeof limits,"%.0f SECONDS - CONTACT ALLOWED",d->limit_ms/1000.0);
        if(d->gold_ms) {
            char tiers[3][32]; const char *names[]={"GOLD","SILVER","BRONZE"};
            unsigned times[]={d->gold_ms,d->silver_ms,d->limit_ms};
            uint32_t colours[]={0xffd800,0xc0c0c0,0xcd7f32}; int width=0;
            for(int j=0;j<3;j++) { snprintf(tiers[j],sizeof tiers[j],"%s %.0f S",names[j],times[j]/1000.0); width+=text_width(FONT_SMALL,tiers[j]); }
            int x=(w-width-24)/2;
            for(int j=0;j<3;j++) { draw_text(fb,w,h,FONT_SMALL,x,352,tiers[j],colours[j]); x+=text_width(FONT_SMALL,tiers[j])+12; }
        } else draw_centered(fb,w,h,FONT_SMALL,352,limits,0xc0c0c0);
    }
    draw_centered(fb,w,h,FONT_SMALL,326,g_mission_cursor<MISSION_COUNT ?
        k_missions[g_mission_cursor].briefing : "CHOOSE A MISSION",0xffffff);
    if(rt_now()<g_mission_reload_toast_until) {
        dim_rect(fb,w,h,w/2-130,2,260,18,220);
        mission_menu_text(fb,w,h,w/2-120,4,g_mission_reload_failed ?
            "CSV RELOAD FAILED - DATA RETAINED" : "MISSION DATA RELOADED",0xffffff,.5f);
    }
    if(g_mission_time_save_error) draw_centered(fb,w,h,FONT_SMALL,378,"COULD NOT SAVE CSV",0xc0c0c0);
    if(g_mission_clear_error) draw_centered(fb,w,h,FONT_SMALL,378,"COULD NOT CLEAR RECORDS",0xffd800);
    if(g_mission_clear_confirm) {
        dim_rect(fb,w,h,0,0,w,h,220);
        draw_centered(fb,w,h,FONT_SMALL,115,"CLEAR ALL MISSION RECORDS?",0xff7070);
        draw_centered(fb,w,h,FONT_SMALL,155,"TIMES AND MEDALS WILL BE REMOVED",0xffffff);
        draw_centered(fb,w,h,FONT_SMALL,215,"CANCEL",g_mission_clear_yes ? 0xffffff : 0xffd800);
        draw_centered(fb,w,h,FONT_SMALL,250,"CLEAR RECORDS",g_mission_clear_yes ? 0xff7070 : 0xe84848);
    }
}

#include "gate_editor.h"

static void debug_text(uint32_t *fb,int w,int h,int x,int y,const char *text,uint32_t colour) {
    (void)fb;(void)h;
    if(g_debug_text_pending_count==96) return;
    DebugNativeText *line=&g_debug_text_pending[g_debug_text_pending_count++];
    /* Native characters occupy 8 x 16 pixels on an 8-pixel position grid.
     * Widescreen adds a centred margin to the game's 512-pixel canvas. */
    line->column=(int)lroundf((x-(w-512)*.5f)/8.f);
    line->row=(int)lroundf(y/8.f);line->colour=colour;
    int fit=(w-x)/8;
    /* The native queue wraps at 64 columns even in a wider viewport. */
    if(fit>64-line->column) fit=64-line->column;
    if(fit<0) fit=0;if(fit>95) fit=95;
    snprintf(line->text,sizeof line->text,"%.*s",fit,text);
}

static const uint16_t *g_overlay_depth;
static int g_overlay_depth_w, g_overlay_depth_h;
static unsigned g_overlay_depth_mode;
static float g_debug_project_depth;
void enh_overlay_depth(const uint16_t *depth, int w, int h, unsigned mode) {
    g_overlay_depth=depth;g_overlay_depth_w=w;g_overlay_depth_h=h;g_overlay_depth_mode=mode;
}
static unsigned debug_depth_value(float depth) {
    if (g_overlay_depth_mode == 1) {
        /* Voodoo's floating W encoding, matching compute_wfloat(). */
        uint64_t iw=(uint64_t)(281474976710656.0 / depth);
        if (!iw) return 65535;
        int exp=__builtin_clzll(iw)-16;
        if(exp<0) return 0;if(exp>=16) return 65535;
        return ((exp<<12)|((iw>>(35-exp))^0x1fff))+1;
    }
    float a=g_distance_slot[0].depth[0],b=g_distance_slot[0].depth[1];
    if(!g_distance_slot[0].valid) return 65535;
    if(g_distance_slot[0].applied) {
        double n=g_distance_slot[0].near,f=g_distance_slot[0].far*k_draw_distance[g_distance_slot[0].applied];
        a=-(f+n)/(f-n);b=-2*f*n/(f-n);
    }
    float z=(( -a+b/depth )*.5f+.5f)*65535.f;
    if(g_overlay_depth_mode==3) {
        uint32_t iz=(uint32_t)(fmaxf(0,fminf(65535,z))*4096.f);
        if(iz&0xf0000000) return 0;
        if(!(iz&0x0ffff000)) return 65535;
        int exp=__builtin_clz(iz)-4;
        return ((exp<<12)|((iz>>(15-exp))^0x1fff))+1;
    }
    return (unsigned)fmaxf(0,fminf(65535,z));
}
static void debug_dot(uint32_t *fb,int x,int y,uint32_t colour) {
    /* Projection and native text use 384-high logical coordinates. Convert
     * once for the colour/depth framebuffer, which may render at 2x or 4x. */
    int px=(int)lroundf(x*g_ui),py=(int)lroundf(y*g_ui),size=(int)ceilf(g_ui);
    for (int dy=0;dy<size;dy++) for (int dx=0;dx<size;dx++)
        if (px+dx>=0 && px+dx<g_fbw && py+dy>=0 && py+dy<g_fbh)
            {
            int alpha=255;
            if(g_overlay_depth && g_overlay_depth_w==g_fbw && g_overlay_depth_h==g_fbh) {
                unsigned scene=g_overlay_depth[(py+dy)*g_fbw+px+dx];
                unsigned gate=debug_depth_value(g_debug_project_depth);
                if(scene!=65535 && gate>scene+16) alpha=128;
            }
            blend(&fb[(py+dy)*g_fbw+px+dx],colour,alpha);
        }
}
/* Whether the last projected point is behind the scene at logical (x, y). */
static int debug_hidden(int x,int y) {
    int px=(int)lroundf(x*g_ui),py=(int)lroundf(y*g_ui);
    if(!g_overlay_depth || g_overlay_depth_w!=g_fbw || g_overlay_depth_h!=g_fbh ||
       px<0 || px>=g_fbw || py<0 || py>=g_fbh) return 0;
    unsigned scene=g_overlay_depth[py*g_fbw+px];
    return scene!=65535 && debug_depth_value(g_debug_project_depth)>scene+16;
}
/* Project diagnostic markers using the native camera's -Z forward convention. */
static int debug_project(float x,float y,float z,int w,int h,int *sx,int *sy) {
    float dx=x-atomic_load(&g_debug_camera[0]),dy=y-atomic_load(&g_debug_camera[1]);
    float dz=z-atomic_load(&g_debug_camera[2]),pitch=atomic_load(&g_debug_camera[3]);
    float yaw=atomic_load(&g_debug_camera[4]);
    float right=cosf(yaw)*dx-sinf(yaw)*dz,forward=-sinf(yaw)*dx-cosf(yaw)*dz;
    float depth=cosf(pitch)*forward+sinf(pitch)*dy;
    float up=cosf(pitch)*dy-sinf(pitch)*forward;
    if ((!isfinite(depth) || depth<1) && !atomic_load(&g_debug_view_valid)) return 0;
    float focal=h*.9f;
    float px=w*.5f+right*focal/depth,py=h*.5f-up*focal/depth;
    int native=atomic_load(&g_debug_view_valid);
    if(native) {
        float v[12];for(unsigned i=0;i<12;i++) v[i]=atomic_load(&g_debug_view[i]);
        right=v[0]+v[3]*x+v[6]*y+v[9]*z;
        up=v[1]+v[4]*x+v[7]*y+v[10]*z;
        depth=-(v[2]+v[5]*x+v[8]*y+v[11]*z);
        if(!isfinite(depth)||depth<1) return 0;
        px=(w-512)*.5f+atomic_load(&g_debug_viewport[1])+atomic_load(&g_debug_viewport[0])*
            (atomic_load(&g_debug_projection[0])*right/depth-atomic_load(&g_debug_projection[1]));
        py=atomic_load(&g_debug_viewport[3])-atomic_load(&g_debug_viewport[2])*
            (atomic_load(&g_debug_projection[2])*up/depth-atomic_load(&g_debug_projection[3]));
    }
    if (!isfinite(px)||!isfinite(py)||px<0||px>=w||py<0||py>=h) return 0;
    if(!native && enh_mirrored()) px=w-1-px;
    g_debug_project_depth=depth;
    *sx=(int)px; *sy=(int)py; return 1;
}
static void debug_draw_gates(uint32_t *fb,int w,int h) {
    for(unsigned i=0;!enh_gate_editor_active() && i<atomic_load(&g_debug_item_count);i++) {
        int x,y;char label[32];
        if(!debug_project(atomic_load(&g_debug_item[i][0]),atomic_load(&g_debug_item[i][1]),
            atomic_load(&g_debug_item[i][2]),w,h,&x,&y)) continue;
        for(unsigned j=0;j<sizeof label;j++) label[j]=(char)atomic_load(&g_debug_item_name[i][j]);
        label[sizeof label-1]=0;
        debug_text(fb,w,h,x,y,label,0x40ffff);
    }
    for (unsigned i=0;i<atomic_load(&g_debug_gate_count);i++) {
        float x=atomic_load(&g_debug_gate[i][0]),y=atomic_load(&g_debug_gate[i][1]);
        float z=atomic_load(&g_debug_gate[i][2]),nx=atomic_load(&g_debug_gate[i][3]);
        float nz=atomic_load(&g_debug_gate[i][4]),half=atomic_load(&g_debug_gate[i][5]);
        float half_height=atomic_load(&g_debug_gate_height[i]);if(half_height<=0) half_height=4;
        float tilt=atomic_load(&g_debug_gate_tilt[i])*.01745329252f,ct=cosf(tilt),st=sinf(tilt);
        unsigned role=atomic_load(&g_debug_gate_role[i]);
        int selected=enh_gate_editor_active() && (g_author_edit_start ? i==g_author_count : i==g_author_selected);
        uint32_t gate_colour=role==3 ? 0x40ffff : role==1 ? 0xff5050 : role==2 ? 0x40ff40 : 0xffd800;
        if(enh_gate_editor_active()) {
            /* World-spaced dots give the plane a visible surface and
             * perspective, including when it is tilted towards horizontal. */
            int columns=(int)fminf(64,fmaxf(2,ceilf(half*2)));
            int rows=(int)fminf(32,fmaxf(2,ceilf(half_height*2)));
            /* Hidden and underground parts use a grid twice as fine, so they
             * read as a surface; the extra points are drawn only there. */
            for(int row=1;row<rows*2;row++) for(int column=1;column<columns*2;column++) {
                int fine=(row|column)&1;
                float u=1.f*column/columns,side=half*(u-1);
                float height=half_height*(1.f*row/rows-1);
                int segment=u<=1?0:1;float blend=u-segment;
                float ground=atomic_load(&g_debug_gate_ground[i][segment])*(1-blend)+
                    atomic_load(&g_debug_gate_ground[i][segment+1])*blend;
                int underground=isfinite(ground) && y+height*ct<ground;
                uint32_t colour=underground?0xbfc3c8:selected?0xffffff:gate_colour;
                int px,py;
                if(debug_project(x+nz*side-nx*height*st,y+height*ct,z-nx*side-nz*height*st,w,h,&px,&py) &&
                   (!fine || underground || debug_hidden(px,py)))
                    debug_dot(fb,px,py,colour);
            }
        }
        for (int edge=0;edge<4;edge++) for (int t=0;t<=80;t++) {
            float side=half*(t/40.f-1),height=-half_height;
            if (edge==1) height=half_height;
            if (edge>=2) { side=edge==2?-half:half; height=half_height*(t/40.f-1); }
            float u=half>0 ? (side/half+1) : 1;
            int segment=u<=1 ? 0 : 1;float blend=u-segment;
            float ground=atomic_load(&g_debug_gate_ground[i][segment])*(1-blend)+
                atomic_load(&g_debug_gate_ground[i][segment+1])*blend;
            int underground=isfinite(ground) && y+height*ct<ground;
            uint32_t editor_colour=underground ? (selected ? 0xbfc3c8 : 0xff60ff) : selected ? 0xffffff : gate_colour;
            int sx,sy;
            if (debug_project(x+nz*side-nx*height*st,y+height*ct,z-nx*side-nz*height*st,w,h,&sx,&sy))
                debug_dot(fb,sx,sy,enh_gate_editor_active() ? editor_colour : atomic_load(&g_debug_gate_any) ?
                    (atomic_load(&g_debug_gate_mask)&(1u<<i) ? 0x40ff40 : 0xffd800) :
                    i==atomic_load(&g_mission_gate)?0xffd800:0x40ff40);
        }
        if(enh_gate_editor_active()) for(int t=0;t<=80;t++) {
            float u=t/40.f,side=half*(u-1);int segment=u<=1?0:1;float blend=u-segment;
            float ground=atomic_load(&g_debug_gate_ground[i][segment])*(1-blend)+
                atomic_load(&g_debug_gate_ground[i][segment+1])*blend;
            int px,py;
            if(isfinite(ground) && fabsf(ground-y)<=half_height &&
               debug_project(x+nz*side,ground,z-nx*side,w,h,&px,&py)) debug_dot(fb,px,py,0x40ffff);
        }
        int sx,sy; char label[32];
        if(role==3 && enh_gate_editor_active()) {
            float heading=g_author_start.heading*.01745329252f;
            /* Heading is independent of the fixed checkpoint plane. */
            for(unsigned t=0;t<=60;t++) {
                float distance=t/10.f;
                int px,py;
                if(debug_project(x+sinf(heading)*distance,y+1,z+cosf(heading)*distance,w,h,&px,&py))
                    debug_dot(fb,px,py,0xffffff);
            }
            for(int side=-1;side<=1;side+=2) for(unsigned t=0;t<=20;t++) {
                float back=t/10.f;
                int px,py;
                if(debug_project(x+sinf(heading)*(6-back)+side*cosf(heading)*back*.5f,y+1,
                    z+cosf(heading)*(6-back)-side*sinf(heading)*back*.5f,w,h,&px,&py)) debug_dot(fb,px,py,0xffffff);
            }
        }
        if (debug_project(x,y+5,z,w,h,&sx,&sy)) {
            snprintf(label,sizeof label,role==3 ? "START" : role==1 ? "FAIL %u" : role==2 ? "FINISH %u" : "GATE %u",i+1);
            debug_text(fb,w,h,sx,sy,label,gate_colour);
        }
    }
}
void enh_draw_overlay(uint32_t *fb, int w, int h) {
    if (!g_enhanced) return;
    g_debug_text_pending_count=0;
    g_fbw = w;
    g_fbh = h;
    g_ui = h >= 768 ? (float)h / 384.0f : 1.0f;     /* lay out in 384-high logical units */
    w = (int)(w / g_ui + 0.5f);
    h = (int)(h / g_ui + 0.5f);
    if (enh_turbo()) {                               /* booting or applying: cover it all */
        dim_rect(fb, w, h, 0, 0, w, h, 255);
        draw_centered(fb, w, h, FONT_MEDIUM, h / 2 - font_height(FONT_MEDIUM) / 2, T(g_apply == 1 || g_apply == 2 ? T_APPLYING : T_LOADING), 0xffffff);
        return;
    }
    if (explorer_active() && !enh_gate_editor_active()) {
        char status[80];
        snprintf(status, sizeof status, explorer_free() ?
                 "F7 EXIT  F6 DRONE  %.0f KM/H  %.0f M" :
                 "F6 EXIT  F7 FREE ROAM  %.0f KM/H  %.0f M",
                 explorer_speed() * 3.6f, explorer_height());
        draw_centered(fb, w, h, FONT_SMALL, h - font_height(FONT_SMALL) - 20, status, 0xffd800);
    }
    if (mission_engaged() && !g_paused) {
        int phase=mission_phase();
        const MissionDefinition *d=&k_missions[atomic_load(&g_mission_selected)];
        char status[100];
        unsigned elapsed=atomic_load(&g_mission_elapsed);
        if (phase==MISSION_ROLLING)
            draw_centered(fb,w,h,FONT_SMALL,h-36,"ROLLING START",0xffd800);
        if (phase==MISSION_COUNTDOWN) {
            snprintf(status,sizeof status,"%u",atomic_load(&g_mission_countdown));
            draw_centered(fb,w,h,FONT_LARGE,h/2-30,status,0xffd800);
            draw_centered(fb,w,h,FONT_SMALL,h/2+28,d->briefing,0xffffff);
        }
        if (mission_result() && !g_paused) {
            dim_rect(fb,w,h,0,0,w,h,190);
            const char *reason=phase==MISSION_PASSED ? "MISSION COMPLETE" :
                phase==MISSION_TIME_FAILED ? "TIME LIMIT EXCEEDED" :
                phase==MISSION_CONTACT_FAILED ? "CONTACT LIMIT EXCEEDED" :
                phase==MISSION_GATE_FAILED ? "MISSED WAYPOINT" :
                phase==MISSION_OBJECTIVE_FAILED ? "MISSION FAILED" :
                phase==MISSION_RECOVERY_FAILED ? "CAR RECOVERED - RETRY" : "MISSION SETUP FAILED";
            unsigned result_colour=phase==MISSION_PASSED ? 0x40ff40 : 0xffd800;
            const char *medal=phase==MISSION_PASSED ? mission_medal(d,elapsed) : NULL;
            if (medal) {
                snprintf(status,sizeof status,"%s MEDAL",medal);
                reason=status;
                result_colour=mission_medal_colour(d,elapsed);
            }
            draw_centered(fb,w,h,FONT_MEDIUM,130,reason,result_colour);
            if(phase==MISSION_PASSED && mission_near_gold(d,elapsed))
                mission_badge(fb,(w+text_width(FONT_MEDIUM,reason))/2+12,133,14,0,1);
            snprintf(status,sizeof status,"TIME %.2f SEC   CONTACTS %u",elapsed/1000.0,atomic_load(&g_mission_contacts));
            draw_centered(fb,w,h,FONT_SMALL,185,status,0xffffff);
            unsigned best=atomic_load(&g_mission_best);
            if (best) {
                const char *best_medal=mission_medal(d,best);
                snprintf(status,sizeof status,"BEST %.2f SEC%s%s",best/1000.0,
                    best_medal ? " - " : "",best_medal ? best_medal : "");
                draw_centered(fb,w,h,FONT_SMALL,213,status,0xc0c0c0);
            }
            draw_centered(fb,w,h,FONT_MEDIUM,240,"START / ENTER: RETRY",0xffd800);
            draw_centered(fb,w,h,FONT_SMALL,290,"BACK: CHOOSE MISSION",0xc0c0c0);
            mission_menu_text(fb,w,h,(w-(int)(text_width(FONT_SMALL,"ESC: PAUSE / EXPLORE")*.5f))/2,320,"ESC: PAUSE / EXPLORE",0xc0c0c0,.5f);
        }
    }
    if (g_paused && g_mission_pause_menu) mission_draw_selector(fb,w,h);
    if (g_paused && !g_pause_controls && !g_mission_pause_menu) {
        const int z = FONT_SMALL, step = font_height(z) + 8;
        int items[8], count = pause_items(items);
        dim_rect(fb, w, h, 0, 0, w, h, 160);
        draw_centered(fb, w, h, FONT_LARGE, h / 2 - 90, T(T_PAUSE), 0xffd800);
        for (int i = 0; i < count; i++) {
            char label[80];
            if (items[i] == T_UNLIMITED_LAPS)
                snprintf(label, sizeof label, "%s: %s", T(items[i]), T(race_unlimited_laps() ? T_ON : T_OFF));
            else if (items[i] == T_TRACK_DEBUG)
                snprintf(label,sizeof label,"%s: %s",T(items[i]),T(atomic_load(&g_track_debug)?T_ON:T_OFF));
            else snprintf(label, sizeof label, "%s", T(items[i]));
            draw_centered(fb, w, h, z, h / 2 - 26 + i * step, label, i == g_pause_cursor ? 0xffd800 : 0xffffff);
        }
    }
    if (g_paused && g_pause_controls) draw_controls(fb, w, h, g_controls_cursor);
    if (enh_menu_active()) draw_menu(fb, w, h);
    if (atomic_load(&g_track_debug) && (race_time_trial() || mission_engaged())) {
        if(!g_paused) {track_debug_view_select();debug_draw_gates(fb,w,h);}
        char label[128]; int y=16, x=w/2-6*8;
        unsigned gates=atomic_load(&g_debug_gate_count);
        unsigned vehicle=atomic_load(&g_debug_vehicle_kind);
        char model[48];for(unsigned i=0;i<sizeof model;i++) model[i]=(char)atomic_load(&g_debug_object_name[i]);
        model[sizeof model-1]=0;
        char solid[32];for(unsigned i=0;i<sizeof solid;i++) solid[i]=(char)atomic_load(&g_debug_solid_name[i]);
        solid[sizeof solid-1]=0;
        (void)gates;
        int flying=enh_gate_editor_active() && g_author_flying;
        float debug_x=flying?atomic_load(&g_debug_camera[0]):atomic_load(&g_debug_x);
        float debug_z=flying?atomic_load(&g_debug_camera[2]):atomic_load(&g_debug_z);
        float debug_y=flying?atomic_load(&g_debug_camera[1]):atomic_load(&g_debug_y);
        float debug_heading=flying?remainderf(atomic_load(&g_debug_camera[4])*57.2957795f+180,360):atomic_load(&g_debug_heading);
        snprintf(label,sizeof label,"%s %u X %.1f Z %.1f",
            flying?"CAM CP":"CP",atomic_load(&g_debug_cp),debug_x,debug_z);
        debug_text(fb,w,h,x,y,label,0x40ff40); y+=16;
        snprintf(label,sizeof label,"Y %.1f H %.1f",
            debug_y,debug_heading);
        debug_text(fb,w,h,x,y,label,0x40ff40); y+=16;
        snprintf(label,sizeof label,"OBJ %08X X %.1f Z %.1f",
            atomic_load(&g_debug_object),atomic_load(&g_debug_object_x),atomic_load(&g_debug_object_z));
        debug_text(fb,w,h,x,y,label,0x40ffff); y+=16;
        if(*model) { debug_text(fb,w,h,x,y,model,0x40ffff);y+=16; }
        if(*solid) { debug_text(fb,w,h,x,y,solid,0xffd800);y+=16; }
        if(vehicle!=UINT_MAX) {
            static const unsigned models[]={13,14,0,1,2,3,5,7,10,11,12,15,8,4,6,9};
            snprintf(label,sizeof label,atomic_load(&g_debug_vehicle_parked) ? "PARKED %u MODEL %02u %.0f M X %.1f Z %.1f" : "CAR %u MODEL %02u %.0f M X %.1f Z %.1f",
                atomic_load(&g_debug_vehicle_parked) ? atomic_load(&g_debug_vehicle_index) : vehicle,
                atomic_load(&g_debug_vehicle_parked) ? vehicle : models[vehicle],atomic_load(&g_debug_vehicle_distance),
                atomic_load(&g_debug_vehicle_x),atomic_load(&g_debug_vehicle_z));
            debug_text(fb,w,h,x,y,label,0x40ffff);y+=16;
        }
        for (unsigned i=0;i<atomic_load(&g_debug_gate_count);i++) {
            unsigned role=atomic_load(&g_debug_gate_role[i]);
            int selected=enh_gate_editor_active() && (g_author_edit_start ? i==g_author_count : i==g_author_selected);
            uint32_t colour=role==1?0xff5050:role==2?0x40ff40:role==3?0x40ffff:0xffd800;
            char prefix[24];snprintf(prefix,sizeof prefix,role==3?"START ":"G%u %s ",i+1,role==1?"FAIL":role==2?"FINISH":"WAYPOINT");
            debug_text(fb,w,h,x,y,prefix,colour);
            snprintf(label,sizeof label,"X %.0f Z %.0f W %.0f",
                atomic_load(&g_debug_gate[i][0]),atomic_load(&g_debug_gate[i][2]),atomic_load(&g_debug_gate[i][5])*2);
            debug_text(fb,w,h,x+(int)strlen(prefix)*8,y,label,selected?0xffffff:colour);y+=16;
        }
    }
    if(enh_gate_editor_active()) {
        char edit[128];int x=w/2-48,y=220;
        pthread_mutex_lock(&g_author_mutex);
        snprintf(edit,sizeof edit,"%s M%u %u OF %u",g_author_flying?"FLY CAMERA":"EDIT GATE",g_author_mission,g_author_count?g_author_selected+1:0,g_author_count);
        debug_text(fb,w,h,x,y,edit,0x00ff00);y+=16;
        if(g_author_edit_start) {
            snprintf(edit,sizeof edit,"START CP %d R %.1f SPEED %.0f",g_author_start_cp,g_author_start.heading,g_author_start_speed);
            debug_text(fb,w,h,x,y,edit,0xffffff);y+=16;
            debug_text(fb,w,h,x,y,"DPAD ROTATE RIGHT STICK SPEED",0xffffff);y+=16;
        } else if(g_author_count) {
            AuthorGate gate=g_author_gates[g_author_selected];
            snprintf(edit,sizeof edit,"W %.1f H %.1f R %.1f %s",gate.width,gate.height,gate.heading,gate.role==1?"FAIL":gate.role==2?"FINISH":"WAYPOINT");
            debug_text(fb,w,h,x,y,edit,0xffffff);y+=16;
        }
        debug_text(fb,w,h,x,y,"A ADD X DELETE B PREV Y NEXT",0xffffff);y+=16;
        debug_text(fb,w,h,x,y,"L3 CAMERA/GATE L R ROLE",0xffffff);y+=16;
        snprintf(edit,sizeof edit,"HOME SET START CP %u",g_author_start_cp>=0 ? (unsigned)g_author_start_cp : atomic_load(&g_debug_cp));
        debug_text(fb,w,h,x,y,edit,0xffffff);y+=16;
        debug_text(fb,w,h,x,y,"START SAVE SELECT CANCEL",0xffffff);y+=16;
        if(g_author_error) debug_text(fb,w,h,x,y,g_author_error==2 ? "NO TRACK SURFACE UNDER CURSOR" : "SAVE FAILED - CHECK GATE ORDER",0xff5050);
        pthread_mutex_unlock(&g_author_mutex);
    }
    pthread_mutex_lock(&g_debug_text_mutex);
    g_debug_text_published_count=g_debug_text_pending_count;
    memcpy(g_debug_text_published,g_debug_text_pending,g_debug_text_pending_count*sizeof *g_debug_text_pending);
    pthread_mutex_unlock(&g_debug_text_mutex);
    if (g_set.show_gyro &&
        g_booted && !g_paused && !enh_menu_active() && !g_starting &&
        !enh_in_attract() && !enh_name_entry_active()) {
        /* Compact, text-free live meter, centred in the current viewport. */
        const int centre = w / 2, half = 60, y = h - 20;
        int marker = centre + (int)lround(fmax(-1, fmin(1, frontend_steering_position())) * half);
        meter_rect(fb, centre - half, y - 1, centre + half, y + 1, 0x909090);
        meter_rect(fb, marker < centre ? marker : centre, y - 1,
                   marker > centre ? marker : centre, y + 1, 0x40ff40);
        meter_rect(fb, centre - 1, y - 5, centre + 1, y + 5, 0xffffff);
        int raw_marker = centre + (int)lround(fmax(-1, fmin(1, frontend_stick_position())) * half);
        meter_rect(fb, raw_marker - 1, y - 7, raw_marker + 1, y - 3, 0x40dfff);
        meter_rect(fb, marker - 2, y - 4, marker + 2, y + 4, 0xffd800);
    }
    if (g_set.show_fps && g_font) {                 /* on top of everything, also in play */
        char buf[16];
        snprintf(buf, sizeof buf, "%d FPS", g_fps);
        draw_text(fb, w, h, FONT_SMALL, w - text_width(FONT_SMALL, buf) - 8, 6, buf, 0x40ff40);
    }
}

static void draw_menu(uint32_t *fb, int w, int h) {
    const uint32_t white = 0xffffff, yellow = 0xffd800, grey = 0xc0c0c0;
    int lang = menu_language();
    if (g_screen == SCREEN_MISSIONS) { mission_draw_selector(fb,w,h); return; }
    if (g_screen == SCREEN_MAIN) {
        /* a compact panel on the left, so the attract stays visible */
        const int z = FONT_MEDIUM, step = font_height(z) * 7 / 8, pad = 12, left = 24;
        int tw = 0;
        for (int i = 0; i < N_MAIN_ITEMS; i++) {
            int iw = text_width(z, T(main_item(i)));
            if (iw > tw) tw = iw;
        }
        int ph = N_MAIN_ITEMS * step + 2 * pad - (step - font_height(z));
        int y0 = h - ph - 40;          /* lower left */
        dim_rect(fb, w, h, left, y0, left + tw + 2 * pad, y0 + ph, 150);
        for (int i = 0; i < N_MAIN_ITEMS; i++)
            draw_text(fb, w, h, z, left + pad, y0 + pad + i * step, T(main_item(i)), i == g_cursor ? yellow : white);
    } else if (g_screen == SCREEN_OPTIONS) {
        static const int items[N_PAGES + 1] = { T_GAME, T_SOUND, T_DISPLAY, T_CONTROLS, T_BACK };
        const int z = FONT_SMALL, step = font_height(z) + 8;
        dim_rect(fb, w, h, 0, 0, w, h, 190);
        draw_centered(fb, w, h, FONT_LARGE, 36, T(T_OPTIONS), yellow);
        for (int i = 0; i <= N_PAGES; i++)
            draw_centered(fb, w, h, z, 95 + i * step + (i == N_PAGES ? step / 2 : 0), T(items[i]), i == g_opt_cursor ? yellow : white);
    } else if (g_screen == SCREEN_PAGE) {
        if (g_page == PAGE_CONTROLS) { draw_controls(fb, w, h, g_page_cursor); return; }
        static const int titles[N_PAGES] = { T_GAME, T_SOUND, T_DISPLAY, T_CONTROLS };
        const int z = FONT_MEDIUM, step = font_height(z) + 4, left = 40, right = w - 40;
        int rows[MAX_GAME_OPTIONS + 2], n = page_rows(g_page, rows);
        dim_rect(fb, w, h, 0, 0, w, h, 190);
        draw_centered(fb, w, h, FONT_LARGE, 24, T(titles[g_page]), yellow);
        int y = 90;
        for (int i = 0; i < n; i++, y += step) {
            uint32_t col = i == g_page_cursor ? yellow : white;
            const char *label, *value;
            char buf[64];
            if (rows[i] == -1) { label = T(T_DISPLAY); value = T(g_set.fullscreen ? T_FULLSCREEN : T_WINDOW); }
            else if (rows[i] == -2) { label = T(T_SHOW_FPS); value = T(g_set.show_fps ? T_ON : T_OFF); }
            else if (rows[i] == -3) { label = T(T_RESOLUTION); value = g_set.scale == 2 ? "2X" : "1X"; }
            else if (rows[i] == -4) { label = T(T_ASPECT); value = k_aspect_name[g_set.aspect]; }
            else if (rows[i] == -10) { label = T(T_TEXTURE_FILTER); value = k_texture_filter_name[g_set.texture_filter]; }
            else if(rows[i]==-13) {label=T(T_MIRROR);value=T(g_set.mirror?T_ON:T_OFF);}
            else if (rows[i] == -12) { label = T(T_DRAW_DISTANCE); value = k_draw_distance_name[g_set.draw_distance]; }
            else {
                const GameOption *o = &k_game_options[rows[i]];
                label = lang ? o->label_it : o->label_en;
                value = option_value_text(o, g_opt_value[rows[i]], lang, buf, sizeof buf);
            }
            /* a value that does not fit next to its label falls back to the small font */
            int zv = text_width(z, label) + text_width(z, value) + 16 > right - left ? FONT_SMALL : z;
            draw_text(fb, w, h, z, left, y, label, col);
            draw_text(fb, w, h, zv, right - text_width(zv, value), y + (font_height(z) - font_height(zv)), value, col);
        }
        draw_text(fb, w, h, z, left, y + step / 2, T(T_BACK), g_page_cursor == n ? yellow : white);
    } else {
        static const int heads[] = { T_ORIGINAL_GAME, T_RECOMPILATION, T_VOODOO };
        static const char *const names[] = { "KONAMI", "KONAMI VIPER RECOMP", "MAME" };
        dim_rect(fb, w, h, 0, 0, w, h, 190);
        draw_centered(fb, w, h, FONT_LARGE, 24, T(T_CREDITS), yellow);
        for (int i = 0, y = 90; i < 3; i++, y += 78) {
            draw_centered(fb, w, h, FONT_SMALL, y, T(heads[i]), grey);
            draw_centered(fb, w, h, FONT_MEDIUM, y + 26, names[i], white);
        }
        draw_centered(fb, w, h, FONT_SMALL, 340, T(T_PRESS_START_BACK), grey);
    }
}

/* RT_ENH_MENU="seconds:action,..." (up/down/left/right/ok/back/esc) drives the menu in headless
 * tests; "name=TEXT" types TEXT in the name entry ('<' for DEL, '>' for END) */
static void scripted_menu(void) {
    static const char *next = (const char *)-1;
    if (next == (const char *)-1) next = getenv("RT_ENH_MENU");
    while (next && *next) {
        char *colon;
        double t = strtod(next, &colon);
        if (*colon != ':' || (double)rt_now() / CPU_HZ < t) return;
        const char *a = colon + 1;
        if (!strncmp(a, "free", 4)) { explorer_free_toggle(); const char *c = strchr(a, ','); next = c ? c + 1 : NULL; continue; }
        if (!strncmp(a, "drone", 5)) { explorer_toggle(); const char *c = strchr(a, ','); next = c ? c + 1 : NULL; continue; }
        if (!strncmp(a, "esc", 3)) { enh_escape(); const char *c = strchr(a, ','); next = c ? c + 1 : NULL; continue; }
        if (!strncmp(a, "name=", 5)) {
            for (a += 5; *a && *a != ','; a++) enh_name_type(*a == '<' ? '\b' : *a == '>' ? '\r' : *a);
            next = *a ? a + 1 : NULL;
            continue;
        }
        int action = !strncmp(a, "up", 2) ? ENH_UP : !strncmp(a, "down", 4) ? ENH_DOWN :
                     !strncmp(a, "page_up", 7) ? ENH_PAGE_UP : !strncmp(a, "page_down", 9) ? ENH_PAGE_DOWN :
                     !strncmp(a, "left", 4) ? ENH_LEFT : !strncmp(a, "right", 5) ? ENH_RIGHT :
                     !strncmp(a, "ok", 2) ? ENH_OK : ENH_BACK;
        enh_menu_action(action);
        const char *comma = strchr(a, ',');
        next = comma ? comma + 1 : NULL;
    }
}
