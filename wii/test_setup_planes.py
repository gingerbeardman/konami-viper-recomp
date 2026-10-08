"""Extract actual MAME setup/subpixel code; compare persistent state and draws.

Finite, bounded nondegenerate input domain; exceptional reference casts excluded.
"""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parents[1]
s=(root/'runtime/voodoo/voodoo_2.cpp').read_text()
start=s.index('// compute the reciprocal now');end=s.index('// draw the triangle',start)
setup=s[start:end]
det=s[s.index('float divisor =',s.index('s32 voodoo_2_device::setup_and_draw_triangle')):];det=det[:det.index(';')+1]
s=(root/'runtime/voodoo/voodoo.cpp').read_text();start=s.index('if (m_reg.fbz_colorpath().cca_subpixel_adjust())');brace=s.index('{',start);end=brace+1;depth=1
while depth:
 depth+=(s[end]=='{')-(s[end]=='}');end+=1
subpixel=s[start:end]
code=r'''
#include "runtime/voodoo/emu.h"
class save_proxy;
#include "runtime/voodoo/voodoo_regs.h"
#include "wii/voodoo_setup_planes.h"
using namespace voodoo;
voodoo_regs m_reg;
struct TMU {voodoo_regs r;voodoo_regs& regs(){return r;}} m_tmu[2];
unsigned m_chipmask=7;
void reference_setup(const WiiSetupVertex v[3],unsigned attrs){
 auto &sv0=v[0];auto &sv1=v[1];auto &sv2=v[2];
 reg_setup_mode setup_mode(attrs);
''' + det + '\n' + setup + r'''
}
WiiSetupPlanes export_state(){
 WiiSetupPlanes out={};
 const unsigned registers[]={0x20,0x24,0x28,0x30,0x2c};
 for(unsigned i=0;i<5;i++)out.plane[i]={(int32_t)m_reg.read(registers[i]/4),(int32_t)m_reg.read((registers[i]+32)/4),(int32_t)m_reg.read((registers[i]+64)/4)};
 out.plane[5]={m_reg.start_w(),m_reg.dw_dx(),m_reg.dw_dy()};
 for(unsigned u=0;u<2;u++){auto &r=m_tmu[u].regs();out.plane[6+3*u]={r.start_s(),r.ds_dx(),r.ds_dy()};out.plane[7+3*u]={r.start_t(),r.dt_dx(),r.dt_dy()};out.plane[8+3*u]={r.start_w(),r.dw_dx(),r.dw_dy()};}
 out.xy[0][0]=m_reg.ax();out.xy[0][1]=m_reg.ay();out.xy[1][0]=m_reg.bx();out.xy[1][1]=m_reg.by();out.xy[2][0]=m_reg.cx();out.xy[2][1]=m_reg.cy();return out;
}
WiiSetupPlanes reference_prepare(int enabled){
 struct Poly {s32 ax,ay,startr,startg,startb,starta,startz,drdx,dgdx,dbdx,dadx,dzdx,drdy,dgdy,dbdy,dady,dzdy;s64 startw,dwdx,dwdy;} poly;
 poly.ax=m_reg.ax();poly.ay=m_reg.ay();
 poly.startr=m_reg.start_r();poly.startg=m_reg.start_g();poly.startb=m_reg.start_b();poly.starta=m_reg.start_a();poly.startz=m_reg.start_z();poly.startw=m_reg.start_w();
 poly.drdx=m_reg.dr_dx();poly.dgdx=m_reg.dg_dx();poly.dbdx=m_reg.db_dx();poly.dadx=m_reg.da_dx();poly.dzdx=m_reg.dz_dx();poly.dwdx=m_reg.dw_dx();
 poly.drdy=m_reg.dr_dy();poly.dgdy=m_reg.dg_dy();poly.dbdy=m_reg.db_dy();poly.dady=m_reg.da_dy();poly.dzdy=m_reg.dz_dy();poly.dwdy=m_reg.dw_dy();
 m_reg.write(voodoo_regs::reg_fbzColorPath,enabled?1u<<26:0);
''' + subpixel + r'''
 auto out=export_state();
 out.plane[0]={poly.startr,poly.drdx,poly.drdy};out.plane[1]={poly.startg,poly.dgdx,poly.dgdy};out.plane[2]={poly.startb,poly.dbdx,poly.dbdy};out.plane[3]={poly.starta,poly.dadx,poly.dady};out.plane[4]={poly.startz,poly.dzdx,poly.dzdy};out.plane[5]={poly.startw,poly.dwdx,poly.dwdy};return out;
}
static uint32_t random_state=123;
unsigned next(){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return random_state;}
void equal(const WiiSetupPlanes&a,const WiiSetupPlanes&b){
 for(unsigned i=0;i<12;i++){assert(a.plane[i].start==b.plane[i].start);assert(a.plane[i].dx==b.plane[i].dx);assert(a.plane[i].dy==b.plane[i].dy);}
 for(unsigned i=0;i<3;i++)for(unsigned j=0;j<2;j++)assert(a.xy[i][j]==b.xy[i][j]);
}
int main(){
 WiiSetupPlanes state={};unsigned draws=0;
 for(unsigned n=0;n<100000;n++){
  WiiSetupVertex v[3]={};float x=(int(next()%1600)-800)/16.0f,y=(int(next()%1600)-800)/16.0f;
  v[0].x=x;v[0].y=y;v[1].x=x+16+(next()%32);v[1].y=y+1;v[2].x=x+2;v[2].y=y+16+(next()%32);
  for(unsigned i=0;i<3;i++){
   v[i].r=float(next()%256);v[i].g=float(next()%256);v[i].b=float(next()%256);v[i].a=float(next()%256);v[i].z=float(next()%1024);
   v[i].wb=(next()%1024)/1024.0f;v[i].w0=(next()%1024)/1024.0f;v[i].w1=(next()%1024)/1024.0f;
   v[i].s0=(int(next()%4096)-2048)/16.0f;v[i].t0=(int(next()%4096)-2048)/16.0f;v[i].s1=(int(next()%4096)-2048)/16.0f;v[i].t1=(int(next()%4096)-2048)/16.0f;
  }
  unsigned attrs=next()&255;
  assert(wii_setup_update(&state,v,attrs));reference_setup(v,attrs);equal(state,export_state());
  // Repeat prepare without setup to expose persistent subpixel accumulation.
  for(unsigned repeat=0;repeat<3;repeat++){
   int subpixel=(n+repeat)&1;WiiSetupPlanes actual;
   assert(wii_setup_prepare(&state,subpixel,&actual));auto expected=reference_prepare(subpixel);
   equal(state,export_state());equal(actual,expected);draws++;
  }
 }
 WiiSetupPlanes old=state;WiiSetupVertex invalid[3]={};assert(!wii_setup_update(&state,invalid,255));equal(state,old);
 invalid[0].x=NAN;assert(!wii_setup_update(&state,invalid,255));equal(state,old);
 // Direct integer/float aliases and chip routing, with independent expected scaling.
 WiiSetupPlanes direct={};assert(wii_setup_write(&direct,0x3c,0xffffffff,7));
 assert(direct.plane[5].start==-4&&direct.plane[8].start==-4&&direct.plane[11].start==-4);
 assert(wii_setup_write(&direct,0x34,0xfffffffe,2));assert(direct.plane[6].start==-32768&&direct.plane[9].start==0);
 float f=1.5f;uint32_t bits;memcpy(&bits,&f,4);assert(wii_setup_write(&direct,0xa0,bits,1));assert(direct.plane[0].start==6144);
 assert(wii_setup_write(&direct,0xbc,bits,4));assert(direct.plane[11].start==6442450944LL&&direct.plane[8].start==-4);
 // RGB signed24 getter boundary and transactional arithmetic overflow.
 WiiSetupPlanes edge={};edge.plane[0]={8388607,16,0};WiiSetupPlanes snapshot;
 assert(wii_setup_prepare(&edge,1,&snapshot));assert(snapshot.plane[0].start==8388615);
 assert(wii_setup_read(&edge,0).start==-8388601);
 WiiSetupPlanes extreme={};extreme.plane[5]={0,INT64_MAX,0};WiiSetupPlanes saved=extreme;
 assert(!wii_setup_prepare(&extreme,1,&snapshot));equal(extreme,saved);
 assert(!wii_setup_prepare(&edge,1,&edge));
 f=INFINITY;memcpy(&bits,&f,4);saved=direct;assert(!wii_setup_write(&direct,0xa0,bits,1));equal(direct,saved);
 assert(draws==300000);
 puts("setup planes PASS 100000 conditional setups / 300000 retained subpixel draws plus direct-write and rejection cases");
}
'''
with tempfile.TemporaryDirectory(prefix='viper-setup-') as d:
 src=Path(d)/'test.cpp';binary=Path(d)/'test';src.write_text(code)
 subprocess.run(['clang++','-std=c++20','-O2','-Wall','-Wextra','-Werror','-Wno-sign-compare','-ffp-contract=off','-fsanitize=address,undefined','-I',str(root),str(src),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
 c=Path(d)/'pure.c';c.write_text('#include "wii/voodoo_setup_planes.h"\nint main(void){WiiSetupPlanes s={0};return !wii_setup_write(&s,0x20,0,1);}\n')
 subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-pedantic','-I',str(root),str(c),'-o',str(Path(d)/'pure')],check=True)
