/* Constant-coordinate original-quarter versus merged lookup GPU probe.
 * Does not prove varying-plane interpolation or geometry/raster equivalence. */
#include <gccore.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
volatile unsigned merged_probe_stage,merged_probe_cases,merged_probe_failures;
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static unsigned char original[4][1024*4*4] ATTRIBUTE_ALIGN(32);
static unsigned char merged[1024*8*4] ATTRIBUTE_ALIGN(32);
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
 merged_probe_stage=1;VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 merged_probe_stage=2;GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
 GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx model;Mtx44 projection;guMtxIdentity(model);guOrtho(projection,0,480,0,640,0,1);
 GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
 GX_SetNumChans(0);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetZCompLoc(GX_FALSE);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});

 merged_probe_stage=3;GX_SetNumTexGens(1);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX0);
 GX_SetNumTevStages(1);GX_SetTevOrder(0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);
 GX_SetTevColor(GX_TEVREG0,(GXColor){255,0,0,255});
 GX_SetTevColorIn(0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_C0);
 GX_SetTevColorOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(0,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO,GX_CA_A0);
 GX_SetTevAlphaOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetZTexture(GX_ZT_DISABLE,GX_TF_Z24X8,0);
 merged_probe_stage=4;unsigned control25,control50;draw(.25f);GX_PeekZ(100,100,&control25);draw(.5f);GX_PeekZ(100,100,&control50);
 GX_SetZTexture(GX_ZT_REPLACE,GX_TF_Z24X8,0);
 unsigned cases=0,failures=0,reference_failures=0,first_e=0;float first_norm=0;unsigned first_old=0,first_new=0;
 merged_probe_stage=5;for(unsigned e=0;e<16;e++){
  float lo=ldexpf(.5f,-(int)e),step=ldexpf(.125f,-(int)e);
  for(unsigned q=0;q<4;q++)for(unsigned y=0;y<4;y++)for(unsigned x=0;x<1024;x++){
   unsigned d=wd(lo+q*step+step*((x+.5f)/1024));
   unsigned o=(x/4)*64+(y*4+(x&3))*2;
   original[q][o]=255;original[q][o+1]=d>>8;original[q][o+32]=d&255;original[q][o+33]=0;
  }
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<1024;x++){
   unsigned q=y<4?y:3,sx=y<4?x:1023;
   unsigned so=(sx/4)*64+(sx&3)*2;
   unsigned o=(y/4)*256*64+(x/4)*64+((y&3)*4+(x&3))*2;
   merged[o]=original[q][so];merged[o+1]=original[q][so+1];
   merged[o+32]=original[q][so+32];merged[o+33]=original[q][so+33];
  }
  DCFlushRange(original,sizeof original);DCFlushRange(merged,sizeof merged);GX_InvalidateTexAll();
  GXTexObj oldtex[4],newtex;
  for(unsigned q=0;q<4;q++){
   GX_InitTexObj(&oldtex[q],original[q],1024,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
   GX_InitTexObjLOD(&oldtex[q],GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
  }
  GX_InitTexObj(&newtex,merged,1024,8,GX_TF_RGBA8,GX_REPEAT,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&newtex,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
  for(unsigned c=0;c<145;c++){
   float norm;
   if(c<128)norm=(c*32+.5f)/4096;
   else if(c<143){unsigned q=(c-128)/3;unsigned edge=(c-128)%3;norm=q*.25f;if(edge==0)norm=nextafterf(norm,-INFINITY);else if(edge==2)norm=nextafterf(norm,INFINITY);if(norm<0)norm=0;if(norm>1)norm=1;}
   else norm=c==143?0:1;
   unsigned q=(unsigned)(norm*4);if(q>3)q=3;
   float os=norm*4-q;
   Mtx om={{0,0,0,os},{0,0,0,.5f},{0,0,0,1}};
   Mtx nm={{0,0,0,norm*4},{0,0,0,norm*.5f},{0,0,0,1}};
   unsigned a,b;
   GX_LoadTexObj(&oldtex[q],GX_TEXMAP0);GX_LoadTexMtxImm(om,GX_TEXMTX0,GX_MTX3x4);draw(.5f);GX_PeekZ(100,100,&a);
   GX_LoadTexObj(&newtex,GX_TEXMAP0);GX_LoadTexMtxImm(nm,GX_TEXMTX0,GX_MTX3x4);draw(.5f);GX_PeekZ(100,100,&b);
   if(c<128){unsigned column=(unsigned)(os*1024);if(column>1023)column=1023;
    /* Compare the actual original RGBA8 table representation: its two
     * depth bytes truncate to16 bits, including the minimum-W endpoint. */
    unsigned expected=(wd(lo+q*step+step*((column+.5f)/1024))&65535u)<<8;
    if(a!=expected)reference_failures++;
   }
   if(a!=b){if(!failures){first_e=e;first_norm=norm;first_old=a;first_new=b;}failures++;}
   cases++;merged_probe_cases=cases;merged_probe_failures=failures;
  }
 }
 merged_probe_stage=6;failures+=reference_failures;if(control25!=0x400000||control50!=0x800000)failures++;
 /* Use a fresh RAM console framebuffer. Reusing a GPU-copied XFB can
  * display the cached triangle instead of later CPU console writes in Dolphin. */
 GX_DrawDone();fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII MERGED LOOKUP %s cases=%u failures=%u\n",failures?"FAIL":"PASS",cases,failures);
 if(failures)printf("first exponent=%u norm=%.9g Z=%06x/%06x\n",first_e,first_norm,first_old,first_new);
 printf("Reference texel controls failures=%u\n",reference_failures);
 printf("Z24 controls .25=%06x .5=%06x expected400000/800000\n",control25,control50);
 printf("Constant coordinates only; varying planes/rasterization unproven\n");
 merged_probe_failures=failures;merged_probe_stage=7;VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
