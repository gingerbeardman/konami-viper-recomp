"""Report cumulative renderer counter deltas over a guest-time interval."""
import argparse
from pathlib import Path
import re
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path)
p.add_argument('--begin',type=int,default=70)
p.add_argument('--end',type=int,default=71)
a=p.parse_args()
if a.end<=a.begin:p.error('end must follow begin')
blocks={};current=None
for line in a.log.read_text(errors='replace').splitlines():
 m=re.search(r'VIPER WII PROFILE guest=([\d.]+) elapsed_us=(\d+)',line)
 if m:
  second=int(float(m[1]));current=None
  if second not in blocks:
   current={'elapsed_us':int(m[2])};blocks[second]=current
 elif current is not None:
  m=re.search(r'VIPER WII GX (SETUP|PLANE|BIND) kind=(\d+) calls=(\d+) us=(\d+)',line)
  if m:
   current[f'{m[1]} {m[2]} calls']=int(m[3]);current[f'{m[1]} {m[2]} us']=int(m[4])
  m=re.search(r'VIPER WII GX COST (.*)',line)
  if m:
   for key,value in re.findall(r'(\w+)=(\d+)',m[1]):current[key]=int(value)
  # Site4 runs exactly once per rendered present, after frame-divisor rejection.
  m=re.search(r'VIPER WII GX FENCE site=4 calls=(\d+) us=\d+',line)
  if m:current['displayed presents']=int(m[1])
  m=re.search(r'VIPER WII GX CONFIG op=(\d+) attempted=(\d+) identical=(\d+)',line)
  if m:
   current[f'CONFIG {m[1]} attempted']=int(m[2]);current[f'CONFIG {m[1]} identical']=int(m[3])
  m=re.search(r'VIPER WII RENDER kind=(\d+) us=(\d+)',line)
  if m:current[f'RENDER {m[1]} us']=int(m[2])
  m=re.search(r'VIPER WII CPU LOOKUP calls=(\d+) us=(\d+)',line)
  if m:
   current['LOOKUP calls']=int(m[1]);current['LOOKUP us']=int(m[2])
  m=re.search(r'VIPER WII WORK (.*)',line)
  if m:
   for key,value in re.findall(r'(\w+)=(\d+)',m[1]):current[key]=int(value)
for second in [a.begin,a.end]:
 if second not in blocks:p.error(f'missing guest second {second}')
start,finish=blocks[a.begin],blocks[a.end]
keys=set(start)&set(finish)
if 'presents' not in keys:p.error('missing work counters')
delta={k:finish[k]-start[k] for k in keys}
if any(v<0 for v in delta.values()):p.error('counters decreased; interval crosses reset or stale log data')
span=delta['elapsed_us']
if span<=0:p.error('invalid elapsed interval')
print(f'Guest {a.begin} -> {a.end}: {span} modeled Wii microseconds')
print(f'Game speed: {(a.end-a.begin)*1e6/span:.3f}; logical guest presents/sec: {delta["presents"]*1e6/span:.2f}')
if 'displayed presents' in delta:
 print(f'Rendered game presents/sec: {delta["displayed presents"]*1e6/span:.2f} ({delta["displayed presents"]} frames); separate from Dolphin host FPS')
print('Setup labels: 0 projection/clip; 1 framebuffer; 2 texture/TEV; 3 fog/depth; 4 vertex submission')
print('Render labels: 0 triangles; 1 clears; 2 presents. Setup/plane times are nested; do not sum them with render totals.')
if 'BIND 0 us' in delta:
 print('Bind labels: 0 fog lookup; 1 depth lookup. Nested diagnostic timings include call/timer overhead; never add them to setup or renderer totals.')
for key in sorted(delta):
 if key=='elapsed_us':continue
 suffix=f' ({delta[key]/span:.1%} of interval)' if key.endswith('us') else ''
 print(f'{key}: {delta[key]}{suffix}')
