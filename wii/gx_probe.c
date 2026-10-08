/* Asset-free ordered alpha/depth test. gx.h documents that ZCompLoc(FALSE)
 * makes both colour and depth conditional on alpha test. Read actual EFB data
 * before display copy; never substitute expected pixels for observations. */
#include <gccore.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>
#include "texture.h"
static unsigned char fifo[256*1024] ATTRIBUTE_ALIGN(32);
static void vertex(float x,float y,float depth,GXColor c) {
    GX_Position3f32(x,y,-depth);GX_Color4u8(c.r,c.g,c.b,c.a);
}
static void rectangle(float x0,float y0,float x1,float y1,float depth,GXColor c) {
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    vertex(x0,y0,depth,c);vertex(x1,y0,depth,c);vertex(x1,y1,depth,c);
    vertex(x0,y0,depth,c);vertex(x1,y1,depth,c);vertex(x0,y1,depth,c);
    GX_End();
}
static unsigned char texture_rgba[64] ATTRIBUTE_ALIGN(32);
static void texvertex(float x,float y,float u,float v) {
    GX_Position3f32(x,y,-.2f);GX_Color4u8(255,255,255,255);GX_TexCoord2f32(u,v);
}
static int textured_cutout(void) {
    uint8_t source[32];
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++) {
        /* Transparent green / opaque green: test alpha independently of RGB. */
        uint16_t pixel=x<2?0x03e0:0x83e0;
        source[(y*4+x)*2]=pixel;source[(y*4+x)*2+1]=pixel>>8;
    }
    if(!wii_texture_rgba8(texture_rgba,sizeof texture_rgba,source,sizeof source,0,4,4,11,NULL))return 0;
    DCFlushRange(texture_rgba,sizeof texture_rgba);GX_InvalidateTexAll();
    GXTexObj texture;GX_InitTexObj(&texture,texture_rgba,4,4,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
    GX_LoadTexObj(&texture,GX_TEXMAP0);
    GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);
    GX_SetNumTexGens(1);GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,GX_REPLACE);
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    texvertex(64,352,0,0);texvertex(480,352,1,0);texvertex(480,448,1,1);
    texvertex(64,352,0,0);texvertex(480,448,1,1);texvertex(64,448,0,1);
    GX_End();
    GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    return 1;
}
typedef struct {const char *name;u16 x,y;GXColor expected,actual;u32 z;} Sample;
int main(void) {
    VIDEO_Init();GXRModeObj *mode=VIDEO_GetPreferredMode(NULL);
    void *xfb=MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    VIDEO_Configure(mode);VIDEO_SetNextFramebuffer(xfb);VIDEO_SetBlack(FALSE);VIDEO_Flush();VIDEO_WaitVSync();
    memset(fifo,0,sizeof fifo);GX_Init(fifo,sizeof fifo);
    GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
    GX_SetDispCopySrc(0,0,640,480);GX_SetDispCopyDst(mode->fbWidth,mode->xfbHeight);
    GX_SetDispCopyYScale((float)mode->xfbHeight/480.0f);
    GX_SetPixelFmt(GX_PF_RGB8_Z24,GX_ZC_LINEAR);
    GX_SetCopyClear((GXColor){0,0,0,255},0xffffff);
    GX_SetZMode(GX_TRUE,GX_ALWAYS,GX_TRUE);GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);
    GX_CopyDisp(xfb,GX_TRUE);GX_DrawDone();
    Mtx model;Mtx44 projection;guMtxIdentity(model);
    guOrtho(projection,0,480,0,640,0,1);
    GX_LoadProjectionMtx(projection,GX_ORTHOGRAPHIC);GX_LoadPosMtxImm(model,GX_PNMTX0);GX_SetCurrentMtx(GX_PNMTX0);
    GX_ClearVtxDesc();GX_SetVtxDesc(GX_VA_POS,GX_DIRECT);GX_SetVtxDesc(GX_VA_CLR0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_POS,GX_POS_XYZ,GX_F32,0);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_CLR0,GX_CLR_RGBA,GX_RGBA8,0);
    GX_SetNumChans(1);GX_SetChanCtrl(GX_COLOR0A0,GX_DISABLE,GX_SRC_REG,GX_SRC_VTX,GX_LIGHTNULL,GX_DF_NONE,GX_AF_NONE);
    GX_SetNumTexGens(0);GX_SetNumTevStages(1);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);GX_SetCullMode(GX_CULL_NONE);
    GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetZMode(GX_TRUE,GX_LESS,GX_TRUE);
    GX_SetZCompLoc(GX_FALSE);
    GX_SetAlphaCompare(GX_GREATER,12,GX_AOP_AND,GX_ALWAYS,0);
    rectangle(64,128,224,320,.8f,(GXColor){255,0,0,255});
    rectangle(320,128,480,320,.8f,(GXColor){255,0,0,255});
    rectangle(64,128,224,320,.2f,(GXColor){0,255,0,0}); /* rejected: no Z write */
    rectangle(320,128,480,320,.2f,(GXColor){0,255,0,255}); /* opaque control */
    rectangle(64,240,224,320,.5f,(GXColor){0,0,255,255}); /* survives rejected front */
    rectangle(320,240,480,320,.5f,(GXColor){0,0,255,255}); /* blocked by opaque */
    rectangle(432,240,480,320,.1f,(GXColor){255,255,0,255}); /* nearer late draw */
    rectangle(64,352,480,448,.8f,(GXColor){255,0,0,255});
    int texture_ok=textured_cutout();
    rectangle(64,400,480,448,.5f,(GXColor){0,0,255,255});
    GX_DrawDone();
    Sample samples[]={
      {"rejected_preserves_red",128,180,{255,0,0,255},{0},0},
      {"rear_visible_after_reject",128,270,{0,0,255,255},{0},0},
      {"opaque_control",384,180,{0,255,0,255},{0},0},
      {"rear_occluded_by_opaque",384,270,{0,255,0,255},{0},0},
      {"later_front_visible",450,270,{255,255,0,255},{0},0},
      {"texture_hole_preserves_red",128,375,{255,0,0,255},{0},0},
      {"texture_hole_rear_visible",128,425,{0,0,255,255},{0},0},
      {"texture_opaque_green",400,375,{0,255,0,255},{0},0},
      {"texture_opaque_blocks_rear",400,425,{0,255,0,255},{0},0}};
    unsigned failures=texture_ok?0:1;
    for(unsigned i=0;i<sizeof samples/sizeof samples[0];i++) {
        Sample *s=&samples[i];GX_PeekARGB(s->x,s->y,&s->actual);GX_PeekZ(s->x,s->y,&s->z);
        if(s->actual.r!=s->expected.r||s->actual.g!=s->expected.g||s->actual.b!=s->expected.b)failures++;
    }
    if(!(samples[0].z>samples[1].z&&samples[1].z>samples[2].z&&
         samples[2].z==samples[3].z&&samples[3].z>samples[4].z))failures++;
    if(!(samples[5].z==samples[0].z&&samples[6].z==samples[1].z&&
         samples[7].z==samples[2].z&&samples[8].z==samples[2].z))failures++;
    GX_CopyDisp(xfb,GX_FALSE);GX_DrawDone();VIDEO_SetNextFramebuffer(xfb);VIDEO_Flush();VIDEO_WaitVSync();
    console_init(xfb,20,20,mode->fbWidth,mode->xfbHeight,mode->fbWidth*2);
    printf("VIPER WII GX ALPHA DEPTH ORDER\n");
    for(unsigned i=0;i<sizeof samples/sizeof samples[0];i++) {
        Sample *s=&samples[i];printf("%s rgb=%u,%u,%u z=%06lx\n",s->name,s->actual.r,s->actual.g,s->actual.b,(unsigned long)s->z);
    }
    printf("VIPER WII GX END failures=%u %s\n",failures,failures?"FAIL":"PASS");
    for(;;)VIDEO_WaitVSync();
}
