/* Native Wii headless bring-up. Console/log I/O is diagnostic, not rendering. */
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <fat.h>
#include <ogc/lwp_watchdog.h>
#include <stdarg.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#include "runtime.h"
#include "modules.h"
#include "game_config.h"
#include "voodoo_headless.h"
#include "enhanced_headless.h"
#include "net_report.h"
/* Scripted-benchmark game-state oracle (ram_fnv32) for this branch's game
 * config: main's config forces NETWORK ID 1 (the HUD says PLAYER 1). */
#ifndef VIPER_WII_ORACLE_RAM
#define VIPER_WII_ORACLE_RAM 0x40971e3du
#endif
#ifdef VIPER_WII_PC_PROFILE
#ifndef VIPER_WII_PC_PROFILE_BEGIN
#define VIPER_WII_PC_PROFILE_BEGIN 20
#endif
#ifndef VIPER_WII_PC_PROFILE_END
#define VIPER_WII_PC_PROFILE_END 21
#endif
#if VIPER_WII_PC_PROFILE_BEGIN < 0 || VIPER_WII_PC_PROFILE_END <= VIPER_WII_PC_PROFILE_BEGIN
#error Invalid CPU sampling interval
#endif
#endif
#ifndef VIPER_WII_SCRIPTED_RACE
#include "input.h"
#endif
#ifdef VIPER_WII_GX_RENDER
#include "gx_renderer.h"
static GXRModeObj *display_mode;
static void *display_buffer;
#endif
uint8_t *g_ram;
int g_enhanced=1;
static FILE *logfile;
static uint64_t started;
static unsigned boot_phase=~0u,boot_step=~0u,driving_substate=~0u;
static const RtModuleInfo *const modules[]={RT_ALL_MODULES};
int rt_verbose(void){return 0;}
static volatile int quit_request;   /* 1 loader, 2 power off, 3 System Menu (see quit_now) */
int wii_quit_pending(void){return quit_request;}
volatile int wii_pause_open,wii_pause_cursor;   /* wii/input.h; never set in scripted runs */
void rt_log(const char *fmt,...) {
#ifdef VIPER_WII_NO_LOG
    /* Release build: no SD log and no console text (every line was a
     * flushed SD write). Fatal reasons still reach the screen. */
    (void)fmt;return;
#endif
    va_list ap;va_start(ap,fmt);
    if(logfile){vfprintf(logfile,fmt,ap);fflush(logfile);}
    else vprintf(fmt,ap);
    va_end(ap);
}
/* Diagnostics: close and reopen the log so a later hang cannot lose it. */
void wii_log_sync(void){if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}}
void rt_fatal(const char *why){
    /* Persist the reason before any display/console operation can stall. */
    rt_log("VIPER WII STOP %s\n",why);
    if(logfile){
        /* Advance Dolphin's buffered SD writes past the fatal tail. Keep
         * padding bounded and text-readable, without a large RAM buffer. */
        char padding[512];memset(padding,'\n',sizeof padding);
        for(unsigned i=0;i<128;i++)
            if(fwrite(padding,1,sizeof padding,logfile)!=sizeof padding)break;
        fclose(logfile);logfile=NULL;
    }
#ifdef VIPER_WII_GX_RENDER
    if(display_buffer)console_init(display_buffer,20,20,display_mode->fbWidth,display_mode->xfbHeight,display_mode->fbWidth*2);
#endif
    printf("VIPER WII STOP %s\n",why);fflush(stdout);
    /* GX owns a separate scanout buffer. Select the console buffer explicitly
     * so an unsupported renderer state is visible instead of a black screen. */
#ifdef VIPER_WII_GX_RENDER
    if(display_buffer){VIDEO_SetNextFramebuffer(display_buffer);VIDEO_SetBlack(FALSE);VIDEO_Flush();}
#endif
    /* Remote runs report the stop and return to the Homebrew Channel. */
    if(wii_net_report_wanted()){
        static const char *const log_only[]={"boot.log",NULL};
        wii_net_report_send("sd:/viper",log_only);
        VIDEO_SetBlack(TRUE);VIDEO_Flush();VIDEO_WaitVSync();
        exit(0);
    }
    /* Reset/Home/Power still leave a stopped game. */
    for(;;){VIDEO_WaitVSync();if(quit_request){if(quit_request==2)SYS_ResetSystem(SYS_POWEROFF,0,0);
        if(quit_request==3)SYS_ResetSystem(SYS_RETURNTOMENU,0,0);exit(0);}}
}
#if !defined(VIPER_WII_SCRIPTED_RACE) && defined(GAME_ENH_HOOK_RACE_DISPATCH)
/* Restart race (D-pad right / GameCube Y), as the desktop pause menu's
 * RESTART RACE: the game's own course loader with the saved selections. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "race_restart.h"
#pragma GCC diagnostic pop
void wii_request_race_restart(void){
    if(race_restart_available()){rt_log("VIPER WII INPUT restart race\n");race_restart_request();}
}
int wii_race_restart_pending(void){return race_restart_pending();}
#else
void wii_request_race_restart(void){}
int wii_race_restart_pending(void){return 0;}
#endif
/* GIVE UP (pause menu): the race clock runs out, so the game itself ends the
 * race as when the time is up. At the native countdown store r5 is the
 * decremented remaining-vblank count: held at 1, the next frame reaches 0. */
#if !defined(VIPER_WII_SCRIPTED_RACE) && defined(GAME_ENH_HOOK_RACE_DISPATCH) && defined(GAME_ENH_HOOK_RACE_COUNTDOWN)
static volatile int give_up;
int wii_give_up_available(void){return race_restart_available()&&!race_restart_pending();}
void wii_request_give_up(void){if(wii_give_up_available())give_up=1;}
static void give_up_hook(PPCContext *c){
    if(!give_up)return;
    if(!race_restart_available()){give_up=0;return;}
    if(c->r[5]>1)c->r[5]=1;
    else{give_up=0;rt_log("VIPER WII give up: race time out\n");}
}
#else
int wii_give_up_available(void){return 0;}
void wii_request_give_up(void){}
#endif
#include "widescreen.h"
void rt_hook(PPCContext *c,uint32_t pc){
    if(wii_wide_hook(pc))return;
#if !defined(VIPER_WII_SCRIPTED_RACE) && defined(GAME_ENH_HOOK_RACE_DISPATCH)
    if(g_enhanced&&pc==GAME_ENH_HOOK_RACE_DISPATCH)race_dispatch_hook(c);
#endif
#if !defined(VIPER_WII_SCRIPTED_RACE) && defined(GAME_ENH_HOOK_RACE_DISPATCH) && defined(GAME_ENH_HOOK_RACE_COUNTDOWN)
    if(pc==GAME_ENH_HOOK_RACE_COUNTDOWN)give_up_hook(c);
#endif
    (void)c;
#ifdef GAME_ENH_HOOK_ATTRACT
    if(g_enhanced&&pc==GAME_ENH_HOOK_ATTRACT)wii_enhanced_attract_hook();
#else
    (void)pc;
#endif
}
#ifdef VIPER_WII_AUDIO
/* The game's own mixed blocks out through the Wii audio DMA (wii/audio.c). */
#include "audio.h"
void audio_push_block(const uint8_t *blk){wii_audio_push_block(blk);}
#else
void audio_push_block(const uint8_t *blk){(void)blk;}
#endif
/* Cabinet steering motor byte (bit 7 drive, bits 0-3 torque), read by the
 * Wii Remote rumble in wii/input_platform.c. */
volatile uint8_t wii_motor;
void frontend_set_motor(uint8_t v){wii_motor=v;}
#ifdef VIPER_WII_SCRIPTED_RACE
void rt_pace_vblank(void){}   /* Benchmarks run unthrottled. */
#else
/* Called at every emulated vblank: never let guest time run ahead of real
 * time (menus and light scenes otherwise ran up to ~1.9x speed). Falling
 * behind more than 0.25 s resynchronises instead of rushing to catch up. */
/* How far guest time is behind real time at the last vblank (us, 0 when
 * ahead): read by the renderer's optional adaptive frame skip. */
volatile int32_t wii_pace_behind_us;
int usleep(unsigned int);   /* newlib hides it under -std=c11 */
void rt_pace_vblank(void){
    static uint64_t origin;static int started;
    uint64_t virt_us=rt_now()/(CPU_HZ/1000000ull),now=gettime();
    if(!started){origin=now-microsecs_to_ticks(virt_us);started=1;return;}
    int64_t ahead=(int64_t)virt_us-(int64_t)ticks_to_microsecs(now-origin);
    wii_pace_behind_us=ahead<0?(int32_t)(ahead< -1000000?1000000:-ahead):0;
#ifdef VIPER_WII_PACE_TRACE
    {extern int32_t wii_pace_trace_max;if(wii_pace_behind_us>wii_pace_trace_max)wii_pace_trace_max=wii_pace_behind_us;
     extern unsigned wii_pace_trace_resyncs;if(ahead< -250000)wii_pace_trace_resyncs++;}
#endif
    if(ahead< -250000){origin=now-microsecs_to_ticks(virt_us);return;}
#if defined(VIPER_WII_AUDIO) && defined(VIPER_WII_AUDIO_LATENCY_MS)
    /* Audio-paced: wait only while more than the target sound is queued, so
     * after any hitch the game runs on until the queue is full again (the
     * time debt alone is forgiven past 0.25 s, which left the queue short and
     * every later hitch audible). Real time still caps the lead, in case
     * the game produces no sound. */
    {int queued=wii_audio_backlog_us();
     if(queued>=0&&ahead<=(VIPER_WII_AUDIO_LATENCY_MS+100)*1000){
        int over=queued-VIPER_WII_AUDIO_LATENCY_MS*1000;
        if(over>2000)usleep((unsigned int)(over-1000));
        return;
     }}
#endif
    if(ahead>2000)usleep((unsigned int)(ahead-1000));
}
#endif
/* Console Reset (and Wii Remote Home) returns to the loader (Homebrew
 * Channel); console or Wii Remote Power switches off. Callbacks only set the
 * request; the guest thread, which owns the SD log, acts on it at its next
 * input tick, and the VSync loop does if the guest has stopped ticking. */
static void reset_pressed(u32 irq,void *ctx){(void)irq;(void)ctx;quit_request=1;}
static void power_pressed(void){quit_request=2;}
static void remote_power_pressed(s32 chan){(void)chan;quit_request=2;}
void wii_request_quit(int kind){if(!quit_request)quit_request=kind;}
static void quit_now(void){
    int kind=quit_request;
#ifdef VIPER_WII_AUDIO
    wii_audio_shutdown();   /* or the loader starts to a looping buffer */
#endif
#ifdef VIPER_WII_PC_PROFILE_PLAY
    /* Whole-session hardware PC profile, written to the log on the way out. */
    if(logfile){extern void wii_pc_profile_stop(void);wii_pc_profile_stop();}
#endif
    if(logfile){rt_log("VIPER WII QUIT %s\n",kind==2?"power off":kind==3?"to System Menu":"to loader");fclose(logfile);logfile=NULL;}
    VIDEO_SetBlack(TRUE);VIDEO_Flush();VIDEO_WaitVSync();
#ifndef VIPER_WII_SCRIPTED_RACE
    wii_input_shutdown();
#endif
    if(kind==2)SYS_ResetSystem(SYS_POWEROFF,0,0);
    if(kind==3)SYS_ResetSystem(SYS_RETURNTOMENU,0,0);
    exit(0);
}
static void quit_buttons_init(void){
    SYS_SetResetCallback(reset_pressed);SYS_SetPowerCallback(power_pressed);
#ifndef VIPER_WII_SCRIPTED_RACE
    WPAD_SetPowerButtonCallback(remote_power_pressed);
#else
    (void)remote_power_pressed;
#endif
}
static void input_tick(void *arg){
    (void)arg;
    if(quit_request)quit_now();
#ifdef VIPER_WII_PC_PROFILE_PLAY
    {static int sampling;if(!sampling){extern void wii_pc_profile_start(void);
        rt_log("VIPER WII PLAY PROFILE start\n");wii_pc_profile_start();sampling=1;}}
#endif
#ifndef VIPER_WII_SCRIPTED_RACE
    wii_input_guest_tick();
    if(wii_race_restart_pending()){   /* as the desktop: no input while the race reloads */
        extern uint8_t g_in[];extern int16_t g_analog[];
        g_in[3]=g_in[4]=0xff;g_analog[0]=0;g_analog[1]=g_analog[2]=g_analog[3]=-200;
    }
#endif
#if defined(VIPER_WII_PACE_TRACE) && defined(VIPER_WII_PC_PROFILE_PLAY)
    /* Sample only while the game is falling behind (the stalls): the sound
     * queue more than 50 ms under its target, or behind real time. */
    {extern volatile uint32_t wii_pc_profile_gate;
#ifdef VIPER_WII_PROFILE_WINDOW_FROM
     /* A fixed guest-time window instead (e.g. the attract tunnel crash). */
     {unsigned g=(unsigned)(rt_now()/CPU_HZ);wii_pc_profile_gate=g>=VIPER_WII_PROFILE_WINDOW_FROM&&g<VIPER_WII_PROFILE_WINDOW_TO;}
#elif defined(VIPER_WII_AUDIO) && defined(VIPER_WII_AUDIO_LATENCY_MS)
     int queued=wii_audio_backlog_us();
     wii_pc_profile_gate=queued>=0&&queued<(VIPER_WII_AUDIO_LATENCY_MS-50)*1000;
#else
     wii_pc_profile_gate=wii_pace_behind_us>20000;
#endif
    }
#endif
    wii_enhanced_input_tick();rt_sched_at(rt_now()+(uint64_t)(CPU_HZ/57.5),input_tick,NULL);
}
static void boot_status(void){
    if(strcmp(GAME_ID,"gticlub2")||LD32(0x45b78)!=0x7c0802a6||LD32(0x45b84)!=0x83e20054)return;
    uint32_t toc=LD32(0x38044);
    if((toc&3)||toc>RAM_SIZE-0x41c)return;
    uint32_t state=LD32(toc+0x54);
    if(!state||(state&3)||state>RAM_SIZE-16||(LD32(state)>>8)!=0x4e574b)return;
    unsigned phase=(LD32(state+4)>>27)&15,step=~0u;
    if(phase==1){uint32_t base=LD32(toc+0x3c);if(base<=RAM_SIZE-0x200000)step=LD32(base+0x1ffeb8);}
    else if(phase==4)step=(LD32(state+4)>>23)&15;
    else if(phase==2||phase==3){uint32_t a=LD32(toc+0x294);if(a&&a<=RAM_SIZE-12)step=LD8(a+11);}
    boot_phase=phase;boot_step=step;driving_substate=~0u;
    if(phase==4&&step==11&&toc<=RAM_SIZE-0x7a4){
        uint32_t countdown=LD32(toc+0x7a0);
        if(countdown&&!(countdown&3)&&countdown<=RAM_SIZE-0x18)
            driving_substate=LD8(countdown+0x13);
    }
    static unsigned last_driving=~0u;
    if(driving_substate!=~0u&&driving_substate!=last_driving){
        rt_log("VIPER WII RACE substate=%u\n",driving_substate);
        last_driving=driving_substate;
    }
    static unsigned last_phase=~0u,last_step=~0u;
    if(phase!=last_phase||step!=last_step){
#ifndef VIPER_WII_GX_RENDER
        printf("phase=%u step=%ld\n",phase,step==~0u?-1L:(long)step);
#endif
        rt_log("VIPER WII PHASE state=%u step=%ld\n",phase,step==~0u?-1L:(long)step);
        last_phase=phase;last_step=step;
    }
}
#ifdef VIPER_WII_GX_PLANE_PROFILE
uint64_t rt_wii_profile_ticks(void){return gettime();}
uint64_t rt_wii_profile_microseconds(uint64_t ticks){return ticks_to_microsecs(ticks);}
#endif
#if defined(VIPER_WII_GX_CAPTURE_CHECKPOINT) && defined(VIPER_WII_GX_RENDER)
/* Diagnostic only: called after measured counters, before the result console.
 * Each pixel is RGBA8 followed by big-endian u32 depth, in row-major order. */
static void capture_efb_checkpoint(void){
    rt_log("VIPER WII CHECKPOINT EFB begin\n");
    wii_gx_batch_flush();wii_gx_own_thread();GX_DrawDone();
    rt_log("VIPER WII CHECKPOINT EFB draw_done\n");
    FILE *file=fopen("sd:/viper/efb.bin","wb");
    if(!file)rt_fatal("cannot create EFB checkpoint");
    uint8_t row[640*8];
    uint32_t color_hash=2166136261u,depth_hash=2166136261u;
    for(unsigned y=0;y<480;y++){
        for(unsigned x=0;x<640;x++){
            GXColor color;u32 depth;
            GX_PeekARGB(x,y,&color);GX_PeekZ(x,y,&depth);
            uint8_t *pixel=row+x*8;
            pixel[0]=color.r;pixel[1]=color.g;pixel[2]=color.b;pixel[3]=color.a;
            pixel[4]=depth>>24;pixel[5]=depth>>16;pixel[6]=depth>>8;pixel[7]=depth;
            for(unsigned c=0;c<4;c++){
                color_hash=(color_hash^pixel[c])*16777619u;
                depth_hash=(depth_hash^pixel[c+4])*16777619u;
            }
        }
        if(fwrite(row,1,sizeof row,file)!=sizeof row)rt_fatal("EFB checkpoint write");
        if((y+1)%120==0)rt_log("VIPER WII CHECKPOINT EFB rows=%u\n",y+1);
    }
    if(fclose(file))rt_fatal("EFB checkpoint close");
    rt_log("VIPER WII EFB CHECKPOINT width=640 height=480 color_fnv32=%08lx depth_fnv32=%08lx\n",
        (unsigned long)color_hash,(unsigned long)depth_hash);
}
#endif
#ifdef VIPER_WII_STRAY_WATCH
/* Diagnostic: guest RAM f00000..fa0000 is never written by the game (zero in
 * Dolphin), yet on hardware a few words appear there. Log the first moment
 * and place anything shows up, tagged with where the scan ran. */
void wii_stray_scan(const char *where);
static void stray_scan(const char *where){wii_stray_scan(where);}
void wii_stray_scan(const char *where){
    static unsigned reported;
    if(reported&&strcmp(where,"end"))return;
    const uint32_t *w=(const uint32_t *)(g_ram+0xf00000);
    for(unsigned i=0;i<(0xfa0000-0xf00000)/4;i++){
        if(!w[i])continue;
        unsigned off=0xf00000+i*4u&~31u;
        const uint32_t *line=(const uint32_t *)(g_ram+off);
        rt_log("VIPER WII STRAY %s guest=%.6f off=%06x %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
            where,(double)rt_now()/CPU_HZ,off,(unsigned long)line[0],(unsigned long)line[1],(unsigned long)line[2],
            (unsigned long)line[3],(unsigned long)line[4],(unsigned long)line[5],(unsigned long)line[6],(unsigned long)line[7]);
        reported++;
        return;
    }
}
#define STRAY_SCAN(where) stray_scan(where)
/* Every 1/100 guest second through the first two seconds. */
static void stray_tick(void *arg){
    (void)arg;
    stray_scan("tick");
    if(rt_now()<2*(uint64_t)CPU_HZ)rt_sched_at(rt_now()+CPU_HZ/100,stray_tick,NULL);
}
#else
#define STRAY_SCAN(where) ((void)0)
#endif
#ifdef VIPER_WII_PACE_TRACE
/* Diagnostic: one line per guest second (wall time it took, audio underruns
 * and backlog skips, worst lag behind real time, pace resyncs), kept in RAM
 * so SD writes cannot cause the stalls being measured. A remote run sends it
 * as pace.txt at guest second VIPER_WII_PACE_TRACE and returns to the loader. */
int32_t wii_pace_trace_max;unsigned wii_pace_trace_resyncs;
static char pace_text[64*1024];static unsigned pace_len;
static void pace_trace(void){
#ifdef VIPER_WII_AUDIO
    extern volatile unsigned wii_audio_underruns,wii_audio_skips;
#else
    static unsigned wii_audio_underruns,wii_audio_skips;   /* no sound in this build */
#endif
    extern unsigned long long hw_cf_sectors,hw_cf_host_reads;
    static uint64_t last;static unsigned last_under,last_skip;static unsigned long long last_sect,last_reads;
    uint64_t now=gettime();unsigned g=(unsigned)(rt_now()/CPU_HZ);
    {struct mallinfo mi=mallinfo();
#ifdef VIPER_WII_SUPERSAMPLE
     extern unsigned ss_dl_peak,ss_dl_overflows;
     rt_log("VIPER WII SSDL g=%u peak=%u overflows=%u\n",g,ss_dl_peak,ss_dl_overflows);ss_dl_peak=0;
#endif
     rt_log("VIPER WII HEAP g=%u used=%u free_in_heap=%u arena=%u mem1_left=%lu mem2_left=%lu\n",g,(unsigned)mi.uordblks,(unsigned)mi.fordblks,(unsigned)mi.arena,
        (unsigned long)((uintptr_t)SYS_GetArena1Hi()-(uintptr_t)SYS_GetArena1Lo()),(unsigned long)((uintptr_t)SYS_GetArena2Hi()-(uintptr_t)SYS_GetArena2Lo()));}
    if(last){
        char line[160];
#ifdef VIPER_WII_AUTO_FRAMESKIP
        extern unsigned wii_frames_skipped;static unsigned last_fskip;unsigned fskip=wii_frames_skipped-last_fskip;last_fskip=wii_frames_skipped;
#else
        unsigned fskip=0;
#endif
        snprintf(line,sizeof line,"g=%u wall_ms=%llu under=%u skip=%u behind_max_ms=%d resync=%u cf_sectors=%llu cf_reads=%llu fskip=%u\n",
            g,(unsigned long long)ticks_to_millisecs(now-last),wii_audio_underruns-last_under,wii_audio_skips-last_skip,
            (int)(wii_pace_trace_max/1000),wii_pace_trace_resyncs,hw_cf_sectors-last_sect,hw_cf_host_reads-last_reads,fskip);
        rt_log("VIPER WII PACE %s",line);   /* also in the SD log (Dolphin) */
        size_t n=strlen(line);
        if(pace_len+n<sizeof pace_text){memcpy(pace_text+pace_len,line,n+1);pace_len+=n;}   /* the network report keeps what fits */
    }
    last=now;last_under=wii_audio_underruns;last_skip=wii_audio_skips;wii_pace_trace_max=0;wii_pace_trace_resyncs=0;
    last_sect=hw_cf_sectors;last_reads=hw_cf_host_reads;
    if(g>=VIPER_WII_PACE_TRACE&&wii_net_report_wanted()){
#ifdef VIPER_WII_PC_PROFILE_PLAY
        /* One file: the receiver takes a single report per run. */
        {extern unsigned wii_pc_profile_text(char *,unsigned);
         wii_pc_profile_text(pace_text+pace_len,sizeof pace_text-pace_len);}
#endif
        wii_net_report_text("pace.txt",pace_text);
        VIDEO_SetBlack(TRUE);VIDEO_Flush();VIDEO_WaitVSync();exit(0);
    }
}
#endif
static void progress(void *arg){
    (void)arg;
#ifdef VIPER_WII_PACE_TRACE
    pace_trace();
#endif
#ifdef VIPER_WII_PC_PROFILE
    extern void wii_pc_profile_start(void),wii_pc_profile_stop(void);
    static unsigned pc_profile_stage;
    if(!pc_profile_stage&&rt_now()/CPU_HZ>=VIPER_WII_PC_PROFILE_BEGIN){
        rt_log("VIPER WII PC WINDOW begin=%u end=%u\n",
            VIPER_WII_PC_PROFILE_BEGIN,VIPER_WII_PC_PROFILE_END);
        wii_pc_profile_start();pc_profile_stage=1;
#ifdef VIPER_WII_SUBMISSION_PROFILE
        extern void wii_submission_profile_start(void);
        wii_submission_profile_start();
#endif
    }
    else if(pc_profile_stage==1&&rt_now()/CPU_HZ>=VIPER_WII_PC_PROFILE_END){
#ifdef VIPER_WII_SUBMISSION_PROFILE
        extern void wii_submission_profile_stop(void);
        wii_submission_profile_stop();
#endif
        wii_pc_profile_stop();pc_profile_stage=2;
    }
#endif
    uint64_t us=ticks_to_microsecs(gettime()-started);
#ifndef VIPER_WII_GX_RENDER
    printf("guest %.1f s / Wii %.3f s / lr %08lx\n",rt_now()/CPU_HZ,us/1e6,(unsigned long)g_ctx.lr);
#endif
    rt_log("VIPER WII PROFILE guest=%.9f elapsed_us=%llu\n",rt_now()/CPU_HZ,(unsigned long long)us);
#if defined(VIPER_WII_FIFO_FORMAT_PROFILE) || defined(VIPER_WII_DIRECT_CAPTURE59)
    voodoo_stats();
#endif
#ifdef VIPER_WII_GX_PLANE_PROFILE
    uint64_t lookup_us,lookup_calls;
    rt_wii_lookup_profile(&lookup_us,&lookup_calls);
    rt_log("VIPER WII CPU LOOKUP calls=%llu us=%llu\n",
        (unsigned long long)lookup_calls,(unsigned long long)lookup_us);
#endif
    boot_status();
#ifdef VIPER_WII_GX_RENDER
    rt_log("VIPER WII GX dither_approximations=%llu\n",(unsigned long long)wii_gx_dither_approximations());
    rt_log("VIPER WII GX wdepth_approximations=%llu\n",(unsigned long long)wii_gx_wdepth_approximations());
    WiiGXProfile gx=wii_gx_profile();
#ifdef VIPER_WII_LOOKUP_PLANE_REUSE
    rt_log("VIPER WII GX lookup_plane_reuses=%llu\n",(unsigned long long)gx.lookup_plane_reuses);
#endif
#ifdef VIPER_WII_COMBINER_PROGRAM_CACHE
    rt_log("VIPER WII GX COMBINER PROGRAM hits=%llu misses=%llu\n",
        (unsigned long long)gx.combiner_hits,(unsigned long long)gx.combiner_misses);
#endif
#ifdef VIPER_WII_GX_BATCH
    {unsigned long long draws,triangles;wii_gx_batch_stats(&draws,&triangles);
     rt_log("VIPER WII GX BATCH draws=%llu triangles=%llu\n",draws,triangles);}
#endif
#ifdef VIPER_WII_TRIANGLE_MEMO
    {unsigned long long hits,misses;wii_gx_triangle_memo_stats(&hits,&misses);
     rt_log("VIPER WII GX TRIANGLE MEMO hits=%llu misses=%llu\n",hits,misses);}
#endif
#if defined(VIPER_WII_NATIVE_GL_DRAW) && defined(VIPER_WII_DIRECT_TRIANGLES)
    {void wii_native_gl_draw_stats(unsigned long long out[5]);unsigned long long o[5];wii_native_gl_draw_stats(o);
     rt_log("VIPER WII NATIVE GL DRAW complete direct=%llu declined=%llu ram=%llu device=%llu unheld=%llu\n",o[0],o[1],o[2],o[3],o[4]);}
#endif
#ifdef VIPER_WII_NATIVE_GL_LIST
    {void wii_native_gl_list_stats(unsigned long long*,unsigned long long*,unsigned long long*);
     unsigned long long direct,stored,unheld;wii_native_gl_list_stats(&direct,&stored,&unheld);
     rt_log("VIPER WII NATIVE GL LIST direct=%llu stored=%llu unheld=%llu\n",direct,stored,unheld);}
#endif
#ifdef VIPER_WII_MATERIAL_RUN
    rt_log("VIPER WII GX MATERIAL RUN resumes=%llu emits=%llu\n",
        (unsigned long long)gx.material_run_resumes,(unsigned long long)gx.material_run_emits);
#endif
#ifdef VIPER_WII_NATIVE_TEXTURE_RESOURCE
    rt_log("VIPER WII GX TEXTURE RESOURCE hits=%llu misses=%llu\n",
        (unsigned long long)gx.texture_resource_hits,(unsigned long long)gx.texture_resource_misses);
#endif
    rt_log("VIPER WII GX split_depth_draws=%llu\n",(unsigned long long)gx.split_depth_draws);
    rt_log("VIPER WII GX tmu_pipeline_draws=%llu dual_texture_draws=%llu\n",
        (unsigned long long)gx.tmu_pipeline_draws,(unsigned long long)gx.dual_texture_draws);
    rt_log("VIPER WII GX COST split_us=%llu children=%llu depth_upload_us=%llu color_upload_us=%llu\n",
        (unsigned long long)gx.split_us,(unsigned long long)gx.split_children,
        (unsigned long long)gx.depth_upload_us,(unsigned long long)gx.color_upload_us);
    rt_log("VIPER WII GX CACHE hits=%llu misses=%llu\n",
        (unsigned long long)gx.color_cache_hits,(unsigned long long)gx.color_cache_misses);
    rt_log("VIPER WII GX CACHE MEMORY bytes=%llu peak=%llu evictions=%llu heap_evictions=%llu\n",
        (unsigned long long)gx.color_cache_bytes,(unsigned long long)gx.color_cache_peak_bytes,
        (unsigned long long)gx.color_cache_evictions,(unsigned long long)gx.color_cache_heap_evictions);
#ifdef VIPER_WII_GX_PLANE_PROFILE
    for(unsigned i=0;i<3;i++)rt_log("VIPER WII GX PLANE kind=%u calls=%llu us=%llu\n",
        i,(unsigned long long)gx.plane_calls[i],(unsigned long long)gx.plane_us[i]);
    for(unsigned i=0;i<5;i++)rt_log("VIPER WII GX SETUP kind=%u calls=%llu us=%llu\n",
        i,(unsigned long long)gx.setup_calls[i],(unsigned long long)gx.setup_us[i]);
#endif
#ifdef VIPER_WII_GX_BIND_PROFILE
    for(unsigned i=0;i<2;i++)rt_log("VIPER WII GX BIND kind=%u calls=%llu us=%llu\n",
        i,(unsigned long long)gx.bind_calls[i],(unsigned long long)gx.bind_us[i]);
#endif
#ifdef VIPER_WII_GX_TEXTURE_TRACE
    rt_log("VIPER WII GX TEXTURES sources=%llu rgba_bytes=%llu source_updates=%llu palette_updates=%llu overflow=%llu\n",
        (unsigned long long)gx.texture_sources,(unsigned long long)gx.texture_source_bytes,
        (unsigned long long)gx.texture_source_updates,(unsigned long long)gx.texture_palette_updates,
        (unsigned long long)gx.texture_trace_overflow);
#endif
    for(unsigned i=1;i<8;i++)rt_log("VIPER WII GX FENCE site=%u calls=%llu us=%llu\n",i,
        (unsigned long long)gx.fence_calls[i],(unsigned long long)gx.fence_us[i]);
#endif
    WiiVoodooStats stats=wii_voodoo_stats();
#ifdef VIPER_WII_GX_PLANE_PROFILE
    for(unsigned i=0;i<3;i++)rt_log("VIPER WII RENDER kind=%u us=%llu\n",i,
        (unsigned long long)stats.renderer_us[i]);
#endif
    rt_log("VIPER WII WORK packets=%llu triangles=%llu presents=%llu clears=%llu\n",
        (unsigned long long)stats.packets,(unsigned long long)stats.triangles,
        (unsigned long long)stats.presents,(unsigned long long)stats.clears);
#ifdef VIPER_WII_SCRIPTED_RACE
#ifdef VIPER_WII_GX_RENDER
    static uint64_t bench_begin_us,bench_elapsed_us,bench_presents,bench_displayed;
    static uint64_t bench_begin_presents,bench_begin_displayed,bench_begin_render,bench_render_us;
    static unsigned bench_complete;
    unsigned bench_second=(unsigned)(rt_now()/CPU_HZ);
    if(bench_second==70){
        bench_begin_us=us;bench_begin_presents=stats.presents;
        bench_begin_displayed=gx.fence_calls[4];bench_begin_render=stats.renderer_us[0];
    }
    if(bench_second==73&&bench_begin_us){
        bench_elapsed_us=us-bench_begin_us;bench_presents=stats.presents-bench_begin_presents;
        bench_displayed=gx.fence_calls[4]-bench_begin_displayed;
        bench_render_us=stats.renderer_us[0]-bench_begin_render;bench_complete=1;
    }
#endif
    if(rt_now()/CPU_HZ>=75){
        STRAY_SCAN("end");
        int ok=boot_phase==4&&boot_step==11&&driving_substate==5&&stats.presents>0;
        uint32_t hash=2166136261u;
        for(size_t i=0;i<RAM_SIZE;i++)hash=(hash^g_ram[i])*16777619u;
        /* Capture after timing/work counters, so diagnostic SD writes do not
         * contaminate measured gameplay. Needed to locate numerical drift. */
        rt_log("VIPER WII CHECKPOINT RAM begin bytes=%u\n",(unsigned)RAM_SIZE);
        FILE *snapshot=fopen("sd:/viper/ram.bin","wb");
        if(!snapshot)rt_fatal("cannot create scripted RAM snapshot");
        size_t written=fwrite(g_ram,1,RAM_SIZE,snapshot);
        int close_error=fclose(snapshot);
        if(written!=RAM_SIZE||close_error)rt_fatal("scripted RAM snapshot write");
        rt_log("VIPER WII CHECKPOINT RAM complete\n");
#if defined(VIPER_WII_GX_CAPTURE_CHECKPOINT) && defined(VIPER_WII_GX_RENDER)
        capture_efb_checkpoint();
#endif
#if defined(VIPER_WII_DIRECT_STATE) || defined(VIPER_WII_DIRECT_VERTEX59) || defined(VIPER_WII_DIRECT_PREHEADER59)
        /* End-only coverage evidence, outside the measured profile windows. */
        voodoo_stats();
#ifdef VIPER_WII_MMIO_CENSUS
    {void hw_mmio_census_log(void);hw_mmio_census_log();}
#endif
#endif
#ifdef VIPER_WII_PGO_GEN
        {int wii_pgo_dump(void);int objects=wii_pgo_dump();rt_log("VIPER WII PGO dump objects=%d\n",objects);}
#endif
#ifdef VIPER_FIBER_COUNT
        {extern uint64_t rt_fiber_switches;rt_log("VIPER WII FIBER SWITCHES %llu\n",(unsigned long long)rt_fiber_switches);}
#endif
#ifdef VIPER_WII_SUPERSAMPLE
        {void wii_gx_ss_stats(unsigned long long *,unsigned long long *,unsigned long long *);unsigned long long t,p,b;
         wii_gx_ss_stats(&t,&p,&b);rt_log("VIPER WII SS frames tiled=%llu plain=%llu backoffs=%llu\n",t,p,b);}
#endif
        rt_log("VIPER WII SCRIPTED END result=%s phase=%u step=%u substate=%u ram_fnv32=%08lx\n",
               ok?"PASS":"FAIL",boot_phase,boot_step,driving_substate,(unsigned long)hash);
        /* Publish the result before touching the console. Keep a fresh log
         * handle so the post-capture display transition can be diagnosed. */
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
#ifdef VIPER_WII_GX_RENDER
        rt_log("VIPER WII END DISPLAY fence_begin\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
        wii_gx_batch_flush();wii_gx_own_thread();GX_DrawDone();
        rt_log("VIPER WII END DISPLAY fence_complete\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
        /* The console repaints only from its origin: clear the last game frame. */
        VIDEO_ClearFrameBuffer(display_mode,display_buffer,COLOR_BLACK);
        console_init(display_buffer,20,20,display_mode->fbWidth,display_mode->xfbHeight,display_mode->fbWidth*2);
        rt_log("VIPER WII END DISPLAY console_initialized\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
#endif
#if defined(VIPER_WII_DIRECT_STATE) || defined(VIPER_WII_DIRECT_VERTEX59) || defined(VIPER_WII_DIRECT_PREHEADER59)
        uint64_t native_states=0,native_vertices=0;
#ifdef VIPER_WII_DIRECT_STATE
        native_states=wii_gx_native_state_packets();
#endif
        printf("VIPER WII SCRIPTED END %s hash=%08lx\nnative states=%llu vertex_batches=%llu\n",
               ok?"PASS":"FAIL",(unsigned long)hash,
               (unsigned long long)native_states,(unsigned long long)native_vertices);
#else
        printf("VIPER WII SCRIPTED END %s phase=%u step=%u substate=%u hash=%08lx\n",
            ok?"PASS":"FAIL",boot_phase,boot_step,driving_substate,(unsigned long)hash);
#endif
#ifdef VIPER_WII_GX_RENDER
#ifndef VIPER_WII_GX_FRAME_NUMERATOR
#define VIPER_WII_GX_FRAME_NUMERATOR 1
#endif
#ifndef VIPER_WII_GX_FRAME_DENOMINATOR
#define VIPER_WII_GX_FRAME_DENOMINATOR VIPER_WII_GX_FRAME_DIVISOR
#endif
        printf("\nHARDWARE BENCHMARK - photograph this screen\nrender fraction: %u/%u (guest timing unchanged)\n",
            VIPER_WII_GX_FRAME_NUMERATOR,VIPER_WII_GX_FRAME_DENOMINATOR);
        printf("Video: RGB8/Z24 480-line EFB / XFB %ux%u\n",display_mode->fbWidth,display_mode->xfbHeight);
#ifdef VIPER_WII_BENCH_ID
        printf("Build: %s\n",VIPER_WII_BENCH_ID==5?"A - 2026-10-08 rows 1-5 (native gl, direct packets)":
            VIPER_WII_BENCH_ID==6?"B - rows 1-6 (+ local RAM base, live default)":
            VIPER_WII_BENCH_ID==7?"C - rows 1-7 (+ localize all, 2x code size)":
            VIPER_WII_BENCH_ID==8?"D - rows 1-19 (live default)":
            VIPER_WII_BENCH_ID==9?"E - D + out-of-line RAM slow path (smaller code)":
            VIPER_WII_BENCH_ID==10?"F - D without hot-function localize":
            VIPER_WII_BENCH_ID==11?"G - E + direct guest floats (live default)":
            VIPER_WII_BENCH_ID==12?"H - G with magenta clears (band diagnostic)":
            VIPER_WII_BENCH_ID==13?"I - G without hot-function localize":
            VIPER_WII_BENCH_ID==14?"J - rows 1-24 (live default)":
            VIPER_WII_BENCH_ID==15?"K - J + cache hints (dcbz/dcbt)":
            VIPER_WII_BENCH_ID==16?"L - J + hot-first code layout":
            VIPER_WII_BENCH_ID==17?"M - rows 1-29 (live default)":
            VIPER_WII_BENCH_ID==18?"N - M + cache hints (dcbz/dcbt)":
            VIPER_WII_BENCH_ID==19?"O - M + hot-first code layout":
            VIPER_WII_BENCH_ID==20?"P - rows 1-36 (live default)":
            VIPER_WII_BENCH_ID==21?"Q - P with magenta clears (band diagnostic)":
            VIPER_WII_BENCH_ID==22?"R - P with the present wait (row 36 off)":
            VIPER_WII_BENCH_ID==23?"S - P, clears with full XF clip disable":
            VIPER_WII_BENCH_ID==24?"T - P, clears one Z step inside far plane":
            VIPER_WII_BENCH_ID==25?"U - P, clears with conventional winding":
            VIPER_WII_BENCH_ID==26?"V - P, magenta clears with S+T+U":
            VIPER_WII_BENCH_ID==27?"W - P + black-band fix (live default)":
            VIPER_WII_BENCH_ID==28?"X - W + 1 MB GPU FIFO":
            VIPER_WII_BENCH_ID==29?"Y - rows 1-42 (live default)":
            VIPER_WII_BENCH_ID==30?"Z - Y + renderer at -O3":
            VIPER_WII_BENCH_ID==31?"AA - Y + six large localized functions":
            VIPER_WII_BENCH_ID==32?"AB - Y without 2adac vertex prefetch":
            VIPER_WII_BENCH_ID==33?"AC - Y + profile-guided optimization":
            VIPER_WII_BENCH_ID==34?"AD - AC + hot/cold code split":
            VIPER_WII_BENCH_ID==35?"AE - AD without loop unrolling":
            VIPER_WII_BENCH_ID==36?"AF - AE + Reset/Power/Home buttons (live default)":
            VIPER_WII_BENCH_ID==37?"AG - AF + guest floats kept in FPRs (live default)":
            VIPER_WII_BENCH_ID==38?"AH - AG without PGO":
            VIPER_WII_BENCH_ID==39?"AI - live default 2026-10-08 late (PGO)":
            VIPER_WII_BENCH_ID==40?"AJ - AI without PGO":
            VIPER_WII_BENCH_ID==41?"K1 - AJ without guest float accessor change":
            VIPER_WII_BENCH_ID==42?"K2 - AJ without fast screen clear":
            VIPER_WII_BENCH_ID==43?"K3 - AJ without sound localization":
            VIPER_WII_BENCH_ID==44?"L1 - AJ, float accessor change: single loads only":
            VIPER_WII_BENCH_ID==45?"L2 - AJ, float accessor change: single stores only":
            VIPER_WII_BENCH_ID==46?"L3 - AJ, float accessor change: doubles only":"unlabelled");
#endif
#ifdef VIPER_WII_NATIVE_FRSQRTE
        printf("Math: native PPC frsqrte ESTIMATE (inexact)\n");
#elif defined(VIPER_WII_EXACT_RSQRT)
        printf("Math: exact reciprocal sqrt (hardware estimate + proof)\n");
#else
        printf("Math: software reciprocal sqrt (exact memo)\n");
#endif
        if(bench_complete&&bench_elapsed_us){
            double seconds=bench_elapsed_us/1e6;
            printf("Driving window: guest 70-73 seconds\nWii elapsed: %.3f seconds / game speed: %.1f%%\nlogical FPS: %.2f / displayed FPS: %.2f\nframes logical/displayed: %llu/%llu\nrenderer: %.3f s (%.1f%% of elapsed)\nother CPU/device: %.3f s\n",
                seconds,300.0/seconds,bench_presents/seconds,bench_displayed/seconds,
                (unsigned long long)bench_presents,(unsigned long long)bench_displayed,
                bench_render_us/1e6,100.0*bench_render_us/bench_elapsed_us,
                (bench_elapsed_us-bench_render_us)/1e6);
        }else printf("Driving window unavailable - inspect boot.log\n");
        printf("RAM oracle: %s (expected %08lx)\n",
            hash==VIPER_WII_ORACLE_RAM?"MATCH":"DIFFERENT",(unsigned long)VIPER_WII_ORACLE_RAM);
#ifdef VIPER_WII_NATIVE_FRSQRTE
        printf("RAM/image changes expected with this estimate.\n");
#endif
        printf("Logs/captures: sd:/viper/\nReset console to run the next test.\n");
#endif
        rt_log("VIPER WII END DISPLAY text_written\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
        fflush(stdout);
        rt_log("VIPER WII END DISPLAY stdout_flushed\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
#ifdef VIPER_WII_GX_RENDER
        VIDEO_SetNextFramebuffer(display_buffer);
        rt_log("VIPER WII END DISPLAY framebuffer_selected\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
        VIDEO_SetBlack(FALSE);
        rt_log("VIPER WII END DISPLAY unblanked\n");
        if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");}
        VIDEO_Flush();
#endif
        rt_log("VIPER WII END DISPLAY ready\n");
        if(logfile){fclose(logfile);logfile=NULL;}
        /* Remote testing (wiiload report=HOST:PORT): send the results and
         * return to the Homebrew Channel for the next DOL. */
        if(wii_net_report_wanted()){
            /* The RAM snapshot only matters when the oracle differs. */
            static const char *const all[]={"boot.log","ram.bin","efb.bin",NULL},*const match[]={"boot.log","efb.bin",NULL};
            /* Once sent, the big captures need not occupy the SD card. */
            if(wii_net_report_send("sd:/viper",hash==VIPER_WII_ORACLE_RAM?match:all)>0){remove("sd:/viper/ram.bin");remove("sd:/viper/efb.bin");}
            VIDEO_SetBlack(TRUE);VIDEO_Flush();VIDEO_WaitVSync();VIDEO_WaitVSync();   /* no static on the way out */
            exit(0);
        }
        /* Direct DOL launch has no loader to return to. Freeze after committing
         * evidence; Dolphin can now stop and export the SD folder safely. */
        for(;;){VIDEO_WaitVSync();if(quit_request)quit_now();}
    }
#endif
    /* libfat publishes file size on close; make each checkpoint extractable. */
#ifdef VIPER_WII_ALLOC_LOG
    {static int dumped;if(!dumped){dumped=1;void wii_alloc_log_dump(void);wii_alloc_log_dump();}}
#endif
    STRAY_SCAN("before-log");
#ifndef VIPER_WII_LOG_BUFFERED
    if(logfile){fclose(logfile);logfile=fopen("sd:/viper/boot.log","a");if(!logfile)rt_fatal("reopen diagnostic log");}
#endif
    STRAY_SCAN("after-log");
    rt_sched_at(rt_now()+(uint64_t)CPU_HZ,progress,NULL);
}
static void require_file(const char *path,size_t bytes){
    FILE *f=fopen(path,"rb");if(!f)rt_fatal(path);
    if(fseek(f,0,SEEK_END)||ftell(f)!=(long)bytes)rt_fatal("asset size mismatch");
    fclose(f);
}
int main(int argc,char **argv){
    /* Guest RAM first: it is the hottest data and only just fits in MEM1
     * (MEM2 CPU reads are 3x slower). Allocated after the framebuffer, a
     * 68 KB larger program pushed it into MEM2 and cost 8.5% on hardware. */
    g_ram=calloc(1,RAM_SIZE);
    wii_net_report_args(argc,argv);
#ifndef VIPER_WII_WATCHDOG_S
#define VIPER_WII_WATCHDOG_S 150   /* a run takes 67-85 s; diagnostics stay inside 2x */
#endif
    wii_net_report_watchdog(VIPER_WII_WATCHDOG_S);
    VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    /* Framebuffer memory is not zeroed on hardware (Dolphin zeroes it): clear
     * it, or the boot and result screens show whatever was there before. */
    VIDEO_ClearFrameBuffer(mode,fb,COLOR_BLACK);
#ifdef VIPER_WII_GX_RENDER
    display_mode=mode;display_buffer=fb;
#endif
    console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
#ifdef VIPER_WII_AUDIO
    wii_audio_init();
#endif
    VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    printf("VIPER WII HEADLESS BEGIN %s\n",GAME_TITLE);
#ifdef VIPER_WII_HEAP_POISON
    /* Test only: fill free heap with 0x61 so any host read of memory that was
     * never written sees garbage, as on hardware (Dolphin starts zeroed). */
    {void *blocks[256];unsigned n=0;
     for(;n<256;n++){blocks[n]=malloc(1u<<20);if(!blocks[n])break;memset(blocks[n],0x61,1u<<20);}
     while(n)free(blocks[--n]);}
#endif
    if(!fatInitDefault())rt_fatal("cannot mount SD assets");
#ifndef VIPER_WII_NO_LOG
    logfile=fopen("sd:/viper/boot.log","w");if(!logfile)rt_fatal("cannot create SD diagnostic log");
#ifdef VIPER_WII_LOG_BUFFERED
    /* Measurement: hold the whole log in RAM (written at the end) so SD card
     * writes, during which the CPU idles, stay out of the timed windows. */
    setvbuf(logfile,NULL,_IOFBF,512*1024);
#endif
#endif
    rt_log("VIPER WII GUEST RAM %08lx %s\n",(unsigned long)(uintptr_t)g_ram,
           (uintptr_t)g_ram<0x90000000u?"MEM1":"MEM2 (slow: CPU reads 3x slower than MEM1)");
    STRAY_SCAN("alloc");
    if(!g_ram)rt_fatal("guest RAM allocation");
    if((uintptr_t)g_ram&7)rt_fatal("guest RAM must be 8-byte aligned");
    FILE *f=fopen("sd:/viper/kernel.bin","rb");if(!f)rt_fatal("missing kernel");
    size_t n=fread(g_ram,1,RAM_SIZE,f);if(ferror(f)||!feof(f)||!n)rt_fatal("invalid kernel size/read");fclose(f);
    STRAY_SCAN("kernel");
    require_file("sd:/viper/bios.bin",0x40000);require_file("sd:/viper/nvram.bin",0x2000);require_file("sd:/viper/ds2430.bin",40);
    f=fopen("sd:/viper/cf.img","rb");if(!f)rt_fatal("missing CF image");fclose(f);
    for(unsigned i=0;i<sizeof modules/sizeof modules[0];i++)rt_register_module(modules[i]);
    static const HwConfig hw={"sd:/viper/cf.img","sd:/viper/nvram.bin","sd:/viper/ds2430.bin","sd:/viper/bios.bin",NULL};
    STRAY_SCAN("pre-enhanced");
    wii_enhanced_init();wii_voodoo_set_present(wii_enhanced_frame);
    STRAY_SCAN("pre-renderer");
#ifdef VIPER_WII_GX_RENDER
    wii_gx_renderer_init(mode,fb);
#endif
    STRAY_SCAN("renderer");
    hw_init(&hw);
    STRAY_SCAN("hw");
#ifndef VIPER_WII_SCRIPTED_RACE
    wii_input_init();
#endif
    quit_buttons_init();
#ifndef VIPER_WII_SCRIPTED_RACE
    /* Public LWP priorities increase with urgency. Guest fibers use 80;
     * let the VSync/input loop wake above them, then block between polls. */
    LWP_SetThreadPriority(LWP_GetSelf(),81);
#endif
    PPCContext *c=&g_ctx;
    for(unsigned i=0;i<32;i++){c->r[i]=0xdeadbeef;c->f[i]=BITS_FPR(0x7ff5beef4afc0721ull);}
    for(unsigned i=0;i<4;i++)c->sprg[i]=0xdeadbeef;
    c->r[31]=hw_boot_param();c->lr=0xdeadbeef;c->ctr=0x10;c->msr=0x2070;rt_cr_unpack(c,0xdeadbeef,0xff);
    started=gettime();input_tick(NULL);rt_sched_at((uint64_t)CPU_HZ,progress,NULL);
#ifdef VIPER_WII_STRAY_WATCH
    STRAY_SCAN("start");rt_sched_at(CPU_HZ/100,stray_tick,NULL);
#endif
    rt_check(c,0x10);rt_start(0x10);
    unsigned quit_wait=0;
    for(;;){
#ifndef VIPER_WII_SCRIPTED_RACE
        wii_input_poll();
#endif
        VIDEO_WaitVSync();
        /* The guest normally quits within a tick; a stalled guest must not
         * trap the player (logfile is then left to exit's own flush). */
        if(quit_request&&++quit_wait>60){logfile=NULL;quit_now();}
    }
}
