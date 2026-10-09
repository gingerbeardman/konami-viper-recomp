/* Updated libogc IRQ entry saves the interrupted PC in the current KThread
 * before KTickTaskRun. This file deliberately avoids the guest PPCContext. */
#include <tuxedo/thread.h>
#include <stdint.h>
extern void rt_log(const char *,...);
#ifndef VIPER_WII_PC_PROFILE_PERIOD_US
#define VIPER_WII_PC_PROFILE_PERIOD_US 997
#endif
#if VIPER_WII_PC_PROFILE_PERIOD_US < 100 || VIPER_WII_PC_PROFILE_PERIOD_US > 10000
#error PC profiling period must be between 100 and 10000 microseconds
#endif
#ifndef VIPER_WII_PC_PROFILE_BUCKET_BYTES
#define VIPER_WII_PC_PROFILE_BUCKET_BYTES 64
#endif
#if VIPER_WII_PC_PROFILE_BUCKET_BYTES == 4
enum { PC_SHIFT=2, SLOTS=32768 };
#elif VIPER_WII_PC_PROFILE_BUCKET_BYTES == 64
enum { PC_SHIFT=6, SLOTS=8192 };
#else
#error PC profiling bucket must be 4 or 64 bytes
#endif
static struct {uint32_t pc,count;} bins[SLOTS];
static KTickTask sampler;
static uint32_t samples,overflow;
static volatile uint32_t running;
#ifdef VIPER_WII_BULK_ADMISSION_DIAGNOSTIC
static uint64_t bulk_admissions[2][6];
void wii_bulk_admission_observe(unsigned site,unsigned reason){
    if(running&&site<2&&reason<6)bulk_admissions[site][reason]++;
}
#endif
#ifdef VIPER_WII_PC_PROFILE_LR
static struct {uint32_t pc,lr,count;} lr_bins[SLOTS];
static uint32_t lr_overflow;
static void sample_lr(uint32_t pc,uint32_t lr){
    unsigned slot=(((pc>>PC_SHIFT)^(lr>>2))*2654435761u)&(SLOTS-1);
    for(unsigned i=0;i<8;i++,slot=(slot+1)&(SLOTS-1)){
        if(!lr_bins[slot].count||(lr_bins[slot].pc==pc&&lr_bins[slot].lr==lr)){
            lr_bins[slot].pc=pc;lr_bins[slot].lr=lr;lr_bins[slot].count++;return;
        }
    }
    lr_overflow++;
}
#endif
/* 0 pauses sampling without stopping the tick (diagnostic gates). */
volatile uint32_t wii_pc_profile_gate=1;
static void sample(KTickTask *task){
    (void)task;
    if(!running||!wii_pc_profile_gate)return;
    uint32_t pc=KThreadGetSelf()->ctx.pc&~(VIPER_WII_PC_PROFILE_BUCKET_BYTES-1u);
#ifdef VIPER_WII_PC_PROFILE_LR
    sample_lr(pc,KThreadGetSelf()->ctx.lr);
#endif
    unsigned slot=((pc>>PC_SHIFT)*2654435761u)&(SLOTS-1);
    samples++;
    for(unsigned i=0;i<8;i++,slot=(slot+1)&(SLOTS-1)){
        if(!bins[slot].count||bins[slot].pc==pc){
            bins[slot].pc=pc;bins[slot].count++;return;
        }
    }
    overflow++;
}
void wii_pc_profile_start(void){
#ifdef VIPER_WII_BULK_ADMISSION_DIAGNOSTIC
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<6;j++)bulk_admissions[i][j]=0;
#endif
    running=1;__asm__ volatile("":::"memory");
    KTickTaskStart(&sampler,sample,PPCUsToTicks(VIPER_WII_PC_PROFILE_PERIOD_US),PPCUsToTicks(VIPER_WII_PC_PROFILE_PERIOD_US));
}
void wii_pc_profile_stop(void){
    /* IRQ callbacks and their counters are invisible to ordinary C alias
     * analysis. Freeze admissions and prevent LTO hoisting the report's
     * counter loads ahead of the SDK stop operation. */
    running=0;__asm__ volatile("":::"memory");
#ifdef VIPER_WII_BULK_ADMISSION_DIAGNOSTIC
    /* Reasons: admitted, logging, staging, source bounds, readiness,
     * forced scalar audit/trace. These are counts, not timing evidence. */
    for(unsigned i=0;i<2;i++)for(unsigned j=0;j<6;j++)
        rt_log("VIPER WII BULK ADMISSION site=%u reason=%u count=%llu\n",i,j,
            (unsigned long long)bulk_admissions[i][j]);
#endif
    KTickTaskStop(&sampler);
    __asm__ volatile("":::"memory");
    rt_log("VIPER WII PC SUMMARY samples=%lu overflow=%lu bucket_bytes=%u period_us=%u\n",
        (unsigned long)samples,(unsigned long)overflow,
        (unsigned)VIPER_WII_PC_PROFILE_BUCKET_BYTES,(unsigned)VIPER_WII_PC_PROFILE_PERIOD_US);
    for(unsigned i=0;i<SLOTS;i++)if(bins[i].count)
        rt_log("VIPER WII PC bin=%08lx count=%lu\n",
            (unsigned long)bins[i].pc,(unsigned long)bins[i].count);
#ifdef VIPER_WII_PC_PROFILE_LR
    rt_log("VIPER WII PC LR SUMMARY overflow=%lu\n",(unsigned long)lr_overflow);
    for(unsigned i=0;i<SLOTS;i++)if(lr_bins[i].count)
        rt_log("VIPER WII PC LR pc=%08lx lr=%08lx count=%lu\n",
            (unsigned long)lr_bins[i].pc,(unsigned long)lr_bins[i].lr,(unsigned long)lr_bins[i].count);
#endif
}

/* The busiest bins of the report wii_pc_profile_stop logs, as text (for
 * network reports; small, so it needs no big buffer in a full heap). */
#include <stdio.h>
unsigned wii_pc_profile_text(char *out,unsigned cap){
    unsigned n=snprintf(out,cap,"VIPER WII PC SUMMARY samples=%lu overflow=%lu bucket_bytes=%u period_us=%u\n",
        (unsigned long)samples,(unsigned long)overflow,
        (unsigned)VIPER_WII_PC_PROFILE_BUCKET_BYTES,(unsigned)VIPER_WII_PC_PROFILE_PERIOD_US);
    for(unsigned pass=0;pass<400&&n<cap-64;pass++){   /* descending counts, one bin per pass */
        unsigned best=SLOTS;
        for(unsigned i=0;i<SLOTS;i++)if(bins[i].count&&(best==SLOTS||bins[i].count>bins[best].count))best=i;
        if(best==SLOTS)break;
        n+=snprintf(out+n,cap-n,"VIPER WII PC bin=%08lx count=%lu\n",(unsigned long)bins[best].pc,(unsigned long)bins[best].count);
        bins[best].count=0;
    }
    return n;
}
