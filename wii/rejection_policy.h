#ifndef VIPER_WII_REJECTION_POLICY_H
#define VIPER_WII_REJECTION_POLICY_H
#include <stdint.h>
/* Policies depend on framebuffer semantics, never colourpath/texture tuples.
 * KEEP_TEST gates failed predicates to alpha0 and retains the guest compare.
 * ADD_NONZERO additionally ANDs alpha>0 with the guest compare.
 * BINARY replaces alpha only when guest comparison and RGB blending ignore it.
 * Alpha-plane storage requires a different framebuffer representation. */
typedef enum {
    WII_REJECT_UNSUPPORTED, WII_REJECT_KEEP_TEST,
    WII_REJECT_ADD_NONZERO, WII_REJECT_BINARY, WII_REJECT_SPLIT_DEPTH
} WiiRejectionPolicy;
static inline int wii_alpha_accepts_zero(uint32_t alpha){
    if(!(alpha&1))return 1;
    unsigned ref=alpha>>24;
    switch((alpha>>1)&7){
        case 0:case 4:return 0;
        case 1:case 5:return ref!=0;
        case 2:case 6:return ref==0;
        default:return 1;
    }
}
static inline WiiRejectionPolicy wii_rejection_policy(uint32_t fbz,
    uint32_t alpha,int positive_alpha){
    if((fbz&(1u<<18))||(alpha&0xe0u))return WII_REJECT_UNSUPPORTED;
    if(!wii_alpha_accepts_zero(alpha))return WII_REJECT_KEEP_TEST;
    if(positive_alpha)return WII_REJECT_ADD_NONZERO;
    unsigned src=(alpha>>8)&15,dst=(alpha>>12)&15;
    int always=!(alpha&1)||((alpha>>1)&7)==7;
    int blend=alpha&16;
    if(always&&(!blend||((src!=1&&src!=5&&src!=15)&&dst!=1&&dst!=5)))
        return WII_REJECT_BINARY;
    int unchanged_depth=!(fbz&1024)||((fbz&16)&&((fbz>>5)&7)==2);
    if(blend&&src==1&&(dst==4||dst==5)&&unchanged_depth)
        return WII_REJECT_ADD_NONZERO;
    /* Colour-first with no depth writes may omit RGB-neutral alpha0.
     * A second, colour-disabled binary-predicate draw supplies depth writes. */
    if(always&&blend&&src==1&&(dst==4||dst==5)&&(fbz&1024))
        return WII_REJECT_SPLIT_DEPTH;
    return WII_REJECT_UNSUPPORTED;
}
#endif
