"""Measure planner-key repetition and existing combiner reuse in a complete window.

Repeated planner keys are not proof that textures, matrices or SDK state stayed
unchanged. Distinct-key deltas count newly seen keys, not window cardinality.
"""
import argparse
import json
from pathlib import Path
import re
from compare_perf_runs import interval

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('log',type=Path);p.add_argument('--begin',type=int,default=70)
p.add_argument('--end',type=int,default=73);p.add_argument('--output',type=Path,required=True)
a=p.parse_args()
timing=interval(a.log,a.begin,a.end)
patterns={
 'material':re.compile(r'^VIPER WII GX MATERIAL calls=(\d+) consecutive=(\d+) distinct=(\d+) overflow=(\d+)$'),
 'combiner':re.compile(r'^VIPER WII GX COMBINER PROGRAM hits=(\d+) misses=(\d+)$')}
blocks={};current=None;second=None
for line in a.log.read_text().splitlines():
 m=re.fullmatch(r'VIPER WII PROFILE guest=(\d+(?:\.\d+)?) elapsed_us=(\d+)',line)
 if m:
  second=int(float(m[1]));current={} if a.begin<=second<=a.end else None
 elif current is not None:
  for kind,pattern in patterns.items():
   m=pattern.fullmatch(line)
   if m:
    if kind in current:raise ValueError('duplicate '+kind+' record')
    current[kind]=tuple(map(int,m.groups()))
  if line.startswith('VIPER WII WORK '):
   if second in blocks:raise ValueError('duplicate profile second')
   blocks[second]=current;current=None
for second in range(a.begin,a.end+1):
 if second not in blocks or set(blocks[second])!=set(patterns):raise ValueError('incomplete material/combiner record')
 if second>a.begin:
  for kind in patterns:
   if any(x<y for x,y in zip(blocks[second][kind],blocks[second-1][kind])):raise ValueError('counter decreased')
def delta(kind):return [x-y for x,y in zip(blocks[a.end][kind],blocks[a.begin][kind])]
calls,repeats,new_keys,overflow=delta('material');hits,misses=delta('combiner')
if repeats>calls:raise ValueError('repeated keys exceed calls')
run_starts=calls-repeats
out={'log':str(a.log),'begin':a.begin,'end':a.end,'timing':timing,
 'material':{'calls':calls,'consecutive_equal':repeats,'equal_percent':100*repeats/max(calls,1),
             'run_starts':run_starts,'approximate_mean_run_length':calls/max(run_starts,1),
             'new_keys_seen':new_keys,'overflow_occurrences':overflow},
 'existing_combiner':{'hits':hits,'misses':misses,'hit_percent':100*hits/max(hits+misses,1)},
 'limits':'Planner keys omit dynamic texture versions/matrices. Mean run length has a window-boundary effect. New-key deltas are not window cardinality. Combiner reuse covers only its admitted programs, not every material call; repeated static program work may already be removed.'}
a.output.write_text(json.dumps(out,indent=2)+'\n');print(json.dumps(out,indent=2))
