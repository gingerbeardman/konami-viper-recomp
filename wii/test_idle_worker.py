#!/usr/bin/env python3
"""Compare batching with the actual emitted loop up to its next checkpoint."""
from pathlib import Path
import subprocess
import re

root = Path(__file__).resolve().parents[1]
out = root / "build/wii-cpu-check"
out.mkdir(parents=True, exist_ok=True)
source = (root / "generated/gticlub2/game_001.c").read_text()
body = source.split("void f_g_00045dd8(PPCContext *c) {", 1)[1].split("\nvoid ", 1)[0]
body = body[:body.rfind("}")]
body = re.sub(r"^\s*rt_idle_worker_batch\(c\);\s*$", "", body, flags=re.M)
reference = 'static void original(PPCContext *c){goto L_00045e1c;\n' + body + '}\n'
harness = r'''
#include "idle_worker.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
uint8_t *g_ram;
static uint32_t checkpoint;
static uint32_t called;
static int allow_call;
void rt_check(PPCContext *c,uint32_t pc){checkpoint=pc;c->unwind=1;}
void rt_call(PPCContext *c,uint32_t pc){assert(allow_call);called=pc;c->unwind=1;}
uint32_t rt_cr_pack(PPCContext *c){(void)c;return 0;}
uint32_t rt_mmio_r8(uint32_t a){(void)a;abort();}
uint32_t rt_mmio_r16(uint32_t a){(void)a;abort();}
uint32_t rt_mmio_r32(uint32_t a){(void)a;abort();}
void rt_mmio_w8(uint32_t a,uint32_t v){(void)a;(void)v;abort();}
void rt_mmio_w16(uint32_t a,uint32_t v){(void)a;(void)v;abort();}
void rt_mmio_w32(uint32_t a,uint32_t v){(void)a;(void)v;abort();}
'''
main = r'''
int main(void){
 g_ram=calloc(1,RAM_SIZE);assert(g_ram);
 for(unsigned trial=0;trial<3500;trial++){
  PPCContext a={0};
  for(unsigned i=0;i<32;i++)a.r[i]=trial*7141+i;
  for(unsigned i=0;i<8;i++)a.cr[i]=(trial+i)&15;
  a.xer_so=trial&1;a.xer_ca=(trial>>1)&1;
  uint32_t base=trial%3==0?0:trial%3==1?0x4000:RAM_LIMIT-142;
  a.r[29]=a.r[26]=base;a.r[23]=base-3;a.r[24]=0;a.r[27]=base+4;
  a.budget=trial<500?trial+1:1+(trial*137u)%20000;
  for(unsigned i=0;i<6;i++){
   uint8_t status=(uint8_t)(trial+i*31);
   g_ram[(base+20+24*i)&RAM_MASK]=status;
   g_ram[(base+21+24*i)&RAM_MASK]=status;
  }
  PPCContext b=a;original(&a);uint32_t expected=checkpoint;
  rt_idle_worker_batch(&b);original(&b);
  assert(checkpoint==expected&&memcmp(&a,&b,sizeof a)==0);
 }
 /* A scheduler event makes one record ready after the preserved checkpoint.
  * Both paths must call the same guest routine with exactly the same state. */
 for(unsigned ready=0;ready<6;ready++){
  memset(g_ram+0x4000,0,142);
  PPCContext a={0};a.r[29]=a.r[26]=0x4000;a.r[23]=0x3ffd;a.r[27]=0x4004;a.budget=193;
  PPCContext b=a;original(&a);uint32_t expected=checkpoint;
  rt_idle_worker_batch(&b);original(&b);
  assert(checkpoint==expected&&!memcmp(&a,&b,sizeof a));
  g_ram[0x4000+21+24*ready]=1;
  a.unwind=b.unwind=0;a.budget=b.budget=20000;allow_call=1;
  original(&a);uint32_t target=called;
  rt_idle_worker_batch(&b);original(&b);
  assert(called==target&&!memcmp(&a,&b,sizeof a));allow_call=0;
 }
 PPCContext a={0};a.r[29]=a.r[26]=0x4000;a.r[23]=0x3ffd;a.budget=20000;
 for(unsigned i=0;i<6;i++){
  memset(g_ram+0x4000,0,142);g_ram[0x4000+21+24*i]=1;
  PPCContext b=a;assert(!rt_idle_worker_batch(&b)&&!memcmp(&a,&b,sizeof a));
 }
 a.r[29]=a.r[26]=RAM_LIMIT-141;a.r[23]=a.r[29]-3;
 PPCContext b=a;assert(!rt_idle_worker_batch(&b)&&!memcmp(&a,&b,sizeof a));
 free(g_ram);puts("Idle worker complete-context/checkpoint differential PASS (3500 cases)");
}
'''
path = out / "idle_worker_differential.c"
path.write_text(harness + reference + main)
exe = out / "idle_worker_differential"
subprocess.run(["clang", "-std=c11", "-O1", "-fsanitize=address,undefined",
                "-I" + str(root / "runtime"), str(path), "-lm", "-o", str(exe)], check=True)
subprocess.run([str(exe)], check=True)
