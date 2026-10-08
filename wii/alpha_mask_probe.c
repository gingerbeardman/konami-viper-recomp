/* Exhaustive native TEV parity test. CPU checks actual EFB color and depth. */
#include <gccore.h>
#include <stdio.h>
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32),image[16*16*4] ATTRIBUTE_ALIGN(32);
static void rect(float x,float y,float w,float h,float u,float v,float ur,float vr,unsigned alpha){
 const float p[6][4]={{x,y,u,v},{x+w,y,ur,v},{x+w,y+h,ur,vr},{x,y,u,v},{x+w,y+h,ur,vr},{x,y+h,u,vr}};
 GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);for(unsigned i=0;i<6;i++){
 GX_Position3f32(p[i][0],p[i][1],-.2f);GX_Color4u8(255,255,255,alpha);GX_TexCoord2f32(p[i][2],p[i][3]);}GX_End();
}
static void passcolor(unsigned stage){
 GX_SetTevColorIn(stage,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_CPREV);
 GX_SetTevColorOp(stage,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
}
static void pipeline(int mask){
 GX_SetNumTevStages(mask?6:1);GX_SetTevOrder(0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
 GX_SetTevColorIn(0,GX_CC_ZERO,GX_CC_ZERO,GX_CC_ZERO,GX_CC_TEXC);
 GX_SetTevColorOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
 GX_SetTevAlphaIn(0,GX_CA_ZERO,GX_CA_TEXA,GX_CA_RASA,GX_CA_ZERO);
 GX_SetTevAlphaOp(0,GX_TEV_ADD,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,mask?GX_TEVREG0:GX_TEVPREV);
 if(!mask)return;
 for(unsigned i=1;i<=4;i++){
  GX_SetTevOrder(i,GX_TEXCOORD0,GX_TEXMAP0,GX_COLORNULL);passcolor(i);
  GX_SetTevAlphaIn(i,i==1?GX_CA_TEXA:GX_CA_A1,GX_CA_ZERO,GX_CA_ZERO,GX_CA_ZERO);
  GX_SetTevAlphaOp(i,GX_TEV_ADD,GX_TB_ZERO,i==4?GX_CS_SCALE_2:GX_CS_SCALE_4,GX_FALSE,GX_TEVREG1);
 }
 GX_SetTevOrder(5,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLORNULL);passcolor(5);
 GX_SetTevAlphaIn(5,GX_CA_A1,GX_CA_ZERO,GX_CA_A0,GX_CA_ZERO);
 GX_SetTevAlphaOp(5,GX_TEV_COMP_A8_GT,GX_TB_ZERO,GX_CS_SCALE_1,GX_TRUE,GX_TEVPREV);
}
static void upload(unsigned width,unsigned height,int linear){
 DCFlushRange(image,sizeof image);GX_InvalidateTexAll();GXTexObj t;
 GX_InitTexObj(&t,image,width,height,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
 GX_InitTexObjLOD(&t,linear?GX_LINEAR:GX_NEAR,linear?GX_LINEAR:GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);GX_LoadTexObj(&t,GX_TEXMAP0);
}
static void clear(void *fb){GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_CopyDisp(fb,GX_TRUE);GX_DrawDone();GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);}
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
 unsigned failures=0,first_bad=999;GXColor badcolor={0};u32 badz=0;unsigned badexpected=0;
 GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);
 GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);
 for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++){
  unsigned o=((y/4)*4+x/4)*64+((y&3)*4+(x&3))*2;
  image[o]=y*16+x;image[o+1]=image[o+32]=image[o+33]=255;
 }
 upload(16,16,0);pipeline(1);
 for(unsigned a=0;a<256;a++){float u=((a%16)+.5f)/16,v=((a/16)+.5f)/16;rect(64+(a%16)*32,48+(a/16)*24,32,24,u,v,u,v,128);}
 GX_DrawDone();
 for(unsigned a=0;a<256;a++){
  GXColor c;u32 z;GX_PeekARGB(80+(a%16)*32,60+(a/16)*24,&c);GX_PeekZ(80+(a%16)*32,60+(a/16)*24,&z);
  unsigned expected=(a&1)?(a*129+128)/256:0;
  if(c.r!=expected||c.g!=expected||c.b!=expected||((z<0xffffff)!=(int)(a&1))){if(first_bad==999){first_bad=a;badcolor=c;badz=z;badexpected=expected;}failures++;}
 }
 /* Independent parity-only output:255 for odd,0 for even, no blend. */
 clear(fb);pipeline(1);GX_SetTevKAlphaSel(5,GX_TEV_KASEL_1);
 GX_SetTevAlphaIn(5,GX_CA_A1,GX_CA_ZERO,GX_CA_KONST,GX_CA_ZERO);
 GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
 for(unsigned a=0;a<256;a++){float u=((a%16)+.5f)/16,v=((a/16)+.5f)/16;rect(64+(a%16)*32,48+(a/16)*24,32,24,u,v,u,v,128);}GX_DrawDone();
 unsigned parity_fail=0;
 for(unsigned a=0;a<256;a++){GXColor c;u32 z;GX_PeekARGB(80+(a%16)*32,60+(a/16)*24,&c);GX_PeekZ(80+(a%16)*32,60+(a/16)*24,&z);
 if(c.r!=((a&1)?255:0)||((z<0xffffff)!=(int)(a&1)))parity_fail++;}
 failures+=parity_fail;
 /* Measure ordinary saved-alpha blending independently for every source value. */
 unsigned control[256];clear(fb);pipeline(0);GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
 for(unsigned a=0;a<256;a++){float u=((a%16)+.5f)/16,v=((a/16)+.5f)/16;rect(64+(a%16)*32,48+(a/16)*24,32,24,u,v,u,v,128);}GX_DrawDone();
 for(unsigned a=0;a<256;a++){GXColor c;GX_PeekARGB(80+(a%16)*32,60+(a/16)*24,&c);control[a]=c.r;}
 /* Actual new game tuple: no depth writes, SRC_ALPHA/INV_SRC_ALPHA. Verify
  * masking does not change surviving alpha/color compared with plain TEV. */
 clear(fb);pipeline(1);GX_SetAlphaCompare(GX_GREATER,0,GX_AOP_AND,GX_ALWAYS,0);GX_SetZMode(GX_TRUE,GX_LEQUAL,GX_FALSE);
 for(unsigned a=0;a<256;a++){float u=((a%16)+.5f)/16,v=((a/16)+.5f)/16;rect(64+(a%16)*32,48+(a/16)*24,32,24,u,v,u,v,128);}GX_DrawDone();
 unsigned tuple_fail=0,one_rgb=999;u32 one_z=0;
 for(unsigned a=0;a<256;a++){GXColor c;u32 z;GX_PeekARGB(80+(a%16)*32,60+(a/16)*24,&c);GX_PeekZ(80+(a%16)*32,60+(a/16)*24,&z);
 if(a==1){one_rgb=c.r;one_z=z;}if(c.r!=((a&1)?control[a]:0)||z!=0xffffff)tuple_fail++;}
 failures+=tuple_fail;
 /* ARGB1555-style binary alpha edge, whiteRGB on both sides. First observe
  * the filtered alpha through unmasked white blending, then test its parity. */
 clear(fb);
 for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++){unsigned o=(y*4+x)*2;image[o]=x<2?0:255;image[o+1]=image[o+32]=image[o+33]=255;}
 upload(4,4,1);pipeline(0);rect(64,64,512,256,0,0,1,1,255);GX_DrawDone();
 const unsigned xs[8]={260,276,292,308,324,340,356,372};unsigned filtered[8],actual[8];u32 edgez[8];
 for(unsigned i=0;i<8;i++){GXColor c;GX_PeekARGB(xs[i],160,&c);filtered[i]=c.r;}
 clear(fb);pipeline(1);rect(64,64,512,256,0,0,1,1,128);GX_DrawDone();
 for(unsigned i=0;i<8;i++){
  GXColor c;GX_PeekARGB(xs[i],160,&c);GX_PeekZ(xs[i],160,&edgez[i]);actual[i]=c.r;
  unsigned expected=(filtered[i]&1)?(filtered[i]*129+128)/256:0;
  if(c.r!=expected||((edgez[i]<0xffffff)!=(int)(filtered[i]&1)))failures++;
 }
 GX_CopyDisp(fb,GX_FALSE);GX_DrawDone();console_init(fb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
 printf("VIPER WII ALPHA MASK %s\n",failures?"FAIL":"PASS");printf("256 alpha inputs firstbad=%u RGB%u,%u,%u expected%u Z%06x\n",first_bad,badcolor.r,badcolor.g,badcolor.b,badexpected,badz);
 printf("parityonlyfail=%u tuplefail=%u a1control=%u tupleRGB%u Z%06x\n",parity_fail,tuple_fail,control[1],one_rgb,one_z);
 for(unsigned i=0;i<8;i++)printf("edge x%u filteredA%u result%u Z%06x\n",xs[i],filtered[i],actual[i],edgez[i]);
 printf("VIPER WII ALPHA MASK END failures=%u\n",failures);VIDEO_SetNextFramebuffer(fb);VIDEO_Flush();for(;;)VIDEO_WaitVSync();
}
