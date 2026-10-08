/* Asset-free runtime adapter probe. Link with libogc/libm; run separately.
 * Same mutex/condition baton and rounding reapplication as cpu.c. */
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <fenv.h>
#include <math.h>
#include "thread.h"
static mutex_t lock=LWP_MUTEX_NULL;
static cond_t cv[2];
static unsigned turn, steps[2], failures;
static void *worker(void *arg) {
    unsigned id=(unsigned)(uintptr_t)arg;
    pthread_mutex_lock(&lock);
    for(unsigned n=0;n<1000;n++) {
        while(turn!=id)pthread_cond_wait(&cv[id],&lock);
        int mode=id?FE_DOWNWARD:FE_UPWARD;
        if(fesetround(mode)||fegetround()!=mode)failures++;
        volatile double input=1.25;
        if(nearbyint(input)!=(id?1.0:2.0))failures++;
        if(steps[id]!=n || steps[1-id]!=(id?n+1:n))failures++;
        steps[id]++;
        turn=1-id;
        pthread_cond_signal(&cv[1-id]);
    }
    pthread_mutex_unlock(&lock);
    return NULL;
}
int main(void) {
    VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
    VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    if(viper_wii_mutex_prepare(&lock)||viper_wii_mutex_prepare(&lock)||
       pthread_cond_init(&cv[0],NULL)||pthread_cond_init(&cv[1],NULL))return 1;
    pthread_attr_t attr;pthread_attr_init(&attr);
    if(pthread_attr_setstacksize(&attr,8192)==0)return 2;
    if(pthread_attr_setstacksize(&attr,128u<<10))return 3;
    pthread_t thread;
    if(pthread_create(&thread,&attr,worker,(void *)(uintptr_t)1))return 4;
    worker((void *)(uintptr_t)0);
    LWP_JoinThread(thread,NULL);
    printf("VIPER WII THREAD END failures=%u steps=%u,%u stack=131072\n",failures,steps[0],steps[1]);
    /* Keep the completed result visible and service video interrupts. */
    for(;;)VIDEO_WaitVSync();
}
