#!/usr/bin/env python3
"""Map interrupt PC bins to matching ELF nm -n -S output; no guessed symbols."""
import argparse
import bisect
import collections
import json
import re
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument("log", type=Path)
p.add_argument("symbols", type=Path)
p.add_argument("--json-output", type=Path, help="Save every attributed function, including those below the printed top 40")
args = p.parse_args()
symbols = []
for line in args.symbols.read_text().splitlines():
    m = re.match(r"([0-9a-fA-F]+) ([0-9a-fA-F]+) [tT] (.+)$", line)
    if m:
        address, size, name = m.groups()
        symbols.append((int(address, 16), int(size, 16), name))
symbols.sort()
starts = [s[0] for s in symbols]
counts = collections.Counter()
unmapped = boundary = total = 0
log = args.log.read_bytes().decode(errors="replace")
summary = re.search(r"VIPER WII PC SUMMARY[^\r\n]*", log)
if not summary:
    p.error('missing PC summary; extraction may be incomplete')
fields=dict(re.findall(r'(\w+)=(\d+)',summary[0]))
bucket_bytes=int(fields.get('bucket_bytes',0))
if bucket_bytes not in (4,64):
    p.error('unsupported or missing PC bucket size')
pc_bins=[]
seen=set()
# SD extraction can retain an old tail beyond the final write. Consume only
# this contiguous report, never unrelated PC-looking lines after later game
# progress/work records. The summary count remains an independent exact gate.
tail=log[summary.end():]
next_record=re.search(r'(?m)^VIPER WII (?!PC )',tail)
log=log[summary.start():summary.end()+(next_record.start() if next_record else len(tail))]
for address, count in re.findall(r"VIPER WII PC bin=([0-9a-fA-F]+) count=(\d+)", log):
    pc, count = int(address, 16), int(count)
    if pc % bucket_bytes or pc in seen or count == 0:
        p.error('unaligned, duplicate or empty PC bin')
    seen.add(pc)
    total += count
    index = bisect.bisect_right(starts, pc) - 1
    if index < 0 or pc + bucket_bytes-1 >= symbols[index][0] + symbols[index][1]:
        # A bucket can straddle functions. Do not guess attribution.
        if index >= 0 and pc < symbols[index][0] + symbols[index][1]:
            boundary += count
        else:
            unmapped += count
        continue
    counts[symbols[index][2]] += count
    pc_bins.append({'pc': f'{pc:08x}', 'samples': count, 'function': symbols[index][2]})
print(f"Recorded bins: {total} samples; boundary-ambiguous: {boundary}; unmapped: {unmapped}")
summary = re.search(r"VIPER WII PC SUMMARY[^\r\n]*", log)
if summary:
    fields=dict(re.findall(r'(\w+)=(\d+)',summary[0]))
    if int(fields.get('samples',-1))!=total+int(fields.get('overflow',0)):
        p.error('PC summary and recorded bins disagree; frozen profile or extraction is incomplete')
print(summary[0] if summary else "Missing summary: extraction may be incomplete")
for name, count in counts.most_common(40):
    print(f"{count:7d} {count / max(total, 1):7.2%} {name}")
lr_counts=collections.Counter()
lr_summary=re.search(r'VIPER WII PC LR SUMMARY[^\r\n]*',log)
if lr_summary:print(lr_summary[0])
for pc,lr,count in re.findall(r"VIPER WII PC LR pc=([0-9a-fA-F]+) lr=([0-9a-fA-F]+) count=(\d+)",log):
    pc,lr,count=int(pc,16),int(lr,16),int(count)
    callee=bisect.bisect_right(starts,pc)-1
    if callee<0 or pc+bucket_bytes-1>=symbols[callee][0]+symbols[callee][1]:continue
    if symbols[callee][2] not in {'memcpy','memcmp','memset'}:continue
    site=lr-4
    caller=bisect.bisect_right(starts,site)-1
    name=symbols[caller][2] if caller>=0 and site<symbols[caller][0]+symbols[caller][1] else 'unmapped'
    lr_counts[(symbols[callee][2],name,site)]+=count
if lr_counts:
    print('Memory routine saved-LR sites (confirm leaf/call instruction in disassembly; not stack traces):')
    for (callee,caller,site),count in lr_counts.most_common(30):
        print(f'{count:7d} {callee} saved-LR-4={site:08x} {caller}')
if args.json_output:
    args.json_output.write_text(json.dumps({
        'log': str(args.log), 'symbols': str(args.symbols),
        'recorded_samples': total, 'boundary_ambiguous_samples': boundary,
        'unmapped_samples': unmapped,
        'summary': fields,
        'attributed_pc_bins': pc_bins,
        'functions': [{'name': name, 'samples': count, 'share_percent': count * 100 / max(total, 1)}
                      for name, count in counts.most_common()],
        'memory_saved_lr_sites': [{'callee': callee, 'caller': caller, 'site': f'{site:08x}', 'samples': count}
                                  for (callee, caller, site), count in lr_counts.most_common()],
    }, indent=2) + '\n')
