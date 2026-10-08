#include "projective_texture.h"
#include <math.h>
#include <float.h>
/* Solve the three affine planes in double to avoid needless cancellation.
 * GX coefficients are float; this preserves the rational interpolation model,
 * not the Voodoo rasterizer's fixed-point rounding at every sample. */
int wii_projective_texture_matrix(float out[3][4],const WiiProjectiveVertex v[3],float ss,float ts){
    if(!out||!v||!isfinite(ss)||!isfinite(ts)||ss<=0||ts<=0)return 0;
    for(unsigned i=0;i<3;i++)if(!isfinite(v[i].x)||!isfinite(v[i].y)||
        !isfinite(v[i].s)||!isfinite(v[i].t)||!isfinite(v[i].w)||v[i].w<=0)return 0;
    double dx1=(double)v[1].x-v[0].x,dy1=(double)v[1].y-v[0].y;
    double dx2=(double)v[2].x-v[0].x,dy2=(double)v[2].y-v[0].y;
    double det=dx1*dy2-dx2*dy1;
    if(!isfinite(det)||det==0)return 0;
    float result[3][4];
    for(unsigned row=0;row<3;row++){
        double a=row==0?v[0].s:row==1?v[0].t:v[0].w;
        double b=row==0?v[1].s:row==1?v[1].t:v[1].w;
        double c=row==0?v[2].s:row==1?v[2].t:v[2].w;
        double scale=row==0?ss:row==1?ts:1;
        double nx=(b-a)*dy2-(c-a)*dy1,ny=dx1*(c-a)-dx2*(b-a);
#ifdef VIPER_WII_ZERO_PLANE_QUOTIENT
        /* Finite zero divided by a finite nonzero determinant stays zero.
         * Preserve its sign; the determinant guard above still applies. */
        double x=nx==0?copysign(0.0,(!!signbit(nx)!=!!signbit(det))?-1.0:1.0):nx/det;
        double y=ny==0?copysign(0.0,(!!signbit(ny)!=!!signbit(det))?-1.0:1.0):ny/det;
#else
        double x=nx/det,y=ny/det;
#endif
        double coeff[4]={x*scale,y*scale,0,(a-x*v[0].x-y*v[0].y)*scale};
        for(unsigned col=0;col<4;col++){
            if(!isfinite(coeff[col])||fabs(coeff[col])>FLT_MAX)return 0;
            result[row][col]=(float)coeff[col];
        }
    }
    for(unsigned row=0;row<3;row++)for(unsigned col=0;col<4;col++)out[row][col]=result[row][col];
    return 1;
}
