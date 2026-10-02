#include "../runtime/enhanced.c"
#include <assert.h>
static int enabled, sensitivity=100, recentered;
int frontend_gyro_enabled(void) {return enabled;}
int frontend_gyro_available(void) {return 1;}
int frontend_gyro_ready(void) {return 1;}
double frontend_gyro_position(void) {return .5;}
int frontend_gyro_sensitivity(void) {return sensitivity;}
void frontend_gyro_set_enabled(int x) {enabled=x;}
void frontend_gyro_set_sensitivity(int x) {sensitivity=x<40?40:x>350?350:x;}
void frontend_gyro_recenter(void) {recentered++;}
void voodoo_set_scale(int n) {(void)n;}
void voodoo_set_wide(int n) {(void)n;}
void rt_log(const char *s,...) {(void)s;}
uint8_t *hw_nvram(void) { static uint8_t nv[8192]; return nv; }
uint64_t rt_now(void) {return 0;}
int main(int argc, char **argv) {
 assert(argc == 2);
 g_enhanced=1; g_font=(uint8_t*)1; g_booted=1;
 snprintf(g_settings_path,sizeof g_settings_path,"%s", argv[1]);
 assert(enh_escape()); assert(g_paused);
 enh_menu_action(ENH_DOWN); enh_menu_action(ENH_OK); assert(g_pause_controls);
 enh_menu_action(ENH_OK); assert(enabled);
 enh_menu_action(ENH_DOWN); enh_menu_action(ENH_RIGHT); assert(sensitivity==110);
 enh_menu_action(ENH_LEFT); assert(sensitivity==100);
 enh_menu_action(ENH_DOWN); enh_menu_action(ENH_OK); assert(recentered==1);
 enh_menu_action(ENH_BACK); assert(!g_pause_controls&&g_paused);
 enh_menu_action(ENH_BACK); assert(!g_paused);
 enabled=0;sensitivity=50;settings_load();assert(enabled&&sensitivity==100);
 remove(g_settings_path);
 g_frame=g_attract_frame=100;g_screen=SCREEN_PAGE;g_page=PAGE_CONTROLS;g_page_cursor=0;
 enh_menu_action(ENH_OK);assert(!enabled);
 enh_menu_action(ENH_DOWN);enh_menu_action(ENH_RIGHT);assert(sensitivity==110);
 remove(g_settings_path);
 puts("gyro menu navigation and settings persistence: passed");
}
