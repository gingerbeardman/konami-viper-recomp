#include "enhanced_headless.c"
#include <assert.h>
#include <stdlib.h>
uint8_t *g_ram;
uint8_t g_in[8];int16_t g_analog[4];
static uint64_t now;
uint64_t rt_now(void){return now;}
void rt_log(const char *fmt,...){(void)fmt;}
uint32_t rt_mmio_r8(uint32_t ea){(void)ea;abort();}
void rt_mmio_w8(uint32_t ea,uint32_t v){(void)ea;(void)v;abort();}
int main(void) {
    g_ram=calloc(1,RAM_SIZE);assert(g_ram);memset(g_in,255,sizeof g_in);
    wii_enhanced_init();assert(!wii_enhanced_menu_active());
    wii_enhanced_start();assert(g_in[3]&16);
    wii_enhanced_attract_hook();assert(wii_enhanced_menu_active());
    for(unsigned i=0;i<300;i++)wii_enhanced_frame();
    assert(wii_enhanced_menu_active());wii_enhanced_frame();assert(!wii_enhanced_menu_active());
    wii_enhanced_attract_hook();wii_enhanced_start();assert(!(g_in[3]&16));
    for(unsigned i=0;i<11;i++){wii_enhanced_frame();wii_enhanced_input_tick();assert(!(g_in[3]&16));}
    wii_enhanced_frame();wii_enhanced_input_tick();assert(g_in[3]&16);
    for(unsigned i=12;i<61;i++)wii_enhanced_frame();
    assert(!starting&&!seen_attract);
    const uint32_t addr=blank_strings[0].addr;const char *text=blank_strings[0].text;
    memcpy(g_ram+addr,text,strlen(text));wii_enhanced_frame();assert(g_ram[addr]==0);
    memcpy(g_ram+addr,text,strlen(text));g_ram[addr+1]^=1;wii_enhanced_frame();assert(g_ram[addr]==(uint8_t)text[0]);
    wii_enhanced_attract_hook();wii_enhanced_start();wii_enhanced_init();assert(g_in[3]&16);assert(frame_no==0&&!wii_enhanced_menu_active());
    now=(uint64_t)(CPU_HZ*18);wii_enhanced_input_tick();assert(!starting);
    wii_enhanced_attract_hook();wii_enhanced_input_tick();
#ifdef VIPER_WII_SCRIPTED_RACE
    assert(starting&&selected&&!(g_in[3]&16));assert(g_analog[1]==-200);
    now=(uint64_t)(CPU_HZ*66);wii_enhanced_input_tick();assert(g_analog[1]==200);
    wii_enhanced_init();assert(!selected&&!accelerator_logged);
#else
    assert(!starting);
#endif
    free(g_ram);puts("Wii enhanced bookkeeping PASS");
}
