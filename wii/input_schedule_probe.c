/* Asset-free validation of the live-input scheduler priority choice. The worker
 * stays runnable without yields; a five-second timebase deadline guarantees it
 * exits even if main is starved. No WPAD, game assets, or input injection. */
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <stdatomic.h>
#include <stdio.h>
static atomic_uint work,done,stop;
static void *worker(void *unused){
    (void)unused;uint64_t start=gettime();
    while(!atomic_load_explicit(&stop,memory_order_relaxed)){
        atomic_fetch_add_explicit(&work,1,memory_order_relaxed);
        if(ticks_to_millisecs(gettime()-start)>=5000)break;
    }
    atomic_store_explicit(&done,1,memory_order_release);return NULL;
}
int main(void){
    VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
    VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    LWP_SetThreadPriority(LWP_GetSelf(),81);
    lwp_t thread;int result=LWP_CreateThread(&thread,worker,NULL,NULL,128*1024,80);
    unsigned polls=0,concurrent=0,advances=0,last=0,failures=0;
    uint64_t start=gettime();
    if(result)failures++;
    else{
        for(unsigned n=0;n<120;n++){
            VIDEO_WaitVSync();polls++;
            unsigned current=atomic_load_explicit(&work,memory_order_relaxed);
            if(!atomic_load_explicit(&done,memory_order_acquire))concurrent++;
            if(current!=last)advances++;
            last=current;
        }
        atomic_store_explicit(&stop,1,memory_order_relaxed);
        LWP_JoinThread(thread,NULL);
        if(polls!=120||concurrent<100||advances<100||!last||!atomic_load(&done))failures++;
    }
    printf("VIPER WII INPUT SCHEDULE %s\n",failures?"FAIL":"PASS");
    printf("main=81 worker=80 polls=%u concurrent=%u advances=%u\n",polls,concurrent,advances);
    printf("work=%u elapsed_ms=%llu create=%d\n",last,(unsigned long long)ticks_to_millisecs(gettime()-start),result);
    printf("VIPER WII INPUT SCHEDULE END failures=%u\n",failures);
    for(;;)VIDEO_WaitVSync();
}
