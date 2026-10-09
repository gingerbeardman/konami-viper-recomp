#ifndef VIPER_WII_MENU_H
#define VIPER_WII_MENU_H
void wii_menu_init(void);
void wii_menu_draw(void);
/* A short message shown at the top of the picture for about two seconds. */
void wii_menu_notice(const char *text);
#include <gccore.h>
/* The HOME pause menu over the frozen frame; shade 255 restores the frame. */
void wii_menu_pause_draw(GXTexObj *frame,unsigned shade);
#endif
