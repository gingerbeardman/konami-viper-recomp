/* Uniform vertex-alpha threshold proof against actual GX color/depth. */
#include <gccore.h>
#include <stdio.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64] ATTRIBUTE_ALIGN(32);
static void quad(float z,GXColor c){
 const float xy[6][4]={{64,64,0,0},{320,64,1,0},{320,320,1,1},{64,64,0,0},{320,320,1,1},{64,320,0,1}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
 for(unsigned i=0;i<6;i++){GX_Position3f32(xy[i][0],xy[i][1],-z);GX_Color4u8(c.r,c.g,c.b,c.a);GX_TexCoord2f32(xy[i][2],xy[i][3]);}GX_End();GX_DrawDone();
}
static void chroma(int on){
 GX_SetNumTevStages(on?2:1);
 GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
 GX_SetTevColorIn(GX_TEVSTAGE0,GX_CC_ZERO,GX_CC_TEXC,GX_CC_RASC,GX_CC_ZERO);
 GX_SetTevColorOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_TEXA,GX_CA_RASA,GX_CA_ZERO);
 GX_SetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 if(on){
 GX_SetTevOrder(GX_TEVSTAGE1,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 GX_SetTevColorIn(GX_TEVSTAGE1,GX_CC_TEXC,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
 GX_SetTevColorOp(GX_TEVSTAGE1,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(GX_TEVSTAGE1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_CA_ZERO);
 GX_SetTevAlphaOp(GX_TEVSTAGE1,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 }
}
static void threshold(unsigned a){
 unsigned threshold=(3328+a)/(a+1); /* ceil(13*256/(a+1)) */
 GX_SetNumTevStages(3);GX_SetTevOrder(GX_TEVSTAGE2,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 GX_SetTevColorIn(GX_TEVSTAGE2,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
 GX_SetTevColorOp(GX_TEVSTAGE2,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevKColor(GX_KCOLOR0,(GXColor){0,0,0,(u8)(threshold>255?255:threshold-1)});
 GX_SetTevKAlphaSel(GX_TEVSTAGE2,GX_TEV_KASEL_K0_A);
 GX_SetTevAlphaIn(GX_TEVSTAGE2,GX_CA_TEXA,GX_CA_KONST,GX_CA_APREV,GX_CA_ZERO);
 GX_SetTevAlphaOp(GX_TEVSTAGE2,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
}
int main(void){
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
 const unsigned ta[4]={25,238,13,255},va[4]={128,13,255,255};
 unsigned fail=0;u32 depths[4][2];GXColor colors[4][2];
 GXTexObj tex;GX_InitTexObj(&tex,image,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&tex,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
 for(unsigned test=0;test<4;test++)for(unsigned fixed=0;fixed<2;fixed++){
  GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
  for(unsigned i=0;i<16;i++){image[i*2]=ta[test];image[i*2+1]=image[i*2+32]=image[i*2+33]=255;}
  DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GX_LoadTexObj(&tex,GX_TEXMAP0);
  chroma(1);GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);
  if(fixed)threshold(va[test]);
  GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
  quad(.2f,(GXColor){255,255,255,(u8)va[test]});
  GX_PeekARGB(192,160,&colors[test][fixed]);GX_PeekZ(192,160,&depths[test][fixed]);
  unsigned reference=ta[test]*(va[test]+1)/256;
  unsigned regular=(ta[test]*(va[test]+(va[test]>>7))+128)/256;
  int pass=(fixed?reference:regular)>12;
  if(pass?(colors[test][fixed].r!=255||depths[test][fixed]>=0xffffff):(colors[test][fixed].r!=0||depths[test][fixed]!=0xffffff))fail++;
 }
 if(depths[0][0]==depths[0][1]||depths[1][0]==depths[1][1])fail++;
 /* Chroma gate survives threshold stage: black with high alpha still rejects. */
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 for(unsigned i=0;i<16;i++){image[i*2]=255;image[i*2+1]=image[i*2+32]=image[i*2+33]=0;}
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GX_LoadTexObj(&tex,GX_TEXMAP0);
 chroma(1);threshold(255);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);quad(.2f,(GXColor){255,255,255,255});
 u32 blackz;GX_PeekZ(192,160,&blackz);if(blackz!=0xffffff)fail++;
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII ALPHA %s\n",fail?"FAIL":"PASS");
 for(unsigned i=0;i<4;i++)printf("t%u a%u ref%u ordinaryZ%06x fixedZ%06x RGB%u/%u\n",ta[i],va[i],ta[i]*(va[i]+1)/256,depths[i][0],depths[i][1],colors[i][0].r,colors[i][1].r);
 printf("black gatedZ%06x\nVIPER WII ALPHA END failures=%u\n",blackz,fail);
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
