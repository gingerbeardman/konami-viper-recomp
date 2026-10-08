/* Native colour-first/depth-second rejection policy probe.
 * Uniform sources, constant fog, planar depth; exact comparison to GX control. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_rejection.h"
#include "gx_fog_stage.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);

static float draw_z=-.5f;

static void quad(GXColor c){
 const float xy[6][2]={{64,64},{320,64},{320,320},{64,64},{320,320},{64,320}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);for(unsigned i=0;i<6;i++){
 GX_Position3f32(xy[i][0],xy[i][1],draw_z);GX_Color4u8(c.r,c.g,c.b,c.a);GX_TexCoord2f32(.5f,.5f);}GX_End();GX_DrawDone();
}

static void background(void){
 GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
 GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);draw_z=-.5f;quad((GXColor){32,64,96,255});
}
static void pipeline(unsigned alpha,int predicates,int binary,int fog){
 GX_SetTevColor(GX_TEVREG1,(GXColor){192,128,64,alpha});
 GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
 GX_SetTevColorIn(0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C1);
 GX_SetTevAlphaIn(0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A1);
 GX_SetTevColorOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 unsigned end=1;
 if(predicates)end=wii_gx_rejection(end,GX_CC_RASC,GX_CA_RASA,0,1,binary,1);
 if(fog)wii_gx_fog_stage(end++,(GXColor){96,224,16,96},0);
 // Largest colour graph is 1 + 6 predicate + 1 fog = 8; depth graph 7.
 GX_SetNumTevStages(end);
}
typedef struct{GXColor color,rear;u32 depth,rear_depth;} Sample;
static Sample render(unsigned alpha,unsigned condition,unsigned destination,unsigned compare,unsigned relation,unsigned masks,int split){
 background();
 GXColor original={(condition&1)?0:128,0,0,(condition&2)?2:1};
 int survives=!(condition&3);
 float z=relation==0?.25f:relation==1?.5f:.75f;
 GX_SetColorUpdate(!!(masks&1));GX_SetZMode(GX_TRUE,compare,split?GX_FALSE:!!(masks&2));
 GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,destination?GX_BL_INVSRCALPHA:GX_BL_ONE,GX_LO_COPY);
 pipeline(alpha,split,0,1);
 GX_SetAlphaCompare(split?GX_GREATER:GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 draw_z=-z;if(split||survives)quad(original);
 if(split&&(masks&2)){
  pipeline(alpha,1,1,0);GX_SetColorUpdate(GX_FALSE);GX_SetZMode(GX_TRUE,compare,GX_TRUE);
  GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);quad(original);
 }
 Sample result;GX_PeekARGB(160,160,&result.color);GX_PeekZ(160,160,&result.depth);
 GX_SetColorUpdate(GX_TRUE);GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);draw_z=-.625f;quad((GXColor){8,240,16,255});
 GX_PeekARGB(160,160,&result.rear);GX_PeekZ(160,160,&result.rear_depth);return result;
}
static int same(GXColor a,GXColor b){return a.r==b.r&&a.g==b.g&&a.b==b.b;}

int main(void){
 int sd=fatInitDefault();
 if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("SPLIT DEPTH START\n",f);fclose(f);}}
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

 const unsigned alphas[]={0,1,255};unsigned cases=0,failures=0;
 for(unsigned a=0;a<3;a++)for(unsigned predicate=0;predicate<4;predicate++)
 for(unsigned dst=0;dst<2;dst++)for(unsigned cmp=0;cmp<8;cmp++)
 for(unsigned relation=0;relation<3;relation++)for(unsigned masks=0;masks<4;masks++){
  Sample expected=render(alphas[a],predicate,dst,cmp,relation,masks,0);
  Sample actual=render(alphas[a],predicate,dst,cmp,relation,masks,1);
  int bad=!same(expected.color,actual.color)||expected.depth!=actual.depth||
   !same(expected.rear,actual.rear)||expected.rear_depth!=actual.rear_depth;
  if(bad&&failures++==0&&sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){
   fprintf(f,"SPLIT FIRST BAD case=%u alpha=%u predicate=%u dst=%u cmp=%u relation=%u masks=%u expected=%u,%u,%u/%06x actual=%u,%u,%u/%06x rear_expected=%u,%u,%u/%06x rear_actual=%u,%u,%u/%06x\n",cases,alphas[a],predicate,dst,cmp,relation,masks,expected.color.r,expected.color.g,expected.color.b,expected.depth,actual.color.r,actual.color.g,actual.color.b,actual.depth,expected.rear.r,expected.rear.g,expected.rear.b,expected.rear_depth,actual.rear.r,actual.rear.g,actual.rear.b,actual.rear_depth);fclose(f);}}
  cases++;
  if(sd&&cases%64==0){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"SPLIT PROGRESS cases=%u failures=%u\n",cases,failures);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 }
 if(sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"SPLIT DEPTH END result=%s cases=%u failures=%u\n",failures?"FAIL":"PASS",cases,failures);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("SPLIT DEPTH END %s cases=%u failures=%u\n",failures?"FAIL":"PASS",cases,failures);fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
