#ifndef VIPER_WII_PROJECTIVE_TEXTURE_H
#define VIPER_WII_PROJECTIVE_TEXTURE_H
/* x/y MUST be the positions submitted to GX (including the renderer's Y flip).
 * s/t are Voodoo's affine texture numerators, w is selected TMU reciprocal W;
 * they are not already divided texture coordinates. Scales match mip layout:
 * 1/(selected_width * 2^level), 1/(selected_height * 2^level).
 * Output rows yield normalized S, T, Q from (x,y,z,1), ignoring z. */
typedef struct {float x,y,s,t,w;} WiiProjectiveVertex;
int wii_projective_texture_matrix(float out[3][4],const WiiProjectiveVertex v[3],
                                 float scale_s,float scale_t);
#endif
