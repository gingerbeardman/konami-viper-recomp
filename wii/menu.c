/* Original enhanced-mode medium glyphs, rendered directly with GX. No input
 * handlers live here: the enhanced Start pulse remains owned by guest input.
 * Laid out as the game draws its captions (traced 2026-10-09): each glyph is
 * its whole 16-pixel cell less one column, letters advance one cell and a
 * space 0.6 of one. */
#include "menu.h"
#include "gx_renderer.h"
#include "enhanced_headless.h"
#include "runtime.h"
#include "input.h"
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <string.h>
#define FONT_W 256u
#define FONT_H 512u
static uint8_t *font;
static GXTexObj texture;
typedef struct {unsigned x,y,w;} Glyph;
#define CELL 16u
#define SPACE (CELL*0.6f)
static Glyph glyphs[128];
static const struct {unsigned y;const char *chars;} rows[]={
 {120,"ABCDEFGHIJKLMNOP"},{152,"QRSTUVWXYZ:;!?,."},{416,"0123456789"}};
void wii_menu_init(void){
    if(font)return;
    FILE *f=fopen("sd:/viper/menu_font.a8","rb");
    if(!f)rt_fatal("Wii menu requires sd:/viper/menu_font.a8");
    uint8_t *atlas=malloc(FONT_W*FONT_H);
    if(!atlas){fclose(f);rt_fatal("Wii menu atlas allocation");}
    size_t n=fread(atlas,1,FONT_W*FONT_H,f);int extra=fgetc(f),error=ferror(f);fclose(f);
    if(n!=FONT_W*FONT_H||extra!=EOF||error){free(atlas);rt_fatal("Wii menu font must be exactly 131072 bytes");}
    font=memalign(32,FONT_W*FONT_H*4);
    if(!font){free(atlas);rt_fatal("Wii menu GX font allocation");}
    for(unsigned r=0;r<sizeof rows/sizeof rows[0];r++){
        for(unsigned col=0;rows[r].chars[col];col++){
            unsigned x0=col*CELL;int found=0;
            if(x0+CELL>FONT_W||rows[r].y+32>FONT_H)rt_fatal("Wii menu glyph bounds");
            for(unsigned x=0;x<CELL&&!found;x++)for(unsigned y=0;y<32;y++)
                if(atlas[(rows[r].y+y)*FONT_W+x0+x]>40){found=1;break;}
            if(found)glyphs[(unsigned char)rows[r].chars[col]]=(Glyph){x0,rows[r].y,CELL-1};
        }
    }
    /* The slash is not in the medium rows: the game's own 16x32 one (it
     * draws "6th/6" with it) sits by itself near the bottom of the atlas. */
    glyphs['/']=(Glyph){91,476,CELL};
    /* GX RGBA8 AR/GB planes. RGB is WHITE, not alpha-premultiplied. */
    for(unsigned y=0;y<FONT_H;y++)for(unsigned x=0;x<FONT_W;x++){
        unsigned off=((y/4)*(FONT_W/4)+x/4)*64+((y&3)*4+(x&3))*2;
        font[off]=atlas[y*FONT_W+x];font[off+1]=255;font[off+32]=255;font[off+33]=255;
    }
    free(atlas);DCFlushRange(font,FONT_W*FONT_H*4);GX_InvalidateTexAll();
    GX_InitTexObj(&texture,font,FONT_W,FONT_H,GX_TF_RGBA8,GX_CLAMP,GX_CLAMP,GX_FALSE);
    GX_InitTexObjLOD(&texture,GX_NEAR,GX_NEAR,0,0,0,GX_FALSE,GX_FALSE,GX_ANISO_1);
}
static void vert(float x,float y,float u,float v,GXColor c,int textured){
    GX_Position3f32(x,y,0);GX_Color4u8(c.r,c.g,c.b,c.a);
    if(textured)GX_TexCoord2f32(u,v);
}
static void quad(float x,float y,float right,float bottom,float u,float v,float ur,float vb,GXColor c,int textured){
    GX_Begin(GX_TRIANGLES,GX_VTXFMT0,6);
    vert(x,y,u,v,c,textured);vert(right,y,ur,v,c,textured);vert(right,bottom,ur,vb,c,textured);
    vert(x,y,u,v,c,textured);vert(right,bottom,ur,vb,c,textured);vert(x,bottom,u,vb,c,textured);GX_End();
}
static void text_color(float x,float y,const char *s,GXColor color){
    for(;*s;s++){
        unsigned ch=(unsigned char)*s;
        if(ch>=128||!glyphs[ch].w){x+=SPACE;continue;}
        Glyph g=glyphs[ch];quad(x,y,x+g.w,y+32,(float)g.x/FONT_W,(float)g.y/FONT_H,
            (float)(g.x+g.w)/FONT_W,(float)(g.y+32)/FONT_H,color,1);x+=CELL;
    }
}
static void text(float x,float y,const char *s,int selected){
    text_color(x,y,s,(GXColor){255,selected?217:255,selected?0:255,255});
}
static const char *notice;
static unsigned notice_frames;
void wii_menu_notice(const char *s){notice=s;notice_frames=60;}   /* the game presents ~30 per second */
static float text_width(const char *s){
    float w=0;int glyph=0;
    for(;*s;s++){unsigned ch=(unsigned char)*s;glyph=ch<128&&glyphs[ch].w;w+=glyph?CELL:SPACE;}
    return glyph?w-1:w;   /* the last glyph is one column narrower than its cell */
}
void wii_menu_draw(void){
#ifdef VIPER_WII_SCRIPTED_RACE
    int menu=wii_enhanced_menu_active();   /* benchmarks have no input layer */
#else
    int wii_menu_revealed(void);
    int menu=wii_enhanced_menu_active()&&wii_menu_revealed();
#endif
    if(!menu&&!notice_frames)return;
    if(!font)rt_fatal("Wii menu draw before font initialization");
    Mtx44 p;guOrtho(p,0,384,0,512,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
    {int bx,by,bw,bh;wii_gx_output_box(&bx,&by,&bw,&bh);   /* the game's box: 1:1/16:9 like the game */
     GX_SetViewport(bx,by,bw,bh,0,1);GX_SetScissor(0,0,640,480);}
    GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetZCompLoc(GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
    GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);GX_SetDither(GX_FALSE);
    GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GX_SetNumTevStages(1);GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
    float nw=notice_frames?text_width(notice):0,nx=(512-nw)/2;
    if(menu)quad(24,195,260,355,0,0,0,0,(GXColor){0,0,0,179},0);
    if(notice_frames)quad(nx-12,12,nx+nw+12,52,0,0,0,0,(GXColor){0,0,0,179},0);
    GX_LoadTexObj(&texture,GX_TEXMAP0);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_MODULATE);
    if(menu){
        text(36,199,"START GAME",1);text(36,229,"PLUS OR START",0);text(36,259,"TILT TO STEER",0);
#ifdef VIPER_WII_DISPLAY_MULTI
        text(36,289,"1 GAS 2/B BRAKE",0);text(36,319,"MINUS DISPLAY",0);
#else
        text(36,289,"1 GAS 2/B BRAKE",0);text(36,319,"MINUS RECENTER",0);
#endif
    }
    if(notice_frames){text(nx,16,notice,1);notice_frames--;}
    /* The backend re-establishes projection, masks, blend and depth per draw.
     * Restore its untextured vertex descriptor/TEV invariant immediately. */
    GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
}
/* The pause menu over the frozen frame (the whole EFB, copied to `frame`):
 * shade 255 redraws that frame exactly as it was, for the game to resume
 * on; below 255 it is dimmed under the menu. Sets its own GX state, as
 * wii_menu_draw does. */
void wii_menu_pause_draw(GXTexObj *frame,unsigned shade){
    if(!font)rt_fatal("Wii menu draw before font initialization");
    Mtx44 p;guOrtho(p,0,480,0,640,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);
    GX_SetViewport(0,0,640,480,0,1);GX_SetScissor(0,0,640,480);
    GX_SetCullMode(GX_CULL_NONE);GX_SetZMode(GX_FALSE,GX_ALWAYS,GX_FALSE);GX_SetZCompLoc(GX_FALSE);
    GX_SetAlphaCompare(GX_ALWAYS,0,GX_AOP_AND,GX_ALWAYS,0);
    GX_SetBlendMode(GX_BM_NONE,GX_BL_ONE,GX_BL_ZERO,GX_LO_COPY);
    GX_SetColorUpdate(GX_TRUE);GX_SetAlphaUpdate(GX_FALSE);GX_SetDither(GX_FALSE);
    GX_SetFog(GX_FOG_NONE,0,1,0,1,(GXColor){0,0,0,0});
    GX_SetNumTevStages(1);GX_SetNumChans(1);
    GX_LoadTexObj(frame,GX_TEXMAP0);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);
    GX_SetVtxAttrFmt(GX_VTXFMT0,GX_VA_TEX0,GX_TEX_ST,GX_F32,0);GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0,GX_TG_MTX2x4,GX_TG_TEX0,GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);
    GX_SetTevOp(GX_TEVSTAGE0,shade>=255?GX_REPLACE:GX_MODULATE);
    quad(0,0,640,480,0,0,1,1,(GXColor){shade,shade,shade,255},1);
    if(shade<255){
        static const char *const rows[4]={"RESUME","GIVE UP","EXIT TO HBC","SYSTEM MENU"};
        int bx,by,bw,bh;wii_gx_output_box(&bx,&by,&bw,&bh);   /* the game's box, as wii_menu_draw */
        guOrtho(p,0,384,0,512,0,1);GX_LoadProjectionMtx(p,GX_ORTHOGRAPHIC);GX_SetViewport(bx,by,bw,bh,0,1);
        GX_SetBlendMode(GX_BM_BLEND,GX_BL_SRCALPHA,GX_BL_INVSRCALPHA,GX_LO_COPY);
        GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
        GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
        quad(136,87,376,293,0,0,0,0,(GXColor){0,0,0,179},0);
        GX_LoadTexObj(&texture,GX_TEXMAP0);GX_SetVtxDesc(GX_VA_TEX0,GX_DIRECT);GX_SetNumTexGens(1);
        GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORD0,GX_TEXMAP0,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_MODULATE);
        text((512-text_width("PAUSE"))/2,97,"PAUSE",0);
        int give_up=wii_give_up_available();
        for(int i=0;i<4;i++){
            GXColor c=i==wii_pause_cursor?(GXColor){255,217,0,255}:
                i==1&&!give_up?(GXColor){110,110,110,255}:(GXColor){255,255,255,255};
            text_color((512-text_width(rows[i]))/2,147+i*34,rows[i],c);
        }
    }
    GX_SetVtxDesc(GX_VA_TEX0,GX_NONE);GX_SetNumTexGens(0);
    GX_SetTevOrder(GX_TEVSTAGE0,GX_TEXCOORDNULL,GX_TEXMAP_NULL,GX_COLOR0A0);GX_SetTevOp(GX_TEVSTAGE0,GX_PASSCLR);
}
