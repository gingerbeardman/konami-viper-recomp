/* VIPER_WII_MEMO_MULTI: a log of the GX setter calls the renderer's full
 * triangle path makes (wii/gx_renderer.c triangle_full), so a memo entry can
 * put the GPU back into its state by replaying them: through the same lazy
 * and shadow wrappers, so libogc's register copy and ours stay right.
 * Included right after gx_batch.h; each setter records, then calls the
 * wrapper it called before. */
#ifndef VIPER_WII_GX_MEMO_LOG_H
#define VIPER_WII_GX_MEMO_LOG_H
#ifdef VIPER_WII_TEV_SHADOW
#define U_TEVCI(a,b,c,d,e) tev_ci(a,b,c,d,e)
#define U_TEVAI(a,b,c,d,e) tev_ai(a,b,c,d,e)
#define U_TEVCO(a,b,c,d,e,f) tev_co(a,b,c,d,e,f)
#define U_TEVAO(a,b,c,d,e,f) tev_ao(a,b,c,d,e,f)
#define U_TEVKCS(a,b) tev_kcs(a,b)
#define U_TEVKAS(a,b) tev_kas(a,b)
#define U_TEVOP(a,b) (memset(gx_shadow.ci,0xff,sizeof gx_shadow.ci+sizeof gx_shadow.ai+sizeof gx_shadow.co+sizeof gx_shadow.ao),gx_batch_flush(),gx_state_gen++,(GX_SetTevOp)(a,b))
#else
#define U_TEVCI(a,b,c,d,e) (gx_batch_flush(),gx_state_gen++,(GX_SetTevColorIn)(a,b,c,d,e))
#define U_TEVAI(a,b,c,d,e) (gx_batch_flush(),gx_state_gen++,(GX_SetTevAlphaIn)(a,b,c,d,e))
#define U_TEVCO(a,b,c,d,e,f) (gx_batch_flush(),gx_state_gen++,(GX_SetTevColorOp)(a,b,c,d,e,f))
#define U_TEVAO(a,b,c,d,e,f) (gx_batch_flush(),gx_state_gen++,(GX_SetTevAlphaOp)(a,b,c,d,e,f))
#define U_TEVKCS(a,b) (gx_batch_flush(),gx_state_gen++,(GX_SetTevKColorSel)(a,b))
#define U_TEVKAS(a,b) (gx_batch_flush(),gx_state_gen++,(GX_SetTevKAlphaSel)(a,b))
#define U_TEVOP(a,b) (gx_batch_flush(),gx_state_gen++,(GX_SetTevOp)(a,b))
#endif
typedef struct { uint8_t op; uint8_t b[7]; uint32_t u[8]; union { Mtx44 p; Mtx m; GXTexObj t; f32 f[6]; } x; } MemoCall;
#define MEMO_LOG_CAP 256
static MemoCall memo_log[MEMO_LOG_CAP];
static unsigned memo_log_n;
static int memo_logging, memo_log_bad;
static inline MemoCall *memo_rec(unsigned op){
    if(!memo_logging)return NULL;
    if(memo_log_n>=MEMO_LOG_CAP){memo_log_bad=1;return NULL;}
    MemoCall *r=&memo_log[memo_log_n++];r->op=(uint8_t)op;return r;
}
enum {
MOP_SetDither, MOP_SetColorUpdate, MOP_SetAlphaUpdate, MOP_SetZCompLoc, MOP_SetNumTevStages, MOP_SetNumTexGens, MOP_SetZMode, MOP_SetAlphaCompare, MOP_SetBlendMode, MOP_SetZTexture, MOP_SetTexCoordGen, MOP_SetTevOrder, MOP_SetScissor, MOP_SetVtxDesc, MOP_ClearVtxDesc, MOP_SetVtxAttrFmt, MOP_SetTevKAlphaSel, MOP_SetTevKColorSel, MOP_SetTevColorIn, MOP_SetTevAlphaIn, MOP_SetTevColorOp, MOP_SetTevAlphaOp, MOP_SetTevOp, MOP_SetNumChans, MOP_SetChanCtrl, MOP_SetCullMode, MOP_SetCurrentMtx, MOP_InvalidateTexAll, MOP_SetTevKColor, MOP_SetTevColor, MOP_SetFog, MOP_SetViewport, MOP_LoadProjectionMtx, MOP_LoadTexMtxImm, MOP_LoadPosMtxImm, MOP_LoadTexObj, MOP_COUNT };
static inline void memo_GX_SetDither(u8 a){ MemoCall *r=memo_rec(MOP_SetDither); if(r){r->u[0]=(uint32_t)a;} lz_dither(a); }
static inline void memo_GX_SetColorUpdate(u8 a){ MemoCall *r=memo_rec(MOP_SetColorUpdate); if(r){r->u[0]=(uint32_t)a;} lz_color(a); }
static inline void memo_GX_SetAlphaUpdate(u8 a){ MemoCall *r=memo_rec(MOP_SetAlphaUpdate); if(r){r->u[0]=(uint32_t)a;} lz_alpha(a); }
static inline void memo_GX_SetZCompLoc(u8 a){ MemoCall *r=memo_rec(MOP_SetZCompLoc); if(r){r->u[0]=(uint32_t)a;} lz_zloc(a); }
static inline void memo_GX_SetNumTevStages(u8 a){ MemoCall *r=memo_rec(MOP_SetNumTevStages); if(r){r->u[0]=(uint32_t)a;} lz_numtev(a); }
static inline void memo_GX_SetNumTexGens(u32 a){ MemoCall *r=memo_rec(MOP_SetNumTexGens); if(r){r->u[0]=(uint32_t)a;} lz_numtexgens(a); }
static inline void memo_GX_SetZMode(u8 a,u8 b,u8 c){ MemoCall *r=memo_rec(MOP_SetZMode); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c;} lz_zmode(a,b,c); }
static inline void memo_GX_SetAlphaCompare(u8 a,u8 b,u8 c,u8 d,u8 e){ MemoCall *r=memo_rec(MOP_SetAlphaCompare); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e;} lz_acmp(a,b,c,d,e); }
static inline void memo_GX_SetBlendMode(u8 a,u8 b,u8 c,u8 d){ MemoCall *r=memo_rec(MOP_SetBlendMode); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d;} lz_blend(a,b,c,d); }
static inline void memo_GX_SetZTexture(u8 a,u8 b,u32 c){ MemoCall *r=memo_rec(MOP_SetZTexture); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c;} lz_ztex(a,b,c); }
static inline void memo_GX_SetTexCoordGen(u16 a,u32 b,u32 c,u32 d){ MemoCall *r=memo_rec(MOP_SetTexCoordGen); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d;} lz_texcoordgen(a,b,c,d); }
static inline void memo_GX_SetTevOrder(u8 a,u8 b,u32 c,u8 d){ MemoCall *r=memo_rec(MOP_SetTevOrder); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d;} lz_tevorder(a,b,c,d); }
static inline void memo_GX_SetScissor(u32 a,u32 b,u32 c,u32 d){ MemoCall *r=memo_rec(MOP_SetScissor); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d;} shadow_scissor(a,b,c,d); }
static inline void memo_GX_SetVtxDesc(u8 a,u8 b){ MemoCall *r=memo_rec(MOP_SetVtxDesc); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b;} shadow_vtx_desc(a,b); }
static inline void memo_GX_ClearVtxDesc(void){ MemoCall *r=memo_rec(MOP_ClearVtxDesc); if(r){} shadow_clear_vtx_desc(); }
static inline void memo_GX_SetVtxAttrFmt(u8 a,u32 b,u32 c,u32 d,u8 e){ MemoCall *r=memo_rec(MOP_SetVtxAttrFmt); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e;} (gx_batch_flush(),gx_state_gen++,(GX_SetVtxAttrFmt)(a,b,c,d,e)); }
static inline void memo_GX_SetTevKAlphaSel(u8 a,u8 b){ MemoCall *r=memo_rec(MOP_SetTevKAlphaSel); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b;} U_TEVKAS(a,b); }
static inline void memo_GX_SetTevKColorSel(u8 a,u8 b){ MemoCall *r=memo_rec(MOP_SetTevKColorSel); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b;} U_TEVKCS(a,b); }
static inline void memo_GX_SetTevColorIn(u8 a,u8 b,u8 c,u8 d,u8 e){ MemoCall *r=memo_rec(MOP_SetTevColorIn); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e;} U_TEVCI(a,b,c,d,e); }
static inline void memo_GX_SetTevAlphaIn(u8 a,u8 b,u8 c,u8 d,u8 e){ MemoCall *r=memo_rec(MOP_SetTevAlphaIn); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e;} U_TEVAI(a,b,c,d,e); }
static inline void memo_GX_SetTevColorOp(u8 a,u8 b,u8 c,u8 d,u8 e,u8 f){ MemoCall *r=memo_rec(MOP_SetTevColorOp); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e; r->u[5]=(uint32_t)f;} U_TEVCO(a,b,c,d,e,f); }
static inline void memo_GX_SetTevAlphaOp(u8 a,u8 b,u8 c,u8 d,u8 e,u8 f){ MemoCall *r=memo_rec(MOP_SetTevAlphaOp); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e; r->u[5]=(uint32_t)f;} U_TEVAO(a,b,c,d,e,f); }
static inline void memo_GX_SetTevOp(u8 a,u8 b){ MemoCall *r=memo_rec(MOP_SetTevOp); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b;} U_TEVOP(a,b); }
static inline void memo_GX_SetNumChans(u8 a){ MemoCall *r=memo_rec(MOP_SetNumChans); if(r){r->u[0]=(uint32_t)a;} (gx_batch_flush(),gx_state_gen++,(GX_SetNumChans)(a)); }
static inline void memo_GX_SetChanCtrl(s32 a,u8 b,u8 c,u8 d,u8 e,u8 f,u8 g){ MemoCall *r=memo_rec(MOP_SetChanCtrl); if(r){r->u[0]=(uint32_t)a; r->u[1]=(uint32_t)b; r->u[2]=(uint32_t)c; r->u[3]=(uint32_t)d; r->u[4]=(uint32_t)e; r->u[5]=(uint32_t)f; r->u[6]=(uint32_t)g;} (gx_batch_flush(),gx_state_gen++,(GX_SetChanCtrl)(a,b,c,d,e,f,g)); }
static inline void memo_GX_SetCullMode(u8 a){ MemoCall *r=memo_rec(MOP_SetCullMode); if(r){r->u[0]=(uint32_t)a;} (gx_batch_flush(),gx_state_gen++,(GX_SetCullMode)(a)); }
static inline void memo_GX_SetCurrentMtx(u32 a){ MemoCall *r=memo_rec(MOP_SetCurrentMtx); if(r){r->u[0]=(uint32_t)a;} (gx_batch_flush(),gx_state_gen++,(GX_SetCurrentMtx)(a)); }
static inline void memo_GX_InvalidateTexAll(void){ MemoCall *r=memo_rec(MOP_InvalidateTexAll); if(r){} (gx_batch_flush(),gx_state_gen++,(GX_InvalidateTexAll)()); }
static inline void memo_GX_SetTevKColor(u8 a,GXColor c){ MemoCall *r=memo_rec(MOP_SetTevKColor); if(r){r->u[0]=(uint32_t)a; memcpy(&r->u[1],&c,4);} lz_kcolor(a,c); }
static inline void memo_GX_SetTevColor(u8 a,GXColor c){ MemoCall *r=memo_rec(MOP_SetTevColor); if(r){r->u[0]=(uint32_t)a; memcpy(&r->u[1],&c,4);} lz_tevcolor(a,c); }
static inline void memo_GX_SetFog(u8 a,f32 s,f32 e,f32 n,f32 f,GXColor c){ MemoCall *r=memo_rec(MOP_SetFog); if(r){r->u[0]=(uint32_t)a; r->x.f[0]=s; r->x.f[1]=e; r->x.f[2]=n; r->x.f[3]=f; memcpy(&r->u[1],&c,4);} (gx_batch_flush(),gx_state_gen++,(GX_SetFog)(a,s,e,n,f,c)); }
static inline void memo_GX_SetViewport(f32 a,f32 b,f32 c,f32 d,f32 e,f32 f){ MemoCall *r=memo_rec(MOP_SetViewport); if(r){r->x.f[0]=a; r->x.f[1]=b; r->x.f[2]=c; r->x.f[3]=d; r->x.f[4]=e; r->x.f[5]=f;} (gx_batch_flush(),gx_state_gen++,(GX_SetViewport)(a,b,c,d,e,f)); }
static inline void memo_GX_LoadProjectionMtx(Mtx44 m,u8 t){ MemoCall *r=memo_rec(MOP_LoadProjectionMtx); if(r){memcpy(r->x.p,m,sizeof(Mtx44)); r->u[0]=(uint32_t)t;} shadow_projection(m,t); }
static inline void memo_GX_LoadTexMtxImm(Mtx m,u32 id,u8 t){ MemoCall *r=memo_rec(MOP_LoadTexMtxImm); if(r){memcpy(r->x.m,m,sizeof(Mtx)); r->u[0]=(uint32_t)id; r->u[1]=(uint32_t)t;} (gx_batch_flush(),gx_state_gen++,(GX_LoadTexMtxImm)(m,id,t)); }
static inline void memo_GX_LoadPosMtxImm(Mtx m,u32 id){ MemoCall *r=memo_rec(MOP_LoadPosMtxImm); if(r){memcpy(r->x.m,m,sizeof(Mtx)); r->u[0]=(uint32_t)id;} (gx_batch_flush(),gx_state_gen++,(GX_LoadPosMtxImm)(m,id)); }
static inline void memo_GX_LoadTexObj(GXTexObj *o,u8 map){ MemoCall *r=memo_rec(MOP_LoadTexObj); if(r){r->x.t=*o; r->u[0]=(uint32_t)map;} (TEXLOAD_FORGET(),gx_batch_flush(),gx_state_gen++,(GX_LoadTexObj)(o,map)); }
static void memo_replay(const MemoCall *c,unsigned n){
    for(unsigned i=0;i<n;i++,c++){
        const uint32_t *u=c->u;
        switch(c->op){
        case MOP_SetDither: lz_dither(((u8)u[0])); break;
        case MOP_SetColorUpdate: lz_color(((u8)u[0])); break;
        case MOP_SetAlphaUpdate: lz_alpha(((u8)u[0])); break;
        case MOP_SetZCompLoc: lz_zloc(((u8)u[0])); break;
        case MOP_SetNumTevStages: lz_numtev(((u8)u[0])); break;
        case MOP_SetNumTexGens: lz_numtexgens(((u32)u[0])); break;
        case MOP_SetZMode: lz_zmode(((u8)u[0]),((u8)u[1]),((u8)u[2])); break;
        case MOP_SetAlphaCompare: lz_acmp(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4])); break;
        case MOP_SetBlendMode: lz_blend(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3])); break;
        case MOP_SetZTexture: lz_ztex(((u8)u[0]),((u8)u[1]),((u32)u[2])); break;
        case MOP_SetTexCoordGen: lz_texcoordgen(((u16)u[0]),((u32)u[1]),((u32)u[2]),((u32)u[3])); break;
        case MOP_SetTevOrder: lz_tevorder(((u8)u[0]),((u8)u[1]),((u32)u[2]),((u8)u[3])); break;
        case MOP_SetScissor: shadow_scissor(((u32)u[0]),((u32)u[1]),((u32)u[2]),((u32)u[3])); break;
        case MOP_SetVtxDesc: shadow_vtx_desc(((u8)u[0]),((u8)u[1])); break;
        case MOP_ClearVtxDesc: shadow_clear_vtx_desc(); break;
        case MOP_SetVtxAttrFmt: (gx_batch_flush(),gx_state_gen++,(GX_SetVtxAttrFmt)(((u8)u[0]),((u32)u[1]),((u32)u[2]),((u32)u[3]),((u8)u[4]))); break;
        case MOP_SetTevKAlphaSel: U_TEVKAS(((u8)u[0]),((u8)u[1])); break;
        case MOP_SetTevKColorSel: U_TEVKCS(((u8)u[0]),((u8)u[1])); break;
        case MOP_SetTevColorIn: U_TEVCI(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4])); break;
        case MOP_SetTevAlphaIn: U_TEVAI(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4])); break;
        case MOP_SetTevColorOp: U_TEVCO(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4]),((u8)u[5])); break;
        case MOP_SetTevAlphaOp: U_TEVAO(((u8)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4]),((u8)u[5])); break;
        case MOP_SetTevOp: U_TEVOP(((u8)u[0]),((u8)u[1])); break;
        case MOP_SetNumChans: (gx_batch_flush(),gx_state_gen++,(GX_SetNumChans)(((u8)u[0]))); break;
        case MOP_SetChanCtrl: (gx_batch_flush(),gx_state_gen++,(GX_SetChanCtrl)(((s32)u[0]),((u8)u[1]),((u8)u[2]),((u8)u[3]),((u8)u[4]),((u8)u[5]),((u8)u[6]))); break;
        case MOP_SetCullMode: (gx_batch_flush(),gx_state_gen++,(GX_SetCullMode)(((u8)u[0]))); break;
        case MOP_SetCurrentMtx: (gx_batch_flush(),gx_state_gen++,(GX_SetCurrentMtx)(((u32)u[0]))); break;
        case MOP_InvalidateTexAll: (gx_batch_flush(),gx_state_gen++,(GX_InvalidateTexAll)()); break;
        case MOP_SetTevKColor: {GXColor c_;memcpy(&c_,&u[1],4);lz_kcolor((u8)u[0],c_);} break;
        case MOP_SetTevColor: {GXColor c_;memcpy(&c_,&u[1],4);lz_tevcolor((u8)u[0],c_);} break;
        case MOP_SetFog: {GXColor c_;memcpy(&c_,&u[1],4);gx_batch_flush();gx_state_gen++;(GX_SetFog)((u8)u[0],c->x.f[0],c->x.f[1],c->x.f[2],c->x.f[3],c_);} break;
        case MOP_SetViewport: {gx_batch_flush();gx_state_gen++;(GX_SetViewport)(c->x.f[0],c->x.f[1],c->x.f[2],c->x.f[3],c->x.f[4],c->x.f[5]);} break;
        case MOP_LoadProjectionMtx: {Mtx44 m_;memcpy(m_,c->x.p,sizeof m_);shadow_projection(m_,(u8)u[0]);} break;
        case MOP_LoadTexMtxImm: {Mtx m_;memcpy(m_,c->x.m,sizeof m_);gx_batch_flush();gx_state_gen++;(GX_LoadTexMtxImm)(m_,u[0],(u8)u[1]);} break;
        case MOP_LoadPosMtxImm: {Mtx m_;memcpy(m_,c->x.m,sizeof m_);gx_batch_flush();gx_state_gen++;(GX_LoadPosMtxImm)(m_,u[0]);} break;
        case MOP_LoadTexObj: {GXTexObj o_=c->x.t;TEXLOAD_FORGET();gx_batch_flush();gx_state_gen++;(GX_LoadTexObj)(&o_,(u8)u[0]);} break;
        }
    }
}
#undef GX_SetDither
#define GX_SetDither memo_GX_SetDither
#undef GX_SetColorUpdate
#define GX_SetColorUpdate memo_GX_SetColorUpdate
#undef GX_SetAlphaUpdate
#define GX_SetAlphaUpdate memo_GX_SetAlphaUpdate
#undef GX_SetZCompLoc
#define GX_SetZCompLoc memo_GX_SetZCompLoc
#undef GX_SetNumTevStages
#define GX_SetNumTevStages memo_GX_SetNumTevStages
#undef GX_SetNumTexGens
#define GX_SetNumTexGens memo_GX_SetNumTexGens
#undef GX_SetZMode
#define GX_SetZMode memo_GX_SetZMode
#undef GX_SetAlphaCompare
#define GX_SetAlphaCompare memo_GX_SetAlphaCompare
#undef GX_SetBlendMode
#define GX_SetBlendMode memo_GX_SetBlendMode
#undef GX_SetZTexture
#define GX_SetZTexture memo_GX_SetZTexture
#undef GX_SetTexCoordGen
#define GX_SetTexCoordGen memo_GX_SetTexCoordGen
#undef GX_SetTevOrder
#define GX_SetTevOrder memo_GX_SetTevOrder
#undef GX_SetScissor
#define GX_SetScissor memo_GX_SetScissor
#undef GX_SetVtxDesc
#define GX_SetVtxDesc memo_GX_SetVtxDesc
#undef GX_ClearVtxDesc
#define GX_ClearVtxDesc memo_GX_ClearVtxDesc
#undef GX_SetVtxAttrFmt
#define GX_SetVtxAttrFmt memo_GX_SetVtxAttrFmt
#undef GX_SetTevKAlphaSel
#define GX_SetTevKAlphaSel memo_GX_SetTevKAlphaSel
#undef GX_SetTevKColorSel
#define GX_SetTevKColorSel memo_GX_SetTevKColorSel
#undef GX_SetTevColorIn
#define GX_SetTevColorIn memo_GX_SetTevColorIn
#undef GX_SetTevAlphaIn
#define GX_SetTevAlphaIn memo_GX_SetTevAlphaIn
#undef GX_SetTevColorOp
#define GX_SetTevColorOp memo_GX_SetTevColorOp
#undef GX_SetTevAlphaOp
#define GX_SetTevAlphaOp memo_GX_SetTevAlphaOp
#undef GX_SetTevOp
#define GX_SetTevOp memo_GX_SetTevOp
#undef GX_SetNumChans
#define GX_SetNumChans memo_GX_SetNumChans
#undef GX_SetChanCtrl
#define GX_SetChanCtrl memo_GX_SetChanCtrl
#undef GX_SetCullMode
#define GX_SetCullMode memo_GX_SetCullMode
#undef GX_SetCurrentMtx
#define GX_SetCurrentMtx memo_GX_SetCurrentMtx
#undef GX_InvalidateTexAll
#define GX_InvalidateTexAll memo_GX_InvalidateTexAll
#undef GX_SetTevKColor
#define GX_SetTevKColor memo_GX_SetTevKColor
#undef GX_SetTevColor
#define GX_SetTevColor memo_GX_SetTevColor
#undef GX_SetFog
#define GX_SetFog memo_GX_SetFog
#undef GX_SetViewport
#define GX_SetViewport memo_GX_SetViewport
#undef GX_LoadProjectionMtx
#define GX_LoadProjectionMtx memo_GX_LoadProjectionMtx
#undef GX_LoadTexMtxImm
#define GX_LoadTexMtxImm memo_GX_LoadTexMtxImm
#undef GX_LoadPosMtxImm
#define GX_LoadPosMtxImm memo_GX_LoadPosMtxImm
#undef GX_LoadTexObj
#define GX_LoadTexObj memo_GX_LoadTexObj
/* A draw outside the batch while recording: that entry is not replayable. */
#undef GX_Begin
#define GX_Begin(...) (memo_log_bad|=memo_logging, gx_draw_prepare(), gx_state_gen++, (GX_Begin)(__VA_ARGS__))
#endif
