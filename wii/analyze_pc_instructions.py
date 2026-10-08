#!/usr/bin/env python3
"""Attribute single-instruction PC samples using disassembly of the same ELF.

These are interrupted-PC counts, not exact instruction latency or class wall time.
The optional context register must be verified in each selected function's ABI.
"""
import argparse
import collections
import json
import re
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('profile', type=Path)
p.add_argument('disassembly', type=Path)
p.add_argument('--function', action='append', required=True)
p.add_argument('--context-register', type=int)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
profile = json.loads(a.profile.read_text())
if int(profile['summary']['bucket_bytes']) != 4:
    p.error('instruction attribution requires four-byte PC buckets')
instructions = {}
for line in a.disassembly.read_text().splitlines():
    m = re.match(r'^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2}\s+){4}\s*([^\s]+)\s*(.*)$', line)
    if m:
        instructions[int(m[1], 16)] = (m[2], m[3])
counts = collections.Counter()
rows = []
for row in profile['attributed_pc_bins']:
    if row['function'] not in a.function:
        continue
    pc = int(row['pc'], 16)
    if pc not in instructions:
        p.error(f'missing sampled instruction {pc:08x}; verify matching ELF/disassembly')
    mnemonic, operands = instructions[pc]
    memory = mnemonic.startswith(('lw', 'lh', 'lb', 'lf', 'st'))
    # This identifies only D-form accesses based directly on the verified
    # context register; indexed and derived-pointer accesses remain separate.
    if memory and a.context_register is not None and re.search(rf'\({a.context_register}\)', operands):
        kind = 'context_base_memory'
    elif memory:
        kind = 'other_memory'
    elif mnemonic in ('bl', 'bla', 'bctrl', 'blrl'):
        kind = 'call'
    elif mnemonic.startswith('b'):
        kind = 'branch'
    elif mnemonic.startswith('f'):
        kind = 'floating_operation'
    else:
        kind = 'integer_or_other'
    counts[kind] += row['samples']
    rows.append(dict(row, mnemonic=mnemonic, operands=operands, category=kind))
total = sum(counts.values())
result = {
    'profile': str(a.profile), 'disassembly': str(a.disassembly),
    'functions': a.function, 'context_register': a.context_register,
    'selected_samples': total, 'all_samples': profile['recorded_samples'],
    'interpretation': 'Interrupted-PC sample attribution; not exact class wall time. Other memory includes stack/RAM and cannot be assumed removable.',
    'categories': [{
        'category': k, 'samples': n,
        'share_of_selected_percent': 100*n/max(total, 1),
        'share_of_all_percent': 100*n/max(profile['recorded_samples'], 1),
    } for k, n in counts.most_common()],
    'instructions': sorted(rows, key=lambda r: r['samples'], reverse=True),
}
a.output.write_text(json.dumps(result, indent=2)+'\n')
print(json.dumps({k: v for k, v in result.items() if k != 'instructions'}, indent=2))
