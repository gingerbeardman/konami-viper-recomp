/* Standalone TMU compiler probe. CPU-oracle error is reported, not exact GX equivalence. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_tmu_equation.h"
#include "gx_color_equation.h"
#include "gx_rejection.h"
#include "gx_fog_stage.h"
#include "voodoo_tmu_eval.h"
#include "voodoo_color_eval.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64], image1[64] ATTRIBUTE_ALIGN(32), fogimage[64] ATTRIBUTE_ALIGN(32);
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

static WiiVoodooTMUPlan tp[2];static WiiVoodooColorPlan fp;
static WiiVoodooRGBA constants[2];static unsigned detailfactor[2],fractionfactor[2];
static unsigned emit(int predicates,int alpha_view){
 GX_SetTevKColor(GX_KCOLOR2,(GXColor){detailfactor[1],detailfactor[1],detailfactor[1],fractionfactor[1]});
 GX_SetTevKColor(GX_KCOLOR3,(GXColor){detailfactor[0],detailfactor[0],detailfactor[0],fractionfactor[0]});
 unsigned end=wii_gx_tmu_equation(tp[1],0,GX_TEXCOORD0,GX_TEXMAP3,GX_CC_ZERO,GX_CA_ZERO,GX_CC_ZERO,GX_TEVREG0,
  GX_TEV_KCSEL_K2,GX_TEV_KASEL_K2_R,GX_TEV_KCSEL_K2_A,GX_TEV_KASEL_K2_A);
 end=wii_gx_tmu_equation(tp[0],end,GX_TEXCOORD0,GX_TEXMAP0,GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEVREG0,
  GX_TEV_KCSEL_K3,GX_TEV_KASEL_K3_R,GX_TEV_KCSEL_K3_A,GX_TEV_KASEL_K3_A);
 if(predicates<0){
  GX_SetTevOrder(end,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,alpha_view?GX_CC_A0:GX_CC_C0);
  GX_SetTevAlphaIn(end,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A0);
  GX_SetTevColorOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  GX_SetTevAlphaOp(end++,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  GX_SetNumTevStages(end);return end;
 }
 end=wii_gx_color_equation_at(fp,gx(constants[0]),gx(constants[1]),end,GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEXCOORD0,GX_TEXMAP0);
 // Preserve color1 components unless that component's OTHER source is TMU.
 GX_SetTevOrder(end,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
 GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,fp.other_rgb==1?GX_CC_C0:GX_CC_C2);
 GX_SetTevAlphaIn(end,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,fp.other_alpha==1?GX_CA_A0:GX_CA_A2);
 GX_SetTevColorOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG2);
 GX_SetTevAlphaOp(end++,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVREG2);
 if(predicates){const u8 rs[]={GX_CC_RASC,GX_CC_C2,GX_CC_C2,GX_CC_ZERO};const u8 as[]={GX_CA_RASA,GX_CA_A2,GX_CA_A2,GX_CA_ZERO};
  end=wii_gx_rejection(end,rs[fp.other_rgb],as[fp.other_alpha],1,predicates&1,0,predicates&2);
 }
 wii_gx_fog_stage(end++,(GXColor){96,224,16,0},1);
 if(alpha_view){
  GX_SetTevOrder(end,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn(end,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_APREV);
  GX_SetTevAlphaIn(end,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
  GX_SetTevColorOp(end,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  GX_SetTevAlphaOp(end++,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 }
 GX_SetNumTevStages(end);return end;
}
static void upload(unsigned char *data,WiiVoodooRGBA c,unsigned map){
 for(unsigned i=0;i<16;i++){unsigned o=i*2;data[o]=c.a;data[o+1]=c.r;data[o+32]=c.g;data[o+33]=c.b;}
 DCFlushRange(data,64);GX_InvalidateTexAll();GXTexObj tex;GX_InitTexObj(&tex,data,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,map);
}

int main(void){
 int sd=fatInitDefault();
 if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("TMU PIPELINE START\n",f);fclose(f);}}
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


 GX_SetNumTexGens(2);GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
 upload(fogimage,(WiiVoodooRGBA){0,0,0,96},GX_TEXMAP2);
 unsigned failures=0,max_error=0,skipped=0,first_bad=~0u;
 for(unsigned n=0;n<256;){
  unsigned modes[2]={next(),next()},cp=next();cp&=~((1u<<7)|(3u<<5));cp|=(next()&1)<<5;
  fp=wii_voodoo_color_plan(cp);tp[0]=wii_voodoo_tmu_plan(modes[0]);tp[1]=wii_voodoo_tmu_plan(modes[1]);
  unsigned cost=0;for(unsigned i=0;i<2;i++)cost+=(tp[i].rgb_sub||tp[i].alpha_sub?3:1)+!!(tp[i].rgb_invert||tp[i].alpha_invert);
  cost+=(fp.rgb_sub||fp.alpha_sub?3:1)+!!(fp.rgb_invert||fp.alpha_invert);
  unsigned predicates=n%4;cost+=1+(predicates?1+((predicates&2)?5:0):0)+1+1;
  if(cost>16){skipped++;continue;} // Whole graph admission, never truncation.
  WiiVoodooRGBA local[2]={rgba(),rgba()},iter=rgba();constants[0]=rgba();constants[1]=rgba();
  if(n<16){local[0]=(WiiVoodooRGBA){0,1,254,255};local[1]=(WiiVoodooRGBA){255,128,0,1};}
  unsigned detail[2]={next(),next()};int lod[2]={(int)(next()%4097)-2048,(int)(next()%4097)-2048};
  for(unsigned i=0;i<2;i++){detailfactor[i]=wii_voodoo_tmu_detail(detail[i],lod[i]);fractionfactor[i]=(unsigned)lod[i]&255;}
  WiiVoodooRGBA t1=wii_voodoo_tmu_eval(tp[1],local[1],(WiiVoodooRGBA){0,0,0,0},lod[1],detail[1]);
  WiiVoodooRGBA tex=wii_voodoo_tmu_eval(tp[0],local[0],t1,lod[0],detail[0]);
  WiiVoodooRGBA expected=wii_voodoo_color_eval(fp,(WiiVoodooColorInputs){iter,tex,constants[0],constants[1],0,0});
  upload(image,local[0],GX_TEXMAP0);upload(image1,local[1],GX_TEXMAP3);
  GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
  emit(-1,0);quad(gx(iter));GXColor actualtex;GX_PeekARGB(160,160,&actualtex);
  emit(-1,1);quad(gx(iter));GXColor actualtexalpha;GX_PeekARGB(160,160,&actualtexalpha);actualtex.a=actualtexalpha.r;
  emit(0,0);quad(gx(iter));GXColor control;GX_PeekARGB(160,160,&control);
  emit(0,1);quad(gx(iter));GXColor ac;GX_PeekARGB(160,160,&ac);
  unsigned actual[4]={control.r,control.g,control.b,ac.r};
  unsigned want[4]={((unsigned)expected.r*159+96*96+127)/255,((unsigned)expected.g*159+224*96+127)/255,((unsigned)expected.b*159+16*96+127)/255,expected.a};
  unsigned before=failures;
  for(unsigned c=0;c<4;c++){unsigned error=actual[c]>want[c]?actual[c]-want[c]:want[c]-actual[c];if(error>max_error)max_error=error;if(error>9)failures++;}
  // Predicate result compared to the same GX equation (separates quantization).
  GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
  GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);draw_z=-.8f;quad((GXColor){32,64,96,255});u32 initial,z;GX_PeekZ(160,160,&initial);
  emit(predicates,0);GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);draw_z=-.5f;quad(gx(iter));GXColor result;GX_PeekARGB(160,160,&result);GX_PeekZ(160,160,&z);
  // Use separately measured GX TMU bytes for predicates: arithmetic drift
  // is checked above, while this independently verifies source lifetime.
  {
   GXColor rgb=fp.other_rgb==0?gx(iter):fp.other_rgb==1?actualtex:fp.other_rgb==2?gx(constants[1]):(GXColor){0,0,0,0};
   unsigned a=fp.other_alpha==0?iter.a:fp.other_alpha==1?actualtex.a:fp.other_alpha==2?constants[1].a:0;
   int pass=ac.r&&(!(predicates&1)||(rgb.r||rgb.g||rgb.b))&&(!(predicates&2)||(a&1));
   GXColor wantcolor=pass?control:(GXColor){32,64,96,255};
   if(result.r!=wantcolor.r||result.g!=wantcolor.g||result.b!=wantcolor.b||((z!=initial)!=pass))failures++;
  }
  if(failures!=before&&first_bad==~0u){first_bad=n;if(sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"TMU PIPE FIRST BAD n=%u modes=%08x,%08x cp=%08x pred=%u cost=%u actual=%u,%u,%u,%u expected=%u,%u,%u,%u lod=%d,%d detail=%08x,%08x\n",n,modes[0],modes[1],cp,predicates,cost,actual[0],actual[1],actual[2],actual[3],want[0],want[1],want[2],want[3],lod[0],lod[1],detail[0],detail[1]);
    fprintf(f,"INPUT local0=%u,%u,%u,%u local1=%u,%u,%u,%u iter=%u,%u,%u,%u c0=%u,%u,%u,%u c1=%u,%u,%u,%u\n",
     local[0].r,local[0].g,local[0].b,local[0].a,local[1].r,local[1].g,local[1].b,local[1].a,
     iter.r,iter.g,iter.b,iter.a,constants[0].r,constants[0].g,constants[0].b,constants[0].a,
     constants[1].r,constants[1].g,constants[1].b,constants[1].a);fclose(f);}}}
  n++;GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
  if(sd&&n%32==0){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"TMU PIPE PROGRESS cases=%u failures=%u max_error=%u overbudget=%u\n",n,failures,max_error,skipped);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 }
 if(sd){FILE*f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"TMU PIPE END result=%s cases=256 failures=%u max_error=%u tolerance=9 overbudget=%u first_bad=%u\n",failures?"FAIL":"PASS",failures,max_error,skipped,first_bad);char pad[512];for(unsigned i=0;i<512;i++)pad[i]='\n';for(unsigned i=0;i<128;i++)if(fwrite(pad,1,512,f)!=512)break;fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("TMU PIPE END %s cases256 failures%u max_error%u\n",failures?"FAIL":"PASS",failures,max_error);fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
