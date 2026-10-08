#include "../runtime/enhanced.c"
#include <assert.h>
uint8_t g_in[8];
int frontend_shift_up_held(void) { return 0; }
static int explorer_mode;
int explorer_active(void) { return explorer_mode != 0; }
int explorer_free(void) { return explorer_mode == 2; }
float explorer_speed(void) { return 0; }
float explorer_height(void) { return 0; }
void explorer_toggle(void) {}
void explorer_free_toggle(void) {}
void explorer_drive(float f,float v) {(void)f;(void)v;}
void explorer_look(float v) {(void)v;}
void explorer_pitch(float v) {(void)v;}
static int switch_layout=1;
int frontend_switch_trigger_layout(void) { return switch_layout; }
void frontend_set_switch_trigger_layout(int v) { switch_layout = v == 0 ? 0 : 1; }
static int stick_response=2;
int frontend_stick_response(void) { return stick_response; }
void frontend_set_stick_response(int v) { stick_response = v>=0 && v<=2 ? v : 2; }
double frontend_stick_position(void) { return .5; }
double frontend_steering_position(void) { return .25; }
static int enabled, sensitivity=100, recentered;
uint8_t *g_ram;
uint32_t rt_mmio_r8(uint32_t address) { (void)address; assert(0); return 0; }
void rt_mmio_w8(uint32_t address, uint32_t value) { (void)address; (void)value; assert(0); }
uint32_t rt_mmio_r32(uint32_t address) { (void)address; assert(0); return 0; }
void rt_mmio_w32(uint32_t address, uint32_t value) { (void)address; (void)value; assert(0); }
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
 int distance_row=-1;for(int i=0;i<count;i++) if(rows[i]==-12) distance_row=i;
 assert(distance_row>=0 && g_set.draw_distance==0);
 g_page_cursor=distance_row;
 enh_menu_action(ENH_RIGHT);assert(g_set.draw_distance==1);
 enh_menu_action(ENH_RIGHT);assert(g_set.draw_distance==2);
 enh_menu_action(ENH_RIGHT);assert(g_set.draw_distance==0);
 enh_menu_action(ENH_LEFT);assert(g_set.draw_distance==2);
 g_set.draw_distance=0;settings_load();assert(g_set.draw_distance==2);
 f=fopen(g_settings_path,"w");assert(f);fputs("draw_distance = 99\n",f);fclose(f);
 settings_load();assert(g_set.draw_distance==0);
 g_ram=calloc(1,RAM_SIZE);assert(g_ram);
 uint32_t m=GAME_ENH_WIDE_PROJ_MATRIX,fr=GAME_ENH_WIDE_PROJ_FRUSTUM;
 STF32(fr+16,1);STF32(fr+20,100);STF32(m,2);STF32(m+8,3);
 STF32(m+16,-101.0/99);STF32(m+20,-200.0/99);
 uint32_t original_a=LD32(m+16),original_b=LD32(m+20);
 g_set.draw_distance=1;distance_capture(0);
 assert(g_distance_slot[0].valid && LDF32(fr+20)==200);
 /* A point beyond the original far plane is now inside clip space. */
 double z=-150,clip=(LDF32(m+16)*z+LDF32(m+20))/-z;
 assert(clip<1 && clip>-1 && LDF32(m)==2 && LDF32(m+8)==3);
 for(int i=0;i<10;i++) {
  g_set.draw_distance=2;distance_apply(0);assert(LDF32(fr+20)==400);
  g_set.draw_distance=0;distance_apply(0);
  assert(LDF32(fr+20)==100 && LD32(m+16)==original_a && LD32(m+20)==original_b);
 }
 /* Mirror flips perspective once, restores exactly, and excludes the HUD. */
 atomic_store(&g_race_ready,1);g_set.mirror=1;STF32(m+4,.25);
 mirror_slot(0);assert(LDF32(m)==-2 && LDF32(m+4)==-.25);
 mirror_slot(0);assert(LDF32(m)==-2);
 ST8(0x3452,0);assert(enh_mirror_projection());
 g_set.mirror=0;mirror_slot(0);assert(LDF32(m)==2 && LDF32(m+4)==.25);
 g_set.mirror=1;g_distance_slot[1].valid=0;
 STF32(m+24,7);mirror_slot(1);assert(LDF32(m+24)==7);
 ST8(0x3452,1);assert(!enh_mirror_projection());
 g_set.mirror=0;atomic_store(&g_race_ready,0);
 /* A freshly written projection does not compound the previous multiplier. */
 g_set.draw_distance=1;distance_capture(0);assert(LDF32(fr+20)==200);
 STF32(fr+16,1);STF32(fr+20,100);STF32(m+16,-2.0/99);STF32(m+20,-101.0/99);
 uint32_t ortho[2]={LD32(m+16),LD32(m+20)};
 distance_capture(0);assert(!g_distance_slot[0].valid && LDF32(fr+20)==100);
 assert(LD32(m+16)==ortho[0] && LD32(m+20)==ortho[1]);
 free(g_ram);g_ram=NULL;
 g_enhanced=0;g_set.texture_filter=1;assert(enh_texture_filter()==0);
 remove(g_settings_path);
 puts("display settings, draw distance clipping, restore and HUD exclusion: passed");
}
