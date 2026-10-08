"""Compile the actual alternative staging predicate against the old predicate."""
from pathlib import Path
import subprocess,tempfile
from specialize_submission import STAGING_CHECK, STAGING_OR
old=STAGING_CHECK.removesuffix(' &&');new=STAGING_OR.removesuffix(' &&')
code='''#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef struct {uint32_t r[32];} Context;
static int original(Context*c){return '''+old+''';}
static int candidate(Context*c){return '''+new+''';}
int main(void){Context c={0};uint32_t seed=1234567;unsigned regs[]={15,14,19,18,17,16};uint32_t values[]={0x3398,0x33ac,4,8,12,16};unsigned cases=0;
for(unsigned n=0;n<100000;n++){
 for(unsigned j=0;j<6;j++){seed=seed*1664525u+1013904223u;c.r[regs[j]]=seed;}
 assert(original(&c)==candidate(&c));cases++;
 for(unsigned j=0;j<6;j++)c.r[regs[j]]=values[j];
 assert(original(&c)&&candidate(&c));cases++;
 for(unsigned j=0;j<6;j++)for(unsigned bit=0;bit<32;bit++){
 c.r[regs[j]]^=1u<<bit;assert(!original(&c)&&!candidate(&c));cases++;c.r[regs[j]]^=1u<<bit;
 }
}
printf("Staging guard exact predicate PASS cases=%u\\n",cases);
}
'''
with tempfile.TemporaryDirectory() as d:
 p=Path(d);(p/'proof.c').write_text(code)
 subprocess.run(['clang','-O2','-fsanitize=address,undefined',str(p/'proof.c'),'-o',str(p/'proof')],check=True)
 subprocess.run([str(p/'proof')],check=True)
