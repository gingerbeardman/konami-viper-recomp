/* Host-only device contract checks; no platform or rendering dependencies. */
#include "voodoo_headless.c"
#include <assert.h>
#include <stdarg.h>
#include <setjmp.h>
static jmp_buf fail;
static const char *expected;
static uint64_t now,scheduled;
static SchedCb callback;
static unsigned irq[32],presents;
static unsigned rendered_triangles,rendered_clears,rendered_presents,sequence;
static void consume_contract(void){
    for(unsigned first=0;first<32;first++)for(unsigned count=0;count<96;count++){
        memset(fifo_present,0xa5,32);
        uint8_t expected_bits[32];memcpy(expected_bits,fifo_present,32);
        for(unsigned i=first;i<first+count;i++)expected_bits[i>>3]&=~(1u<<(i&7));
        fifo_consume(first*4,count);
        assert(!memcmp(fifo_present,expected_bits,32));
    }
    memset(fifo_present+sizeof(fifo_present)-16,0xff,16);
    fifo_consume(0x800000-33*4,33);
    for(unsigned i=0;i<33;i++)assert(!fifo_has(0x800000-4-i*4));
    assert(fifo_has(0x800000-34*4));
    fifo_presence_reset();
}
static void bounds_contract(void){
#ifdef VIPER_WII_FIFO_BOUNDS_CACHE
    uint32_t state=0x19283746u,saved8=agp[8],saved9=agp[9];
    for(unsigned i=0;i<100000;i++){
        state=state*1664525u+1013904223u;uint32_t value=state;
        state=state*1664525u+1013904223u;uint32_t mask=state;
        unsigned r=8+(i&1);
        voodoo_reg_write(0x80000+r*4,value,mask);
        unsigned base=(agp[8]&0xffffffu)<<12;
        unsigned end=base+(((agp[9]&255u)+1u)<<12);
        assert(fifo_cached_base==base&&fifo_cached_end==end);
        unsigned probes[]={0,base,base-1,end,end-1,0xffffffffu};
        for(unsigned j=0;j<6;j++)assert((probes[j]>=base&&probes[j]<end)==
            (probes[j]>=fifo_cached_base&&probes[j]<fifo_cached_end));
    }
    voodoo_reg_write(0x80000+8*4,saved8,~0u);
    voodoo_reg_write(0x80000+9*4,saved9,~0u);
#endif
}
static void readiness_contract(void){
    bounds_contract();
    unsigned saved_enable=agp[9];
    voodoo_reg_write(0x80000+8*4,1,~0u);
    voodoo_reg_write(0x80000+9*4,256,~0u);swap_pending=0;
    for(unsigned a=0;a<5;a++)for(unsigned b=0;b<5;b++)
    for(unsigned c=0;c<5;c++)for(unsigned d=0;d<5;d++)
    for(unsigned e=0;e<5;e++){
        unsigned order[5]={a,b,c,d,e};
        int duplicate=0;
        for(unsigned i=0;i<5;i++)for(unsigned j=0;j<i;j++)duplicate|=order[i]==order[j];
        if(duplicate)continue;
        agp[11]=0x1800;fifo_presence_reset();fifo_hdr_invalidate();
        for(unsigned i=0;i<4;i++)regs[0x104/4+i]=0;
        /* Include header-last arrival, duplicates and payload overwrites. */
        for(unsigned i=0;i<5;i++){
            unsigned word_index=order[i];
            uint32_t value=word_index?0xff00+word_index:(4u<<16)|(1u<<15)|((0x104/4)<<3)|1;
            voodoo_lfb_write(0x1800+word_index*4,value,~0u);
            if(i<4){
                assert(agp[11]==0x1800);assert(regs[0x104/4]==0);
                value=word_index?0x100+word_index-1:value;
                voodoo_lfb_write(0x1800+word_index*4,value,~0u);
                assert(agp[11]==0x1800);assert(regs[0x104/4]==0);
            }
        }
        assert(agp[11]==0x1814&&!fifo_hdr_valid);
        for(unsigned i=0;i<4;i++)assert(regs[0x104/4+i]==(order[4]==i+1?0xff01+i:0x100+i));
    }
    voodoo_reg_write(0x80000+9*4,saved_enable,~0u);
}
static void draw_cb(void *user,const WiiVoodooView *view,const WiiVoodooVertex vertices[3],uint32_t cmd){
    assert(user==&sequence&&view->vram==vram&&view->regs==regs);
    assert(vertices[0].wb==1&&vertices[2].x==0&&cmd==((3<<6)|3));
    assert(++sequence==1);rendered_triangles++;
}
static void clear_cb(void *user,const WiiVoodooView *view){
    assert(user==&sequence&&view->tmu[0][0]==tmu_regs[0][0]);
    assert(vram_read(0x300008/4)==0xf8000000); /* device write precedes callback */
    assert(++sequence==3);rendered_clears++;
}
static void present_cb(void *user,const WiiVoodooView *view,unsigned base){
    assert(user==&sequence&&view->palette_epoch==palette_epoch&&base==(regs[0x250/4]&0x7ffff0));
    assert(++sequence==2);rendered_presents++;
}
void rt_log(const char *fmt,...) {(void)fmt;}
void rt_fatal(const char *why) {
    if(expected&&strcmp(expected,why)==0)longjmp(fail,1);
    fprintf(stderr,"unexpected fatal: %s\n",why);abort();
}
uint64_t rt_now(void) {return now;}
void rt_eat_cycles(uint32_t n) {now+=n;}
void rt_sched_at(uint64_t t,SchedCb cb,void *arg) {(void)arg;scheduled=t;callback=cb;}
void rt_sched_cancel(SchedCb cb,void *arg) {(void)cb;(void)arg;callback=NULL;}
void epic_raise(int n) {assert(n>=0&&n<32);irq[n]++;}
static void presented(void) {presents++;}
static void word(unsigned off,uint32_t v) {voodoo_lfb_write(off,v,~0u);}
static void agp_write(unsigned r,uint32_t v) {voodoo_reg_write(0x80000+r*4,v,~0u);}
static void tick(void) {now=scheduled;assert(callback);callback(NULL);}
static struct {unsigned count;uint32_t cmd;WiiVoodooVertex v[3];} observed[16];
static unsigned observed_count;
static void strip_cb(void *user,const WiiVoodooView *view,const WiiVoodooVertex v[3],uint32_t cmd){
    (void)user;assert(observed_count<16);
    observed[observed_count].count=view->strip_count;observed[observed_count].cmd=cmd;
    memcpy(observed[observed_count++].v,v,sizeof observed[0].v);
}
static void xy_packet(unsigned code,unsigned flags,const float xy[][2],unsigned n){
    for(unsigned i=0;i<n;i++)for(unsigned j=0;j<2;j++){
        uint32_t bits;memcpy(&bits,&xy[i][j],4);vram_write(0x1001+i*2+j,bits);
    }
    triangle_packet(0x4000,3|(code<<3)|(n<<6)|flags);
}
static int rejected(unsigned i,unsigned extra){
    const WiiVoodooVertex *v=observed[i].v;uint32_t cmd=observed[i].cmd|extra;
    float area=(v[0].x-v[1].x)*(v[0].y-v[2].y)-(v[0].x-v[2].x)*(v[0].y-v[1].y);
    unsigned sign=(cmd>>24)&1;
    if(!(cmd&(1u<<22))&&!(cmd&(1u<<25)))sign^=(observed[i].count-3)&1;
    return (area<0)==sign;
}
static void strip_contract(void){
    const float xy[6][2]={{0,0},{1,0},{0,1},{1,1},{0,2},{1,2}};
    const float next[2][2]={{0,3},{1,3}};
    WiiVoodooRenderer backend={strip_cb,NULL,NULL};wii_voodoo_set_renderer(&backend,NULL);
    observed_count=0;xy_packet(1,(1u<<23)|(1u<<24),xy,6);
    assert(observed_count==4);
    for(unsigned i=0;i<4;i++){
        assert(observed[i].count==i+3&&!rejected(i,0));
        for(unsigned j=0;j<3;j++)assert(observed[i].v[j].x==xy[i+j][0]&&observed[i].v[j].y==xy[i+j][1]);
        assert(rejected(i,1u<<25)==(int)(i&1));
        observed[i].cmd^=1u<<24;assert(rejected(i,0));observed[i].cmd^=1u<<24;
    }
    xy_packet(2,(1u<<23)|(1u<<24),next,2);assert(observed_count==6);
    assert(observed[4].count==7&&observed[5].count==8&&!rejected(4,0)&&!rejected(5,0));
    assert(observed[4].v[0].y==2&&observed[4].v[1].y==2&&observed[4].v[2].y==3);
    observed_count=0;xy_packet(0,0,xy,6);assert(observed_count==2);
    assert(observed[0].count==3&&observed[1].count==3&&observed[1].v[0].x==1&&observed[1].v[0].y==1);
    const float fan[4][2]={{0,0},{1,0},{1,1},{0,1}};
    observed_count=0;xy_packet(1,(1u<<22)|(1u<<23)|(1u<<24),fan,4);
    assert(observed_count==2&&observed[0].count==3&&observed[1].count==4);
    assert(observed[1].v[0].x==0&&observed[1].v[0].y==0&&observed[1].v[1].x==1&&observed[1].v[1].y==1);
    assert(!rejected(0,0)&&!rejected(1,0)&&!rejected(1,1u<<25));
    wii_voodoo_set_renderer(NULL,NULL);
}
int main(void) {
    uint8_t *bytes=malloc(WII_VOODOO_VRAM_BYTES);assert(bytes);
    wii_voodoo_set_vram(bytes,WII_VOODOO_VRAM_BYTES);wii_voodoo_set_present(presented);voodoo_init();
    word(0,0x12345678);assert(bytes[0]==0x78&&bytes[1]==0x56&&bytes[2]==0x34&&bytes[3]==0x12);
    voodoo_lfb_write(0,0xab00,0xff00);assert(voodoo_lfb_read(0)==0x1234ab78);
    word(0x800000,0xabcdef01);assert(voodoo_lfb_read(0)==0xabcdef01);
    for(unsigned i=0;i<0x200000;i++)vram_write(i,i^0x71ab0032u);
    for(unsigned i=0;i<0x200000;i++)assert(vram_read(i)==(i^0x71ab0032u));
    voodoo_init();assert(voodoo_lfb_read(0)==0);
    agp_write(8,1);agp_write(9,256);agp_write(11,0x1000);
    uint32_t header=(1u<<16)|((0x104/4)<<3)|1;
    word(0x1000,header);assert(fifo_hdr_valid&&agp[11]==0x1000);
    word(0x1004,0x11223344);assert(regs[0x104/4]==0x11223344&&agp[11]==0x1008&&!fifo_hdr_valid);
    word(0x1008,((0x1100/4)<<6)|(3<<3));assert(agp[11]==0x1100);
    word(0x1100,0);assert(agp[11]==0x1104); /* zero NOP is present, not missing */
    word(0x1104,header);assert(fifo_hdr_valid);
    expected="partial FIFO word write";
    if(!setjmp(fail)){voodoo_lfb_write(0x1108,1,0xff);assert(0);}expected=NULL;
    agp_write(9,0);assert(!fifo_has(0x1104)&&!fifo_hdr_valid);
    agp_write(9,256);agp_write(11,0x1200);
    WiiVoodooRenderer backend={draw_cb,clear_cb,present_cb};
    wii_voodoo_set_renderer(&backend,&sequence);
    word(0x1200,(3<<6)|3); /* three XY-only vertices, delayed until complete */
    assert(fifo_hdr_checked==1);
    word(0x1214,0);assert(fifo_hdr_checked==1); /* out-of-order tail */
    for(unsigned i=1;i<=4;i++){
        word(0x1200+i*4,0);
        assert(fifo_hdr_checked==(i==4?6:i+1));
    }
    /* Rewriting the header invalidates and safely rescans the prefix. */
    word(0x1200,(3<<6)|3);assert(fifo_hdr_checked==6);
    assert(counters.packets==0);word(0x1218,0);
    assert(counters.packets==1&&counters.triangles==1&&geometry_started);
    voodoo_reg_write(0x200128,5,~0u);assert(swap_pending);
    tick();assert(swap_pending&&irq[EPIC_IRQ0]==1&&presents==0);
    tick();assert(!swap_pending&&irq[EPIC_IRQ0]==2&&presents==1&&counters.presents==1);
    regs[1]=0x20;voodoo_reg_write(0x20013c,7,~0u);assert(irq[EPIC_IRQ4]==1);
    agp_write(9,0);
    regs[0x110/4]=(1<<9)|(1<<10)|(1<<17);regs[0x148/4]=0xff0000;regs[0x130/4]=0x5678;
    regs[0x1f8/4]=regs[0x1f0/4]=8;regs[0x1ec/4]=0x300000;regs[0x1f4/4]=0x400000;
    regs[0x118/4]=(1<<16)|3;regs[0x11c/4]=1;io[0x10/4]=1<<18;
    fastfill();assert(voodoo_lfb_read(0x300008)==0xf8000000);assert(voodoo_lfb_read(0x400008)==0x56780000);
    assert(rendered_triangles==1&&rendered_presents==1&&rendered_clears==1);
    wii_voodoo_set_renderer(NULL,NULL);
    readiness_contract();
    vram_write(0x700,1);vram_write(0x701,2);vram_write(0x702,3);
    upload_words(0x701,0x700,3);assert(vram_read(0x701)==1&&vram_read(0x703)==1);
    voodoo_init();assert(!counters.packets&&!counters.presents&&!geometry_started);
    strip_contract();
    consume_contract();
    /* Decoder guards reject packets crossing VRAM and invalid read pointers. */
    unsigned count=0;
    fifo_describe(0x7ffff8,(1u<<16)|1u,&count);assert(count==2);
    fifo_mark(0x7ffffc,1);assert(fifo_has(0x7ffffc));
    fifo_mark(0x7ffffc,0);assert(!fifo_has(0x7ffffc));
    expected="FIFO packet crosses VRAM end";
    if(!setjmp(fail)){fifo_describe(0x7ffffc,(1u<<16)|1u,&count);assert(0);}
    expected=NULL;
    unsigned saved_pc=agp[11];agp[11]=0x800000;swap_pending=0;
    expected="invalid FIFO read pointer";
    if(!setjmp(fail)){fifo_run();assert(0);}
    expected=NULL;agp[11]=saved_pc;

    free(bytes);puts("Wii headless endian, full VRAM, FIFO, upload, fastfill, IRQ and reset PASS");
}
