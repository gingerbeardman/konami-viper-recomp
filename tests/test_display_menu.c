#include "../runtime/enhanced.c"
#include <assert.h>
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
 assert(argc==2);
 g_enhanced=1;g_font=(uint8_t*)1;g_booted=1;g_frame=g_attract_frame=100;
 snprintf(g_settings_path,sizeof g_settings_path,"%s",argv[1]);
 g_screen=SCREEN_PAGE;g_page=PAGE_DISPLAY;g_page_cursor=0;
 int rows[MAX_GAME_OPTIONS+2];int count=page_rows(PAGE_DISPLAY,rows);
 int filter_row=-1;for(int i=0;i<count;i++) if(rows[i]==-10) filter_row=i;
 assert(filter_row>=0 && g_set.texture_filter==0);
 for(int i=0;i<filter_row;i++) enh_menu_action(ENH_DOWN);
 enh_menu_action(ENH_RIGHT);assert(g_set.texture_filter==1 && applied_filter==1 && enh_texture_filter()==1);
 enh_menu_action(ENH_RIGHT);assert(g_set.texture_filter==2 && applied_filter==2);
 enh_menu_action(ENH_RIGHT);assert(g_set.texture_filter==0 && applied_filter==0);
 enh_menu_action(ENH_LEFT);assert(g_set.texture_filter==2 && applied_filter==2);
 g_set.texture_filter=0;settings_load();assert(g_set.texture_filter==2);
 FILE *f=fopen(g_settings_path,"w");assert(f);fputs("texture_filter = 99\n",f);fclose(f);
 settings_load();assert(g_set.texture_filter==0);
 g_enhanced=0;g_set.texture_filter=1;assert(enh_texture_filter()==0);
 remove(g_settings_path);
 puts("texture filter menu cycling, live application and persistence: passed");
}
