/* GX filtered-RGB black rejection with independent iterated alpha. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64] ATTRIBUTE_ALIGN(32);
static void progress(const char *stage,unsigned value){
 FILE *log=fopen("sd:/viper/boot.log","a");
 if(log){fprintf(log,"CHROMA PROGRESS %s %u\n",stage,value);fclose(log);}
}
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
 GX_SetTevAlphaIn(GX_TEVSTAGE0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_RASA);
 GX_SetTevAlphaOp(GX_TEVSTAGE0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 if(on){
 GX_SetTevOrder(GX_TEVSTAGE1,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 GX_SetTevColorIn(GX_TEVSTAGE1,GX_CC_TEXC,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
 GX_SetTevColorOp(GX_TEVSTAGE1,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(GX_TEVSTAGE1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV,GX_CA_ZERO);
 if(on==2){GX_SetTevKColor(GX_KCOLOR0,(GXColor){0,0,0,255});GX_SetTevKAlphaSel(GX_TEVSTAGE1,GX_TEV_KASEL_K0_A);GX_SetTevAlphaIn(GX_TEVSTAGE1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_KONST,GX_CA_ZERO);}
 GX_SetTevAlphaOp(GX_TEVSTAGE1,GX_TEV_COMP_BGR24_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 }
}
int main(void){
 int sd=fatInitDefault();
 if(sd){FILE *log=fopen("sd:/viper/boot.log","w");if(log){fputs("CHROMA START\n",log);fclose(log);}}
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
 /* Texture alpha deliberately ZERO everywhere; visible alpha must come from vertex. */
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){unsigned o=(y*4+x)*2;image[o]=0;image[o+1]=image[o+32]=image[o+33]=x<2?0:255;}
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj tex;
 GX_InitTexObj(&tex,image,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);GX_InitTexObjLOD(&tex,GX_LINEAR,GX_LINEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
 unsigned fail=0;GXColor before[3],after[3],nonwhite,edge_modulated,edge_control;u32 z[3];const unsigned xs[3]={96,192,288};
 chroma(1);GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
 quad(.2f,(GXColor){255,255,255,128});
 for(unsigned i=0;i<3;i++){GX_PeekARGB(xs[i],160,&before[i]);GX_PeekZ(xs[i],160,&z[i]);}
 if(before[0].r||before[2].r<127||before[2].r>129||before[1].r<60||before[1].r>70||z[0]!=0xffffff||z[1]>=z[0]||z[2]!=z[1])fail++;
 /* Later rear blue appears only where exact black was rejected. */
 GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);quad(.5f,(GXColor){0,0,255,255});
 for(unsigned i=0;i<3;i++)GX_PeekARGB(xs[i],160,&after[i]);
 if(after[0].b!=255||after[0].r||after[1].r!=before[1].r||after[2].r!=before[2].r)fail++;
 chroma(1);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_FALSE);quad(.1f,(GXColor){64,128,192,255});GX_PeekARGB(288,240,&nonwhite);
 if(nonwhite.r!=64||nonwhite.g!=128||nonwhite.b!=192)fail++;
 GX_PeekARGB(192,240,&edge_modulated);
 chroma(0);quad(.1f,(GXColor){64,128,192,255});GX_PeekARGB(192,240,&edge_control);
 if(edge_modulated.r!=edge_control.r||edge_modulated.g!=edge_control.g||edge_modulated.b!=edge_control.b)fail++;
 /* Opaque ALWAYS-alpha chroma: nonblack vertexalpha0 must still write RGB/Z. */
 GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);quad(.8f,(GXColor){0,0,255,255});
 chroma(2);GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);quad(.1f,(GXColor){255,255,255,0});
 GXColor opaque[3];u32 opaque_z[3];
 for(unsigned i=0;i<3;i++){GX_PeekARGB(xs[i],160,&opaque[i]);GX_PeekZ(xs[i],160,&opaque_z[i]);}
 if(opaque[0].r||opaque[0].b!=255||opaque[2].r!=255||opaque_z[0]<=opaque_z[2]||opaque_z[1]!=opaque_z[2])fail++;
 /* DST_COLOR/ONE ignores source alpha. Coloured fog must not resurrect
  * filtered black key texels, and the overlay must leave depth untouched. */
 GXColor additive[3],additive_control[3];u32 additive_z[3],background_z[3];
 GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);quad(.8f,(GXColor){32,64,96,255});
 for(unsigned i=0;i<3;i++)GX_PeekZ(xs[i],160,&background_z[i]);
 chroma(2);GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_FALSE);
 GX_SetBlendMode(GX_BM_BLEND,GX_BL_DSTCLR,GX_BL_ONE,GX_LO_COPY);
 GX_SetFog(GX_FOG_ORTHO_LIN,0,.05f,0,1,(GXColor){128,32,64,255});
 quad(.1f,(GXColor){255,255,255,0});
 for(unsigned i=0;i<3;i++){GX_PeekARGB(xs[i],160,&additive[i]);GX_PeekZ(xs[i],160,&additive_z[i]);}
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
 GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);quad(.8f,(GXColor){32,64,96,255});
 chroma(0);GX_SetBlendMode(GX_BM_BLEND,GX_BL_DSTCLR,GX_BL_ONE,GX_LO_COPY);
 GX_SetFog(GX_FOG_ORTHO_LIN,0,.05f,0,1,(GXColor){128,32,64,255});quad(.1f,(GXColor){255,255,255,0});
 for(unsigned i=0;i<3;i++)GX_PeekARGB(xs[i],160,&additive_control[i]);
 if(additive[0].r!=32||additive[0].g!=64||additive[0].b!=96||additive_control[0].r<=32)fail++;
 for(unsigned i=0;i<3;i++)if(additive_z[i]!=background_z[i])fail++;
 for(unsigned i=1;i<3;i++)if(additive[i].r!=additive_control[i].r||additive[i].g!=additive_control[i].g||additive[i].b!=additive_control[i].b)fail++;
 /* Double-colour blend: exercise EQUAL at matching depth, then LESS with
  * a nearer draw so rejected key depth can be distinguished from survivors. */
 GXColor doubled[3];u32 doubled_z[3];
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
 GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);quad(.8f,(GXColor){32,64,96,255});
 chroma(2);GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_BLEND,GX_BL_DSTCLR,GX_BL_SRCCLR,GX_LO_COPY);
 GX_SetZMode(GX_TRUE,GX_EQUAL,GX_TRUE);quad(.8f,(GXColor){255,255,255,0});
 for(unsigned i=0;i<3;i++)GX_PeekARGB(xs[i],160,&doubled[i]);
 if(doubled[0].r!=32||doubled[0].g!=64||doubled[0].b!=96||doubled[2].r!=64||doubled[2].g!=128||doubled[2].b!=192)fail++;
 GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);quad(.1f,(GXColor){255,255,255,0});
 for(unsigned i=0;i<3;i++)GX_PeekZ(xs[i],160,&doubled_z[i]);
 if(doubled_z[0]!=background_z[0]||doubled_z[1]>=doubled_z[0]||doubled_z[2]!=doubled_z[1])fail++;
 /* EQUAL + source-alpha: compare gated output with ordinary blending for
  * every alpha byte, at both passing and failing incoming depths. */
 unsigned equal_fail=0;
 if(sd)progress("equal-start",0);
 for(unsigned destination=0;destination<2;destination++)
 for(unsigned a=0;a<256;a++)for(unsigned mismatch=0;mismatch<2;mismatch++){
  if(sd&&!mismatch&&!(a&31))progress("equal-alpha",a);
  GXColor reference[3],actual[3];u32 initial[3],actual_z[3];
  for(unsigned pass=0;pass<2;pass++){
   GX_SetNumTevStages(1);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
   GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
   GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
   GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);quad(.8f,(GXColor){32,64,96,255});
   for(unsigned i=0;i<3;i++)GX_PeekZ(xs[i],160,&initial[i]);
   chroma(pass?1:0);GX_SetAlphaCompare(pass?GX_GREATER:GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
   GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,destination?GX_BL_ONE:GX_BL_INVSRCALPHA,GX_LO_COPY);
   GX_SetZMode(GX_TRUE,GX_EQUAL,GX_TRUE);quad(mismatch?.1f:.8f,(GXColor){255,255,255,a});
   for(unsigned i=0;i<3;i++){
    if(pass){GX_PeekARGB(xs[i],160,&actual[i]);GX_PeekZ(xs[i],160,&actual_z[i]);}
    else GX_PeekARGB(xs[i],160,&reference[i]);
   }
  }
  if(actual[0].r!=32||actual[0].g!=64||actual[0].b!=96)equal_fail++;
  for(unsigned i=0;i<3;i++)if(actual_z[i]!=initial[i])equal_fail++;
  for(unsigned i=1;i<3;i++)if(actual[i].r!=reference[i].r||actual[i].g!=reference[i].g||actual[i].b!=reference[i].b)equal_fail++;
 }
 fail+=equal_fail;
 if(sd){FILE *log=fopen("sd:/viper/boot.log","a");if(log){fprintf(log,"CHROMA END result=%s failures=%u equal_cases=1024 equal_failures=%u\n",fail?"FAIL":"PASS",fail,equal_fail);
  /* Drive the host SD image's buffered writes out before the final wait. */
  static const char padding[65536]={0};fwrite(padding,1,sizeof padding,log);fclose(log);}}
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII CHROMA %s\n",fail?"FAIL":"PASS");
 for(unsigned i=0;i<3;i++)printf("x%u RGB%u,%u,%u Z%06x rear%u,%u,%u\n",xs[i],before[i].r,before[i].g,before[i].b,z[i],after[i].r,after[i].g,after[i].b);
 printf("nonwhite fulltexel RGB%u,%u,%u expected64,128,192\n",nonwhite.r,nonwhite.g,nonwhite.b);
 printf("nonwhite edge RGB%u,%u,%u plainTEV%u,%u,%u\n",edge_modulated.r,edge_modulated.g,edge_modulated.b,edge_control.r,edge_control.g,edge_control.b);
 for(unsigned i=0;i<3;i++)printf("opaque alpha0 x%u RGB%u,%u,%u Z%06x\n",xs[i],opaque[i].r,opaque[i].g,opaque[i].b,opaque_z[i]);
 for(unsigned i=0;i<3;i++)printf("DST fog alpha0 x%u RGB%u,%u,%u control%u,%u,%u Z%06x/%06x\n",xs[i],additive[i].r,additive[i].g,additive[i].b,additive_control[i].r,additive_control[i].g,additive_control[i].b,additive_z[i],background_z[i]);
 for(unsigned i=0;i<3;i++)printf("DOUBLE alpha0 x%u RGB%u,%u,%u keyZ%06x\n",xs[i],doubled[i].r,doubled[i].g,doubled[i].b,doubled_z[i]);
 printf("EQUAL source-alpha 1024 cases failures=%u\n",equal_fail);
 printf("VIPER WII CHROMA END failures=%u\n",fail);fflush(stdout);VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
