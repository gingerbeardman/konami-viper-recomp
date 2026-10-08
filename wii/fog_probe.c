/* Asset-free GX fog lerp/alpha/depth check. Tests the proposed GPU operation,
 * not exact equivalence to Voodoo's signed arithmetic or spatial dithering. */
#include <gccore.h>
#include <stdio.h>
#include "fog.h"
#include "gx_fog_stage.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64] ATTRIBUTE_ALIGN(32);
static void quad(float z,GXColor c){
 const float xy[6][4]={{64,64,0,0},{576,64,1,0},{576,320,1,1},{64,64,0,0},{576,320,1,1},{64,320,0,1}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
 for(unsigned i=0;i<6;i++){GX_Position3f32(xy[i][0],xy[i][1],-z);GX_Color4u8(c.r,c.g,c.b,c.a);GX_TexCoord2f32(xy[i][2],xy[i][3]);}GX_End();GX_DrawDone();
}
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);
 GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx model;Mtx44 projection;guMtxIdentity(model);guOrtho(projection,0,480,0,640,0,1);
 GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
 GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
 GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
 GX_SetNumTexGens(2);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);GX_SetCullMode(GX_CULL_NONE);GX_SetZCompLoc(GX_FALSE);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});GX_SetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);
 const unsigned factors[4]={0,32,128,256};
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){unsigned o=(y*4+x)*2;image[o]=wii_fog_tev_factor(factors[x]);image[o+1]=image[o+32]=image[o+33]=255;}
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj tex;
 GX_InitTexObj(&tex,image,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP2);
 GX_SetNumTevStages(2);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
 wii_gx_fog_stage(1,(GXColor){96,224,16,255},1);
 GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 quad(.2f,(GXColor){64,128,192,128});
 unsigned failures=0;GXColor before[4],after[4];u32 depths[4];const unsigned xs[4]={128,256,384,512};
 for(unsigned i=0;i<4;i++){
  GX_PeekARGB(xs[i],160,&before[i]);GX_PeekZ(xs[i],160,&depths[i]);
  unsigned c=wii_fog_tev_factor(factors[i]),f=c+(c>>7);const unsigned src[3]={64,128,192},fog[3]={96,224,16},got[3]={before[i].r,before[i].g,before[i].b};
  for(unsigned j=0;j<3;j++){unsigned expected=(src[j]*(256-f)+fog[j]*f+128)>>8;if(got[j]+1<expected||got[j]>expected+1)failures++;}
  if(depths[i]>=0xffffff)failures++;
 }
 /* Alpha must stay128 for every fog factor, including opaque fog texels. */
 GX_SetAlphaCompare(GX_GREATER,128,GX_AOP_AND,GX_ALWAYS,0);quad(.1f,(GXColor){255,0,0,128});
 for(unsigned i=0;i<4;i++){u32 z;GX_PeekARGB(xs[i],160,&after[i]);GX_PeekZ(xs[i],160,&z);if(after[i].r!=before[i].r||after[i].g!=before[i].g||after[i].b!=before[i].b||z!=depths[i])failures++;}
 GXColor constant[4];
 GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_FALSE);
 for(unsigned i=0;i<4;i++){
  wii_gx_fog_stage(1,(GXColor){96,224,16,wii_fog_tev_factor(factors[i])},0);quad(.1f,(GXColor){64,128,192,128});GX_PeekARGB(128,160,&constant[i]);
  if(constant[i].r!=before[i].r||constant[i].g!=before[i].g||constant[i].b!=before[i].b)failures++;
 }
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII FOG %s\n",failures?"FAIL":"PASS");
 for(unsigned i=0;i<4;i++)printf("factor%u RGB%u,%u,%u Z%06x alpha_rejectRGB%u,%u,%u\n",factors[i],before[i].r,before[i].g,before[i].b,depths[i],after[i].r,after[i].g,after[i].b);
 for(unsigned i=0;i<4;i++)printf("constant factor%u RGB%u,%u,%u\n",factors[i],constant[i].r,constant[i].g,constant[i].b);
 printf("VIPER WII FOG END failures=%u\n",failures);fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
