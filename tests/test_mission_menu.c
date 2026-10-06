#include "../runtime/enhanced.c"
#include <assert.h>
#include <unistd.h>
uint8_t g_in[8];
int16_t g_analog[4]={0,-200,-200,-200};
int explorer_active(void) { return 0; }
int explorer_free(void) { return 0; }
float explorer_speed(void) { return 0; }
float explorer_height(void) { return 0; }
void explorer_toggle(void) {}
void explorer_free_toggle(void) {}
static int switch_layout=1;
static int shift_up_held;
int frontend_shift_up_held(void) { return shift_up_held; }
int frontend_switch_trigger_layout(void) { return switch_layout; }
void frontend_set_switch_trigger_layout(int v) { switch_layout = v == 0 ? 0 : 1; }
static int stick_response=2;
int frontend_stick_response(void) { return stick_response; }
void frontend_set_stick_response(int v) { stick_response = v>=0 && v<=2 ? v : 2; }
double frontend_stick_position(void) { return .5; }
double frontend_steering_position(void) { return .25; }
static int enabled, sensitivity=100, recentered;
uint8_t *g_ram;
uint32_t rt_mmio_r32(uint32_t a) {(void)a;assert(0);return 0;}
uint32_t rt_mmio_r16(uint32_t a) {(void)a;assert(0);return 0;}
uint32_t rt_mmio_r8(uint32_t a) {(void)a;assert(0);return 0;}
void rt_mmio_w32(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}
void rt_mmio_w16(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}
void rt_mmio_w8(uint32_t a,uint32_t v) {(void)a;(void)v;assert(0);}

static int rumble=100;
int frontend_rumble_multiplier(void) { return rumble; }
void frontend_set_rumble_multiplier(int v) { rumble=v<0?0:v>400?400:v; }
int frontend_gyro_enabled(void) {return enabled;}
int frontend_gyro_available(void) {return 1;}
int frontend_gyro_ready(void) {return 1;}
double frontend_gyro_position(void) {return .5;}
int frontend_gyro_sensitivity(void) {return sensitivity;}
void frontend_gyro_set_enabled(int x) {enabled=x;}
void frontend_gyro_set_sensitivity(int x) {sensitivity=x<50?50:x>350?350:x;}
void frontend_gyro_recenter(void) {recentered++;}
static int applied_filter;
void voodoo_set_texture_filter(int n) {applied_filter=n;}
void voodoo_set_scale(int n) {(void)n;}
void voodoo_set_wide(int n) {(void)n;}
void rt_log(const char *s,...) {(void)s;}
uint8_t *hw_nvram(void) { static uint8_t nv[8192]; return nv; }
uint64_t rt_now(void) {return 0;}
int main(void) {
    setenv("RT_MISSIONS_CSV","tests/fixtures/missions.csv",1); mission_csv_load();
    g_enhanced=1; g_font=(uint8_t*)1; g_booted=1;
    PPCContext confirm={0};confirm.r[3]=1;g_analog[2]=200;
    course_reverse_confirm(&confirm);assert(confirm.r[3]==0 && g_reverse_selected);
    g_analog[2]=-200;confirm.r[3]=0;
    course_reverse_confirm(&confirm);assert(!g_reverse_selected);
    confirm.r[3]=1;course_reverse_confirm(&confirm);assert(confirm.r[3]==1);

    /* Course reload restores the exact native bytes, rather than a second
     * floating-point transform; all captured copies are released. */
    g_ram=calloc(1,RAM_SIZE);assert(g_ram);
    uint32_t node=0x40000,points=0x41000,new_root=0;
    ST16(node,3);ST32(node+4,points);ST32(node+8,node);ST32(node+12,node);
    for(unsigned i=0;i<3;i++) {STF32(points+32*i+4,10*i);STF32(points+32*i+24,10*i);}
    ST8(points+28,1);ST8(points+32+28,3);
    uint8_t saved_node[0x44],saved_points[96];
    memcpy(saved_node,g_ram+node,sizeof saved_node);memcpy(saved_points,g_ram+points,sizeof saved_points);
    assert(course_reverse_capture(node));assert(course_reverse_graph(node,20,&new_root));
    ST32(0x8c0188,new_root);g_reverse_root=new_root;g_reverse_applied=1;
    course_reverse_restore();assert(!g_reverse_copy_count && !g_reverse_applied);
    assert(!memcmp(saved_node,g_ram+node,sizeof saved_node));assert(!memcmp(saved_points,g_ram+points,sizeof saved_points));
    /* Reverse signs rotate locally, leaving position, vertical direction and
     * unrelated scene assets intact. Normal courses retain native matrices. */
    uint32_t groups=0x42000,entry=0x43000,assets=0x44000,toc=0x45000;
    confirm.r[2]=toc;ST32(toc+0x40,groups);ST32(groups,2);
    ST32(groups+36+8,1);ST32(groups+36+16,0x200);ST32(groups+36+20,assets);
    for(unsigned i=0;i<12;i++) STF32(entry+4*i,(float)i+1);
    ST32(entry+112,assets+16*0x11f);
    uint8_t placement[48];memcpy(placement,g_ram+entry,48);
    course_reverse_sign(&confirm,entry);assert(!memcmp(placement,g_ram+entry,48));
    g_reverse_applied=1;course_reverse_sign(&confirm,entry);
    for(unsigned i=0;i<12;i++) {
        float expected=(float)i+1;
        if(i>=3 && (i-3)%3!=1) expected=-expected;
        assert(LDF32(entry+4*i)==expected);
    }
    memcpy(g_ram+entry,placement,48);ST32(entry+112,assets+16*0x110);
    course_reverse_sign(&confirm,entry);assert(!memcmp(placement,g_ram+entry,48));
    g_reverse_applied=0;
    free(g_ram);g_ram=NULL;
    /* Missing atlas punctuation must not hide the sign of debug coordinates. */
    uint32_t minus_pixels[32*40]={0};g_fbw=32;g_fbh=40;g_ui=1;
    draw_text(minus_pixels,32,40,FONT_SMALL,2,2,"-",0x40ff40);
    unsigned minus_ink=0;
    for(unsigned i=0;i<32*40;i++) if((minus_pixels[i]&0xffffff)==0x40ff40) minus_ink++;
    assert(minus_ink>0 && text_width(FONT_SMALL,"-")==glyph_space(FONT_SMALL));
    g_frame=g_attract_frame=100;
    assert(mission_available() && N_MAIN_ITEMS==5);
    enh_menu_action(ENH_DOWN); assert(g_cursor==1);
    enh_menu_action(ENH_OK); assert(g_screen==SCREEN_MISSIONS);
    int fixture_count=MISSION_COUNT;g_mission_count=55;g_mission_cursor=31;
    enh_menu_action(ENH_PAGE_DOWN);assert(g_mission_cursor==41);
    char selection_settings[]="/private/tmp/mission-selection-XXXXXX";
    int selection_fd=mkstemp(selection_settings);assert(selection_fd>=0);close(selection_fd);
    snprintf(g_settings_path,sizeof g_settings_path,"%s",selection_settings);
    settings_save();g_set.mission_cursor=0;settings_load();assert(g_set.mission_cursor==41);
    unlink(selection_settings);g_settings_path[0]=0;
    enh_menu_action(ENH_PAGE_UP);assert(g_mission_cursor==31);
    enh_menu_action(ENH_BACK);assert(g_screen==SCREEN_MAIN);
    enh_menu_action(ENH_OK);assert(g_screen==SCREEN_MISSIONS && g_mission_cursor==31);
    g_mission_cursor=9;enh_menu_action(ENH_PAGE_UP);assert(g_mission_cursor==56);
    g_paused=g_mission_pause_menu=1;g_mission_cursor=31;
    enh_menu_action(ENH_PAGE_DOWN);assert(g_mission_cursor==41);
    enh_menu_action(ENH_PAGE_UP);assert(g_mission_cursor==31);
    g_paused=g_mission_pause_menu=0;g_mission_count=fixture_count;g_mission_cursor=0;
    enh_menu_action(ENH_UP); assert(g_mission_cursor==MISSION_COUNT+1);
    /* Clearing requires an explicit confirmation, defaulting to cancel. */
    enh_menu_action(ENH_UP); assert(g_mission_cursor==MISSION_COUNT);
    g_mission_record_count=1; g_mission_records[0].best=1234;
    enh_menu_action(ENH_OK); assert(g_mission_clear_confirm && !g_mission_clear_yes);
    enh_menu_action(ENH_OK); assert(!g_mission_clear_confirm && g_mission_record_count==1);
    enh_menu_action(ENH_OK); enh_menu_action(ENH_BACK); assert(g_mission_record_count==1);
    enh_menu_action(ENH_OK); enh_menu_action(ENH_DOWN); enh_menu_action(ENH_OK);
    assert(!g_mission_clear_confirm && !g_mission_record_count && mission_phase()==MISSION_OFF);
    enh_menu_action(ENH_DOWN);
    enh_menu_action(ENH_OK); assert(g_screen==SCREEN_MAIN && mission_phase()==MISSION_OFF);
    g_mission_cursor=0; /* Explicit fixture selection; reopening no longer resets it. */
    enh_menu_action(ENH_OK); enh_menu_action(ENH_DOWN); enh_menu_action(ENH_OK);
    assert(g_starting && mission_phase()==MISSION_ARMED && atomic_load(&g_mission_selected)==1);
    assert(enh_turbo()); /* Mission load is unpaced, rolling approach is paced. */
    atomic_store(&g_mission_phase,MISSION_ROLLING); assert(!enh_turbo());
    atomic_store(&g_mission_phase,MISSION_ARMED);
    /* Native attract ticks must retain a request during game selection. */
    attract_hook(); assert(mission_phase()==MISSION_ARMED);
    g_starting=0; g_attract_frame=0; atomic_store(&g_race_ready,1);
    atomic_store(&g_mission_phase,MISSION_PASSED);
    assert(enh_paused() && enh_inputs_owned());
    enh_menu_action(ENH_OK);
    assert(!enh_paused() && !race_restart_pending() && !race_restart_available());
    assert(g_returning && g_mission_after_return && mission_phase()==MISSION_ARMED);
    assert(atomic_load(&g_mission_selected)==1);
    g_returning=g_mission_after_return=0; atomic_store(&g_race_ready,1);
    atomic_store(&g_race_restart,0); atomic_store(&g_mission_phase,MISSION_CONTACT_FAILED);
    enh_menu_action(ENH_BACK); assert(g_paused && g_mission_pause_menu && mission_phase()==MISSION_OFF);
    enh_menu_action(ENH_BACK); assert(!g_mission_pause_menu && g_paused);
    int items[8]; assert(pause_items(items)==5 && items[3]==T_MISSIONS);
    atomic_store(&g_race_time_trial,1); g_pause_practice=1;
    int count=pause_items(items); assert(count==7);
    assert(items[3]==T_UNLIMITED_LAPS && items[5]==T_TRACK_DEBUG);
    g_pause_cursor=5; enh_menu_action(ENH_OK); assert(atomic_load(&g_track_debug));
    enh_menu_action(ENH_LEFT); assert(!atomic_load(&g_track_debug));
    atomic_store(&g_race_time_trial,0); g_pause_practice=0;
    g_pause_cursor=3; enh_menu_action(ENH_OK); assert(g_mission_pause_menu);
    enh_menu_action(ENH_OK); assert(!g_paused && g_returning && g_mission_after_return && !race_restart_pending());
    /* Pause restart must use the same full teardown as a result retry. */
    g_returning=g_mission_after_return=0; g_mission_pause_menu=0; g_paused=1;
    atomic_store(&g_race_ready,1); atomic_store(&g_mission_phase,MISSION_RUNNING);
    g_pause_cursor=2; enh_menu_action(ENH_OK);
    assert(!g_paused && g_returning && g_mission_after_return && !race_restart_pending());
    assert(!race_restart_available() && mission_phase()==MISSION_ARMED);
    /* Failed missions expose the exploration pause action without losing location. */
    g_returning=g_mission_after_return=0; g_paused=0;
    atomic_store(&g_race_ready,1); atomic_store(&g_mission_phase,MISSION_CONTACT_FAILED);
    assert(enh_escape() && g_paused && !g_mission_pause_menu);
    assert(mission_phase()==MISSION_CONTACT_FAILED && pause_items(items)<=8);
    assert(items[3]==T_MISSION_PRACTICE);
    enh_menu_action(ENH_OK); assert(!g_paused && mission_result()); /* Resume returns to results. */
    /* Exploration leaves the active mission without reloading or moving the car.
     * Convert traffic mode to Time Attack on the guest thread. */
    g_returning=g_mission_after_return=0; g_paused=1;
    atomic_store(&g_race_ready,1); atomic_store(&g_mission_phase,MISSION_RUNNING);
    count=pause_items(items); assert(count<=8 && items[3]==T_MISSION_PRACTICE);
    g_pause_cursor=3; enh_menu_action(ENH_OK);
    assert(!g_paused && !g_returning && mission_phase()==MISSION_OFF);
    assert(atomic_load(&g_track_debug) && atomic_load(&g_mission_explore_request));
    g_ram=calloc(1,RAM_SIZE); assert(g_ram);
    PPCContext explore={0}; explore.r[2]=0x100000;
    ST32(explore.r[2]+0x54,0x110000); ST32(0x110010,0x1234);
    ST32(explore.r[2]+0x488,0x120000); STF32(0x120174,724.5);
    mission_explore_tick(&explore);
    assert(LD32(0x110010)==(0x1234|0x400000));
    assert(race_time_trial() && race_unlimited_laps() && atomic_load(&g_practice_reset));
    assert(!atomic_load(&g_mission_explore_request) && LDF32(0x120174)==724.5);
    char copied[64]; atomic_store(&g_debug_x,-693.5f); atomic_store(&g_debug_z,-39.6f);
    g_paused=1; assert(enh_track_debug_position_text(copied,sizeof copied));
    assert(!strcmp(copied,"-693.5,-39.6"));
    atomic_store(&g_debug_heading,-176.6f); assert(enh_track_debug_heading_text(copied,sizeof copied));
    assert(!strcmp(copied,"-176.6"));
    atomic_store(&g_track_debug,0); assert(!enh_track_debug_position_text(copied,sizeof copied));
    assert(!enh_track_debug_heading_text(copied,sizeof copied));
    free(g_ram); g_ram=NULL;
    puts("mission menu: selection, briefing/back, armed lifecycle, result pause, retry and pause entry passed");
}
