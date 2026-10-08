/* Native affine RGB/alpha interpolation probe; Voodoo fixed-point setup and
 * sample rounding are not asserted equivalent by this GX-only test. */
#include <gccore.h>
#include <stdio.h>
#include <math.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static void triangle(float z,int blue){
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);
 GX_Position3f32(64,64,-z);GX_Color4u8(blue?0:255,0,blue?255:0,blue?255:0);
 GX_Position3f32(576,64,-z);GX_Color4u8(0,blue?0:255,blue?255:0,255);
 GX_Position3f32(64,448,-z);GX_Color4u8(0,0,255,blue?255:0);GX_End();GX_DrawDone();
}
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);GX_SetColorUpdate(GX_TRUE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx m;Mtx44 p;guMtxIdentity(m);guOrtho(p,0,480,0,640,0,1);GX_LoadPosMtxImm(m,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);
 GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
 GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
 GX_SetNumTexGens(0);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);GX_SetCullMode(GX_CULL_NONE);GX_SetZCompLoc(GX_FALSE);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
 GX_SetNumTevStages(1);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
 triangle(.2f,0);
 const unsigned xy[3][2]={{128,100},{256,160},{448,100}};GXColor actual[3];unsigned expected[3][3],fail=0;
 for(unsigned i=0;i<3;i++){
  double b=(xy[i][0]+.5-64)/512,c=(xy[i][1]+.5-64)/384,a=1-b-c;
  expected[i][0]=(unsigned)lround(a*255);expected[i][1]=(unsigned)lround(b*255);expected[i][2]=(unsigned)lround(c*255);
  GX_PeekARGB(xy[i][0],xy[i][1],&actual[i]);
  if(fabs((double)actual[i].r-expected[i][0])>2||fabs((double)actual[i].g-expected[i][1])>2||fabs((double)actual[i].b-expected[i][2])>2)fail++;
 }
 /* Repeat after clear with alpha =255*b and threshold128. Then a rear blue
  * triangle must pass only rejected samples, retaining accepted depth/color. */
 GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();GX_SetAlphaCompare(GX_GREATER,128,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);triangle(.2f,0);
 u32 depth[3],afterdepth[3];GXColor after[3];
 for(unsigned i=0;i<3;i++)GX_PeekZ(xy[i][0],xy[i][1],&depth[i]);
 if(depth[0]!=0xffffff||depth[1]!=0xffffff||depth[2]>=0xffffff)fail++;
 triangle(.5f,1);
 for(unsigned i=0;i<3;i++){GX_PeekARGB(xy[i][0],xy[i][1],&after[i]);GX_PeekZ(xy[i][0],xy[i][1],&afterdepth[i]);}
 if(after[0].r||after[0].g||after[0].b!=255||after[1].r||after[1].g||after[1].b!=255||after[2].r!=actual[2].r||after[2].g!=actual[2].g||afterdepth[2]!=depth[2])fail++;
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII COLOR %s\n",fail?"FAIL":"PASS");
 for(unsigned i=0;i<3;i++)printf("%u,%u RGB%u,%u,%u ideal%u,%u,%u Z%06x rear%u,%u,%u\n",xy[i][0],xy[i][1],actual[i].r,actual[i].g,actual[i].b,expected[i][0],expected[i][1],expected[i][2],depth[i],after[i].r,after[i].g,after[i].b);
 printf("VIPER WII COLOR END failures=%u RGB tolerance2codes\n",fail);VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
