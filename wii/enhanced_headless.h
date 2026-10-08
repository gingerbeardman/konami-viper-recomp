#ifndef VIPER_WII_ENHANCED_HEADLESS_H
#define VIPER_WII_ENHANCED_HEADLESS_H
void wii_enhanced_init(void);
void wii_enhanced_attract_hook(void);
void wii_enhanced_frame(void);
int wii_enhanced_menu_active(void);
void wii_enhanced_start(void);
/* Call on the guest input scheduler at CPU_HZ/57.5, not wall-clock time. */
void wii_enhanced_input_tick(void);
#endif
