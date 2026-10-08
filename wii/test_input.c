#include "input.h"
#include <assert.h>
#include <stdio.h>
int main(void){
 uint8_t i3=0xa8,i4=0xaa;int16_t a[4]={7,8,9,77};
 wii_input_map(NULL,0,&i3,&i4,a);assert(i3==0xff&&i4==0xab&&a[0]==0&&a[1]==-200&&a[2]==-200&&a[3]==77);
 WiiInputState s={WII_IN_SERVICE|WII_IN_TEST|WII_IN_COIN|WII_IN_START|WII_IN_DOWN|WII_IN_UP|WII_IN_HANDBRAKE,900,900,-900,1};
 wii_input_map(&s,1,&i3,&i4,a);assert(i3==0xa8&&i4==0xaa&&a[0]==200&&a[1]==200&&a[2]==-200&&a[3]==200);
 s.buttons=WII_IN_LEFT|WII_IN_ACCEL|WII_IN_BRAKE;s.accel=s.brake=-200;
 wii_input_map(&s,1,&i3,&i4,a);assert(a[0]==-200&&a[1]==200&&a[2]==200&&a[3]==-200);
 WiiInputLatch l={0};uint32_t edges;s.buttons=WII_IN_START;wii_input_observe(&l,&s);
 s.buttons=0;s.steer=42;wii_input_observe(&l,&s);
 WiiInputState got=wii_input_consume(&l,&edges);assert(edges==WII_IN_START&&got.buttons==WII_IN_START&&got.steer==42);
 got=wii_input_consume(&l,&edges);assert(!edges&&!got.buttons);
 s.buttons=WII_IN_START;wii_input_observe(&l,&s);wii_input_consume(&l,&edges);assert(edges==WII_IN_START);
 wii_input_observe(&l,&s);wii_input_consume(&l,&edges);assert(!edges);
 s.buttons|=WII_IN_ACCEL;wii_input_observe(&l,&s);wii_input_observe(&l,NULL);
 got=wii_input_consume(&l,&edges);assert(!edges&&!got.connected&&!got.buttons);
 wii_input_observe(&l,&s);wii_input_consume(&l,&edges);assert(edges==(WII_IN_START|WII_IN_ACCEL));
 puts("Wii input mapper/latch PASS");return 0;
}
