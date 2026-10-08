"""Summarize one complete hot-writer admission diagnostic; never infer speed."""
import argparse,json,re
from pathlib import Path
LABELS=('admitted','logging','staging','source_bounds','fifo_readiness','audit_trace')
ROW=re.compile(r'VIPER WII BULK ADMISSION site=(\d+) reason=(\d+) count=(\d+)')
def analyze(text):
 lines=text.splitlines()
 start=next((i for i,x in enumerate(lines) if x.startswith('VIPER WII BULK ADMISSION ')),None)
 if start is None: raise ValueError('no admission report')
 rows=lines[start:start+12]
 if len(rows)!=12: raise ValueError('incomplete admission report')
 result=[]
 for site in range(2):
  counts={}
  for reason,label in enumerate(LABELS):
   m=ROW.fullmatch(rows[site*6+reason])
   if not m or (int(m[1]),int(m[2]))!=(site,reason):
    raise ValueError('malformed, duplicate or out-of-order admission row')
   counts[label]=int(m[3])
  total=sum(counts.values())
  result.append({'site':site,'scope':'hot_body' if site==0 else 'tail',
                 'calls':total,'admitted_percent':100*counts['admitted']/total if total else None,
                 'reasons':counts})
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('log',type=Path);a=p.parse_args()
 # SD may retain bytes past the current file's final write. NUL before a
 # complete twelve-row report still causes an incomplete-report failure.
 text=a.log.read_bytes().split(b'\0',1)[0].decode('utf8',errors='strict')
 print(json.dumps(analyze(text),indent=2))
