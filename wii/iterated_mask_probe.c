/* Native iterated-alpha parity/chroma probe; checks actual EFB RGB and Z. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_rejection.h"
#include "render_family.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static float draw_z=-.8f;
static void quad(GXColor c,unsigned right_alpha){
 const float xy[6][2]={{64,64},{320,64},{320,320},{64,64},{320,320},{64,320}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);for(unsigned i=0;i<6;i++){
 GX_Position3f32(xy[i][0],xy[i][1],draw_z);GX_Color4u8(c.r,c.g,c.b,xy[i][0]>64?right_alpha:c.a);GX_TexCoord2f32(0,0);}GX_End();GX_DrawDone();
}
static void plain(void){GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);}
static void background(void){plain();GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);quad((GXColor){32,64,96,255},255);}
static void blend(void){GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);}
static int same(GXColor a,GXColor b){return a.r==b.r&&a.g==b.g&&a.b==b.b;}
int main(void){
 int sd=fatInitDefault();if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("ITERATED START\n",f);fclose(f);}}
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


 GX_SetNumTexGens(0);GX_SetAlphaUpdate(GX_FALSE);
 /* Volatile inputs force the PowerPC/JIT to execute the classifier rather
  * than checking a constant folded result. */
 volatile uint32_t captured[]={0x15024100,0x217b,0x4511f,0x40,0,0x10000000};
 unsigned classifier_pass=wii_iterated_mask_family(captured[0],captured[1],captured[2],captured[3],captured[4],captured[5]);
 unsigned failures=!classifier_pass;const GXColor bg={32,64,96,255};
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"ITERATED RUNTIME CLASSIFIER accepted=%u\n",classifier_pass);fclose(f);}}
 for(unsigned a=0;a<256;a++)for(unsigned key=0;key<2;key++)for(unsigned black=0;black<2;black++)for(unsigned additive=0;additive<2;additive++){
  GXColor source={black?0:64,black?0:128,black?0:192,a},control,actual;u32 initial,z;
  background();GX_PeekZ(160,160,&initial);blend();
  GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,additive?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
  quad(source,a);GX_PeekARGB(160,160,&control);
  background();blend();
  GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,additive?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
  if(wii_gx_rejection(1,GX_CC_RASC,GX_CA_RASA,0,key,0,1)!=7)failures++;
  GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);quad(source,a);
  GX_PeekARGB(160,160,&actual);GX_PeekZ(160,160,&z);
  GXColor expected=(a&1)&&!(key&&black)?control:bg;
  if(!same(actual,expected)||z!=initial)failures++;
 }
 unsigned chroma_depth_fail=0;
 for(unsigned a=1;a<256;a++)for(unsigned black=0;black<2;black++)for(unsigned additive=0;additive<2;additive++){
  GXColor source={black?0:64,black?0:128,black?0:192,a},control,actual;u32 initial,control_z,z;
  draw_z=-.8f;background();GX_PeekZ(160,160,&initial);blend();
  draw_z=-.4f;GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);
  GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,additive?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
  quad(source,a);GX_PeekARGB(160,160,&control);GX_PeekZ(160,160,&control_z);
  draw_z=-.8f;background();blend();draw_z=-.4f;
  GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);
  GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,additive?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
  wii_gx_rejection(1,GX_CC_RASC,GX_CA_RASA,0,1,0,0);
  GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);quad(source,a);
  GX_PeekARGB(160,160,&actual);GX_PeekZ(160,160,&z);
  if(!same(actual,black?bg:control)||z!=(black?initial:control_z)||control_z==initial)chroma_depth_fail++;
 }
 failures+=chroma_depth_fail;draw_z=-.8f;
 /* The common-positive-RGB proof bypasses rejection, preserving depth even
  * when alpha0 makes the fragment's colour contribution zero. */
 unsigned zero_alpha_depth_fail=0;
 for(unsigned additive=0;additive<2;additive++){
  draw_z=-.8f;background();u32 initial,z;GXColor actual;GX_PeekZ(160,160,&initial);
  blend();draw_z=-.4f;GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);
  GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,additive?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
  quad((GXColor){1,0,0,0},0);GX_PeekARGB(160,160,&actual);GX_PeekZ(160,160,&z);
  if(!same(actual,bg)||z==initial)zero_alpha_depth_fail++;
 }
 failures+=zero_alpha_depth_fail;draw_z=-.8f;
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"ITERATED ZERO ALPHA DEPTH cases=2 failures=%u\n",zero_alpha_depth_fail);fclose(f);}}
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"ITERATED CHROMA DEPTH cases=1020 failures=%u\n",chroma_depth_fail);fclose(f);}}
 /* Determine GX's interpolated alpha bytes independently, then check that
  * the parity predicate preserves exactly the odd-byte blend survivors. */
 unsigned alphas[256];GXColor control[256];
 background();GX_SetNumTevStages(1);GX_SetTevColorIn(0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_RASA);
 GX_SetTevColorOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 quad((GXColor){255,255,255,0},255);
 for(unsigned i=0;i<256;i++){GXColor c;GX_PeekARGB(64+i,160,&c);alphas[i]=c.r;}
 background();blend();quad((GXColor){255,255,255,0},255);
 for(unsigned i=0;i<256;i++)GX_PeekARGB(64+i,160,&control[i]);
 background();blend();wii_gx_rejection(1,GX_CC_RASC,GX_CA_RASA,0,1,0,1);
 GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);quad((GXColor){255,255,255,0},255);
 unsigned gradient_fail=0;
 for(unsigned i=0;i<256;i++){GXColor c;GX_PeekARGB(64+i,160,&c);if(!same(c,(alphas[i]&1)?control[i]:bg))gradient_fail++;}
 failures+=gradient_fail;
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"ITERATED END result=%s uniform_cases=2048 gradient_samples=256 failures=%u gradient_failures=%u\n",failures?"FAIL":"PASS",failures,gradient_fail);static const char padding[65536]={0};fwrite(padding,1,sizeof padding,f);fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("ITERATED END %s failures=%u gradient=%u sd=%d\n",failures?"FAIL":"PASS",failures,gradient_fail,sd);fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
