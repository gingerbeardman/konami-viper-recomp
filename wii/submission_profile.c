/* Diagnostic only: count executed scalar GL stores after optional batching.
 * Bulk publications do not pass these hooks. Not candidate timing evidence. */
#include "submission_profile.h"
#include "runtime.h"
#include <string.h>
#define SLOTS 1024u
typedef struct {uint32_t pc,low,high;uint64_t count,little;} Writer;
static Writer writers[SLOTS];
static unsigned active;
static uint64_t total,overflow;
void wii_submission_profile_start(void){
    memset(writers,0,sizeof writers);total=overflow=0;active=1;
}
void wii_submission_observe(uint32_t pc,uint32_t ea,unsigned little){
    if(!active || ea<0x84000000u || ea>=0x86000000u)return;
    total++;
    unsigned slot=(pc>>2)&(SLOTS-1);
    for(unsigned probe=0;probe<SLOTS;probe++,slot=(slot+1)&(SLOTS-1)){
        Writer *w=writers+slot;
        if(!w->count){w->pc=pc;w->low=w->high=ea;}
        if(w->pc==pc){
            w->count++;w->little+=!!little;
            if(ea<w->low)w->low=ea;
            if(ea>w->high)w->high=ea;
            return;
        }
    }
    overflow++;
}
void wii_submission_profile_stop(void){
    active=0;
    rt_log("VIPER WII SUBMISSION scope=generated_gl_lfb total=%llu overflow=%llu\n",
        (unsigned long long)total,(unsigned long long)overflow);
    for(unsigned i=0;i<SLOTS;i++)if(writers[i].count){
        const Writer *w=writers+i;
        rt_log("VIPER WII WRITER pc=%08lx count=%llu little=%llu low=%08lx high=%08lx\n",
            (unsigned long)w->pc,(unsigned long long)w->count,
            (unsigned long long)w->little,(unsigned long)w->low,(unsigned long)w->high);
    }
}
