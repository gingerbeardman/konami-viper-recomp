#ifndef VIPER_WII_INPUT_H
#define VIPER_WII_INPUT_H
#include <stdint.h>
enum { WII_IN_SERVICE=1u<<0,WII_IN_TEST=1u<<1,WII_IN_COIN=1u<<2,
 WII_IN_START=1u<<3,WII_IN_DOWN=1u<<4,WII_IN_UP=1u<<5,
 WII_IN_ACCEL=1u<<6,WII_IN_BRAKE=1u<<7,WII_IN_HANDBRAKE=1u<<8,
 WII_IN_LEFT=1u<<9,WII_IN_RIGHT=1u<<10,
 WII_IN_RESTART=1u<<11 /* restart race (enhanced), not a cabinet input */ };
typedef struct { uint32_t buttons; int steer,accel,brake; int connected; } WiiInputState;
typedef struct { WiiInputState held; uint32_t rises; } WiiInputLatch;
void wii_input_observe(WiiInputLatch *,const WiiInputState *);
WiiInputState wii_input_consume(WiiInputLatch *,uint32_t *edges);
void wii_input_map(const WiiInputState *,int handbrake,uint8_t *in3,uint8_t *in4,int16_t analog[4]);
void wii_input_init(void);
void wii_input_poll(void);
void wii_input_shutdown(void);
void wii_input_guest_tick(void);
/* Pause menu (HOME): open flag and row, set by the input poll; give-up is
 * wii/main.c's (in a race only). */
extern volatile int wii_pause_open,wii_pause_cursor;
int wii_give_up_available(void);
void wii_request_give_up(void);
#endif
