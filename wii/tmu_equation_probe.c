/* Standalone TMU compiler probe. CPU-oracle error is reported, not exact GX equivalence. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_tmu_equation.h"
#include "voodoo_tmu_eval.h"
#include "voodoo_color_eval.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64] ATTRIBUTE_ALIGN(32);
static unsigned rng=1;
static float draw_z=-.5f;
static unsigned next(void){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static WiiVoodooRGBA rgba(void){unsigned v=next();return (WiiVoodooRGBA){v,v>>8,v>>16,v>>24};}
static GXColor gx(WiiVoodooRGBA c){return (GXColor){c.r,c.g,c.b,c.a};}
static void quad(GXColor c){
 const float xy[6][2]={{64,64},{320,64},{320,320},{64,64},{320,320},{64,320}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);for(unsigned i=0;i<6;i++){
 GX_Position3f32(xy[i][0],xy[i][1],draw_z);GX_Color4u8(c.r,c.g,c.b,c.a);GX_TexCoord2f32(.5f,.5f);}GX_End();GX_DrawDone();
}
int main(void){
 int sd=fatInitDefault();
 if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("TMU EQUATION START\n",f);fclose(f);}}
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

 unsigned failures=0,max_error=0,first_bad=~0u;
 for(unsigned n=0;n<1024;n++){
  unsigned mode_bits=next();
  // All 64 RGB/alpha factor pairs, including both reserved selectors.
  mode_bits=(mode_bits&~((7u<<14)|(7u<<23)))|((n&7)<<14)|(((n>>3)&7)<<23);
  WiiVoodooTMUPlan plan=wii_voodoo_tmu_plan(mode_bits);
  WiiVoodooRGBA local=rgba(),other=rgba();
  if(n<64){local=(WiiVoodooRGBA){0,1,254,255};other=(WiiVoodooRGBA){255,128,1,0};}
  int lod=(int)(next()%4097)-2048;unsigned detail=next();
  unsigned df=wii_voodoo_tmu_detail(detail,lod),fraction=(unsigned)lod&255;
  WiiVoodooRGBA expected=wii_voodoo_tmu_eval(plan,local,other,lod,detail);
  for(unsigned i=0;i<16;i++){unsigned o=i*2;image[o]=local.a;image[o+1]=local.r;image[o+32]=local.g;image[o+33]=local.b;}
  DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj tex;
  GX_InitTexObj(&tex,image,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  GX_SetTevColor(GX_TEVREG2,gx(other));
  GX_SetTevKColor(GX_KCOLOR0,(GXColor){df,df,df,df});
  GX_SetTevKColor(GX_KCOLOR1,(GXColor){fraction,fraction,fraction,fraction});
  unsigned first=n%3;
  unsigned output=(n&1)?GX_TEVPREV:GX_TEVREG0;
  for(unsigned s=0;s<first;s++){
   GX_SetTevOrder(s,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
   GX_SetTevColorIn(s,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C2);
   GX_SetTevAlphaIn(s,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A2);
   GX_SetTevColorOp(s,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
   GX_SetTevAlphaOp(s,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  }
  // Exercise upstream PREV and explicit non-PREV output lifetimes.
  unsigned end=wii_gx_tmu_equation(plan,first,GX_TEXCOORD0,GX_TEXMAP0,
   first?GX_CC_CPREV:GX_CC_C2,first?GX_CA_APREV:GX_CA_A2,
   first?GX_CC_APREV:GX_CC_A2,output,
   GX_TEV_KCSEL_K0,GX_TEV_KASEL_K0_A,GX_TEV_KCSEL_K1,GX_TEV_KASEL_K1_A);
  if(!end){failures++;continue;}
  GX_SetNumTevStages(end+1);GX_SetTevOrder(end,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,output==GX_TEVPREV?GX_CC_CPREV:GX_CC_C0);
  GX_SetTevAlphaIn(end,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,output==GX_TEVPREV?GX_CA_APREV:GX_CA_A0);
  GX_SetTevColorOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  GX_SetTevAlphaOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  quad((GXColor){255,255,255,255});GXColor result;GX_PeekARGB(160,160,&result);
  unsigned actual[4]={result.r,result.g,result.b,0},want[4]={expected.r,expected.g,expected.b,expected.a};
  GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,output==GX_TEVPREV?GX_CC_APREV:GX_CC_A0);
  quad((GXColor){255,255,255,255});GX_PeekARGB(160,160,&result);actual[3]=result.r;
  for(unsigned c=0;c<4;c++){
   unsigned error=actual[c]>want[c]?actual[c]-want[c]:want[c]-actual[c];
   if(error>max_error)max_error=error;
   if(error>3){failures++;if(first_bad==~0u){
    first_bad=n;
    if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){
     fprintf(f,"TMU FIRST BAD n=%u mode=%08x local=%u,%u,%u,%u other=%u,%u,%u,%u lod=%d detail=%08x actual=%u,%u,%u,%u expected=%u,%u,%u,%u first=%u output=%u\n",
      n,mode_bits,local.r,local.g,local.b,local.a,other.r,other.g,other.b,other.a,lod,detail,
      actual[0],actual[1],actual[2],actual[3],want[0],want[1],want[2],want[3],first,output);fclose(f);
    }}
   }}
  }
  if(sd&&(n%64)==63){FILE *f=fopen("sd:/viper/boot.log","a");if(f){
   fprintf(f,"TMU PROGRESS cases=%u failures=%u max_error=%u\n",n+1,failures,max_error);
   char padding[512];for(unsigned i=0;i<sizeof padding;i++)padding[i]='\n';
   for(unsigned i=0;i<128;i++)if(fwrite(padding,1,sizeof padding,f)!=sizeof padding)break;
   fclose(f);
  }}
 }
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"TMU EQUATION END result=%s cases=1024 failures=%u max_error=%u tolerance=3 first_bad=%u\n",failures?"FAIL":"PASS",failures,max_error,first_bad);char padding[512];for(unsigned i=0;i<sizeof padding;i++)padding[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(padding,1,sizeof padding,f)!=sizeof padding)break;fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("TMU EQUATION END result=%s cases=1024\nfailures=%u max_error=%u tolerance=3 first_bad=%u\n",failures?"FAIL":"PASS",failures,max_error,first_bad);
 fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();
 for(;;)VIDEO_WaitVSync();
}
