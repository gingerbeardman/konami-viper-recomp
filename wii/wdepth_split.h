#ifndef VIPER_WII_WDEPTH_SPLIT_H
#define VIPER_WII_WDEPTH_SPLIT_H
#include "voodoo_headless.h"
/* Bands0..63: exponent=e=band/4, quarter=k=band%4,
 * [(.5+k*.125)*2^-e, (.5+(k+1)*.125)*2^-e].
 * Band64 saturates below2^-16; band65 saturates above1.
 * Clip intermediates use double precision; final float output preserves original
 * winding, and affine interpolation of every attribute.
 * Maximum3 triangles per slab,66 slabs. No GX rounding equivalence claimed. */
#define WII_WDEPTH_SPLIT_MAX 198u
typedef struct {WiiVoodooVertex v[3];unsigned band;} WiiWDepthTriangle;
int wii_wdepth_band(unsigned band,float *low,float *high);
/* Render slab selection can merge four quarters in the opt-in lookup spike.
 * The original quarter definition remains available for identical texel values. */
static inline int wii_wdepth_render_band(unsigned band,float *low,float *high){
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 if(band<64&&(band&3))return 0;
#endif
 if(!wii_wdepth_band(band,low,high))return 0;
#ifdef VIPER_WII_WDEPTH_MERGED_LOOKUP
 if(band<64)*high=*low*2;
#endif
 return 1;
}
int wii_wdepth_split(const WiiVoodooVertex in[3],WiiWDepthTriangle *out,
                    unsigned capacity,unsigned *count);
#endif
