#ifndef VIPER_WII_THREAD_H
#define VIPER_WII_THREAD_H
/* LWP condition wait releases/reacquires the same mutex. Existing running-bit
 * loops preserve the baton; aliases are private to the runtime translation unit. */
#include <ogc/lwp.h>
#include <ogc/mutex.h>
#include <ogc/cond.h>
#include <stddef.h>
#define pthread_t lwp_t
#define pthread_mutex_t mutex_t
#define pthread_cond_t cond_t
#define pthread_attr_t ViperWiiThreadAttr
#define PTHREAD_MUTEX_INITIALIZER LWP_MUTEX_NULL
typedef struct { size_t stack_size; } ViperWiiThreadAttr;
static inline int viper_wii_mutex_prepare(mutex_t *m) {
    return *m==LWP_MUTEX_NULL ? LWP_MutexInit(m,0) : 0;
}
static inline int viper_wii_mutex_lock(mutex_t *m) { return LWP_MutexLock(*m); }
static inline int viper_wii_mutex_unlock(mutex_t *m) { return LWP_MutexUnlock(*m); }
static inline int viper_wii_cond_init(cond_t *c,const void *attr) {
    (void)attr;return LWP_CondInit(c);
}
static inline int viper_wii_cond_signal(cond_t *c) { return LWP_CondSignal(*c); }
static inline int viper_wii_cond_wait(cond_t *c,mutex_t *m) { return LWP_CondWait(*c,*m); }
static inline int viper_wii_attr_init(ViperWiiThreadAttr *a) { a->stack_size=128u<<10;return 0; }
static inline int viper_wii_attr_stack(ViperWiiThreadAttr *a,size_t n) {
    if(n!=(128u<<10))return -1;
    a->stack_size=n;return 0;
}
static inline int viper_wii_attr_destroy(ViperWiiThreadAttr *a) { (void)a;return 0; }
static inline int viper_wii_create(lwp_t *t,const ViperWiiThreadAttr *a,
                                  void *(*entry)(void *),void *arg) {
    if(!a||a->stack_size!=(128u<<10))return -1;
    return LWP_CreateThread(t,entry,arg,NULL,(u32)a->stack_size,80);
}
#define pthread_mutex_lock viper_wii_mutex_lock
#define pthread_mutex_unlock viper_wii_mutex_unlock
#define pthread_cond_init viper_wii_cond_init
#define pthread_cond_signal viper_wii_cond_signal
#define pthread_cond_wait viper_wii_cond_wait
#define pthread_attr_init viper_wii_attr_init
#define pthread_attr_setstacksize viper_wii_attr_stack
#define pthread_attr_destroy viper_wii_attr_destroy
#define pthread_create viper_wii_create
#endif
