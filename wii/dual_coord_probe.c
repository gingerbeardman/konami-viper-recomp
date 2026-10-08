/* Two simultaneous texture fetches with independent POS/STQ matrices.
 * Nearest samples near boundaries are excluded; equations allow 3/255 error. */
#include <gccore.h>
#include <stdio.h>
#include <fat.h>
#include "gx_tmu_equation.h"
#include "voodoo_tmu_eval.h"
#include <string.h>
#include <math.h>
#include "projective_texture.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static unsigned char image[2][8*8*4] ATTRIBUTE_ALIGN(32);
static GXColor texel(unsigned x,unsigned y){return (GXColor){(u8)(20+x*28),(u8)(20+y*28),(u8)(30+((x+3*y)%8)*27),255};}
int main(void){
 int sd=fatInitDefault();
 VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
 void *fb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
 VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(fb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
 GX_Init(fifo,sizeof fifo);GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
 GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);GX_SetDispCopyYScale((float)mode->xfbHeight/480);
 GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);GX_SetDither(GX_FALSE);
 GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);GX_SetColorUpdate(GX_TRUE);GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);
 GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();
 Mtx model;Mtx44 projection;guMtxIdentity(model);guOrtho(projection,0,480,0,640,0,1);
 GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);
 GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
 GX_SetNumChans(0);GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);
 GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
 GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});

 unsigned failures=0,samples=0,maxerror=0,skipped=0;
 for(unsigned unit=0;unit<2;unit++){
  for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++){
   GXColor c=unit?texel(7-y,x):texel(x,y);unsigned o=((y/4)*2+x/4)*64+((y&3)*4+(x&3))*2;
   image[unit][o]=255;image[unit][o+1]=c.r;image[unit][o+32]=c.g;image[unit][o+33]=c.b;
  }
  DCFlushRange(image[unit],sizeof image[unit]);GXTexObj texture;
  GX_InitTexObj(&texture,image[unit],8,8,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
  GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
  GX_LoadTexObj(&texture,unit?GX_TEXMAP3:GX_TEXMAP0);
 }
 GX_InvalidateTexAll();
 WiiVoodooTMUPlan replace=wii_voodoo_tmu_plan(0x10241000u);
 WiiVoodooTMUPlan multiply=wii_voodoo_tmu_plan((1u<<14)|(1u<<17)|(1u<<23)|(1u<<26));
 for(unsigned phase=0;phase<4;phase++){
  WiiProjectiveVertex v[2][3]={{{64,64,0,0,1},{576,64,2,0,2},{64,448,0,.5f,.5f}},
                             {{64,64,.7f,.1f,1},{576,64,.1f,.7f,1},{64,448,.6f,.8f,1}}};
  if(phase&1)for(unsigned i=0;i<3;i++)v[0][i].w=1;
  if(phase&2){v[1][1].w=1.5f;v[1][2].w=.75f;}
  for(unsigned unit=0;unit<2;unit++){
   Mtx matrix;if(!wii_projective_texture_matrix(matrix,v[unit],1,1)){failures++;continue;}
   GX_LoadTexMtxImm(matrix,unit?GX_TEXMTX2:GX_TEXMTX0,GX_MTX3x4);
  }
  GX_SetNumTexGens(3);
  GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX0);
  GX_SetTexCoordGen(GX_TEXCOORD1,GX_TG_MTX2x4,GX_TG_POS,GX_IDENTITY);
  GX_SetTexCoordGen(GX_TEXCOORD2,GX_TG_MTX3x4,GX_TG_POS,GX_TEXMTX2);
  unsigned end=wii_gx_tmu_equation(replace,0,GX_TEXCOORD2,GX_TEXMAP3,GX_CC_ZERO,GX_CA_ZERO,GX_CC_ZERO,GX_TEVREG0,
    GX_TEV_KCSEL_K2,GX_TEV_KASEL_K2_R,GX_TEV_KCSEL_K2_A,GX_TEV_KASEL_K2_A);
  end=wii_gx_tmu_equation(multiply,end,GX_TEXCOORD0,GX_TEXMAP0,GX_CC_C0,GX_CA_A0,GX_CC_A0,GX_TEVPREV,
    GX_TEV_KCSEL_K3,GX_TEV_KASEL_K3_R,GX_TEV_KCSEL_K3_A,GX_TEV_KASEL_K3_A);
  GX_SetNumTevStages(end);
  GX_Begin(GX_TRIANGLES,GX_VTXFMT0,3);for(unsigned i=0;i<3;i++)GX_Position3f32(v[0][i].x,v[0][i].y,-.5f);GX_End();GX_DrawDone();
  for(unsigned y=96;y<400;y+=29)for(unsigned x=96;x<530;x+=31){
   double weights[3];weights[1]=(x+.5-64)/512;weights[2]=(y+.5-64)/384;weights[0]=1-weights[1]-weights[2];
   if(weights[0]<.05)continue;
   GXColor local[2];int safe=1;
   for(unsigned unit=0;unit<2;unit++){
    double sn=0,tn=0,q=0;for(unsigned i=0;i<3;i++){sn+=weights[i]*v[unit][i].s;tn+=weights[i]*v[unit][i].t;q+=weights[i]*v[unit][i].w;}
    double u=8*sn/q,t=8*tn/q;
    if(fabs(u-round(u))<.12||fabs(t-round(t))<.12){safe=0;break;}
    int tx=(int)floor(u),ty=(int)floor(t);if(tx<0)tx=0;if(tx>7)tx=7;if(ty<0)ty=0;if(ty>7)ty=7;
    local[unit]=unit?texel(7-ty,tx):texel(tx,ty);
   }
   if(!safe){skipped++;continue;}
   WiiVoodooRGBA l0={local[0].r,local[0].g,local[0].b,255},l1={local[1].r,local[1].g,local[1].b,255};
   WiiVoodooRGBA want=wii_voodoo_tmu_eval(multiply,l0,l1,0,0);GXColor actual;GX_PeekARGB(x,y,&actual);
   unsigned av[3]={actual.r,actual.g,actual.b},wv[3]={want.r,want.g,want.b};
   for(unsigned c=0;c<3;c++){unsigned error=av[c]>wv[c]?av[c]-wv[c]:wv[c]-av[c];if(error>maxerror)maxerror=error;if(error>3)failures++;}
   samples++;
  }
 }
 if(sd){FILE*f=fopen("sd:/viper/boot.log","w");if(f){fprintf(f,"DUAL COORD END result=%s samples=%u failures=%u max_error=%u tolerance=3 boundary_skipped=%u\n",failures?"FAIL":"PASS",samples,failures,maxerror,skipped);char pad[512];memset(pad,'\n',sizeof pad);for(unsigned i=0;i<128;i++)fwrite(pad,1,sizeof pad,f);fclose(f);}}
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();
 console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("DUAL COORD %s samples%u failures%u max_error%u\n",failures?"FAIL":"PASS",samples,failures,maxerror);
 VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
