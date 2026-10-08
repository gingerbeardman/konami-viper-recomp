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
 * Bluetooth command, so: on while the drive is strong enough, at most one
 * change per 100 ms (6 polls), never during the first 10 s after boot (the
 * game's own motor test), and only with a remote connected. */
#ifndef VIPER_WII_RUMBLE_MIN_TORQUE
#define VIPER_WII_RUMBLE_MIN_TORQUE 6
#endif
extern volatile uint8_t wii_motor;
static int rumble_on;static unsigned rumble_hold;
static void rumble_update(int connected){
    uint8_t m=wii_motor;
    int want=connected&&poll_count>600&&(m&0x80)&&(m&15)>=VIPER_WII_RUMBLE_MIN_TORQUE;
    if(rumble_hold){rumble_hold--;return;}
    if(want!=rumble_on&&remote_enabled&&connected){WPAD_Rumble(WPAD_CHAN_0,want);rumble_on=want;rumble_hold=6;}
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
#ifdef VIPER_WII_DISPLAY_MULTI
            if(d->btns_d&WPAD_BUTTON_MINUS){void wii_gx_display_cycle(void);wii_gx_display_cycle();}
#else
            if(d->btns_d&WPAD_BUTTON_MINUS)roll_center=tilt;
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
