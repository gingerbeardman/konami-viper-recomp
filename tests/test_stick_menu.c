#include "../runtime/enhanced.c"
#include <assert.h>
uint8_t g_in[8];
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
int main(int argc, char **argv) {
 assert(argc==2); g_enhanced=1; g_font=(uint8_t*)1; g_booted=1;
 snprintf(g_settings_path,sizeof g_settings_path,"%s",argv[1]);
 assert(frontend_stick_response()==2);
 g_frame=g_attract_frame=100;g_screen=SCREEN_PAGE;g_page=PAGE_CONTROLS;
 int rows[MAX_GAME_OPTIONS+2]; assert(page_rows(PAGE_CONTROLS,rows)==6);
 int cursor=5; /* stick response, followed by Switch triggers */
 g_page_cursor=cursor;
 enh_menu_action(ENH_LEFT);assert(frontend_stick_response()==1);
 enh_menu_action(ENH_LEFT);assert(frontend_stick_response()==0);
 enh_menu_action(ENH_LEFT);assert(frontend_stick_response()==2);
 enh_menu_action(ENH_RIGHT);assert(frontend_stick_response()==0);
 frontend_set_stick_response(2);settings_load();assert(frontend_stick_response()==0);
 FILE *f=fopen(g_settings_path,"w");assert(f);fputs("stick_response = 99\n",f);fclose(f);
 settings_load();assert(frontend_stick_response()==2);
 g_attract_frame=0;g_frame=500;assert(enh_escape());
 enh_menu_action(ENH_DOWN);enh_menu_action(ENH_OK);assert(g_pause_controls);
 g_controls_cursor=cursor;enh_menu_action(ENH_LEFT);assert(frontend_stick_response()==1);
 g_controls_cursor=6; assert(frontend_switch_trigger_layout()==1);
 enh_menu_action(ENH_LEFT); assert(frontend_switch_trigger_layout()==0);
 frontend_set_switch_trigger_layout(1); settings_load(); assert(frontend_switch_trigger_layout()==0);
 enh_menu_action(ENH_RIGHT); assert(frontend_switch_trigger_layout()==1);
 enh_menu_action(ENH_DOWN); assert(g_controls_cursor==0);
 enh_menu_action(ENH_UP); assert(g_controls_cursor==6);
 enh_menu_action(ENH_BACK);assert(!g_pause_controls && g_paused);
 enh_menu_action(ENH_BACK); assert(!g_paused);
 /* Practice is revealed by Shift Up at entry, only during Time Trial. */
 int items[5];
 shift_up_held=1; atomic_store(&g_race_ready,1);
 assert(enh_escape()); assert(pause_items(items)==4);
 enh_menu_action(ENH_BACK);
 atomic_store(&g_race_time_trial,1); shift_up_held=0;
 assert(enh_escape()); assert(pause_items(items)==4);
 shift_up_held=1; assert(pause_items(items)==4); /* pressing after entry does not reveal it */
 enh_menu_action(ENH_BACK);
 assert(enh_escape()); assert(pause_items(items)==5 && items[3]==T_UNLIMITED_LAPS);
 shift_up_held=0; assert(pause_items(items)==5); /* release does not hide it */
 enh_menu_action(ENH_UP); assert(g_pause_cursor==4);
 enh_menu_action(ENH_UP); assert(g_pause_cursor==3);
 enh_menu_action(ENH_OK); assert(g_paused && race_unlimited_laps());
 enh_menu_action(ENH_LEFT); assert(!race_unlimited_laps());
 enh_menu_action(ENH_RIGHT); assert(race_unlimited_laps());
 enh_menu_action(ENH_BACK);
 assert(enh_escape()); assert(pause_items(items)==4 && race_unlimited_laps());
 enh_menu_action(ENH_BACK); race_restart_cancel(); assert(!race_unlimited_laps());
 g_frame=g_attract_frame=600; g_screen=SCREEN_PAGE; g_page=PAGE_CONTROLS; g_page_cursor=6;
 enh_menu_action(ENH_OK); assert(frontend_switch_trigger_layout()==0);
 frontend_set_switch_trigger_layout(1); settings_load(); assert(frontend_switch_trigger_layout()==0);
 enh_menu_action(ENH_LEFT); assert(frontend_switch_trigger_layout()==1);
 enh_menu_action(ENH_DOWN); assert(g_page_cursor==0);
 enh_menu_action(ENH_UP); assert(g_page_cursor==6);
 f=fopen(g_settings_path,"w"); assert(f); fputs("switch_trigger_layout = 99\n",f); fclose(f);
 frontend_set_switch_trigger_layout(0); settings_load(); assert(frontend_switch_trigger_layout()==1);
 /* Restart appears between Controls and Main Menu only during a loaded race. */
 g_attract_frame=0; g_frame=1000;
 enh_focus_lost(); assert(g_paused); /* play can pause before restart records exist */
 enh_menu_action(ENH_BACK); assert(!g_paused);
 enh_focus_lost(); assert(g_paused && g_pause_cursor==0);
 g_pause_controls=1; enh_focus_lost(); assert(g_paused && g_pause_controls);
 g_pause_controls=0; enh_menu_action(ENH_BACK); assert(!g_paused);
 assert(enh_escape());
 atomic_store(&g_race_ready,1);
 enh_menu_action(ENH_UP); assert(g_pause_cursor==3);
 enh_menu_action(ENH_UP); assert(g_pause_cursor==2);
 enh_menu_action(ENH_OK); assert(!g_paused && race_restart_pending()==1);
 assert(enh_inputs_owned() && !enh_menu_active());
 race_restart_cancel(); assert(enh_escape());
 enh_menu_action(ENH_UP); assert(g_pause_cursor==2);
 enh_menu_action(ENH_BACK); assert(!g_paused);
 /* Leaving practice through MAIN MENU clears it immediately, before native dispatch. */
 g_returning=0; g_apply=0;
 atomic_store(&g_race_ready,1); atomic_store(&g_race_time_trial,1);
 atomic_store(&g_race_unlimited_laps,1); atomic_store(&g_practice_reset,0);
 assert(race_practice_hud_visible()); assert(enh_escape());
 g_pause_cursor=pause_items(items)-1; enh_menu_action(ENH_OK);
 assert(!race_unlimited_laps() && !race_time_trial() && !race_practice_hud_visible());
 g_returning=0; g_apply=0;
 /* A native return to attract also cancels stale race state. */
 atomic_store(&g_race_ready,1); atomic_store(&g_race_time_trial,1);
 atomic_store(&g_race_unlimited_laps,1);
 attract_hook();
 assert(!race_unlimited_laps() && !race_restart_available());
 remove(g_settings_path);
 puts("stick response menu, pause, persistence and invalid settings: passed");
}
