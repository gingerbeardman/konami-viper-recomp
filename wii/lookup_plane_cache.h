#ifndef VIPER_WII_LOOKUP_PLANE_CACHE_H
#define VIPER_WII_LOOKUP_PLANE_CACHE_H
#include <string.h>
#include "projective_texture.h"
/* Only fixed-scale (1,1) lookup planes. Bitwise keys retain signed zero.
 * Store only successful solves; invalidate for every original/child triangle. */
typedef struct {
    WiiProjectiveVertex vertices[3];
    float matrix[3][4];
    int valid;
} WiiLookupPlaneCache;
static inline int wii_lookup_plane_get(const WiiLookupPlaneCache *cache,
    const WiiProjectiveVertex vertices[3],float matrix[3][4]){
    if(!cache->valid||memcmp(cache->vertices,vertices,sizeof cache->vertices))return 0;
    memcpy(matrix,cache->matrix,sizeof cache->matrix);return 1;
}
static inline void wii_lookup_plane_store(WiiLookupPlaneCache *cache,
    const WiiProjectiveVertex vertices[3],const float matrix[3][4]){
    memcpy(cache->vertices,vertices,sizeof cache->vertices);
    memcpy(cache->matrix,matrix,sizeof cache->matrix);cache->valid=1;
}
#endif
