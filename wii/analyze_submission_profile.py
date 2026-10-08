"""Attribute diagnostic GL graphics-aperture stores to generated functions."""
import argparse
from collections import defaultdict
from pathlib import Path
import re

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('log', type=Path)
parser.add_argument('--source', type=Path,
                    default=Path(__file__).resolve().parent.parent / 'generated/gticlub2/gl_000.c')
args = parser.parse_args()
owners = {}
function = None
for line in args.source.read_text().splitlines():
    match = re.match(r'void (f_\w+)\(PPCContext \*c\)', line)
    if match:
        function = match[1]
    match = re.search(r'/\* ([0-9a-f]{8}):', line)
    if match and function:
        owners[int(match[1], 16)] = function

text = args.log.read_text(errors='replace')
headers = list(re.finditer(r'VIPER WII SUBMISSION scope=generated_gl_lfb total=(\d+) overflow=(\d+)', text))
if not headers:
    raise SystemExit('No completed submission census in log')
header = headers[-1]
rows = re.findall(r'VIPER WII WRITER pc=([0-9a-f]+) count=(\d+) little=(\d+) low=([0-9a-f]+) high=([0-9a-f]+)', text[header.end():])
totals = defaultdict(lambda: [0, 0, []])
for pc, count, little, low, high in rows:
    pc, count, little = int(pc, 16), int(count), int(little)
    data = totals[owners.get(pc, 'unmapped')]
    data[0] += count
    data[1] += little
    data[2].append((pc, count, int(low, 16), int(high, 16)))
total, overflow = map(int, header.groups())
observed = sum(data[0] for data in totals.values())
if observed + overflow != total:
    raise SystemExit(f'Incomplete census: rows={observed} overflow={overflow} total={total}')
print(f'GL graphics-aperture stores: {total}; overflow={overflow}')
print('Counts include FIFO and texture-aperture writes; not timing or proof of FIFO eligibility.')
for name, (count, little, sites) in sorted(totals.items(), key=lambda item: item[1][0], reverse=True):
    share = 100 * count / total if total else 0
    print(f'{name}: {count} stores ({share:.2f}%), ST32LE={little}, sites={len(sites)}')
    for pc, calls, low, high in sorted(sites, key=lambda item: item[1], reverse=True)[:12]:
        print(f'  {pc:08x}: {calls}, destinations {low:08x}..{high:08x}')
