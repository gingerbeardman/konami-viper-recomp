/* Asset-free GX STQ validation; requires Dolphin Graphics.Hacks.EFBAccessEnable.
 * Expected texels are calculated independently from barycentric S/T/Q planes. */
#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "projective_texture.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static unsigned char image[8*8*4] ATTRIBUTE_ALIGN(32);
static GXColor texel(unsigned x,unsigned y){return (GXColor){(u8)(20+x*28),(u8)(20+y*28),(u8)(30+((x+3*y)%8)*27),255};}
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
 GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx model;Mtx44 projection;guMtxIdentity(model);guOrtho(projection,0,480,0,640,0,1);
 GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
 GX_SetNumChans(0);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
 for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
  GXColor c=texel(x,y);unsigned o=((y/4)*2+x/4)*64+((y&3)*4+(x&3))*2;
  image[o]=255;image[o+1]=c.r;image[o+32]=c.g;image[o+33]=c.b;
 }
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj texture;
 GX_InitTexObj(&texture,image,8,8,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&texture,GX_TEXMAP0);
 WiiProjectiveVertex v[3]={{64,64,0,0,1},{576,64,2,0,2},{64,448,0,.5f,.5f}};
 Mtx matrix;unsigned failures=0;if(!wii_projective_texture_matrix(matrix,v,1,1))failures++;
 GX_LoadTexMtxImm(matrix,GX_TEXMTX0,GX_MTX3x4);GX_SetNumTexGens(1);
 GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX0);
 GX_SetNumTevStages(1);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);for(unsigned i=0;i<3;i++)GX_Position3f32(v[i].x,v[i].y,-.5f);GX_End();GX_DrawDone();
 const unsigned xy[3][2]={{240,160},{160,280},{320,128}};
 GXColor actual[3],expected[3];unsigned tx[3],ty[3],affx[3],affy[3];
 for(unsigned i=0;i<3;i++){
  double b=(xy[i][0]+.5-64)/512,c=(xy[i][1]+.5-64)/384,a=1-b-c;
  double q=a+2*b+.5*c,u=2*b/q,t=.5*c/q;
  tx[i]=(unsigned)floor(8*u);ty[i]=(unsigned)floor(8*t);affx[i]=(unsigned)floor(8*b);affy[i]=(unsigned)floor(8*c);
  if(a<=0||tx[i]>7||ty[i]>7||(tx[i]==affx[i]&&ty[i]==affy[i]))failures++;
  /* Keep each expected sample comfortably away from nearest texel boundaries. */
  if(fabs(8*u-round(8*u))<.08||fabs(8*t-round(8*t))<.08)failures++;
  expected[i]=texel(tx[i],ty[i]);GX_PeekARGB(xy[i][0],xy[i][1],&actual[i]);
  if(actual[i].r!=expected[i].r||actual[i].g!=expected[i].g||actual[i].b!=expected[i].b)failures++;
 }
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII PROJECTIVE %s\n",failures?"FAIL":"PASS");
 for(unsigned i=0;i<3;i++)printf("%u,%u texel%u,%u affine%u,%u RGB%u,%u,%u expected%u,%u,%u\n",xy[i][0],xy[i][1],tx[i],ty[i],affx[i],affy[i],actual[i].r,actual[i].g,actual[i].b,expected[i].r,expected[i].g,expected[i].b);
 printf("VIPER WII PROJECTIVE END failures=%u\n",failures);
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
