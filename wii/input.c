#include "input.h"
#include <string.h>
static int clamp(int x,int lo,int hi){return x<lo?lo:x>hi?hi:x;}
void wii_input_observe(WiiInputLatch *l,const WiiInputState *s){
    if(!s||!s->connected){memset(l,0,sizeof *l);return;}
    l->rises|=s->buttons&~l->held.buttons;l->held=*s;
}
WiiInputState wii_input_consume(WiiInputLatch *l,uint32_t *edges){
    WiiInputState s=l->held;*edges=l->rises;s.buttons|=l->rises;l->rises=0;return s;
}
static void bit(uint8_t *p,unsigned b,int pressed){if(pressed)*p&=(uint8_t)~(1u<<b);else *p|=(uint8_t)(1u<<b);}
void wii_input_map(const WiiInputState *s,int handbrake,uint8_t *in3,uint8_t *in4,int16_t a[4]){
    uint32_t b=s&&s->connected?s->buttons:0;
    bit(in3,0,b&WII_IN_SERVICE);bit(in3,1,b&WII_IN_TEST);bit(in3,2,b&WII_IN_COIN);
    bit(in3,4,b&WII_IN_START);bit(in3,6,b&WII_IN_DOWN);bit(in4,0,b&WII_IN_UP);
    int steer=s&&s->connected?clamp(s->steer,-200,200):0;
    if(!!(b&WII_IN_LEFT)!=!!(b&WII_IN_RIGHT))steer=b&WII_IN_LEFT?-200:200;
    a[0]=steer;a[1]=b&WII_IN_ACCEL?200:s&&s->connected?clamp(s->accel,-200,200):-200;
    a[2]=b&WII_IN_BRAKE?200:s&&s->connected?clamp(s->brake,-200,200):-200;
    if(handbrake)a[3]=b&WII_IN_HANDBRAKE?200:-200;
}
