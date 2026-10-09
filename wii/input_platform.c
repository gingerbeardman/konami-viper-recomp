/* Poll from the main/VSync thread, never an IRQ. Guest consumes a mutex-protected
 * snapshot. Button rises survive release until one guest tick; repeated rises
 * of one button coalesce. Disconnect discards pending input, analog never latches. */
#include "input.h"
#include "enhanced_headless.h"
#include "game_config.h"
#include "runtime.h"
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <math.h>
#include <string.h>
extern uint8_t g_in[];
extern int16_t g_analog[];
static mutex_t mutex;
static WiiInputLatch latch;
static int initialized,remote_connected;
static int last_remote=-1,last_pad=-1,remote_enabled,format_result;
static uint64_t poll_count;
typedef struct {int kind,a,b,c;uint64_t poll;} InputDiagnostic;
static InputDiagnostic diagnostics[16];
static unsigned diagnostic_count,diagnostic_dropped;
/* Caller holds mutex, or initialization occurs before any guest execution. */
static void queue_diagnostic(int kind,int a,int b,int c){
    if(diagnostic_count<16)diagnostics[diagnostic_count++]=(InputDiagnostic){kind,a,b,c,poll_count};
    else diagnostic_dropped++;
}
static float roll_center;
/* Our menu stays hidden in attract until a button is pressed; that press
 * only reveals it (buttons are ignored until all are released). It hides
 * again once a game starts. */
static volatile int menu_revealed,menu_reveal_request;
static int menu_swallow;
int wii_menu_revealed(void){return menu_revealed;}
void wii_input_init(void){
    if(initialized)return;
    if(LWP_MutexInit(&mutex,0))rt_fatal("Wii input mutex initialization");
    PAD_Init();
    int init_result=WPAD_Init();remote_enabled=init_result==WPAD_ERR_NONE;
    format_result=remote_enabled?WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC):init_result;
    queue_diagnostic(0,init_result,format_result,remote_enabled);
    /* A Bluetooth initialization failure leaves GameCube input operational. */
    memset(&latch,0,sizeof latch);initialized=1;
}
/* Wii Remote rumble from the cabinet's K-type steering motor (bit 7 drive,
 * bits 0-3 torque). The remote's motor is on/off and every change is a
 * Bluetooth command, and nothing happens during the first 10 s after boot
 * (the game's own motor test) or without a remote.
 * The game shakes the wheel over rough surfaces and impacts: commands with
 * bit 5 set alternating with torque (cobbles: 0xa0 and 0x89 about 14 times a
 * second), or torque flipping direction (bit 4) from the last command with
 * torque (car rubs: 0x8a 0x80 0x9a 0x80, zero commands between). Strength is
 * the game's own torque, mapped directly (wii/rumble_tester.c, button 2):
 * software PWM with the motor on for torque/15 of each period, the torque
 * averaged over the last TORQUE_WINDOW polls (60 Hz) so the game's zero
 * commands between shakes count. Light shakes (torque up to 9: cobbles, wall
 * scrapes) use an 8-poll period, heavy ones (car rubs, big bumps) 6, steady
 * steering pull from torque VIPER_WII_RUMBLE_MIN_TORQUE up 5. A shake lasts
 * SHAKE_HOLD polls past its last command. Hold B and press MINUS to cycle
 * FULL, MILD (half the on-time, same rhythm) and OFF. */
#ifndef VIPER_WII_RUMBLE_MIN_TORQUE
#define VIPER_WII_RUMBLE_MIN_TORQUE 6
#endif
#define LIGHT_SHAKE_PERIOD 8
#define HEAVY_SHAKE_PERIOD 6
#define PULL_PERIOD 5
#define SHAKE_HOLD 7
#define TORQUE_WINDOW 8
extern volatile uint8_t wii_motor;
enum{RUMBLE_FULL,RUMBLE_MILD,RUMBLE_OFF};
static int rumble_on,rumble_mode;
static uint8_t shake_prev,shake_drive;static unsigned shake_left,shake_level,phase;
static uint8_t torque_ring[TORQUE_WINDOW];static unsigned torque_sum,torque_at;
/* drive: the last command with torque */
static unsigned shake_level_of(uint8_t prev,uint8_t drive,uint8_t m){
    unsigned level=(m&15)>(prev&15)?(m&15):(prev&15);
    if(!(m&0x80))return 0;
    if(m&0x20)return level?level:8;
    if((drive&0x80)&&(drive&15)&&(m&15)&&((drive^m)&0x10))return (m&15)>(drive&15)?(m&15):(drive&15);
    return 0;
}
static void rumble_set(int want){
    if(want!=rumble_on&&remote_enabled){WPAD_Rumble(WPAD_CHAN_0,want);rumble_on=want;}
}
static void rumble_cycle_mode(void){
    static const char *const names[]={"RUMBLE FULL","RUMBLE MILD","RUMBLE OFF"};
    rumble_mode=(rumble_mode+1)%3;
    void wii_menu_notice(const char *);wii_menu_notice(names[rumble_mode]);
}
/* motor on for torque/15 of every period polls (rounded); MILD halves it */
static int rumble_pwm(unsigned period,unsigned torque_x_window){
    unsigned on=(2*period*torque_x_window+15*TORQUE_WINDOW)/(30*TORQUE_WINDOW);
    if(rumble_mode==RUMBLE_MILD)on=on>1?on/2:on;
    return phase++%period<on;
}
static void rumble_update(int connected){
    uint8_t m=wii_motor;
    unsigned torque=(m&0x80)?m&15:0;
    torque_sum+=torque-torque_ring[torque_at];torque_ring[torque_at]=(uint8_t)torque;torque_at=(torque_at+1)%TORQUE_WINDOW;
    if(m!=shake_prev){
        unsigned level=shake_level_of(shake_prev,shake_drive,m);
        if(level){shake_level=level;if(!shake_left)phase=0;shake_left=SHAKE_HOLD;}
        shake_prev=m;if((m&0x80)&&(m&15))shake_drive=m;
    }
    if(!connected||poll_count<=600||rumble_mode==RUMBLE_OFF){if(connected)rumble_set(0);shake_left=0;return;}
    if(shake_left){
        shake_left--;
        rumble_set(rumble_pwm(shake_level>=10?HEAVY_SHAKE_PERIOD:LIGHT_SHAKE_PERIOD,torque_sum));
        return;
    }
    if(torque>=VIPER_WII_RUMBLE_MIN_TORQUE)rumble_set(rumble_pwm(PULL_PERIOD,torque*TORQUE_WINDOW));
    else{rumble_set(0);phase=0;}
}
void wii_input_shutdown(void){
    if(initialized&&remote_enabled){if(rumble_on)WPAD_Rumble(WPAD_CHAN_0,0);WPAD_Shutdown();}
}
void wii_input_poll(void){
    if(!initialized)return;
    poll_count++;
    u32 pads=PAD_ScanPads();if(remote_enabled)WPAD_ScanPads();
    WiiInputState s={0,0,-200,-200,0};u32 type;
    int probe=remote_enabled?WPAD_Probe(WPAD_CHAN_0,&type):WPAD_ERR_NOT_READY;
    int connected=probe==WPAD_ERR_NONE,pad_connected=!!(pads&PAD_CHAN0_BIT);
    if(connected!=last_remote||pad_connected!=last_pad){
        LWP_MutexLock(mutex);queue_diagnostic(1,connected,probe,pad_connected);LWP_MutexUnlock(mutex);
        last_remote=connected;last_pad=pad_connected;
    }
    if(connected){
        if(!remote_connected&&format_result!=WPAD_ERR_NONE){
            format_result=WPAD_SetDataFormat(WPAD_CHAN_0,WPAD_FMT_BTNS_ACC);
            LWP_MutexLock(mutex);queue_diagnostic(2,format_result,0,0);LWP_MutexUnlock(mutex);
        }
        WPADData *d=WPAD_Data(WPAD_CHAN_0);u32 b=d->btns_h;
        if(d->btns_d&WPAD_BUTTON_HOME){void wii_request_quit(int);wii_request_quit(1);}
        s.connected=1;
        /* Held sideways and turned like a steering wheel, the remote's long
         * axis tilts: its gravity component g.y against the other two axes.
         * (wiiuse roll is rotation about the long axis: a rolling pin.) */
        /* Calibrated on hardware (steering recorder, 2026-10-08): tilting
         * left is positive; full lock at 45 degrees from a level centre of
         * 0 (MINUS recentres), with a 2.5 degree deadzone; the arcade wheel
         * spans -200 (left) to +200 (right). */
        float gx=d->gforce.x,gy=d->gforce.y,gz=d->gforce.z;
        float tilt=atan2f(gy,sqrtf(gx*gx+gz*gz))*(180.0f/(float)M_PI);
        if(isfinite(tilt)){
            if(d->btns_d&&!menu_revealed&&wii_enhanced_menu_active())menu_reveal_request=1;   /* swallowed: not a display change */
            else
#ifdef VIPER_WII_DISPLAY_MULTI
            if((d->btns_d&WPAD_BUTTON_MINUS)&&(b&WPAD_BUTTON_B))rumble_cycle_mode();
            else if(d->btns_d&WPAD_BUTTON_MINUS){void wii_gx_display_cycle(void);wii_gx_display_cycle();}
#else
            if((d->btns_d&WPAD_BUTTON_MINUS)&&(b&WPAD_BUTTON_B))rumble_cycle_mode();
            else if(d->btns_d&WPAD_BUTTON_MINUS)roll_center=tilt;
#endif
            float delta=tilt-roll_center,mag=fabsf(delta)-2.5f;
            float frac=mag<=0?0:fminf(1,mag/(45.0f-2.5f));
            s.steer=(int)lroundf((delta>0?-frac:frac)*200);
        }
        /* Held sideways, D-pad on the left (player's directions in
         * brackets): RIGHT (up) shifts up, LEFT (down) shifts down, UP
         * (left) is the handbrake, DOWN (right) restarts the race. A and Plus are the cabinet's START/VIEW button.
         * Steering is tilt only. 1 gas; B and 2 brake (user layout). */
        if(b&(WPAD_BUTTON_PLUS|WPAD_BUTTON_A))s.buttons|=WII_IN_START;
        if(b&WPAD_BUTTON_1)s.buttons|=WII_IN_ACCEL;
        if(b&(WPAD_BUTTON_B|WPAD_BUTTON_2))s.buttons|=WII_IN_BRAKE;
        if(b&WPAD_BUTTON_UP)s.buttons|=WII_IN_HANDBRAKE;
        if(b&WPAD_BUTTON_DOWN)s.buttons|=WII_IN_RESTART;
        if(b&WPAD_BUTTON_RIGHT)s.buttons|=WII_IN_UP;
        if(b&WPAD_BUTTON_LEFT)s.buttons|=WII_IN_DOWN;
        remote_connected=1;
    }else remote_connected=0;
#ifndef VIPER_WII_NO_RUMBLE
    rumble_update(connected);
#endif
    if(pads&PAD_CHAN0_BIT){
        u16 b=PAD_ButtonsHeld(0);s.connected=1;
        int x=PAD_StickX(0);if(x>12||x< -12)s.steer=x>0?(x-12)*200/115:(x+12)*200/116;
        s.accel=-200+PAD_TriggerR(0)*400/255;s.brake=-200+PAD_TriggerL(0)*400/255;
        if(b&PAD_BUTTON_START)s.buttons|=WII_IN_START;
        if(b&PAD_BUTTON_A)s.buttons|=WII_IN_ACCEL;
        if(b&PAD_BUTTON_B)s.buttons|=WII_IN_BRAKE;
        if(b&PAD_BUTTON_X)s.buttons|=WII_IN_HANDBRAKE;
        if(b&PAD_TRIGGER_L)s.buttons|=WII_IN_DOWN;
        if(b&PAD_TRIGGER_R)s.buttons|=WII_IN_UP;
        if(b&PAD_BUTTON_LEFT)s.buttons|=WII_IN_LEFT;
        if(b&PAD_BUTTON_RIGHT)s.buttons|=WII_IN_RIGHT;
        if(b&PAD_TRIGGER_Z)s.buttons|=WII_IN_COIN;
        if(b&PAD_BUTTON_Y)s.buttons|=WII_IN_RESTART;
    }
    LWP_MutexLock(mutex);wii_input_observe(&latch,&s);LWP_MutexUnlock(mutex);
}
void wii_input_guest_tick(void){
    if(!initialized)return;
    static unsigned tick_count;
    uint64_t observed_polls;
    uint32_t edges;InputDiagnostic pending[16];unsigned count,dropped;
    LWP_MutexLock(mutex);
    WiiInputState s=wii_input_consume(&latch,&edges);
    observed_polls=poll_count;
    count=diagnostic_count;dropped=diagnostic_dropped;
    memcpy(pending,diagnostics,count*sizeof pending[0]);diagnostic_count=diagnostic_dropped=0;
    LWP_MutexUnlock(mutex);
    /* Only the guest thread touches rt_log's FILE, including checkpoint reopen. */
    for(unsigned i=0;i<count;i++){
        InputDiagnostic *d=&pending[i];
        if(d->kind==0)rt_log("VIPER WII INPUT INIT wpad=%d format=%d remote_enabled=%d\n",d->a,d->b,d->c);
        else if(d->kind==1)rt_log("VIPER WII INPUT CONNECTION poll=%llu remote=%d probe=%d gamecube=%d\n",
            (unsigned long long)d->poll,d->a,d->b,d->c);
        else rt_log("VIPER WII INPUT FORMAT reconnect result=%d\n",d->a);
    }
    if(dropped)rt_log("VIPER WII INPUT DIAGNOSTICS dropped=%u\n",dropped);
    if(!wii_enhanced_menu_active())menu_revealed=menu_reveal_request=0;
    else if(!menu_revealed&&(edges||menu_reveal_request)){menu_revealed=1;menu_reveal_request=0;menu_swallow=1;}
    if(menu_swallow){
        if(s.buttons)edges=0,s.buttons=0;else menu_swallow=0;
    }
    if(edges)rt_log("VIPER WII INPUT EDGE mask=%08lx connected=%d steer=%d accel=%d brake=%d\n",
        (unsigned long)edges,s.connected,s.steer,s.accel,s.brake);
    if(edges&WII_IN_RESTART){void wii_request_race_restart(void);wii_request_race_restart();}
    if(edges&WII_IN_START){
        int menu=wii_enhanced_menu_active();
        rt_log("VIPER WII INPUT START consumed connected=%d menu=%d buttons=%08lx\n",
               s.connected,menu,(unsigned long)s.buttons);
        if(menu)wii_enhanced_start();
    }
    wii_input_map(&s,GAME_HAS_HANDBRAKE,&g_in[3],&g_in[4],g_analog);
    if(++tick_count%575==0)rt_log("VIPER WII INPUT SNAPSHOT polls=%llu connected=%d buttons=%08lx steer=%d accel=%d brake=%d mapped=%d,%d,%d\n",
        (unsigned long long)observed_polls,s.connected,(unsigned long)s.buttons,s.steer,s.accel,s.brake,
        g_analog[0],g_analog[1],g_analog[2]);
}
