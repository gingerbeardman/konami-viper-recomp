/* Pure conversion bookkeeping from dc/enhanced_menu.c. No drawing, font,
 * controller mapping or host-time scheduling. Scripted race is opt-in. */
#include "enhanced_headless.h"
#include "runtime.h"
#include "game_config.h"
#include <string.h>
extern uint8_t g_in[];
extern int16_t g_analog[];
static unsigned frame_no,attract_frame,start_until;
static int seen_attract,starting;
#ifdef VIPER_WII_SCRIPTED_RACE
static int selected,accelerator_logged;
#endif
static const struct {uint32_t addr;const char *text;} blank_strings[]=GAME_ENH_BLANK_STRINGS;
static void apply_input(void) {
    if(wii_enhanced_menu_active()) {g_in[3]|=0x17;g_in[4]|=1;}
    if(starting&&frame_no<start_until)g_in[3]&=(uint8_t)~0x10;
    else if(starting&&frame_no>=start_until)g_in[3]|=0x10;
}
void wii_enhanced_init(void) {
    if(starting)g_in[3]|=0x10;
    frame_no=attract_frame=start_until=0;seen_attract=starting=0;
#ifdef VIPER_WII_SCRIPTED_RACE
    selected=accelerator_logged=0;
#endif
}
void wii_enhanced_attract_hook(void) {
    if(!seen_attract)rt_log("VIPER MENU attract hook reached\n");
    seen_attract=1;attract_frame=frame_no;
    if(starting&&frame_no>=start_until&&frame_no-start_until>240)starting=0;
}
int wii_enhanced_menu_active(void) {
    return seen_attract&&!starting&&frame_no-attract_frame<=300;
}
void wii_enhanced_start(void) {
    if(!wii_enhanced_menu_active())return;
    starting=1;start_until=frame_no+12;
    rt_log("VIPER MENU start game\n");apply_input();
}
void wii_enhanced_frame(void) {
    frame_no++;
    for(unsigned i=0;blank_strings[i].text;i++) {
        uint32_t addr=blank_strings[i].addr;
        const char *s=blank_strings[i].text;size_t n=strlen(s);
        if(addr+n>=RAM_SIZE)continue;
        size_t j=0;
        while(j<n&&LD8(addr+(uint32_t)j)==(uint8_t)s[j])j++;
        if(j==n)ST8(addr,0);
    }
    if(starting&&frame_no-attract_frame>60){starting=0;seen_attract=0;}
}
void wii_enhanced_input_tick(void) {
#ifdef VIPER_WII_SCRIPTED_RACE
    /* Same released controls as the disconnected-pad scripted DC workload. */
    g_in[3]|=0x57;g_in[4]|=1;
    g_analog[0]=0;g_analog[1]=g_analog[2]=-200;
#if GAME_HAS_HANDBRAKE
    g_analog[3]=-200;
#endif
#endif
    apply_input();
#ifdef VIPER_WII_SCRIPTED_RACE
    if(rt_now()/CPU_HZ>=66) {
        g_analog[1]=200;
        if(!accelerator_logged){accelerator_logged=1;rt_log("VIPER DIAGNOSTIC scripted full accelerator\n");}
    }
#ifdef VIPER_WII_SCRIPTED_VIEW
    /* Diagnostic: tap START/VIEW (6 ticks every 2 s) through the race, to
     * cycle the cameras into interior view. */
    {static unsigned tick;uint64_t g=rt_now()/CPU_HZ;
     if(g>=68){if(tick%115<6){g_in[3]&=(uint8_t)~0x10;if(tick%115==0)rt_log("VIPER DIAGNOSTIC scripted view press g=%u\n",(unsigned)g);}tick++;}}
#endif
    if(!selected&&rt_now()/CPU_HZ>=18&&wii_enhanced_menu_active()) {
        selected=1;rt_log("VIPER DIAGNOSTIC Wii selecting enhanced Start Game\n");
        wii_enhanced_start();
    }
#endif
}
