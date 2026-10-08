#ifndef WII_SUBMISSION_PROFILE_H
#define WII_SUBMISSION_PROFILE_H
#include "ppc_rt.h"
void wii_submission_observe(uint32_t pc,uint32_t ea,unsigned little);
void wii_submission_profile_start(void);
void wii_submission_profile_stop(void);
static inline void WII_SUBMISSION_ST32(uint32_t pc,uint32_t ea,uint32_t value){
    wii_submission_observe(pc,ea,0);ST32(ea,value);
}
static inline void WII_SUBMISSION_ST32LE(uint32_t pc,uint32_t ea,uint32_t value){
    wii_submission_observe(pc,ea,1);ST32LE(ea,value);
}
#endif
