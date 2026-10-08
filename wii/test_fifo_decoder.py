"""Compare actual device decoder vertex streams with specialization on/off."""
from pathlib import Path
import subprocess
import tempfile
root=Path(__file__).resolve().parent.parent
base=(root/'wii/voodoo_headless_test.c').read_text().replace('int main(void)', 'void device_contract_main(void)')
harness=r'''
static uint64_t stream_hash=1469598103934665603ull;
static unsigned stream_calls;
static FILE *stream_file;
static void digest(const void *data,size_t n){assert(fwrite(data,1,n,stream_file)==n);const unsigned char *p=data;while(n--){stream_hash^=*p++;stream_hash*=1099511628211ull;}}
static void stream_cb(void *user,const WiiVoodooView *view,const WiiVoodooVertex vertices[3],uint32_t cmd){
 (void)user;digest(vertices,3*sizeof(*vertices));digest(&cmd,sizeof cmd);digest(&view->strip_count,sizeof view->strip_count);stream_calls++;
}
int main(int argc,char **argv){
 assert(argc==2);stream_file=fopen(argv[1],"wb");assert(stream_file);
 device_contract_main();vram=calloc(1,WII_VOODOO_VRAM_BYTES);assert(vram);renderer=(WiiVoodooRenderer){.triangle=stream_cb};renderer_user=NULL;
 unsigned cases=0;
 for(unsigned format=0;format<256;format++)for(unsigned packed=0;packed<2;packed++)for(unsigned fan=0;fan<2;fan++){
  memset(strip,0,sizeof strip);strip_count=0;
  for(unsigned packet=0;packet<3;packet++){
   unsigned code=packet==0?1:packet==1?2:0;
   unsigned vertices=packet==0?5:packet==1?7:6;
   uint32_t cmd=3|(code<<3)|(vertices<<6)|(format<<10)|(fan<<22)|(packed<<28);
   for(unsigned word=0;word<256;word++)vram_write(0x1800/4+word,0x3e000000u+((word*17911u+format*37u+packet*19u)&0x1ffffff));
   triangle_packet(0x1800,cmd);
   digest(strip,sizeof strip);digest(&strip_count,sizeof strip_count);cases++;
  }
 }
 printf("Decoder stream cases=%u callbacks=%u hash=%016llx\n",cases,stream_calls,(unsigned long long)stream_hash);
 assert(fclose(stream_file)==0);free(vram);return 0;
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'test.c').write_text(base+harness)
 flags=['clang','-O2','-std=c11','-fsanitize=address,undefined','-I'+str(root/'runtime'),'-I'+str(root/'wii'),'-I'+str(root/'tests/fixtures')]
 outputs=[];traces=[]
 for name,extra in [('reference',[]),('candidate',['-DVIPER_WII_FIFO_FORMAT59','-DVIPER_WII_FIFO_FRONTIER_WRITE','-DVIPER_WII_FIFO_BOUNDS_CACHE','-DVIPER_WII_FIFO_SPLIT_TRIANGLE'])]:
  subprocess.run(flags+extra+[str(p/'test.c'),'-o',str(p/name)],check=True)
  trace=p/(name+".stream");outputs.append(subprocess.check_output([str(p/name),str(trace)],text=True));traces.append(trace.read_bytes())
 assert outputs[0]==outputs[1],outputs
 assert traces[0]==traces[1],"vertex or strip stream differs"
 print(f"Byte-identical stream: {len(traces[0])} bytes")
 print(outputs[1],end='');print('FIFO decoder differential PASS')
