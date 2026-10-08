/* Asset-free limited-interval W-depth lookup validation. Requires Dolphin
 * Graphics.Hacks.EFBAccessEnable. No whole-range depth equivalence claim. */
#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static unsigned char image[1024*4*4] ATTRIBUTE_ALIGN(32);
static unsigned wd(double w){
 uint64_t value=(uint64_t)(w*4294967296.0)<<16;
 if(!value)return 65535;
 int e=__builtin_clzll(value)-16;
 if(e<0)return 0;
 if(e>=16)return 65535;
 return ((e<<12)|((value>>(35-e))^0x1fff))+1;
}
static void draw(float depth){
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);
 GX_Position3f32(64,64,-depth);GX_Position3f32(576,64,-depth);GX_Position3f32(64,448,-depth);GX_End();GX_DrawDone();
}
int main(void){
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB565_Z16,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
 GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx model;Mtx44 projection;guMtxIdentity(model);guOrtho(projection,0,480,0,640,0,1);
 GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
 GX_SetNumChans(0);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetZCompLoc(GX_FALSE);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<1024;x++){
  unsigned d=wd(.5+(x+.5)/8192.0),o=(x/4)*64+(y*4+(x&3))*2;
  image[o]=255;image[o+1]=d>>8;image[o+32]=d&255;image[o+33]=0;
 }
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj texture;
 GX_InitTexObj(&texture,image,1024,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&texture,GX_TEXMAP0);
 Mtx matrix={{1.0f/512,0,0,-64.0f/512},{0,0,0,.5f},{0,0,0,1}};
 GX_LoadTexMtxImm(matrix,GX_TEXMTX0,GX_MTX3x4);GX_SetNumTexGens(1);
 GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX0);
 GX_SetNumTevStages(1);GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 GX_SetTevColor(GX_TEVREG0,(GXColor){255,0,0,255});
 GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C0);
 GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A0);
 GX_SetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetZTexture(GX_ZT_REPLACE,GX_TF_Z24X8,0);draw(.5f);
 const unsigned xy[3][2]={{128,100},{256,100},{384,100}};
 unsigned actual[3],expected[3],failures=0;
 for(unsigned i=0;i<3;i++){
  expected[i]=wd(.5+(xy[i][0]+.5-64)/4096.0);
  GX_PeekZ(xy[i][0],xy[i][1],&actual[i]);
  /* Dolphin2606 EFBInterface::PeekDepthInternal compresses RGB565_Z16
   * peeks to raw16 bits (GX_ZC_LINEAR shifts the internal Z24 by8). */
  if(actual[i]!=expected[i])failures++;
 }
 /* Alpha-zero near replacement must leave both existing color and Z intact. */
 GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetTevColor(GX_TEVREG0,(GXColor){0,255,0,0});
 GX_SetZTexture(GX_ZT_REPLACE,GX_TF_Z24X8,0);draw(.5f);
 unsigned hole;GX_PeekZ(256,100,&hole);GXColor holecolor;GX_PeekARGB(256,100,&holecolor);
 if(hole!=actual[1]||holecolor.r!=255||holecolor.g!=0)failures++;
 /* A rear reference Z must fail; a front reference Z must pass. */
 GX_SetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);GX_SetTevColor(GX_TEVREG0,(GXColor){0,0,255,255});draw(.5f);
 GXColor rear;GX_PeekARGB(256,100,&rear);if(rear.r!=255||rear.b!=0)failures++;
 GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);draw(.001f);
 GXColor front;GX_PeekARGB(256,100,&front);if(front.b!=255||front.r!=0)failures++;
 /* Independent non-textured normalized-depth controls verify peek units. */
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);draw(.25f);
 unsigned control25,control50;GX_PeekZ(256,100,&control25);
 draw(.5f);GX_PeekZ(256,100,&control50);
 if(control25!=0x4000||control50!=0x8000)failures++;
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII WDEPTH %s\n",failures?"FAIL":"PASS");
 for(unsigned i=0;i<3;i++)printf("%u,%u Z%06x expected16=%04x\n",xy[i][0],xy[i][1],actual[i],expected[i]);
 printf("alpha hole Z%06x RGB%u,%u,%u rear%u,%u,%u front%u,%u,%u\n",hole,holecolor.r,holecolor.g,holecolor.b,rear.r,rear.g,rear.b,front.r,front.g,front.b);
 printf("normalized controls .25=%06x .5=%06x expected004000,008000\n",control25,control50);
 printf("VIPER WII WDEPTH END failures=%u\n",failures);
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
