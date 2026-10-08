/* Native probe of the general FBI TEV compiler. Quantization is approximate. */
#include <gccore.h>
#include <fat.h>
#include <stdio.h>
#include "gx_color_equation.h"
#include "voodoo_color_eval.h"
#include "gx_rejection.h"
#include "gx_fog_stage.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[64] ATTRIBUTE_ALIGN(32);
static unsigned rng=1;
static int probe_untextured;
static unsigned probe_equation(WiiVoodooColorPlan p,GXColor c0,GXColor c1){
 if(probe_untextured)return wii_gx_color_equation_at(p,c0,c1,0,GX_CC_ZERO,GX_CA_ZERO,GX_CC_ZERO,GX_TEXCOORDNULL,GX_TEXMAP_NULL);
 return wii_gx_color_equation(p,c0,c1);
}
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
 if(sd){FILE *f=fopen("sd:/viper/boot.log","w");if(f){fputs("EQUATION START\n",f);fclose(f);}}
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
 unsigned failures=0,max_error=0,pipeline_failures=0;
 for(unsigned n=0;n<512;n++){
  unsigned cp=next()&~((1u<<7)|(3u<<5));cp|=(next()&1)<<5;
  WiiVoodooColorInputs in={rgba(),rgba(),rgba(),rgba(),0,0};
  /* Cover all independent original RGB/alpha sources, including reserved
   * zero, and both black/nonblack and odd/even predicate inputs. */
  cp=(cp&~15u)|(n&15u);
  probe_untextured=!!(n&128);
  if(probe_untextured){
   if((cp&3)==1)cp=(cp&~3u)|3;
   if(((cp>>2)&3)==1)cp=(cp&~12u)|12;
   if(((cp>>10)&7)==4||((cp>>10)&7)==5)cp=(cp&~(7u<<10))|(6u<<10);
   if(((cp>>19)&7)==4)cp=(cp&~(7u<<19))|(5u<<19);
   in.texture=(WiiVoodooRGBA){0,0,0,0};
  }
  WiiVoodooColorPlan p=wii_voodoo_color_plan(cp);
  WiiVoodooRGBA *rgb=p.other_rgb==0?&in.iterated:p.other_rgb==1?&in.texture:&in.color1;
  WiiVoodooRGBA *alpha=p.other_alpha==0?&in.iterated:p.other_alpha==1?&in.texture:&in.color1;
  if(n&16)rgb->r=rgb->g=rgb->b=0;else rgb->r|=1;
  alpha->a=(alpha->a&254)|!!(n&32);
  WiiVoodooRGBA expected=wii_voodoo_color_eval(p,in);
  for(unsigned i=0;i<16;i++){unsigned o=i*2;image[o]=in.texture.a;image[o+1]=in.texture.r;image[o+32]=in.texture.g;image[o+33]=in.texture.b;}
  DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj tex;
  GX_InitTexObj(&tex,image,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&tex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&tex,GX_TEXMAP0);
  unsigned stages=probe_equation(p,gx(in.color0),gx(in.color1));
  if(!stages){failures++;continue;}
  quad(gx(in.iterated));GXColor result;GX_PeekARGB(160,160,&result);
  GXColor equation_result=result;
  unsigned actual[4]={result.r,result.g,result.b,0},want[4]={expected.r,expected.g,expected.b,expected.a};
  GX_SetNumTevStages(stages+1);GX_SetTevOrder(stages,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);
  GX_SetTevColorIn(stages,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_APREV);
  GX_SetTevColorOp(stages,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  GX_SetTevAlphaIn(stages,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_APREV);
  GX_SetTevAlphaOp(stages,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
  quad(gx(in.iterated));GX_PeekARGB(160,160,&result);actual[3]=result.r;
  for(unsigned channel=0;channel<4;channel++){
   unsigned error=actual[channel]>want[channel]?actual[channel]-want[channel]:want[channel]-actual[channel];
   if(error>max_error)max_error=error;
   if(error>3)failures++;
  }
  GXColor fog_colour={93,133,177,(u8)n};
  if(n&64){
   stages=probe_equation(p,gx(in.color0),gx(in.color1));
   wii_gx_fog_stage(stages,fog_colour,0);
   quad(gx(in.iterated));GX_PeekARGB(160,160,&equation_result);
  }
  for(unsigned predicates=0;predicates<4;predicates++){
   const GXColor bg={32,64,96,255};u32 initial,z;
   GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
   GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
   GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);draw_z=-.8f;quad(bg);GX_PeekZ(160,160,&initial);
   stages=probe_equation(p,gx(in.color0),gx(in.color1));
   const u8 rs[]={GX_CC_RASC,GX_CC_TEXC,GX_CC_C2,GX_CC_ZERO};
   const u8 as[]={GX_CA_RASA,GX_CA_TEXA,GX_CA_A2,GX_CA_ZERO};
   unsigned end=wii_gx_rejection(stages,rs[p.other_rgb],as[p.other_alpha],!probe_untextured,predicates&1,1,!!(predicates&2));
   if(!end)pipeline_failures++;
   if(n&64)wii_gx_fog_stage(end,fog_colour,0);
   GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
   GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_TRUE);draw_z=-.5f;quad(gx(in.iterated));
   GX_PeekARGB(160,160,&result);GX_PeekZ(160,160,&z);
   int pass=(!(predicates&1)||(p.other_rgb!=3&&(rgb->r||rgb->g||rgb->b)))&&
       (!(predicates&2)||(p.other_alpha!=3&&(alpha->a&1)));
   GXColor want_rgb=pass?equation_result:bg;
   if(result.r!=want_rgb.r||result.g!=want_rgb.g||result.b!=want_rgb.b||((z!=initial)!=pass))pipeline_failures++;
   if(n>=64&&n<128)for(unsigned preserve=0;preserve<2;preserve++){
    /* Guest GREATER127 (KEEP_TEST) with depth writes, or ALWAYS blended
     * alpha (ADD_NONZERO) without writes. Both append constant fog. */
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);draw_z=-.8f;quad(bg);
    GX_PeekZ(160,160,&initial);
    stages=probe_equation(p,gx(in.color0),gx(in.color1));wii_gx_fog_stage(stages,fog_colour,0);
    GX_SetAlphaCompare(preserve?GX_ALWAYS:GX_GREATER,preserve?0:127,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
    GX_SetZMode(GX_TRUE,GX_LEQUAL,!preserve);draw_z=-.5f;quad(gx(in.iterated));
    GXColor control;GX_PeekARGB(160,160,&control);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(0,GX_PASSCLR);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);draw_z=-.8f;quad(bg);
    stages=probe_equation(p,gx(in.color0),gx(in.color1));
    end=wii_gx_rejection(stages,rs[p.other_rgb],as[p.other_alpha],!probe_untextured,predicates&1,0,!!(predicates&2));
    wii_gx_fog_stage(end,fog_colour,0);
    GX_SetAlphaCompare(preserve?GX_ALWAYS:GX_GREATER,preserve?0:127,GX_AOP_AND,preserve?GX_GREATER:GX_ALWAYS,0);
    GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
    GX_SetZMode(GX_TRUE,GX_LEQUAL,!preserve);draw_z=-.5f;quad(gx(in.iterated));
    GX_PeekARGB(160,160,&result);GX_PeekZ(160,160,&z);
    GXColor expected_rgb=pass?control:bg;
    int writes=pass&&!preserve&&actual[3]>127;
    if(result.r!=expected_rgb.r||result.g!=expected_rgb.g||result.b!=expected_rgb.b||((z!=initial)!=writes))pipeline_failures++;
   }
   GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
  }
  GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
  if(sd&&(n%64)==63){FILE *f=fopen("sd:/viper/boot.log","a");if(f){
   fprintf(f,"PIPELINE PROGRESS equations=%u arithmetic_failures=%u predicate_failures=%u\n",n+1,failures,pipeline_failures);
   char padding[512];for(unsigned i=0;i<sizeof padding;i++)padding[i]='\n';
   for(unsigned i=0;i<128;i++)if(fwrite(padding,1,sizeof padding,f)!=sizeof padding)break;
   fclose(f);
  }}
 }
 failures+=pipeline_failures;
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"PIPELINE predicates_cases=2048 blended_policy_cases=512 failures=%u\n",pipeline_failures);fclose(f);}}
 if(sd){FILE *f=fopen("sd:/viper/boot.log","a");if(f){fprintf(f,"EQUATION END result=%s cases=512 failures=%u max_error=%u tolerance=3\n",failures?"FAIL":"PASS",failures,max_error);static const char padding[65536]={0};fwrite(padding,1,sizeof padding,f);fclose(f);}}
 void *result_fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 console_init(result_fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("EQUATION END result=%s cases=512 failures=%u max_error=%u tolerance=3 sd=%d\n",failures?"FAIL":"PASS",failures,max_error,sd);
 fflush(stdout);VIDEO_SetNextFramebuffer(result_fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();
 for(;;)VIDEO_WaitVSync();
}
