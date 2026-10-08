/* Immutable constant-fog tile and TEV register lifetime probe. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_constant_fog.h"
#include "gx_fog_stage.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);

static float draw_z=-.5f;

static void quad(GXColor c){
 const float xy[6][2]={{64,64},{320,64},{320,320},{64,64},{320,320},{64,320}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);for(unsigned i=0;i<6;i++){
 GX_Position3f32(xy[i][0],xy[i][1],draw_z);GX_Color4u8(c.r,c.g,c.b,c.a);GX_TexCoord2f32(.5f,.5f);}GX_End();GX_DrawDone();
}


static void initial(GXColor input,unsigned poison){
 GX_SetTevColor(GX_TEVREG1,input);GX_SetTevColor(GX_TEVREG2,(GXColor){poison,255-poison,poison^85,17});
 GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
 // Simulate a TMU's GPU REG0 write, rather than merely initial CPU state.
 GX_SetTevColorIn(0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C2);
 GX_SetTevAlphaIn(0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A2);
 GX_SetTevColorOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG0);
 GX_SetTevAlphaOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG0);
 GX_SetTevOrder(1,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
 GX_SetTevColorIn(1,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C1);
 GX_SetTevAlphaIn(1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A1);
 GX_SetTevColorOp(1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaOp(1,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
}
static GXColor sample(int alpha){
 if(alpha){GX_SetTevOrder(3,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
 GX_SetTevColorIn(3,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_APREV);
 GX_SetTevAlphaIn(3,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
 GX_SetTevColorOp(3,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaOp(3,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);GX_SetNumTevStages(4);}
 quad((GXColor){255,255,255,255});GXColor result;GX_PeekARGB(160,160,&result);return result;
}
int main(void){
 int sd=fatInitDefault();
 if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("CONSTANT FOG START\n",f);fclose(f);}}
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);GX_SetColorUpdate(GX_TRUE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx m;Mtx44 p;guMtxIdentity(m);guOrtho(p,0,480,0,640,0,1);GX_LoadPosMtxImm(m,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
 GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
 GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
 GX_SetNumTexGens(1);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);GX_SetCullMode(GX_CULL_NONE);GX_SetZCompLoc(GX_FALSE);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});

 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetAlphaUpdate(GX_FALSE);
 GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);


 wii_gx_constant_fog_init();
 unsigned failures=0,cases=0;const unsigned alphas[]={0,1,128,255};
 static unsigned char reference[64] ATTRIBUTE_ALIGN(32);
 for(unsigned factor=0;factor<256;factor++)for(unsigned ai=0;ai<4;ai++){
  GXColor input={64,128,192,alphas[ai]},fog={96,224,16,factor};
  for(unsigned i=0;i<16;i++){unsigned o=i*2;reference[o]=factor;reference[o+1]=reference[o+32]=reference[o+33]=0;}
  DCFlushRange(reference,64);GX_InvalidateTexAll();GXTexObj tex;GX_InitTexObj(&tex,reference,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP2);
  GX_SetNumTexGens(3);GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
  GX_SetTexCoordGen(GX_TEXCOORD2,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
  initial(input,factor);wii_gx_fog_stage(2,fog,1);GXColor expected=sample(0);
  initial(input,255-factor);if(!wii_gx_constant_fog_bind(2,fog,3))failures++;
  GXColor actual=sample(0);GXColor alpha=sample(1);
  // Also overwrite coord1 as a later W-depth setup would; a solid tile stays constant.
  GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
  if(!wii_gx_constant_fog_bind(2,fog,3))failures++;
  GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);GXColor remapped=sample(0);
  int bad=actual.r!=expected.r||actual.g!=expected.g||actual.b!=expected.b||alpha.r!=input.a||
   remapped.r!=expected.r||remapped.g!=expected.g||remapped.b!=expected.b;
  if(bad&&failures++==0&&sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"CONSTANT FOG BAD factor=%u a=%u expected=%u,%u,%u actual=%u,%u,%u alpha=%u\n",factor,input.a,expected.r,expected.g,expected.b,actual.r,actual.g,actual.b,alpha.r);fclose(f);}}
  cases++;if(sd&&cases%64==0){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"CONSTANT FOG PROGRESS cases=%u failures=%u\n",cases,failures);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 }
 if(sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"CONSTANT FOG END result=%s cases=%u failures=%u\n",failures?"FAIL":"PASS",cases,failures);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("CONSTANT FOG END %s cases=%u failures=%u\n",failures?"FAIL":"PASS",cases,failures);fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
